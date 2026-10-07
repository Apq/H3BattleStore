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

static int g_failures = 0;

static void Expect(bool condition, const char* name)
{
    if (!condition) {
        printf("FAIL %s\n", name);
        ++g_failures;
    }
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
    const size_t extraStart = expected[0].bytes.size() - tailSize - sizeof(saved->extraScalars);
    Expect(extraStart == 736, "empty eagle-eye layout places manager extras at 736");
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

int wmain(int argc, wchar_t** argv)
{
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
    Expect(decoded.fortWallsHp[17] == 900, "fort hp");
    Expect(decoded.stacks[0][2].numberAlive == 18, "stack count");
    Expect(decoded.stacks[0][2].infoFlags == 0x1234ABCDu, "info flags width");
    Expect(decoded.heroCasted[1] == 0xFFFFFFFFu, "hero casted bool32");
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
        bad = siege; bad.towers[0].scalars[5] = -1;
        Expect(!RestorePolicy_(bad, &error), "negative tower frame rejected");
        bad = siege; memset(bad.towers[0].defName, 'x', 13);
        Expect(!RestorePolicy_(bad, &error), "unterminated tower resource name rejected");
        bad = siege; memset(bad.wallPcxNames[89], 'x', 13);
        Expect(!RestorePolicy_(bad, &error), "unterminated last wall resource name rejected");
        for (int hp : {-1, 1000001}) {
            bad = siege; bad.fortWallsHp[17] = hp;
            Expect(!RestorePolicy_(bad, &error), "wall HP outside zero to one million rejected");
        }
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

    if (argc > 1) {
        std::vector<uint8_t> bytes;
        std::wstring archiveError;
        hbs::ArchiveDocument document = {};
        std::unique_ptr<CodecCapture> recorded(new CodecCapture{});
        bool ok = hbs::detail::ReadWholeFile(argv[1], bytes, archiveError)
            && hbs::detail::Decode(bytes.data(), bytes.size(), document, archiveError)
            && CodecDecode(document.sections, recorded.get(), &error);
        if (ok) {
            Expect(true, "real archive CRC and codec decode");
            for (int side = 0; side < 2; ++side) {
                const CodecStack& reserved = recorded->stacks[side][20];
                printf("real archive reserved side=%d occupied=%d type=%d alive=%d initial=%d hp=%d sideIndex=%d\n",
                    side, reserved.occupied ? 1 : 0, reserved.type, reserved.numberAlive,
                    reserved.numberAtStart, reserved.infoCombat[0], reserved.sideIndex);
            }
            {
                size_t packed = 0;
                for (const auto& range : kManagerExtraRanges_) {
                    if (range.offset == 0x14031u) break;
                    packed += range.size;
                }
                size_t travelingOn = 0;
                for (int cell = 0; cell < 187; ++cell)
                    if (recorded->extraScalars[packed + cell]) ++travelingOn;
                printf("real archive traveling mask on=%zu\n", travelingOn);
            }
            error.clear();
            bool accepted = RestorePolicy_(*recorded, &error);
            printf("real archive self-preflight accepted=%d reason=%s\n", accepted, error.c_str());
            Expect(accepted, "real archive self-preflight accepted (no game writes)");
        } else if (error.find("version") != std::string::npos) {
            // v5 archives predate travelingSquares (v6); rejecting them at the
            // version gate is the designed behavior (no legacy compatibility).
            printf("real archive rejected by version gate (legacy save, expected)\n");
        } else {
            Expect(false, "real archive CRC and codec decode");
            printf("real archive decode error=%s\n", error.c_str());
        }
    }

    if (g_failures) {
        printf("%d codec test(s) failed\n", g_failures);
        return 1;
    }
    printf("codec tests passed\n");
    return 0;
}
