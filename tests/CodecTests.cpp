#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <algorithm>
#include <vector>

#include "../modules/BattleArchive.inc.cpp"

#include "../modules/BattleCodec.inc.cpp"
#include "../modules/BattleFingerprint.hpp"

static int g_failures = 0;

static void Expect(bool condition, const char* name)
{
    if (!condition) {
        printf("FAIL %s\n", name);
        ++g_failures;
    }
}

static void TestMapFingerprint_()
{
    const auto encode = [](const std::string& title) {
        std::vector<uint8_t> bytes;
        FingerprintAppendMap_(bytes, title);
        FingerprintPut32_(bytes, 0x12345678);
        return bytes;
    };
    const auto first = encode("Map title");
    Expect(first == encode("Map title"), "same title bytes encode identically");
    Expect(first != encode("Other title"), "map titles participate in identity");
    Expect(encode("").size() == 12, "empty title preserves schema and battle scalar");
    Expect(encode("a") != encode("a "), "title whitespace is preserved");
    Expect(encode("a") != encode("{~c}a"), "title color codes are preserved");
    const std::string chinese("\xB5\xD8\xCD\xBC", 4);
    Expect(encode(chinese) == encode(chinese), "native Chinese bytes are stable");
    Expect(encode(std::string("a\0b", 3)) != encode("a"),
        "length framing preserves embedded zero bytes");
    const auto framed = encode("a");
    Expect(framed.size() == 13 && framed[0] == 'M' && framed[1] == 'A'
        && framed[2] == 'P' && framed[3] == '2'
        && framed[4] == 1 && framed[5] == 0 && framed[8] == 'a'
        && framed[9] == 0x78 && framed[12] == 0x12,
        "title-only framing and following battle fields use little endian");
    printf("PASS map fingerprint: title only, empty and native bytes\n");
}

static void TestWarMachineEquipment_()
{
    std::unique_ptr<CodecCapture> state(new CodecCapture{});
    state->version = kCodecVersion;
    for (int side = 0; side < 2; ++side) {
        state->heroPresent[side] = true;
        for (int i = 0; i < kMachineSlotCount_; ++i)
            state->warMachines[side][i] = {kMachineArtifactIds_[i], -1};
    }
    state->warMachines[0][0].subtype = 123; // Preserve the full record, not just presence.
    state->stacks[1][1].occupied = true;
    state->stacks[1][1].type = 41;
    state->stacks[1][1].infoCombat[6] = 7;
    state->stacks[1][7].occupied = true;
    state->stacks[1][7].type = 148;
    state->stacks[1][7].numberAlive = 0;
    state->warMachines[1][1] = {-1, -1};
    std::vector<hbs::ArchiveSection> sections;
    std::string error;
    std::unique_ptr<CodecCapture> decoded(new CodecCapture{});
    Expect(CodecEncode(*state, &sections, &error) && CodecDecode(sections, decoded.get(), &error),
        "v7 dead-cart snapshot encodes hero equipment and remaining shots");
    Expect(decoded->heroPresent[1] && decoded->warMachines[1][1].id == -1
        && decoded->stacks[1][1].infoCombat[6] == 7 && decoded->warMachines[0][0].subtype == 123,
        "machine absence, subtype and D8 ammunition survive codec");
    Expect(FindSection_(sections, kSectionHeroes)->bytes.size() == 74,
        "hero section contains both presence flags and eight complete machine records");

    // Fake native hero buffers with sentinels around slots 13..16. Production uses
    // the same byte transfer helpers; no game functions or real player data.
    uint8_t heroes[2][0x1D4];
    memset(heroes, 0xA5, sizeof(heroes));
    const size_t at = 0x12D + kMachineBodySlot_ * 8;
    CodecWarMachine_ initial[4];
    for (int i = 0; i < kMachineSlotCount_; ++i) initial[i] = {kMachineArtifactIds_[i], -1};
    const auto ammoPresent = [&](int side) {
        int32_t id = -1;
        memcpy(&id, heroes[side] + 0x19D, 4);
        return id == 5;
    };
    for (int side = 0; side < 2; ++side) CodecRestoreWarMachines_(heroes[side] + at, initial);
    Expect(ammoPresent(1), "fresh battle has ammo-cart exemption before loading dead cart");
    CodecWarMachine_ before[2][4];
    for (int side = 0; side < 2; ++side) {
        CodecCaptureWarMachines_(heroes[side] + at, before[side]);
        CodecRestoreWarMachines_(heroes[side] + at, decoded->warMachines[side]);
    }
    Expect(ammoPresent(0) && !ammoPresent(1), "dead cart restore removes only the saved side's exemption");
    int shots = decoded->stacks[1][1].infoCombat[6];
    if (!ammoPresent(1)) --shots; // Model the audited 0x43F723 native guard.
    Expect(shots == 6, "native shooting consumes restored ammunition without cart equipment");
    for (int side = 0; side < 2; ++side) {
        CodecRestoreWarMachines_(heroes[side] + at, before[side]);
        Expect(heroes[side][at - 1] == 0xA5 && heroes[side][at + kMachineSlotsBytes_] == 0xA5,
            "machine commit and rollback leave neighboring equipment bytes unchanged");
    }
    Expect(ammoPresent(0) && ammoPresent(1), "failed restore rolls back both original equipment states");
    shots = 7;
    if (!ammoPresent(1)) --shots;
    Expect(shots == 7, "living cart retains original unlimited-ammunition rule");
    CodecWarMachine_ actual[4];
    CodecCaptureWarMachines_(heroes[1] + at, actual);
    for (int i = 0; i < kMachineSlotCount_; ++i)
        Expect(actual[i].id == initial[i].id && actual[i].subtype == initial[i].subtype,
            "recapture verifies complete restored machine records");

    // Strict verification must notice equipment differences even when stacks match.
    std::unique_ptr<CodecCapture> mismatch(new CodecCapture(*decoded));
    mismatch->warMachines[1][1] = {5, -1};
    std::vector<hbs::ArchiveSection> different;
    Expect(CodecEncode(*mismatch, &different, &error)
        && !CodecSectionEqual_(*FindSection_(sections, kSectionHeroes), *FindSection_(different, kSectionHeroes), nullptr)
        && CodecSectionEqual_(*FindSection_(sections, kSectionStacks), *FindSection_(different, kSectionStacks), nullptr),
        "strict verify detects wrong cart equipment even when stack section is equal");
    auto bad = sections;
    bad[5].bytes[6] = 2;
    Expect(!CodecDecode(bad, decoded.get(), &error), "invalid hero presence flag rejected");
    bad = sections;
    bad[5].bytes[7] = 5; // Slot 13 cannot contain the cart normally worn in slot 14.
    Expect(!CodecDecode(bad, decoded.get(), &error), "artifact in wrong machine slot rejected");
    mismatch->heroPresent[1] = false;
    Expect(!CodecEncode(*mismatch, &different, &error), "absent hero cannot carry machine equipment");
    for (size_t i = 0; i < sections.size(); ++i) {
        bad = sections;
        bad[i].bytes[0] = 6;
        Expect(!CodecDecode(bad, decoded.get(), &error), "v6 sections rejected instead of guessing lost equipment");
    }
    printf("PASS war-machine equipment: dead/live cart, native ammo guard, rollback, strict verification, v6 rejection\n");
}

static uint32_t FixtureNext_(uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    return state;
}

static void SetExtraWord_(CodecStack& stack, uint32_t offset, uint32_t word)
{
    size_t packed = 0;
    for (const auto& range : kStackExtraRanges_) {
        if (offset >= range.offset && offset + 4 <= range.offset + range.size) {
            memcpy(stack.extraScalars + packed + offset - range.offset, &word, 4);
            return;
        }
        packed += range.size;
    }
    Expect(false, "extra float offset belongs to numeric range");
}

static void TestControlStatePolicy_()
{
    Expect(CodecWaitControlEnabled_(0, false), "wait control enabled in normal player phase");
    Expect(!CodecWaitControlEnabled_(1, false), "wait control disabled after waiting");
    Expect(!CodecWaitControlEnabled_(0, true), "wait control disabled in tactics phase");
    Expect(CodecDefendControlEnabled_(false), "defend control enabled outside tactics");
    Expect(!CodecDefendControlEnabled_(true), "defend control disabled in tactics");
    for (int tactics = 0; tactics < 2; ++tactics)
        for (int hero = 0; hero < 2; ++hero)
            for (int casted = 0; casted < 2; ++casted)
                for (int overrideCast = 0; overrideCast < 2; ++overrideCast)
                    for (int book = 0; book < 2; ++book)
                        Expect(CodecSpellControlEnabled_(tactics != 0, hero != 0,
                            casted ? 0xFFFFFFFFu : 0, overrideCast != 0, book != 0)
                            == (!tactics && hero && (!casted || overrideCast) && book),
                            "native spell control predicate truth table");
    Expect(CodecSpellControlEnabled_(false, true, 0, false, true),
        "loading pre-cast state re-enables spellbook gate");
    Expect(!CodecSpellControlEnabled_(false, true, 1, false, true),
        "loading already-cast state keeps spellbook gate closed");
    // 145~149 豁免恢复（2026-10-10 17:43 玩家日志实证：攻城战箭塔槽
    // stack->def==0 正常，其 DEF 挂在攻城塔记录上，不走普通生物 DEF 检查）。
    Expect(CodecDefFrameGateApplies_(0) && CodecDefFrameGateApplies_(132)
        && CodecDefFrameGateApplies_(144), "creature DEF gate applies to regular creatures");
    Expect(!CodecDefFrameGateApplies_(145) && !CodecDefFrameGateApplies_(146)
        && !CodecDefFrameGateApplies_(147) && !CodecDefFrameGateApplies_(148)
        && !CodecDefFrameGateApplies_(149),
        "war machines and arrow tower skip the DEF gate (player-verified)");
    printf("PASS control state policy: wait, defend, spellbook and DEF gate\n");
}

static void TestNormalizeStaleLinks_()
{
    CodecCapture capture;
    capture.stacks[0][0].occupied = true;
    capture.stacks[0][3].occupied = true;
    capture.stacks[1][2].occupied = true;
    capture.stacks[0][20].occupied = true; // 保留位也可占用，但不作为链接目标

    // AI 目标：合法保留；指向空槽/保留位 20/半初始化配对的一律清空。
    capture.stacks[0][0].aiTarget = {1, 2};  // side1 slot2 占用 → 保留
    capture.stacks[0][3].aiTarget = {0, 5};  // slot5 空置 → 丢弃
    capture.stacks[1][2].aiTarget = {0, 20}; // slot20 保留位 → 丢弃
    capture.stacks[0][20].aiTarget = {0, 5}; // 保留位栈的合法瞬态同样规范化：目标空置 → 丢弃

    // 关系向量：仅指向占用槽的条目存活。
    CodecIdentity keep = {0, 0};
    CodecIdentity emptySlot = {0, 6};
    CodecIdentity otherSideEmpty = {1, 3};
    capture.stacks[1][2].relations[0].push_back(keep);
    capture.stacks[1][2].relations[0].push_back(emptySlot);
    capture.stacks[1][2].relations[0].push_back(otherSideEmpty);

    const CodecLinkDropReport_ report = CodecNormalizeStaleLinks_(capture);
    Expect(report.aiTargets == 3, "stale ai targets counted");
    Expect(report.relationEntries == 2, "stale relation entries counted");
    Expect(capture.stacks[0][0].aiTarget.side == 1 && capture.stacks[0][0].aiTarget.slot == 2,
        "valid ai target kept");
    Expect(capture.stacks[0][3].aiTarget.side == -1 && capture.stacks[0][3].aiTarget.slot == -1,
        "ai target to empty slot cleared");
    Expect(capture.stacks[1][2].aiTarget.side == -1 && capture.stacks[1][2].aiTarget.slot == -1,
        "ai target to reserved slot cleared");
    Expect(capture.stacks[0][20].aiTarget.side == -1,
        "ai target from reserved stack cleared");
    Expect(capture.stacks[1][2].relations[0].size() == 1
        && capture.stacks[1][2].relations[0][0].side == 0
        && capture.stacks[1][2].relations[0][0].slot == 0,
        "only occupied relation targets survive");

    const CodecLinkDropReport_ again = CodecNormalizeStaleLinks_(capture);
    Expect(again.aiTargets == 0 && again.relationEntries == 0, "normalization is idempotent");

    // 半初始化配对（side=-1 但 slot>=0）同样清空——策略层对这种形态是拒绝项。
    capture.stacks[0][3].aiTarget = {-1, 7};
    const CodecLinkDropReport_ mismatched = CodecNormalizeStaleLinks_(capture);
    Expect(mismatched.aiTargets == 1
        && capture.stacks[0][3].aiTarget.side == -1
        && capture.stacks[0][3].aiTarget.slot == -1,
        "mismatched ai target pair cleared");
    printf("PASS stale link normalization: ai targets, relations, idempotence\n");
}

static void TestDisplayCaches_()
{
    std::unique_ptr<CodecCapture> saved(new CodecCapture{}), actual(new CodecCapture{});
    saved->version = kCodecVersion;
    size_t packed = 0;
    for (const auto& range : kManagerExtraRanges_) {
        if (range.offset == 0x14031u) break;
        packed += range.size;
    }
    Expect(packed + 187 == sizeof(saved->extraScalars), "native path occupies full final range");
    for (int i = 0; i < 187; ++i) {
        saved->accessibleSquares[i] = (uint8_t)(i % 4);
        saved->accessibleSquares2[i] = (uint8_t)((i + 1) % 4);
        saved->extraScalars[packed + i] = (uint8_t)(i % 3 == 0);
    }
    std::vector<uint8_t> native(0x140ED, 0xA5);
    // Both shade settings have the same cache restore contract. Display gating
    // stays in the game, so stale nonzero bytes must survive even with shade off.
    for (int shadeOption = 0; shadeOption < 2; ++shadeOption) {
        std::fill(native.begin(), native.end(), (uint8_t)0xA5);
        for (int repeat = 0; repeat < 3; ++repeat)
            CodecRestoreDisplayCaches_(native.data() + 0x4C, native.data() + 0x107,
                native.data() + 0x14031, *saved);
        Expect(!memcmp(native.data() + 0x4C, saved->accessibleSquares, 187), "previous shade cache restored");
        Expect(!memcmp(native.data() + 0x107, saved->accessibleSquares2, 187), "current shade cache restored even if disabled");
        Expect(!memcmp(native.data() + 0x14031, saved->extraScalars + packed, 187), "native path exact after repeated restore");
        Expect(native[0x4B] == 0xA5 && native[0x1C2] == 0xA5
            && native[0x14030] == 0xA5 && native[0x140EC] == 0xA5, "cache boundaries preserve adjacent state");
    }
    *actual = *saved;
    actual->mouseCoord = 43;
    CodecInvalidateHover_(actual.get());
    Expect(!memcmp(actual->accessibleSquares, saved->accessibleSquares, 187)
        && !memcmp(actual->accessibleSquares2, saved->accessibleSquares2, 187)
        && !memcmp(actual->extraScalars, saved->extraScalars, sizeof(saved->extraScalars)),
        "hover invalidation never erases shade or path caches");
    std::vector<hbs::ArchiveSection> expected, changed;
    std::string error;
    Expect(CodecEncode(*actual, &expected, &error), "encode display cache baseline");
    const size_t tailSize = 3 * (7 * sizeof(int32_t) + 2 * 13) + sizeof(saved->wallPcxNames);
    // v8：battle 段删除 2×U16 法力字段（只存 heroes 段），锚点前移 4 字节。
    const size_t extraStart = expected[0].bytes.size() - tailSize - sizeof(saved->extraScalars);
    Expect(extraStart == 732, "empty eagle-eye layout places manager extras at 732 (v8)");
    actual->extraScalars[packed + 157] ^= 1;
    Expect(CodecEncode(*actual, &changed, &error), "encode corrupt path cache");
    size_t first = 0;
    Expect(!CodecSectionEqual_(expected[0], changed[0], &first)
        && first == extraStart + packed + 157, "strict verification detects path corruption at 1037");
    *actual = *saved;
    actual->eagleEye[0].push_back(5);
    Expect(CodecEncode(*actual, &changed, &error)
        && changed[0].bytes.size() - tailSize - sizeof(saved->extraScalars) == extraStart + 4,
        "eagle-eye entries shift serialized cache offsets");
    *actual = *saved;
    memset(actual->accessibleSquares2, 0, sizeof(actual->accessibleSquares2));
    CodecInvalidateHover_(actual.get());
    CodecInvalidateHover_(saved.get());
    Expect(CodecEncode(*saved, &expected, &error) && CodecEncode(*actual, &changed, &error)
        && !CodecSectionEqual_(expected[0], changed[0], &first), "strict verification detects lost shallow shading");
    printf("PASS display caches: settings-independent restore, native boundaries, hover preservation, strict diffs\n");
}

static void TestV5Roundtrip_()
{
    static_assert(sizeof(((CodecTower_*)0)->scalars) == 7 * sizeof(int32_t), "seven tower scalars");
    static_assert(sizeof(((CodecTower_*)0)->defName) == 13 && sizeof(((CodecTower_*)0)->missileName) == 13,
        "bounded tower resource names");
    static_assert(sizeof(((CodecCapture*)0)->wallPcxNames) == 90 * 13, "ninety bounded wall names");
    static_assert(sizeof(((CodecStack*)0)->extraScalars) == StackExtraSize_(), "fixed stack numeric ranges");
    static_assert(sizeof(((CodecCapture*)0)->extraScalars) == ManagerExtraSize_(), "fixed manager numeric ranges");
    static_assert(sizeof(((CodecSquare*)0)->extraScalars) == SquareExtraSize_(), "fixed square numeric ranges");
    std::unique_ptr<CodecCapture> source(new CodecCapture{}), decoded(new CodecCapture{});
    Expect(source->stacks[0][0].aiTarget.side == -1 && source->stacks[0][0].aiTarget.slot == -1,
        "default AI target is NULL pair");
    uint32_t random = 0x13579BDFu;
    for (int iteration = 0; iteration < 16; ++iteration) {
        source->version = kCodecVersion;
        for (auto& byte : source->extraScalars) byte = static_cast<uint8_t>(FixtureNext_(random) >> 24);
        for (int side = 0; side < 2; ++side)
            for (int slot = 0; slot < 20; ++slot) {
                CodecStack& stack = source->stacks[side][slot];
                stack.occupied = true;
                for (auto& byte : stack.extraScalars) byte = static_cast<uint8_t>(FixtureNext_(random) >> 24);
                stack.aiTarget = (slot % 3) ? CodecIdentity{1 - side, (slot + iteration) % 20} : CodecIdentity{-1, -1};
            }
        for (int tower = 0; tower < 3; ++tower) {
            for (auto& scalar : source->towers[tower].scalars) scalar = static_cast<int32_t>(FixtureNext_(random));
            sprintf_s(source->towers[tower].defName, "T%02d%02d.DEF", iteration, tower);
            sprintf_s(source->towers[tower].missileName, "M%02d%02d.DEF", iteration, tower);
        }
        for (int wall = 0; wall < 90; ++wall)
            sprintf_s(source->wallPcxNames[wall], "W%02d%02d.PCX", iteration, wall);
        strcpy_s(source->towers[2].defName, "12345678.DEF");
        strcpy_s(source->towers[2].missileName, "ABCDEFGH.DEF");
        strcpy_s(source->wallPcxNames[89], "12345678.PCX");
        for (auto& square : source->squares)
            for (auto& byte : square.extraScalars) byte = static_cast<uint8_t>(FixtureNext_(random) >> 24);
        std::vector<hbs::ArchiveSection> first, second;
        std::string error;
        const bool ok = CodecEncode(*source, &first, &error) && CodecDecode(first, decoded.get(), &error)
            && CodecEncode(*decoded, &second, &error);
        Expect(ok && decoded->version == kCodecVersion, "v6 randomized decode and reencode");
        Expect(ok && first.size() == second.size(), "v6 randomized section count");
        if (ok && first.size() == second.size())
            for (size_t i = 0; i < first.size(); ++i)
                Expect(CodecSectionEqual_(first[i], second[i], nullptr), "v5 randomized byte-exact roundtrip");
        Expect(memcmp(source->extraScalars, decoded->extraScalars, sizeof(source->extraScalars)) == 0,
            "v5 manager numeric bytes preserved");
        Expect(memcmp(source->wallPcxNames, decoded->wallPcxNames, sizeof(source->wallPcxNames)) == 0,
            "v5 all wall names preserved");
        for (int square = 0; square < 187; ++square)
            Expect(memcmp(source->squares[square].extraScalars, decoded->squares[square].extraScalars,
                sizeof(source->squares[square].extraScalars)) == 0, "v5 omitted square numeric bytes preserved");
        for (int tower = 0; tower < 3; ++tower) {
            Expect(memcmp(source->towers[tower].scalars, decoded->towers[tower].scalars,
                sizeof(source->towers[tower].scalars)) == 0, "v5 tower frames and all scalars preserved");
            Expect(strcmp(source->towers[tower].defName, decoded->towers[tower].defName) == 0
                && strcmp(source->towers[tower].missileName, decoded->towers[tower].missileName) == 0,
                "v5 tower resource names preserved");
        }
        for (int side = 0; side < 2; ++side)
            for (int slot = 0; slot < 20; ++slot) {
                const CodecStack& a = source->stacks[side][slot];
                const CodecStack& b = decoded->stacks[side][slot];
                Expect(memcmp(a.extraScalars, b.extraScalars, sizeof(a.extraScalars)) == 0
                    && a.aiTarget.side == b.aiTarget.side && a.aiTarget.slot == b.aiTarget.slot,
                    "v5 stack numeric bytes and AI identities preserved");
            }
    }
}

int main()
{
    TestMapFingerprint_();
    TestWarMachineEquipment_();
    TestControlStatePolicy_();
    TestNormalizeStaleLinks_();
    TestV5Roundtrip_();
    std::unique_ptr<CodecCapture> captureStorage(new CodecCapture{});
    CodecCapture& capture = *captureStorage;
    capture.version = kCodecVersion;
    capture.turn = 7;
    capture.currentMonSide = 1;
    capture.currentMonIndex = 3;
    capture.spellPoints[0] = 12;
    capture.spellPoints[1] = -4;
    capture.rngTlsSeed = 0x12345678;
    capture.rngMirrorSeed = 0x89ABCDEF;
    capture.fortWallsHp[17] = 900;
    capture.stacks[0][2].occupied = true;
    capture.stacks[0][2].type = 45;
    capture.stacks[0][2].numberAlive = 18;
    capture.stacks[0][2].healthLost = 9;
    capture.stacks[0][2].infoFlags = 0x1234ABCDu;
    for (int i = 0; i < 8; ++i) capture.stacks[0][2].infoCombat[i] = 100 + i;
    capture.stacks[0][2].defendingDelta = 7;
    capture.stacks[0][2].animationSpeed = 120;
    capture.stacks[0][2].movementDirection = 4;
    capture.stacks[0][2].renderOffsetY = -31;
    capture.stacks[0][2].renderOffsetX = 42;
    capture.heroCasted[0] = 1;
    capture.heroCasted[1] = 0xFFFFFFFFu;
    capture.stacks[0][2].activeSpellDuration[5] = 3;
    capture.stacks[0][2].spellIds.push_back(5);
    capture.stacks[0][2].spellIds.push_back(80);
    capture.stacks[0][2].relations[2].push_back(CodecIdentity{1, 4});
    capture.squares[186].deadStacksNumber = 2;
    capture.squares[186].deadStackSide[1] = 1;
    capture.squares[186].deadStackIndex[1] = 20;
    CodecObstacle obstacle = {};
    obstacle.kindId = 100; obstacle.anchorHex = 22; obstacle.ownerSide = -1;
    obstacle.featureTriggered = 1; obstacle.featureDamage = 50; obstacle.featureDuration = 2;
    obstacle.cellCount = 1; obstacle.cells[0] = 22;
    memcpy(obstacle.defName, "C17SPE1.DEF", sizeof("C17SPE1.DEF"));
    capture.obstacles.push_back(obstacle);
    capture.logLines.push_back(std::string("line-a"));
    capture.logLines.push_back(std::string(300, 'x'));
    capture.eagleEye[1].push_back(42);

    std::vector<hbs::ArchiveSection> sections;
    std::string error;
    Expect(CodecEncode(capture, &sections, &error), "encode");
    std::unique_ptr<CodecCapture> decodedStorage(new CodecCapture{});
    CodecCapture& decoded = *decodedStorage;
    Expect(CodecDecode(sections, &decoded, &error), "decode");
    Expect(decoded.turn == 7 && decoded.currentMonIndex == 3, "battle scalars");
    Expect(decoded.spellPoints[1] == -4, "signed spell points");
    {
        // v8 升版回归：旧 v7 段版本必须整体拒绝（永不兼容旧档，玩家重新存档）。
        std::vector<hbs::ArchiveSection> v7Sections = sections;
        v7Sections[0].bytes[0] = 7;
        std::unique_ptr<CodecCapture> legacy(new CodecCapture{});
        Expect(!CodecDecode(v7Sections, legacy.get(), &error), "legacy v7 sections rejected");
    }
    Expect(decoded.fortWallsHp[17] == 900, "fort hp");
    Expect(decoded.stacks[0][2].numberAlive == 18, "stack count");
    Expect(decoded.stacks[0][2].infoFlags == 0x1234ABCDu, "info flags width");
    Expect(decoded.heroCasted[0] == 1 && decoded.heroCasted[1] == 0xFFFFFFFFu, "hero casted bool32");
    std::unique_ptr<CodecCapture> preCast(new CodecCapture(capture)), preCastRead(new CodecCapture{});
    preCast->heroCasted[0] = preCast->heroCasted[1] = 0;
    std::vector<hbs::ArchiveSection> preCastSections;
    Expect(CodecEncode(*preCast, &preCastSections, &error)
        && CodecDecode(preCastSections, preCastRead.get(), &error)
        && preCastRead->heroCasted[0] == 0 && preCastRead->heroCasted[1] == 0,
        "unspent hero cast flags survive archive codec");
    Expect(CodecSpellControlEnabled_(false, true, preCastRead->heroCasted[0], false, true)
        && !CodecSpellControlEnabled_(false, true, decoded.heroCasted[0], false, true),
        "decoded pre-cast and spent snapshots produce opposite spellbook gates");
    for (int i = 0; i < 8; ++i)
        Expect(decoded.stacks[0][2].infoCombat[i] == 100 + i, "combat info numeric fields");
    Expect(decoded.stacks[0][2].defendingDelta == 7 && decoded.stacks[0][2].animationSpeed == 120,
        "defence delta and animation speed");
    Expect(decoded.stacks[0][2].movementDirection == 4 && decoded.stacks[0][2].renderOffsetY == -31
        && decoded.stacks[0][2].renderOffsetX == 42, "render position and direction roundtrip");
    Expect(decoded.stacks[0][2].activeSpellDuration[5] == 3, "spell duration");
    Expect(decoded.stacks[0][2].spellIds.size() == 2 && decoded.stacks[0][2].spellIds[1] == 80, "spell ids");
    Expect(decoded.stacks[0][2].relations[2].size() == 1 && decoded.stacks[0][2].relations[2][0].slot == 4, "relations");
    Expect(decoded.squares[186].deadStackIndex[1] == 20, "square corpses");
    Expect(decoded.obstacles.size() == 1 && decoded.obstacles[0].featureDamage == 50
        && decoded.obstacles[0].kindId == 100 && decoded.obstacles[0].cellCount == 1
        && decoded.obstacles[0].cells[0] == 22
        && strcmp(decoded.obstacles[0].defName, "C17SPE1.DEF") == 0, "v4 obstacle payload roundtrip");
    Expect(decoded.logLines.size() == 2 && decoded.logLines[1].size() == 300, "full log line");

    std::unique_ptr<CodecCapture> hoverStorage(new CodecCapture(decoded));

    CodecCapture& hover = *hoverStorage;
    hover.mouseCoord = 22; hover.creatureAtMousePos = 22; hover.attackerCoord = 23; hover.moveType = 7;
    hover.stacks[0][2].highlightContour = 1;
    CodecInvalidateHover_(&hover);
    TestDisplayCaches_();
    Expect(hover.mouseCoord == -1 && hover.creatureAtMousePos == -1 && hover.attackerCoord == -1
        && hover.moveType == -99 && !hover.stacks[0][2].highlightContour, "hover invalidated");
    Expect(hover.stacks[0][2].numberAlive == decoded.stacks[0][2].numberAlive
        && hover.rngTlsSeed == decoded.rngTlsSeed && hover.turn == decoded.turn,
        "hover invalidation preserves authoritative data");
    std::vector<hbs::ArchiveSection> hoverSections;
    std::unique_ptr<CodecCapture> hoverRead(new CodecCapture{});
    Expect(CodecEncode(hover, &hoverSections, &error) && CodecDecode(hoverSections, hoverRead.get(), &error)
        && hoverRead->mouseCoord == -1 && hoverRead->creatureAtMousePos == -1
        && hoverRead->attackerCoord == -1 && hoverRead->moveType == -99
        && !hoverRead->stacks[0][2].highlightContour,
        "save payload contains no current mouse intent");
    std::vector<hbs::ArchiveSection> roundtrip;
    Expect(CodecEncode(decoded, &roundtrip, &error), "encode decoded snapshot");
    size_t first = 0;
    for (size_t i = 0; i < sections.size(); ++i)
        Expect(CodecSectionEqual_(sections[i], roundtrip[i], &first), "roundtrip section exact");
    hbs::ArchiveSection changed = sections[0];
    changed.bytes[10] ^= 1;
    Expect(!CodecSectionEqual_(sections[0], changed, &first) && first == 10, "diff offset");
    changed = sections[0];
    changed.bytes.pop_back();
    Expect(!CodecSectionEqual_(sections[0], changed, &first)
        && first == changed.bytes.size(), "short section diff");
    changed = sections[0];
    changed.bytes.push_back(0);
    Expect(!CodecSectionEqual_(sections[0], changed, &first)
        && first == sections[0].bytes.size(), "long section diff");
    changed = sections[0];
    changed.id = 99;
    Expect(!CodecSectionEqual_(sections[0], changed, &first), "section id mismatch");

    for (size_t i = 0; i < sections.size(); ++i) {
        std::vector<hbs::ArchiveSection> bad = sections;
        bad[i].bytes[0] = 99;
        error.clear();
        Expect(!CodecDecode(bad, &decoded, &error) && !error.empty(), "bad version has diagnostic");
        bad = sections;
        bad[i].bytes[0] = 4;
        error.clear();
        Expect(!CodecDecode(bad, &decoded, &error) && !error.empty(), "v4 section explicitly rejected by v5 codec");
        bad = sections;
        bad[i].bytes.pop_back();
        error.clear();
        Expect(!CodecDecode(bad, &decoded, &error) && !error.empty(), "truncation has diagnostic");
    }
    {
        std::vector<hbs::ArchiveSection> malformed = roundtrip;
        malformed[3].bytes[25] = 9; // first obstacle cellCount after v4 scalar payload
        Expect(!CodecDecode(malformed, &decoded, &error), "v4 oversized cellCount rejected on decode");
        malformed = roundtrip;
        malformed[3].bytes[27] = 16; // name length after one cell
        Expect(!CodecDecode(malformed, &decoded, &error), "v4 oversized defName rejected on decode");
        malformed = roundtrip;
        malformed[0].bytes[0] = 3;
        Expect(!CodecDecode(malformed, &decoded, &error), "v3 codec explicitly rejected");
        std::unique_ptr<CodecCapture> invalidStorage(new CodecCapture(capture));
        CodecCapture& invalid = *invalidStorage;
        invalid.obstacles[0].cellCount = 9;
        Expect(!CodecEncode(invalid, &sections, &error), "v4 oversized cellCount rejected before read");
        invalid = capture;
        memset(invalid.obstacles[0].defName, 'x', sizeof(invalid.obstacles[0].defName));
        Expect(!CodecEncode(invalid, &sections, &error), "v4 unterminated defName rejected on encode");
    }
    Expect(CodecDecode(roundtrip, &decoded, &error), "reuse decoded object");
    Expect(decoded.logLines.size() == 2 && decoded.stacks[0][2].spellIds.size() == 2,
        "reuse clears old containers");

    capture.stacks[1][0].spellIds.push_back(81);
    Expect(!CodecEncode(capture, &sections, &error), "reject invalid spell id");
    capture.stacks[1][0].spellIds.clear();
    capture.logLines.push_back(std::string(70000, 'y'));
    Expect(!CodecEncode(capture, &sections, &error), "reject overlong log");

    std::unique_ptr<CodecCapture> stableStorage(new CodecCapture{});

    CodecCapture& stable = *stableStorage;
    stable.version = kCodecVersion;
    stable.isHuman[0] = 1;
    for (int i = 0; i < 187; ++i) { stable.squares[i].stackSide = -1; stable.squares[i].stackIndex = -1; }
    CodecStack& unit = stable.stacks[0][0];
    unit.occupied = true; unit.type = 45; unit.side = 0; unit.sideIndex = 0;
    unit.numberAtStart = 10; unit.numberAlive = 10;
    unit.infoCombat[0] = 20; unit.position = 22;
    stable.squares[22].stackSide = 0; stable.squares[22].stackIndex = 0; stable.squares[22].twoHexMonsterSquare = 0xFF;
    Expect(RestorePolicy_(stable, &error), "stable movement damage waiting restore accepted");
    std::unique_ptr<CodecCapture> badStorage(new CodecCapture(stable));
    CodecCapture& bad = *badStorage;
    bad.stacks[0][0].infoCombat[0] = 0;
    Expect(!RestorePolicy_(bad, &error), "zero HP rejected");
    bad = stable; bad.stacks[0][0].spellIds.push_back(5);
    Expect(RestorePolicy_(bad, &error), "changed legal spell deque accepted for prepared replacement");
    bad = stable; bad.stacks[0][1] = unit;
    bad.stacks[0][1].type = 46; bad.stacks[0][1].sideIndex = 1; bad.stacks[0][1].position = 23;
    bad.heroMonCount[0] = 2;
    bad.squares[23].stackSide = 0; bad.squares[23].stackIndex = 1; bad.squares[23].twoHexMonsterSquare = 0xFF;
    Expect(RestorePolicy_(bad, &error), "legal summon topology accepted for native construction");
    bad = stable; bad.stacks[0][0].type = 46;
    Expect(RestorePolicy_(bad, &error), "legal changed creature type accepted for native reconstruction");
    bad = stable; bad.stacks[0][0].relations[0].push_back({0, 0});
    Expect(RestorePolicy_(bad, &error), "legal changed relation accepted for prepared replacement");
    bad = stable; bad.stacks[0][0].spellIds.push_back(81);
    Expect(!RestorePolicy_(bad, &error), "invalid prepared spell id rejected");
    bad = stable; bad.stacks[0][0].relations[0].push_back({1, 1});
    Expect(!RestorePolicy_(bad, &error), "relation to absent saved target rejected");
    bad = stable; bad.stacks[0][0].sideIndex = 1;
    Expect(!RestorePolicy_(bad, &error), "saved stack slot identity rejected");
    bad = stable; bad.currentActiveSide = 2;
    Expect(!RestorePolicy_(bad, &error), "active side bounds before indexing");
    bad = stable;
    bad.stacks[0][20].occupied = true;
    bad.stacks[1][20].occupied = true;
    // Reserved slot 20 is partially constructed native storage: constructor
    // residue is saved and written back as-is, never required to be empty.
    Expect(RestorePolicy_(bad, &error), "zero-constructed reserved slots accepted");
    bad.stacks[0][20].retaliations = 1;
    bad.stacks[0][20].side = 0; bad.stacks[0][20].sideIndex = 20; bad.stacks[0][20].slotIndex = 20;
    bad.stacks[0][20].numberAlive = 1; bad.stacks[0][20].position = 22;
    Expect(RestorePolicy_(bad, &error), "reserved constructor and combat residue accepted");
    {
        std::vector<hbs::ArchiveSection> reservedSections;
        std::unique_ptr<CodecCapture> reservedDecoded(new CodecCapture{});
        Expect(CodecEncode(bad, &reservedSections, &error)
            && CodecDecode(reservedSections, reservedDecoded.get(), &error)
            && reservedDecoded->stacks[0][20].occupied
            && reservedDecoded->stacks[0][20].retaliations == 1
            && reservedDecoded->stacks[0][20].sideIndex == 20
            && reservedDecoded->stacks[0][20].numberAlive == 1,
            "reserved slot residue survives exact codec roundtrip");
    }
    bad.stacks[0][20].type = 0x96;
    Expect(!RestorePolicy_(bad, &error) && error == "reserved slot scalar outside valid range",
        "reserved slot creature type outside range rejected");
    bad.stacks[0][20].type = 0; bad.stacks[0][20].activeSpellNumber = 82;
    Expect(!RestorePolicy_(bad, &error) && error == "reserved slot scalar outside valid range",
        "reserved slot spell count outside range rejected");
    bad.stacks[0][20].activeSpellNumber = 0; bad.stacks[0][20].aiTarget = {-1, 0};
    Expect(!RestorePolicy_(bad, &error) && error == "reserved slot AI target invalid",
        "reserved malformed NULL AI pair rejected");
    bad = stable; bad.stacks[0][0].type = 149;
    Expect(!RestorePolicy_(bad, &error), "attacker-side arrow tower on battlefield rejected");
    bad = stable; bad.squares[23].stackSide = 0; bad.squares[23].stackIndex = 20;
    bad.stacks[0][20].occupied = true;
    Expect(!RestorePolicy_(bad, &error), "battlefield reference to reserved slot rejected");
    bad = stable; bad.heroMonCount[0] = 21;
    Expect(!RestorePolicy_(bad, &error), "participant count overflow rejected");
    bad = stable; bad.squares[23].deadStacksNumber = 1;
    bad.squares[23].deadStackSide[0] = 0; bad.squares[23].deadStackIndex[0] = 20;
    Expect(!RestorePolicy_(bad, &error), "saved corpse reserved reference rejected");
    bad = stable; bad.stacks[0][0].relations[0].push_back({0, 20});
    Expect(!RestorePolicy_(bad, &error), "regular stack reserved relation rejected");
    bad = stable; bad.stacks[0][0].healthLost = 20;
    Expect(!RestorePolicy_(bad, &error), "invalid health loss rejected");
    bad = stable; bad.squares[22].stackIndex = 1;
    Expect(!RestorePolicy_(bad, &error), "inconsistent battlefield occupancy rejected");
    bad = stable; bad.siegeKind2 = 1;
    Expect(RestorePolicy_(bad, &error), "legal attack siege accepted without old gate");
    for (int door : {-1, 4, INT32_MIN, INT32_MAX}) {
        bad = stable; bad.siegeKind = door;
        Expect(RestorePolicy_(bad, &error), "non-fortified stale door value is restorable raw state");
        std::vector<hbs::ArchiveSection> doorSections;
        std::unique_ptr<CodecCapture> doorDecoded(new CodecCapture{});
        Expect(CodecEncode(bad, &doorSections, &error) && CodecDecode(doorSections, doorDecoded.get(), &error)
            && doorDecoded->siegeKind == door, "non-fortified stale door value survives exact codec roundtrip");
        bad.siegeKind2 = 1;
        Expect(!RestorePolicy_(bad, &error) && error == "城门状态超出有效范围",
            "fortified invalid door value still rejected before resource indexing");
    }
    bad = stable; bad.artifactAutoCast[0] = 1; bad.artifactAutoCast[1] = 1;
    Expect(RestorePolicy_(bad, &error), "artifact autocast changes are restorable authoritative state");
    const CodecIdentity invalidAi[] = {{2, 0}, {-2, 0}, {0, 20}, {0, -1}, {-1, 0}, {1, 19}};
    for (const auto& id : invalidAi) {
        bad = stable; bad.stacks[0][0].aiTarget = id;
        Expect(!RestorePolicy_(bad, &error), "invalid AI side/slot/NULL pair/absent target rejected");
    }
    bad = stable; bad.stacks[0][0].aiTarget = {0, 0};
    Expect(RestorePolicy_(bad, &error), "AI identity may refer to an occupied saved stack");
    for (uint32_t offset : {0x450u, 0x4A4u}) {
        bad = stable; SetExtraWord_(bad.stacks[0][0], offset, 0x7FC00000u);
        Expect(!RestorePolicy_(bad, &error), "NaN in omitted stack float rejected");
    }
    bad = stable; bad.stacks[0][20].occupied = true;
    Expect(RestorePolicy_(bad, &error), "reserved constructor accepts semantic NULL AI identity");
    bad.stacks[0][20].aiTarget = {0, 0};
    Expect(RestorePolicy_(bad, &error), "reserved AI identity may refer to an occupied saved stack");
    bad.stacks[0][20].aiTarget = {-1, 0};
    Expect(!RestorePolicy_(bad, &error), "reserved malformed NULL pair rejected");

    // Real logs reported differing empty-square residue, not occupied-stack divergence.
    bad = stable; bad.squares[60].twoHexMonsterSquare = 0; bad.squares[61].twoHexMonsterSquare = 3;
    Expect(RestorePolicy_(bad, &error), "real log hex60 saved0 and hex61 saved3 empty residue accepted");

    {
        std::unique_ptr<CodecCapture> siegeStorage(new CodecCapture(stable));
        CodecCapture& siege = *siegeStorage;
        siege.siegeKind2 = 3;
        const int positions[] = {254, 251, 255};
        for (int tower = 0; tower < 3; ++tower) {
            CodecStack& stack = siege.stacks[1][tower];
            stack = unit; stack.type = 149; stack.side = 1; stack.sideIndex = tower;
            stack.position = positions[tower];
            CodecTower_& savedTower = siege.towers[tower];
            savedTower.scalars[0] = 149; savedTower.scalars[4] = 2;
            savedTower.scalars[5] = 4; savedTower.scalars[6] = tower;
            strcpy_s(savedTower.defName, "TOWER.DEF"); strcpy_s(savedTower.missileName, "SHOT.DEF");
            Expect(CodecStackPositionValid_(stack), "side1 arrow tower pseudohex accepted");
        }
        siege.fortWallsHp[0] = 1000000; siege.fortWallsAlive[0] = 4;
        strcpy_s(siege.wallPcxNames[89], "WALL.PCX");
        Expect(RestorePolicy_(siege, &error), "three legal siege towers skip battlefield indexing");
        for (int position : {187, 250, 252, 253, 256, -1, 22}) {
            bad = siege; bad.stacks[1][0].position = position;
            Expect(!RestorePolicy_(bad, &error), "illegal tower position rejected before square indexing");
        }
        bad = siege; bad.stacks[1][0].side = 0;
        Expect(!CodecStackPositionValid_(bad.stacks[1][0]) && !RestorePolicy_(bad, &error),
            "attacker-side tower rejected");
        bad = siege; bad.towers[0].scalars[6] = 20;
        Expect(!RestorePolicy_(bad, &error), "tower reserved-slot association rejected");
        bad = siege; bad.towers[0].scalars[6] = 1;
        Expect(!RestorePolicy_(bad, &error), "tower association must match its exact pseudohex");
        bad = siege; bad.stacks[1][0].type = 45;
        Expect(!RestorePolicy_(bad, &error), "tower resource association requires type149");
        bad = siege; bad.stacks[1][0].occupied = false;
        Expect(!RestorePolicy_(bad, &error), "tower resource association requires occupied slot");
        // 2026-10-10 用户裁定：塔帧号（scalars[5]）数值界删除——存档值来自正在
        // 正常渲染的塔，同名资源即同一对象，帧号必然合法。
        bad = siege; bad.towers[0].scalars[5] = -1;
        Expect(RestorePolicy_(bad, &error), "negative tower frame trusted by policy (v8)");
        bad = siege; memset(bad.towers[0].defName, 'x', 13);
        Expect(!RestorePolicy_(bad, &error), "unterminated tower resource name rejected");
        bad = siege; memset(bad.wallPcxNames[89], 'x', 13);
        Expect(!RestorePolicy_(bad, &error), "unterminated last wall resource name rejected");
        // 2026-10-10 用户裁定：HP 上限删除（拍脑袋界），非负保留。
        bad = siege; bad.fortWallsHp[17] = -1;
        Expect(!RestorePolicy_(bad, &error), "negative wall HP rejected");
        bad = siege; bad.fortWallsHp[17] = 1000001;
        Expect(RestorePolicy_(bad, &error), "large wall HP trusted by policy (v8)");
        for (int frame : {-1, 5}) {
            bad = siege; bad.fortWallsAlive[17] = frame;
            Expect(!RestorePolicy_(bad, &error), "wall frame outside zero to four rejected");
        }
        bad = stable; bad.stacks[0][0].position = 187;
        Expect(!CodecStackPositionValid_(bad.stacks[0][0]) && !RestorePolicy_(bad, &error),
            "ordinary stack offboard position rejected before indexing");
        bad = stable; bad.stacks[0][0].position = 186;
        bad.squares[22] = stable.squares[23]; bad.squares[186] = stable.squares[22];
        Expect(RestorePolicy_(bad, &error), "ordinary stack final legal square186 accepted");
        // 2026-10-10 用户裁定：浮点只查有限性（幅度界删除）——NaN 会骗过逐字节
        // verify（自比较"相等"），必须事前拦截；有限大值信任游戏。
        {
            const uint32_t nanBits = 0x7FC00000u, infBits = 0x7F800000u;
            float nanValue = 0, infValue = 0;
            memcpy(&nanValue, &nanBits, sizeof(nanValue));
            memcpy(&infValue, &infBits, sizeof(infValue));
            bad = stable; bad.stacks[0][0].frenzyMultiplier = nanValue;
            Expect(!RestorePolicy_(bad, &error), "NaN spell effect rejected");
            bad = stable; bad.stacks[0][0].slowEffect = infValue;
            Expect(!RestorePolicy_(bad, &error), "infinite spell effect rejected");
            bad = stable; bad.stacks[0][0].frenzyMultiplier = 900000.0f;
            Expect(RestorePolicy_(bad, &error), "finite large spell effect trusted (v8)");
        }
    }
    bad = stable; bad.version = 2;
    Expect(!RestorePolicy_(bad, &error), "old incomplete v2 rejected");
    bad = stable; bad.logLines.push_back("alternate future");
    Expect(RestorePolicy_(bad, &error), "alternate log branch accepts preallocated replacement");

    {
        std::unique_ptr<CodecCapture> obstacleSavedStorage(new CodecCapture(stable));
        CodecCapture& obstacleSaved = *obstacleSavedStorage;
        obstacleSaved.obstacles.push_back(obstacle);
        obstacleSaved.squares[22].obstacleBits = 0x05;
        Expect(RestorePolicy_(obstacleSaved, &error), "v4 saved obstacle accepted for rebuild");
        Expect(RestorePolicy_(stable, &error), "v4 obstacle-free capture accepted");
        std::unique_ptr<CodecCapture> invalidStorage(new CodecCapture(obstacleSaved));
        CodecCapture& invalid = *invalidStorage;
        invalid.obstacles.push_back(obstacle);
        Expect(!RestorePolicy_(invalid, &error), "v4 duplicate key and anchor rejected");
        invalid = obstacleSaved; invalid.obstacles[0].kindId = 91;
        Expect(!RestorePolicy_(invalid, &error), "v4 unknown kind rejected");
        invalid = obstacleSaved; invalid.obstacles[0].ownerSide = 2;
        Expect(!RestorePolicy_(invalid, &error), "v4 invalid owner rejected");
        invalid = obstacleSaved; invalid.obstacles[0].anchorHex = 187;
        Expect(!RestorePolicy_(invalid, &error), "v4 offboard anchor rejected");
        invalid = obstacleSaved; invalid.obstacles[0].cells[0] = 187;
        Expect(!RestorePolicy_(invalid, &error), "v4 offboard cell rejected");
        invalid = obstacleSaved; invalid.obstacles[0].defName[0] = 0;
        Expect(!RestorePolicy_(invalid, &error), "v4 empty resource name rejected");
        invalid = obstacleSaved; invalid.squares[22].obstacleBits = 1;
        Expect(RestorePolicy_(invalid, &error), "v4 grid bits are independent saved data");
        invalid = obstacleSaved; invalid.obstacles[0].cells[0] = 23;
        invalid.squares[22].obstacleBits = 0; invalid.squares[23].obstacleBits = 5;
        Expect(RestorePolicy_(invalid, &error), "v4 shifted grid anchor flag accepted as recorded");
        invalid = obstacleSaved;
        CodecObstacle second = obstacle; second.anchorHex = 23;
        invalid.obstacles.push_back(second); invalid.squares[23].obstacleBits = 1;
        Expect(!RestorePolicy_(invalid, &error), "v4 shared relative cell rejected");
        invalid = obstacleSaved; invalid.obstacles[0].cells[0] = 23;
        invalid.squares[22].obstacleBits = 1; invalid.squares[23].obstacleBits = 4;
        Expect(RestorePolicy_(invalid, &error), "v4 anchor need not be a relative cell");
        invalid = stable;
        invalid.obstacles.assign(4096, obstacle);
        Expect(!RestorePolicy_(invalid, &error), "v4 large duplicate list never wraps claim marker");
        std::vector<CodecObstacle> ordered(3, obstacle);
        ordered[0].kindId = 101; ordered[0].anchorHex = 22;
        ordered[1].kindId = 100; ordered[1].anchorHex = 40;
        ordered[2].kindId = 100; ordered[2].anchorHex = 23;
        std::sort(ordered.begin(), ordered.end(), CodecObstacleKeyLess_);
        Expect(ordered[0].kindId == 100 && ordered[0].anchorHex == 23
            && ordered[1].kindId == 100 && ordered[1].anchorHex == 40
            && ordered[2].kindId == 101, "v4 active payload sort is kind then anchor");
        printf("PASS obstacles v4: payload, length gates, derived bits, overlap, rebuild policy, canonical sort\n");
    }

    bad = stable; bad.stacks[0][0].infoFlags |= 1;
    Expect(!RestorePolicy_(bad, &error), "double-wide missing second hex rejected");
    bad.squares[21].stackSide = 0; bad.squares[21].stackIndex = 0; bad.squares[21].twoHexMonsterSquare = 0; bad.squares[22].twoHexMonsterSquare = 1;
    Expect(RestorePolicy_(bad, &error), "double-wide left second hex accepted");
    bad.stacks[0][0].secondHexOrientation = 1;
    bad.squares[21] = stable.squares[21];
    bad.squares[22].twoHexMonsterSquare = 0;
    bad.squares[23].stackSide = 0; bad.squares[23].stackIndex = 0; bad.squares[23].twoHexMonsterSquare = 1;
    Expect(RestorePolicy_(bad, &error), "double-wide right second hex accepted");
    bad.squares[23].twoHexMonsterSquare = 0;
    Expect(!RestorePolicy_(bad, &error), "double-wide incorrect flag rejected");

    if (g_failures) {
        printf("%d codec test(s) failed\n", g_failures);
        return 1;
    }
    printf("codec tests passed\n");
    return 0;
}
