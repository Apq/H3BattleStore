// ArchiveTests.cpp
// 独立 Win32 控制台：只验证 hbs 存档文件层，不链接插件或游戏。
// 2026-10-11 双格式改造（用户裁定 docs/02 §2/§3）：binary 与 json 双格式
// 参数化；指纹目录布局 <根>\<指纹>\手动\<回合>_<编号>；自动\ 预留只建不写；
// 列表按 (回合, 编号) 两段数值倒序；旧平铺档留盘不删不列举；JSON 帧以
// 文本断言校验（含"无分隔符伪打包"的结构断言）。

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "../modules/BattleArchive.inc.cpp"

using hbs::ArchiveDocument;
using hbs::ArchiveFormat;
using hbs::ArchiveRecord;
using hbs::ArchiveSection;
using hbs::ArchiveStore;

static int g_failed = 0;

static void Expect(bool condition, const char* name)
{
    if (condition) {
        printf("  PASS  %s\n", name);
        return;
    }
    printf("  FAIL  %s\n", name);
    ++g_failed;
}

static std::string Key(char fill, char tail)
{
    std::string key(64, fill);
    key[63] = tail;
    return key;
}

static std::wstring KeyWide(const std::string& key)
{
    return hbs::detail::KeyToWide(key);
}

static std::wstring ExOf(ArchiveFormat format)
{
    return format == ArchiveFormat::Json ? L".json" : L".hbs";
}

static const char* TagOf(ArchiveFormat format)
{
    return format == ArchiveFormat::Json ? "json" : "binary";
}

static ArchiveFormat Flip(ArchiveFormat format)
{
    return format == ArchiveFormat::Json ? ArchiveFormat::Binary : ArchiveFormat::Json;
}

// 双格式构造器：binary 填段字节；json 填 {"turn","sections"}（Archive 层只
// 要求 sections 为对象，段完整性由 CodecDecodeJson 在 Codec 测试覆盖）。
static ArchiveDocument Doc(const std::string& battle, const std::string& target,
                           uint64_t stamp, uint32_t sequence, uint32_t sectionId, uint8_t byte,
                           int32_t turn, ArchiveFormat format)
{
    ArchiveDocument document;
    document.battleKey = battle;
    document.targetKey = target;
    document.timestampUtcMs = stamp;
    document.sequence = sequence;
    document.turn = turn;
    if (format == ArchiveFormat::Json) {
        BattleJson sections;
        sections["battle"] = BattleJson{{"action", (int32_t)byte}};
        document.json["turn"] = turn;
        document.json["sections"] = sections;
    } else {
        ArchiveSection section;
        section.id = sectionId;
        section.bytes.push_back(byte);
        document.sections.push_back(section);
    }
    return document;
}

static std::wstring ExeDir()
{
    wchar_t buffer[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    std::wstring path(buffer, n);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) path.resize(slash + 1);
    return path;
}

static std::wstring TempRoot()
{
    const std::wstring base = ExeDir();
    CreateDirectoryW(base.c_str(), nullptr);
    wchar_t unique[64] = {};
    _snwprintf_s(unique, _TRUNCATE, L"case-%lu-%lu", GetCurrentProcessId(), static_cast<unsigned long>(GetTickCount() % 100000u));
    const std::wstring root = base + unique + L"\\";
    CreateDirectoryW(root.c_str(), nullptr);
    return root;
}

static void RemoveTree(const std::wstring& root)
{
    const std::wstring pattern = root + L"*";
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(pattern.c_str(), &found);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(found.cFileName, L".") == 0 || wcscmp(found.cFileName, L"..") == 0) continue;
            const std::wstring path = root + found.cFileName;
            if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                RemoveTree(path + L"\\");
                RemoveDirectoryW(path.c_str());
            } else {
                SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(path.c_str());
            }
        } while (FindNextFileW(search, &found));
        FindClose(search);
    }
    RemoveDirectoryW(root.c_str());
}

static int CountFiles(const std::wstring& dir, const wchar_t* ext)
{
    int count = 0;
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW((dir + L"*" + ext).c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return 0;
    do {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) ++count;
    } while (FindNextFileW(search, &found));
    FindClose(search);
    return count;
}

static bool DirExists(const std::wstring& path)
{
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static bool Touch(const std::wstring& path)
{
    std::wstring error;
    const uint8_t byte = 1;
    return hbs::detail::WriteNewFile(path, std::vector<uint8_t>(&byte, &byte + 1), error);
}

static bool ReadText(const std::wstring& path, std::string& text)
{
    std::vector<uint8_t> bytes;
    std::wstring error;
    if (!hbs::detail::ReadWholeFile(path, bytes, error)) return false;
    text.assign(bytes.begin(), bytes.end());
    return true;
}

static void PutU32(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 24));
}

// ===== 二进制帧编解码（原样保留的纯函数回归）=====

static void TestCodec()
{
    printf("codec\n");
    const std::string battle = Key('a', 'b');
    const std::string target = Key('c', 'd');
    ArchiveDocument document = Doc(battle, target, 0x1122334455667788ull, 7, 3, 0x5A, 25, ArchiveFormat::Binary);
    document.sections[0].bytes.push_back(0x00);
    document.sections[0].bytes.push_back(0xFF);
    ArchiveSection second;
    second.id = 16;
    second.bytes.push_back(1);
    document.sections.push_back(second);

    std::vector<uint8_t> encoded;
    std::wstring error;
    Expect(hbs::detail::Encode(document, encoded, error), "encode");
    Expect(encoded.size() > 8 && encoded[0] == 'H' && encoded[1] == 'S' && encoded[2] == 'B' && encoded[3] == '1',
           "magic little endian HBS1");

    ArchiveDocument decoded;
    Expect(hbs::detail::Decode(encoded.data(), encoded.size(), decoded, error), "decode");
    Expect(decoded.battleKey == battle && decoded.targetKey == target, "keys roundtrip");
    Expect(decoded.timestampUtcMs == document.timestampUtcMs && decoded.sequence == 7, "stamp and sequence");
    Expect(decoded.sections.size() == 2 && decoded.sections[0].id == 3 && decoded.sections[1].id == 16, "section ids");
    Expect(decoded.sections[0].bytes == document.sections[0].bytes, "section bytes");

    const uint32_t crc = hbs::detail::Crc32(reinterpret_cast<const uint8_t*>("123456789"), 9);
    Expect(crc == 0xCBF43926u, "crc32 vector");
}

static void TestRejects()
{
    printf("rejects\n");
    const std::string battle = Key('a', '1');
    const std::string target = Key('b', '2');
    ArchiveDocument document = Doc(battle, target, 9, 1, 1, 42, 25, ArchiveFormat::Binary);
    std::vector<uint8_t> encoded;
    std::wstring error;
    Expect(hbs::detail::Encode(document, encoded, error), "base encode");

    std::vector<uint8_t> badLength = encoded;
    badLength[152 + 4] = 0xFF;
    ArchiveDocument decoded;
    Expect(!hbs::detail::Decode(badLength.data(), badLength.size(), decoded, error), "bad length rejected");
    Expect(error.find(L"CRC") != std::wstring::npos || error.find(L"length") != std::wstring::npos,
           "bad length diagnostic");

    std::vector<uint8_t> badCrc = encoded;
    badCrc.back() ^= 0xFF;
    Expect(!hbs::detail::Decode(badCrc.data(), badCrc.size(), decoded, error), "bad crc rejected");
    Expect(error.find(L"CRC") != std::wstring::npos, "bad crc diagnostic");

    std::vector<uint8_t> truncated = encoded;
    truncated.pop_back();
    Expect(!hbs::detail::Decode(truncated.data(), truncated.size(), decoded, error), "truncated tail rejected");

    {
        // v2 升版回归（2026-10-10 用户裁定）：旧 v1 格式头必须拒绝，旧档全部作废。
        std::vector<uint8_t> legacy = encoded;
        legacy[4] = 1; // 头部 offset 4 为 uint16 格式版本
        legacy[5] = 0;
        Expect(!hbs::detail::Decode(legacy.data(), legacy.size(), decoded, error), "legacy v1 format rejected");
    }

    ArchiveDocument duplicate = document;
    duplicate.sections.push_back(document.sections[0]);
    std::vector<uint8_t> dupEncoded;
    Expect(!hbs::detail::Encode(duplicate, dupEncoded, error), "duplicate section rejected");

    ArchiveDocument unknown = document;
    unknown.sections[0].id = 17;
    Expect(!hbs::detail::Encode(unknown, dupEncoded, error), "unknown section rejected");

    ArchiveDocument empty;
    empty.battleKey = battle;
    empty.targetKey = target;
    Expect(!hbs::detail::Encode(empty, dupEncoded, error), "zero sections rejected");

    std::vector<uint8_t> forged = encoded;
    const size_t body = forged.size() - 4;
    PutU32(forged, 99);
    PutU32(forged, 0);
    PutU32(forged, hbs::detail::Crc32(nullptr, 0));
    forged[148] = 2;
    const uint32_t fileCrc = hbs::detail::Crc32(forged.data(), forged.size());
    PutU32(forged, fileCrc);
    Expect(forged.size() > body, "forged grew");
    Expect(!hbs::detail::Decode(forged.data(), forged.size(), decoded, error), "forged unknown section rejected");
    Expect(error.find(L"unknown") != std::wstring::npos, "unknown section diagnostic");

    ArchiveDocument huge = document;
    huge.sections[0].bytes.assign(64ull * 1024ull * 1024ull, 1);
    Expect(!hbs::detail::Encode(huge, dupEncoded, error), "over 64MB rejected");
}

// ===== 指纹目录布局（双格式参数化）=====

static bool SameRecord(const ArchiveRecord& left, const ArchiveRecord& right, ArchiveFormat format)
{
    // JSON 帧不含 sequence/targetKey（读回恒 0 与 64 个 '0'），不参与比较。
    bool same = left.path == right.path
        && left.battleKey == right.battleKey
        && left.timestampUtcMs == right.timestampUtcMs
        && left.turn == right.turn && left.number == right.number;
    if (format == ArchiveFormat::Json) return same;
    return same && left.targetKey == right.targetKey && left.sequence == right.sequence;
}

static void ExpectLoadedDocument(ArchiveStore& store, const ArchiveRecord& record,
                                 const ArchiveDocument& expected, ArchiveFormat format)
{
    ArchiveDocument loaded;
    std::wstring error;
    const bool ok = store.Load(record, loaded, error);
    Expect(ok, "load retained archive");
    if (!ok) return;
    bool same = loaded.battleKey == expected.battleKey
        && loaded.timestampUtcMs == expected.timestampUtcMs
        && loaded.turn == expected.turn;
    if (format == ArchiveFormat::Json) {
        // JSON 帧不含 sequence/targetKey：读回恒为 0 与 64 个 '0'；负载比文档树。
        same = same && loaded.sequence == 0
            && loaded.targetKey == std::string(64, '0')
            && loaded.json == expected.json;
    } else {
        same = same && loaded.targetKey == expected.targetKey
            && loaded.sequence == expected.sequence
            && loaded.sections.size() == expected.sections.size();
        for (size_t i = 0; same && i < loaded.sections.size(); ++i) {
            same = loaded.sections[i].id == expected.sections[i].id
                && loaded.sections[i].bytes == expected.sections[i].bytes;
        }
    }
    Expect(same, "retained identity and payload unchanged");
}

static int IndexOf(const std::vector<ArchiveRecord>& records, int32_t turn, uint64_t number)
{
    for (size_t i = 0; i < records.size(); ++i)
        if (records[i].turn == turn && records[i].number == number) return (int)i;
    return -1;
}

static void TestLayoutFor(ArchiveFormat format)
{
    printf("layout %s\n", TagOf(format));
    const std::wstring root = TempRoot();
    const std::wstring ext = ExOf(format);
    ArchiveStore store(root, format);
    const std::string battle = Key('d', 'e');
    const std::string target = Key('f', '0');
    const std::string otherTarget = Key('f', '1');
    const std::wstring battleDir = root + KeyWide(battle) + L"\\";
    const std::wstring manualDir = battleDir + L"手动\\";
    std::wstring error;
    ArchiveRecord committed;

    // 首存建树 + 同回合编号从 1 起。
    Expect(store.Save(Doc(battle, target, 1000, 1, 2, 0x11, 25, format), error, &committed),
        "first save creates archive");
    Expect(hbs::detail::FileNameOf(committed.path) == L"25_1" + ext, "first archive named 25_1");
    Expect(DirExists(battleDir) && DirExists(manualDir) && DirExists(battleDir + L"自动"),
        "first save builds fingerprint dir with manual and auto subtrees");
    Expect(committed.turn == 25 && committed.number == 1, "committed identity carries turn and number");

    // 同回合编号递增、跨回合重置。
    Expect(store.Save(Doc(battle, target, 1001, 2, 2, 0x12, 25, format), error, &committed)
        && hbs::detail::FileNameOf(committed.path) == L"25_2" + ext, "same turn bumps number");
    Expect(store.Save(Doc(battle, target, 1002, 3, 2, 0x13, 25, format), error, &committed)
        && hbs::detail::FileNameOf(committed.path) == L"25_3" + ext, "same turn keeps counting");
    Expect(store.Save(Doc(battle, target, 1003, 4, 2, 0x14, 26, format), error, &committed)
        && hbs::detail::FileNameOf(committed.path) == L"26_1" + ext, "next turn restarts at 1");

    // 编号无上限自然位数：占位 9 → 10；999 → 1000（跳过中间名）。
    Expect(Touch(manualDir + L"25_9" + ext), "place 25_9 fixture");
    Expect(store.Save(Doc(battle, target, 1004, 5, 2, 0x15, 25, format), error, &committed)
        && hbs::detail::FileNameOf(committed.path) == L"25_10" + ext, "number 9 rolls to 10");
    Expect(Touch(manualDir + L"25_998" + ext) && Touch(manualDir + L"25_999" + ext), "place 25_999 fixtures");
    Expect(store.Save(Doc(battle, target, 1005, 6, 2, 0x16, 25, format), error, &committed)
        && hbs::detail::FileNameOf(committed.path) == L"25_1000" + ext, "number 999 rolls to 1000");

    // 高回合档参与排序：7134 > 25（字符串序 "25_*" > "7134_*" 会错）。
    Expect(store.Save(Doc(battle, target, 1006, 7, 2, 0x17, 7134, format), error, &committed),
        "save turn 7134");

    // 可见档：25_1,25_2,25_3,25_10,25_1000,26_1,7134_1（占位坏档被隐藏）。
    std::vector<ArchiveRecord> records;
    Expect(store.List(battle, "", records, error) && records.size() == 7,
        "list seven visible archives");
    Expect(!records.empty() && records[0].turn == 7134 && records[0].number == 1,
        "turn 7134 sorts before turn 25 by numeric order");
    {
        // 两段数值倒序：26_1 > 25_1000 > 25_10 > 25_3 > 25_2 > 25_1；
        // 25_10 在 25_2 之前（字符串序会得到相反结果——字典序陷阱）。
        const int order[][2] = {{26, 1}, {25, 1000}, {25, 10}, {25, 3}, {25, 2}, {25, 1}};
        bool ordered = records.size() == 7;
        for (size_t i = 0; ordered && i < sizeof(order) / sizeof(order[0]); ++i)
            ordered = records[i + 1].turn == order[i][0] && records[i + 1].number == (uint64_t)order[i][1];
        Expect(ordered, "descend by turn then number, 25_10 beats 25_2");
        Expect(IndexOf(records, 25, 10) < IndexOf(records, 25, 2), "numeric beats lexicographic for 10 vs 2");
    }

    // 旧平铺档（存档根直下，两种旧后缀）留盘不删、不进读档视野。
    const std::wstring legacyFlat = root + KeyWide(battle) + L"_20261006_192213.hbs";
    const std::wstring legacyFlatJson = root + KeyWide(battle) + L"_20261011_201500.json";
    {
        std::vector<uint8_t> bytes;
        bytes.push_back('x');
        std::wstring writeError;
        Expect(hbs::detail::WriteNewFile(legacyFlat, bytes, writeError)
            && hbs::detail::WriteNewFile(legacyFlatJson, bytes, writeError),
            "place legacy flat archives in root");
    }
    std::vector<ArchiveRecord> all;
    Expect(store.List("", "", all, error) && all.size() == 7,
        "legacy flat archives in root stay invisible");
    Expect(GetFileAttributesW(legacyFlat.c_str()) != INVALID_FILE_ATTRIBUTES
        && GetFileAttributesW(legacyFlatJson.c_str()) != INVALID_FILE_ATTRIBUTES,
        "legacy flat archives stay on disk");

    // 同指纹目录另一格式的档互不可见（后缀分流）。
    {
        ArchiveStore otherStore(root, Flip(format));
        const ArchiveDocument other = Doc(battle, target, 2000, 9, 5, 0x55, 99, Flip(format));
        ArchiveRecord otherCommitted;
        Expect(otherStore.Save(other, error, &otherCommitted)
            && hbs::detail::FileNameOf(otherCommitted.path) == L"99_1" + ExOf(Flip(format)),
            "save in the other format");
        std::vector<ArchiveRecord> mine;
        Expect(store.List(battle, "", mine, error) && IndexOf(mine, 99, 1) < 0,
            "other-format archives in same manual dir stay invisible");
        std::vector<ArchiveRecord> theirs;
        Expect(otherStore.List(battle, "", theirs, error) && theirs.size() == 1,
            "other format sees only its own suffix");
        ExpectLoadedDocument(otherStore, otherCommitted, other, Flip(format));
    }

    // targetKey 过滤与多指纹列举。JSON 帧顶层无 targetKey（读回恒 64 个
    // '0'），过滤语义按帧内值匹配：非零键在 json 模式下匹配不到任何档。
    Expect(store.Save(Doc(battle, otherTarget, 3000, 8, 9, 0x19, 25, format), error, &committed),
        "save other-target archive");
    std::vector<ArchiveRecord> otherRecords;
    if (format == ArchiveFormat::Json) {
        Expect(store.List(battle, otherTarget, otherRecords, error) && otherRecords.empty(),
            "json frame carries no target key, foreign filter matches nothing");
        Expect(store.List(battle, std::string(64, '0'), otherRecords, error) && otherRecords.size() == 8,
            "json zero-target filter matches every json archive");
    } else {
        Expect(store.List(battle, otherTarget, otherRecords, error) && otherRecords.size() == 1,
            "target filter keeps only other-target archive");
    }
    Expect(store.List(battle, target, records, error)
        && records.size() == (format == ArchiveFormat::Json ? 0u : 7u),
        "target filter hides other-target archive");
    {
        const std::string second = Key('7', '7');
        Expect(store.Save(Doc(second, target, 4000, 1, 1, 0x21, 3, format), error, &committed),
            "save another battle");
        std::vector<ArchiveRecord> global;
        Expect(store.List("", "", global, error) && global.size() == 9,
            "global list spans all fingerprint dirs");
        Expect(!global.empty() && global[0].turn == 7134, "global order still turn-major");
        // 重启后全局顺序与负载一致。
        ArchiveStore restarted(root, format);
        std::vector<ArchiveRecord> again;
        Expect(restarted.List("", "", again, error) && again.size() == global.size(),
            "restart lists every archive");
        bool sameOrder = again.size() == global.size();
        for (size_t i = 0; sameOrder && i < again.size(); ++i)
            sameOrder = SameRecord(again[i], global[i], format);
        Expect(sameOrder, "restart keeps identities and full order");
    }

    // 非法键/负回合拒绝；committed 清空。
    ArchiveDocument invalid = Doc("bad", target, 1, 1, 1, 1, 1, format);
    Expect(!store.Save(invalid, error, &committed), "invalid battle key rejected");
    Expect(committed.path.empty() && committed.battleKey.empty() && committed.number == 0,
        "failed save clears committed identity");
    ArchiveDocument negativeTurn = Doc(battle, target, 1, 1, 1, 1, -3, format);
    Expect(!store.Save(negativeTurn, error, &committed), "negative turn rejected");

    // 记录元数据伪造（2026-10-10 裁定）：内容为准。
    {
        std::vector<ArchiveRecord> mine;
        Expect(store.List(battle, "", mine, error) && !mine.empty(), "list for forged record test");
        ArchiveRecord forged = mine[0];
        ++forged.sequence;
        forged.targetKey = Key('9', '9');
        ArchiveDocument loaded;
        std::wstring loadError;
        const bool ok = store.Load(forged, loaded, loadError);
        Expect(ok, "forged record metadata ignored");
        if (ok && format == ArchiveFormat::Binary)
            Expect(loaded.targetKey == mine[0].targetKey && loaded.sequence == mine[0].sequence,
                "loaded identity comes from file");
        ArchiveRecord outside = mine[0];
        outside.path = root + L"..\\not-owned" + ext;
        Expect(!store.Load(outside, loaded, error), "outside load path refused");
    }

    // 当前后缀档数：7 可见 + 1 other-target + 3 占位坏档 = 11（另一后缀不计）。
    Expect(CountFiles(manualDir, ext.c_str()) == 11, "all archives of this format stay on disk");
    RemoveTree(root);
}

// ===== 坏档隐藏与留盘（双格式参数化）=====

static void TestHiddenArchivesStayOnDiskFor(ArchiveFormat format)
{
    printf("hidden archives stay on disk (%s)\n", TagOf(format));
    const std::wstring root = TempRoot();
    const std::wstring ext = ExOf(format);
    ArchiveStore store(root, format);
    std::wstring error;
    const std::string battle = Key('1', '2');
    const std::wstring manualDir = root + KeyWide(battle) + L"\\手动\\";
    ArchiveDocument document = Doc(battle, Key('3', '4'), 42, 1, 8, 9, 5, format);
    ArchiveRecord original;
    Expect(store.Save(document, error, &original), "save visible archive");
    std::vector<ArchiveRecord> records;
    Expect(store.List("", "", records, error) && records.size() == 1, "one valid record");

    {
        // 5_2 名义合法但内容为垃圾字节：List 隐藏、Load 拒绝、字节留盘。
        const uint8_t corruptText[] = {'t', 'r', 'u', 'n', 'c', 'a', 't', 'e', 'd'};
        const std::vector<uint8_t> corruptBytes(corruptText, corruptText + sizeof(corruptText));
        std::wstring writeError;
        Expect(hbs::detail::WriteNewFile(manualDir + L"5_2" + ext, corruptBytes, writeError),
            "write truncated fixture");
        // 5_3：可解析但 battleKey 与指纹目录不符。
        {
            ArchiveDocument wrong = document;
            wrong.battleKey = Key('5', '6');
            std::vector<uint8_t> wrongBytes;
            std::wstring wrongError;
            if (format == ArchiveFormat::Binary)
                hbs::detail::Encode(wrong, wrongBytes, wrongError);
            else
                hbs::detail::EncodeJsonDocument(wrong, wrongBytes, wrongError);
            Expect(hbs::detail::WriteNewFile(manualDir + L"5_3" + ext, wrongBytes, writeError),
                "write mismatched fingerprint fixture");
        }
        // 5_4：binary 用坏 CRC 尾字节；json 用帧内 turn 与文件名不符。
        {
            std::vector<uint8_t> badBytes;
            if (format == ArchiveFormat::Binary) {
                std::wstring crcError;
                hbs::detail::Encode(document, badBytes, crcError);
                if (!badBytes.empty()) badBytes.back() ^= 0xFF;
            } else {
                ArchiveDocument turnBad = document;
                turnBad.turn = 9; // 帧内 turn 与文件名 5_4 不符
                std::wstring turnError;
                hbs::detail::EncodeJsonDocument(turnBad, badBytes, turnError);
            }
            Expect(hbs::detail::WriteNewFile(manualDir + L"5_4" + ext, badBytes, writeError),
                "write bad payload fixture");
        }
        Expect(store.List("", "", records, error), "bad archives do not abort scan");
        Expect(records.size() == 1 && SameRecord(records[0], original, format), "bad archives hidden only");
        Expect(error.find(L"5_2") != std::wstring::npos
            && error.find(L"5_3") != std::wstring::npos
            && error.find(L"5_4") != std::wstring::npos
            && error.find(L"filename does not match") != std::wstring::npos,
            "scan reports every bad archive");
        ArchiveRecord forged = original;
        ArchiveDocument loaded;
        forged.path = manualDir + L"5_2" + ext;
        Expect(!store.Load(forged, loaded, error), "truncated archive cannot be loaded");
        forged.path = manualDir + L"5_3" + ext;
        Expect(!store.Load(forged, loaded, error), "mismatched fingerprint cannot be loaded");
        forged.path = manualDir + L"5_4" + ext;
        Expect(!store.Load(forged, loaded, error), "bad payload cannot be loaded");

        // 编号分配只看可解析文件名：已有 5_1..5_4，下一次保存取 5_5。
        ArchiveDocument later = Doc(battle, document.targetKey, 5000, 2, 8, 10, 5, format);
        ArchiveRecord laterCommitted;
        Expect(store.Save(later, error, &laterCommitted)
            && hbs::detail::FileNameOf(laterCommitted.path) == L"5_5" + ext,
            "next number uses max parsed number in turn");
        Expect(store.List("", "", records, error) && records.size() == 2,
            "new archive stays visible alongside original");
        std::vector<uint8_t> readback;
        Expect(hbs::detail::ReadWholeFile(manualDir + L"5_2" + ext, readback, error)
            && readback == corruptBytes, "truncated bytes unchanged");
    }
    RemoveTree(root);
}

// ===== 已存在 tmp 的保留（双格式参数化）=====

static void TestExistingTemporaryFileIsPreservedFor(ArchiveFormat format)
{
    printf("existing temporary file is preserved (%s)\n", TagOf(format));
    const std::wstring root = TempRoot();
    const std::wstring ext = ExOf(format);
    ArchiveStore store(root, format);
    std::wstring error;
    const std::string battle = Key('a', '9');
    ArchiveDocument document = Doc(battle, Key('b', '8'), 77, 1, 1, 5, 5, format);
    ArchiveRecord committed;
    Expect(store.Save(document, error, &committed), "first save succeeds");
    const std::wstring finalPath = root + KeyWide(battle) + L"\\手动\\5_2" + ext;
    const std::wstring tempPath = finalPath + L".tmp";
    const uint8_t sentinel[] = {'k', 'e', 'e', 'p'};
    const std::vector<uint8_t> sentinelBytes(sentinel, sentinel + sizeof(sentinel));
    Expect(hbs::detail::WriteNewFile(tempPath, sentinelBytes, error), "write pre-existing tmp");
    ArchiveDocument second = Doc(battle, document.targetKey, 78, 2, 1, 6, 5, format);
    Expect(!store.Save(second, error, &committed), "save into existing tmp fails");
    Expect(error.find(L"temporary archive already exists") != std::wstring::npos,
        "tmp collision diagnostic");
    Expect(committed.path.empty() && committed.sequence == 0 && committed.number == 0,
        "tmp collision clears committed identity");
    std::vector<uint8_t> readback;
    Expect(hbs::detail::ReadWholeFile(tempPath, readback, error) && readback == sentinelBytes,
        "pre-existing tmp preserved");
    Expect(GetFileAttributesW(finalPath.c_str()) == INVALID_FILE_ATTRIBUTES,
        "no final file appears after tmp collision");
    RemoveTree(root);
}

// ===== JSON 帧文本断言（含无分隔符伪打包的结构断言）=====

// 深度遍历：任何字符串值不得是多字段分隔符打包（"0,1,15" 一类伪打包）。
static bool NoPackedStrings(const BattleJson& node, std::string* where)
{
    if (node.is_string()) {
        const std::string text = node.get<std::string>();
        int groups = 1;
        bool digits = !text.empty();
        for (size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (c == ',' || c == ';' || c == '|') ++groups;
            else if (c < '0' || c > '9') { digits = false; break; }
        }
        if (digits && groups >= 2) {
            if (where) *where = text;
            return false;
        }
        return true;
    }
    if (node.is_array()) {
        for (const BattleJson& item : node)
            if (!NoPackedStrings(item, where)) return false;
        return true;
    }
    if (node.is_object()) {
        for (BattleJson::const_iterator it = node.begin(); it != node.end(); ++it) {
            if (!NoPackedStrings(it.value(), where)) return false;
        }
    }
    return true;
}

static void TestJsonFrameText()
{
    printf("json frame text\n");
    const std::wstring root = TempRoot();
    ArchiveStore store(root, ArchiveFormat::Json);
    const std::string battle = Key('c', '3');
    const std::string target = Key('d', '4');
    std::wstring error;
    ArchiveRecord committed;
    ArchiveDocument document = Doc(battle, target, 1760000000000ull, 3, 2, 0x42, 25, ArchiveFormat::Json);
    document.json["sections"]["battle"]["landType"] = 12;
    Expect(store.Save(document, error, &committed), "save json archive");
    Expect(hbs::detail::FileNameOf(committed.path) == L"25_1.json", "json archive named by turn and number");

    std::string text;
    Expect(ReadText(committed.path, text) && !text.empty(), "read json text");
    Expect(text.find("\n  ") != std::string::npos, "json is indented for humans");
    const BattleJson frame = BattleJson::parse(text, nullptr, false, true);
    Expect(!frame.is_discarded() && frame.is_object(), "saved file parses as json");
    Expect(frame.contains("format") && frame["format"] == "H3BS", "format tag is H3BS");
    Expect(frame.contains("version") && frame["version"] == 1, "json frame version starts at 1");
    Expect(frame.contains("battleKey") && frame["battleKey"].get<std::string>() == battle, "battleKey roundtrip");
    Expect(frame.contains("timestampUtcMs") && frame["timestampUtcMs"].get<uint64_t>() == 1760000000000ull,
        "timestamp roundtrip");
    Expect(frame.contains("turn") && frame["turn"].get<int>() == 25, "turn roundtrip");
    Expect(frame.contains("sections") && frame["sections"].is_object()
        && frame["sections"].contains("battle") && frame["sections"]["battle"].is_object(),
        "sections carry battle object");
    std::string packedWhere;
    Expect(NoPackedStrings(frame, &packedWhere), "no separator-packed pseudo fields");
    {
        // 无分隔符打包断言的负样例：手工伪打包必须被检出。
        const BattleJson bad = BattleJson::parse("{\"stack\": \"0,1,15,20\"}", nullptr, false, true);
        std::string hit;
        Expect(!NoPackedStrings(bad, &hit) && hit == "0,1,15,20", "packed string detector works");
    }

    // 帧级拒绝：坏 JSON、错版本、错 format、缺字段。
    std::vector<uint8_t> bytes;
    ArchiveDocument decoded;
    {
        const char broken[] = "{ \"format\": \"H3BS\", ";
        bytes.assign(broken, broken + sizeof(broken) - 1);
        Expect(!hbs::detail::DecodeJsonDocument(bytes.data(), bytes.size(), decoded, error)
            && error.find(L"parse failed") != std::wstring::npos, "broken json rejected");
    }
    {
        const std::string payload = "{\"format\":\"H3BS\",\"version\":2,\"battleKey\":\""
            + std::string(64, 'a') + "\",\"timestampUtcMs\":1,\"turn\":1,\"sections\":{}}";
        bytes.assign(payload.begin(), payload.end());
        Expect(!hbs::detail::DecodeJsonDocument(bytes.data(), bytes.size(), decoded, error)
            && error.find(L"unsupported archive version") != std::wstring::npos,
            "json version 2 rejected");
    }
    {
        const std::string payload = "{\"format\":\"JSON!\",\"version\":1,\"battleKey\":\""
            + std::string(64, 'a') + "\",\"timestampUtcMs\":1,\"turn\":1,\"sections\":{}}";
        bytes.assign(payload.begin(), payload.end());
        Expect(!hbs::detail::DecodeJsonDocument(bytes.data(), bytes.size(), decoded, error)
            && error.find(L"magic") != std::wstring::npos, "wrong format tag rejected");
    }
    {
        const std::string payload = "{\"format\":\"H3BS\",\"version\":1,\"battleKey\":\""
            + std::string(64, 'a') + "\",\"turn\":1,\"sections\":{}}";
        bytes.assign(payload.begin(), payload.end());
        Expect(!hbs::detail::DecodeJsonDocument(bytes.data(), bytes.size(), decoded, error)
            && error.find(L"schema") != std::wstring::npos, "missing timestamp rejected");
    }
    {
        // 文档 turn 与 json 负载 turn 不一致：Save 拒绝。
        ArchiveDocument mismatch = Doc(battle, target, 5, 1, 1, 1, 26, ArchiveFormat::Json);
        mismatch.json["turn"] = 27;
        Expect(!hbs::detail::EncodeJsonDocument(mismatch, bytes, error)
            && error.find(L"turn") != std::wstring::npos, "payload turn mismatch rejected at encode");
    }
    {
        // json 负载缺 sections：Save 拒绝。
        ArchiveDocument noSections = Doc(battle, target, 5, 1, 1, 1, 26, ArchiveFormat::Json);
        noSections.json = BattleJson{{"turn", 26}};
        Expect(!hbs::detail::EncodeJsonDocument(noSections, bytes, error), "missing sections rejected at encode");
    }
    {
        // 文件名 turn 与帧内 turn 不符：List 隐藏、Load 拒绝（§2 冗余校验）。
        const std::wstring dir = committed.path.substr(0, committed.path.find_last_of(L"\\/") + 1);
        const std::wstring renamed = dir + L"26_1.json";
        Expect(MoveFileW(committed.path.c_str(), renamed.c_str()), "rename to mismatched turn");
        std::vector<ArchiveRecord> records;
        std::wstring listError;
        Expect(store.List(battle, "", records, listError) && records.empty(),
            "renamed turn mismatch hidden from list");
        ArchiveRecord renamedRecord;
        renamedRecord.path = renamed;
        renamedRecord.battleKey = battle;
        ArchiveDocument loaded;
        Expect(!store.Load(renamedRecord, loaded, error), "renamed turn mismatch refused on load");
    }
    RemoveTree(root);
}

// ===== 本地时间显示 =====

static void TestLocalStamp()
{
    printf("local stamp\n");
    const uint64_t stamp = 1760000000000ull;
    char text[64] = {};
    TIME_ZONE_INFORMATION zone = {};
    Expect(hbs::detail::FormatLocalArchiveStamp(stamp, text, sizeof(text), &zone)
        && strlen(text) == 15 && text[8] == '-', "local stamp shape yyyymmdd-hhmmss");
    char numbered[80] = {};
    Expect(hbs::detail::FormatNumberedArchiveStamp(stamp, 1, numbered, sizeof(numbered), &zone)
        && strncmp(numbered, text, 15) == 0 && strcmp(numbered + 15, "_1") == 0,
        "numbered stamp appends natural-width number");
    Expect(hbs::detail::FormatNumberedArchiveStamp(stamp, 1000, numbered, sizeof(numbered), &zone)
        && strcmp(numbered + 15, "_1000") == 0, "number 1000 has no historical 999 cap");
    const unsigned long long huge = 18446744073709551615ull;
    Expect(hbs::detail::FormatNumberedArchiveStamp(stamp, huge, numbered, sizeof(numbered), &zone)
        && strcmp(numbered + 15, "_18446744073709551615") == 0, "number has no upper bound");
    Expect(!hbs::detail::FormatNumberedArchiveStamp(stamp, 0, numbered, sizeof(numbered), &zone)
        && numbered[0] == 0, "number zero rejected");
    char tinyBuffer[4] = {};
    Expect(!hbs::detail::FormatLocalArchiveStamp(stamp, tinyBuffer, sizeof(tinyBuffer), nullptr)
        && tinyBuffer[3] == 0, "short display buffer is terminated");
    Expect(!hbs::detail::FormatLocalArchiveStamp(~uint64_t(0), text, sizeof(text), nullptr)
        && text[0] == 0, "overflow timestamp rejected instead of wrapping");
    Expect(!hbs::detail::FormatLocalArchiveStamp(stamp, nullptr, 0, nullptr), "null output rejected");
}

int main()
{
    TestCodec();
    TestRejects();
    TestLayoutFor(ArchiveFormat::Binary);
    TestLayoutFor(ArchiveFormat::Json);
    TestHiddenArchivesStayOnDiskFor(ArchiveFormat::Binary);
    TestHiddenArchivesStayOnDiskFor(ArchiveFormat::Json);
    TestExistingTemporaryFileIsPreservedFor(ArchiveFormat::Binary);
    TestExistingTemporaryFileIsPreservedFor(ArchiveFormat::Json);
    TestJsonFrameText();
    TestLocalStamp();
    printf(g_failed ? "FAILED %d\n" : "ALL PASSED\n", g_failed);
    return g_failed ? 1 : 0;
}
