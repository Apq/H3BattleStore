// ArchiveTests.cpp
// 独立 Win32 控制台：只验证 hbs 存档文件层，不链接插件或游戏。

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

static ArchiveDocument Doc(const std::string& battle, const std::string& target,
                           uint64_t stamp, uint32_t sequence, uint32_t sectionId, uint8_t byte)
{
    ArchiveDocument document;
    document.battleKey = battle;
    document.targetKey = target;
    document.timestampUtcMs = stamp;
    document.sequence = sequence;
    ArchiveSection section;
    section.id = sectionId;
    section.bytes.push_back(byte);
    document.sections.push_back(section);
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

static int CountHbs(const std::wstring& root)
{
    int count = 0;
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW((root + L"*.hbs").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return 0;
    do {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) ++count;
    } while (FindNextFileW(search, &found));
    FindClose(search);
    return count;
}

static void PutU32(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 24));
}

static void TestCodec()
{
    printf("codec\n");
    const std::string battle = Key('a', 'b');
    const std::string target = Key('c', 'd');
    ArchiveDocument document = Doc(battle, target, 0x1122334455667788ull, 7, 3, 0x5A);
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
    ArchiveDocument document = Doc(battle, target, 9, 1, 1, 42);
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

static bool SameRecord(const ArchiveRecord& left, const ArchiveRecord& right)
{
    return left.path == right.path
        && left.battleKey == right.battleKey && left.targetKey == right.targetKey
        && left.timestampUtcMs == right.timestampUtcMs && left.sequence == right.sequence;
}

static void ExpectLoadedDocument(ArchiveStore& store, const ArchiveRecord& record,
                                 const ArchiveDocument& expected)
{
    ArchiveDocument loaded;
    std::wstring error;
    const bool ok = store.Load(record, loaded, error);
    Expect(ok, "load retained archive");
    if (!ok) return;
    bool same = loaded.battleKey == expected.battleKey && loaded.targetKey == expected.targetKey
        && loaded.timestampUtcMs == expected.timestampUtcMs && loaded.sequence == expected.sequence
        && loaded.sections.size() == expected.sections.size();
    for (size_t i = 0; same && i < loaded.sections.size(); ++i) {
        same = loaded.sections[i].id == expected.sections[i].id
            && loaded.sections[i].bytes == expected.sections[i].bytes;
    }
    Expect(same, "retained identity and payload unchanged");
}

static void TestStoreKeepsAllArchives()
{
    printf("store keeps all archives\n");
    const std::wstring root = TempRoot();
    ArchiveStore store(root);
    const std::string battle = Key('d', 'e');
    const std::string target = Key('f', '0');
    const std::string otherTarget = Key('f', '1');
    std::wstring error;
    std::vector<ArchiveRecord> savedRecords;
    std::vector<ArchiveDocument> savedDocuments;

    for (int i = 0; i < 75; ++i) {
        ArchiveDocument document = Doc(battle, target, 1000 + static_cast<uint64_t>(i), static_cast<uint32_t>(i + 1), 2,
                                       static_cast<uint8_t>(i));
        ArchiveRecord committed;
        if (store.Save(document, error, &committed)) {
            savedRecords.push_back(committed);
            savedDocuments.push_back(document);
        } else {
            Expect(false, "save all 75 same-battle archives");
        }
    }
    std::vector<ArchiveRecord> records;
    Expect(store.List(battle, target, records, error), "list 75 archives without trimming");
    Expect(records.size() == 75 && CountHbs(root) == 75, "all 75 same-battle archives stay on disk");
    Expect(!records.empty() && records.front().timestampUtcMs == 1074
        && records.back().timestampUtcMs == 1000, "75 archives sorted newest first with oldest retained");

    ArchiveDocument newest = Doc(battle, target, 5000, 76, 2, 99);
    ArchiveRecord original;
    Expect(store.Save(newest, error, &original), "76th save succeeds");
    savedRecords.push_back(original);
    savedDocuments.push_back(newest);

    ArchiveDocument collision = Doc(battle, target, 5000, 76, 4, 7);
    ArchiveRecord committed;
    Expect(store.Save(collision, error, &committed), "same second bumps attempt suffix");
    savedRecords.push_back(committed);
    savedDocuments.push_back(collision);
    const std::wstring committedName = hbs::detail::FileNameOf(committed.path);
    Expect(committedName == hbs::detail::MakeArchiveName(collision.timestampUtcMs, battle, 2)
        && committedName.size() == 86
        && committedName.compare(committedName.size() - 6, 6, L"_2.hbs") == 0
        && committedName.compare(0, 64, std::wstring(battle.begin(), battle.end())) == 0,
        "collision filename is <fingerprint>_yyyymmdd_hhmmss_2.hbs");
    Expect(store.List(battle, target, records, error), "list after collision");
    Expect(records.size() == 77 && !records.empty() && records[0].sequence == 76
        && records[0].timestampUtcMs == 5000, "collision retains both archives and document sequence");
    Expect(!records.empty() && SameRecord(committed, records[0])
        && committed.battleKey == battle && committed.targetKey == target,
        "save returns actual committed identity");
    Expect(original.path != committed.path, "collision never overwrites existing archive path");
    ExpectLoadedDocument(store, original, newest);
    ExpectLoadedDocument(store, committed, collision);

    ArchiveDocument other = Doc(battle, otherTarget, 1000, 76, 1, 3);
    ArchiveRecord otherCommitted;
    Expect(store.Save(other, error, &otherCommitted), "save older archive for another target");
    savedRecords.push_back(otherCommitted);
    savedDocuments.push_back(other);
    std::vector<ArchiveRecord> battleRecords;
    Expect(store.List(battle, "", battleRecords, error), "list whole battle");
    Expect(battleRecords.size() == 78, "all targets retained in one battle");
    std::vector<ArchiveRecord> otherRecords;
    Expect(store.List(battle, otherTarget, otherRecords, error)
        && otherRecords.size() == 1 && SameRecord(otherRecords[0], otherCommitted),
        "older other-target archive stays visible");
    Expect(battleRecords.size() >= 2
        && battleRecords[battleRecords.size() - 2].timestampUtcMs == 1000
        && battleRecords[battleRecords.size() - 2].sequence == 76
        && battleRecords.back().sequence == 1, "equal UTC timestamps sort by descending sequence");

    ArchiveDocument invalid = collision;
    invalid.battleKey = "bad";
    Expect(!store.Save(invalid, error, &committed), "invalid save rejected");
    Expect(committed.path.empty() && committed.battleKey.empty() && committed.sequence == 0,
        "failed save clears committed identity");
    Expect(CountHbs(root) == 78, "invalid save leaves all committed archives untouched");

    ArchiveStore restarted(root);
    std::vector<ArchiveRecord> again;
    Expect(restarted.List(battle, target, again, error), "restart list");
    bool sameOrder = again.size() == records.size();
    for (size_t i = 0; sameOrder && i < again.size(); ++i)
        sameOrder = SameRecord(again[i], records[i]);
    Expect(sameOrder, "restart keeps every identity and full sort order including filename ties");

    std::vector<std::string> battles;
    for (int i = 0; i < 35; ++i) {
        const char fill = "0123456789abcdef"[i % 16];
        const char tail = "0123456789abcdef"[i / 16];
        battles.push_back(Key(fill, tail));
        ArchiveDocument battleDoc = Doc(battles.back(), target, 10000 + static_cast<uint64_t>(i), 1, 1,
                                        static_cast<uint8_t>(i));
        ArchiveRecord battleCommitted;
        Expect(store.Save(battleDoc, error, &battleCommitted), "save another battle without eviction");
        savedRecords.push_back(battleCommitted);
        savedDocuments.push_back(battleDoc);
    }
    Expect(store.List("", "", records, error), "list all 36 battles");
    Expect(records.size() == 113 && CountHbs(root) == 113,
        "78 same-battle archives plus 35 other battles all remain on disk");
    Expect(!records.empty() && records.front().battleKey == battles.back()
        && records.front().timestampUtcMs == 10034 && records.back().battleKey == battle
        && records.back().timestampUtcMs == 1000 && records.back().sequence == 1,
        "global ordering uses header UTC and sequence across every battle");
    int keptOld = 0;
    for (size_t i = 0; i < records.size(); ++i) {
        if (records[i].battleKey == battle) ++keptOld;
    }
    Expect(keptOld == 78, "oldest battle retains all archives beyond 30 battles");
    for (size_t b = 0; b < battles.size(); ++b) {
        int kept = 0;
        for (size_t i = 0; i < records.size(); ++i) {
            if (records[i].battleKey == battles[b]) ++kept;
        }
        Expect(kept == 1, "each of the 35 newer battles is retained");
    }

    ArchiveStore restartedAll(root);
    Expect(restartedAll.List("", "", again, error), "restart lists all retained battles");
    sameOrder = again.size() == records.size();
    for (size_t i = 0; sameOrder && i < again.size(); ++i)
        sameOrder = SameRecord(again[i], records[i]);
    Expect(sameOrder && again.size() == 113, "restart preserves complete multi-battle order");
    for (size_t i = 0; i < savedRecords.size(); ++i)
        ExpectLoadedDocument(restartedAll, savedRecords[i], savedDocuments[i]);

    RemoveTree(root);
}

static void TestHiddenArchivesStayOnDisk()
{
    printf("hidden archives stay on disk\n");
    const std::wstring root = TempRoot();
    ArchiveStore store(root);
    std::wstring error;
    ArchiveDocument document = Doc(Key('1', '2'), Key('3', '4'), 42, 1, 8, 9);
    ArchiveRecord original;
    Expect(store.Save(document, error, &original), "save visible archive");
    std::vector<ArchiveRecord> records;
    Expect(store.List("", "", records, error) && records.size() == 1, "one valid record");

    ArchiveDocument loaded;
    ArchiveRecord outside = original;
    outside.path = root + L"..\\not-owned.hbs";
    Expect(!store.Load(outside, loaded, error), "outside load path refused");
    ArchiveRecord forged = original;
    ++forged.sequence;
    Expect(!store.Load(forged, loaded, error), "forged record metadata refused");
    Expect(GetFileAttributesW(original.path.c_str()) != INVALID_FILE_ATTRIBUTES,
        "refused loads leave valid archive untouched");

    const std::wstring foreignPath = root + L"foreign.hbs";
    const std::wstring corruptPath = root + hbs::detail::MakeArchiveName(document.timestampUtcMs, document.battleKey, 2);
    const std::wstring crcPath = root + hbs::detail::MakeArchiveName(document.timestampUtcMs, document.battleKey, 3);
    const std::wstring wrongName = hbs::detail::MakeArchiveName(document.timestampUtcMs, Key('5', '6'), 1);
    const std::wstring wrongPath = root + wrongName;
    const uint8_t foreignText[] = { 'n', 'o', 'p', 'e' };
    const std::vector<uint8_t> foreignBytes(foreignText, foreignText + sizeof(foreignText));
    Expect(hbs::detail::WriteNewFile(foreignPath, foreignBytes, error), "write foreign hbs fixture");
    const uint8_t corruptText[] = { 't', 'r', 'u', 'n', 'c', 'a', 't', 'e', 'd' };
    const std::vector<uint8_t> corruptBytes(corruptText, corruptText + sizeof(corruptText));
    Expect(hbs::detail::WriteNewFile(corruptPath, corruptBytes, error), "write truncated fixture");
    std::vector<uint8_t> encoded;
    Expect(hbs::detail::Encode(document, encoded, error), "encode wrong-name fixture");
    Expect(hbs::detail::WriteNewFile(wrongPath, encoded, error), "write mismatched fingerprint fixture");
    std::vector<uint8_t> badCrc = encoded;
    if (!badCrc.empty()) badCrc.back() ^= 0xFF;
    Expect(hbs::detail::WriteNewFile(crcPath, badCrc, error), "write bad CRC fixture");

    Expect(store.List("", "", records, error), "bad archives do not abort scan");
    Expect(records.size() == 1 && SameRecord(records[0], original), "bad and foreign-name archives hidden only");
    Expect(error.find(L"foreign.hbs") != std::wstring::npos
        && error.find(hbs::detail::FileNameOf(corruptPath)) != std::wstring::npos
        && error.find(L"CRC") != std::wstring::npos
        && error.find(wrongName) != std::wstring::npos
        && error.find(L"filename does not match") != std::wstring::npos,
        "scan reports bad bytes and mismatched fingerprint");
    forged = original;
    forged.path = wrongPath;
    Expect(!store.Load(forged, loaded, error), "mismatched filename cannot be loaded");
    forged.path = corruptPath;
    Expect(!store.Load(forged, loaded, error), "truncated archive cannot be loaded");
    forged.path = crcPath;
    Expect(!store.Load(forged, loaded, error), "bad CRC archive cannot be loaded");

    ArchiveDocument later = Doc(document.battleKey, document.targetKey, 5000, 2, 8, 10);
    ArchiveRecord laterCommitted;
    Expect(store.Save(later, error, &laterCommitted) && error.empty(),
        "save succeeds without rescanning or deleting existing bad archives");
    Expect(store.List("", "", records, error) && records.size() == 2,
        "new archive stays visible alongside original");
    Expect(CountHbs(root) == 6, "all valid and hidden archives remain on disk after save");
    std::vector<uint8_t> readback;
    Expect(hbs::detail::ReadWholeFile(foreignPath, readback, error) && readback == foreignBytes,
        "foreign bytes unchanged");
    Expect(hbs::detail::ReadWholeFile(corruptPath, readback, error) && readback == corruptBytes,
        "truncated bytes unchanged");
    Expect(hbs::detail::ReadWholeFile(crcPath, readback, error) && readback == badCrc,
        "bad CRC bytes unchanged");
    Expect(hbs::detail::ReadWholeFile(wrongPath, readback, error) && readback == encoded,
        "mismatched filename bytes unchanged");
    ExpectLoadedDocument(store, original, document);

    // Local filename time is not an identity gate: the saving timezone may differ.
    // Header UTC and CRC remain authoritative.
    ArchiveDocument honest = Doc(document.battleKey, document.targetKey, 42, 1, 8, 9);
    ArchiveRecord honestCommitted;
    Expect(store.Save(honest, error, &honestCommitted), "save for rename");
    const std::wstring renamed = root
        + std::wstring(document.battleKey.begin(), document.battleKey.end())
        + L"_19700102_080001.hbs";
    if (MoveFileW(honestCommitted.path.c_str(), renamed.c_str())) {
        std::vector<ArchiveRecord> renamedList;
        Expect(store.List("", "", renamedList, error), "renamed scan continues");
        bool found = false;
        for (size_t i = 0; i < renamedList.size(); ++i)
            if (renamedList[i].path == renamed) found = true;
        Expect(found, "valid archive remains visible with different local filename time");
        for (const ArchiveRecord& record : renamedList) {
            if (record.path != renamed) continue;
            ArchiveDocument renamedLoaded;
            Expect(store.Load(record, renamedLoaded, error) && renamedLoaded.timestampUtcMs == honest.timestampUtcMs,
                "load uses header UTC rather than current timezone interpretation");
        }
        std::wstring invalidDate = std::wstring(document.battleKey.begin(), document.battleKey.end())
            + L"_19700230_080001.hbs";
        Expect(!hbs::detail::ParseGeneratedName(invalidDate, honest.timestampUtcMs, honest.battleKey),
            "invalid filename calendar date still rejected");
    } else {
        Expect(false, "rename for tamper test");
    }

    ArchiveStore restarted(root);
    Expect(restarted.List("", "", records, error) && records.size() == 3 && CountHbs(root) == 7,
        "restart hides only invalid archives without deleting them");
    ExpectLoadedDocument(restarted, original, document);
    ExpectLoadedDocument(restarted, laterCommitted, later);
    RemoveTree(root);
}

static void TestExistingTemporaryFileIsPreserved()
{
    printf("existing temporary file is preserved\n");
    const std::wstring root = TempRoot();
    ArchiveStore store(root);
    const ArchiveDocument document = Doc(Key('7', '8'), Key('9', 'a'), 1000, 1, 1, 7);
    const std::wstring finalPath = root + hbs::detail::MakeArchiveName(document.timestampUtcMs, document.battleKey, 1);
    const std::wstring tempPath = finalPath + L".tmp";
    const std::vector<uint8_t> sentinel(8, 0x5A);
    std::wstring error;
    Expect(hbs::detail::WriteNewFile(tempPath, sentinel, error), "create preexisting tmp fixture");
    ArchiveRecord committed;
    committed.path = L"stale";
    committed.sequence = 42;
    Expect(!store.Save(document, error, &committed), "save refuses preexisting temporary file");
    Expect(committed.path.empty() && committed.sequence == 0, "tmp collision clears committed identity");
    Expect(GetFileAttributesW(finalPath.c_str()) == INVALID_FILE_ATTRIBUTES && CountHbs(root) == 0,
        "failed tmp creation never commits an archive");
    std::vector<uint8_t> readback;
    Expect(hbs::detail::ReadWholeFile(tempPath, readback, error) && readback == sentinel,
        "failed save neither overwrites nor deletes preexisting tmp");
    std::vector<ArchiveRecord> records;
    Expect(store.List("", "", records, error) && records.empty(), "tmp file is not listed as an archive");
    RemoveTree(root);
}

static uint64_t UtcStamp_(WORD year, WORD month, WORD day, WORD hour, WORD minute, WORD second)
{
    SYSTEMTIME utc = {};
    utc.wYear = year; utc.wMonth = month; utc.wDay = day;
    utc.wHour = hour; utc.wMinute = minute; utc.wSecond = second;
    FILETIME ft = {};
    Expect(SystemTimeToFileTime(&utc, &ft) != FALSE, "construct UTC fixture");
    const uint64_t ticks = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return (ticks - 116444736000000000ull) / 10000ull;
}

static void TestLocalStamp()
{
    printf("local display time\n");
    const uint64_t stamp = UtcStamp_(2026, 10, 7, 16, 15, 26);
    TIME_ZONE_INFORMATION zone = {};
    zone.Bias = -480;
    char text[32] = {};
    Expect(hbs::detail::FormatLocalArchiveStamp(stamp, text, sizeof(text), &zone)
        && strcmp(text, "20261008-001526") == 0, "UTC+8 display crosses midnight");
    zone.Bias = 420;
    Expect(hbs::detail::FormatLocalArchiveStamp(UtcStamp_(2026, 1, 1, 2, 3, 4), text, sizeof(text), &zone)
        && strcmp(text, "20251231-190304") == 0, "UTC-7 display crosses year boundary");
    zone.Bias = -330;
    Expect(hbs::detail::FormatLocalArchiveStamp(stamp, text, sizeof(text), &zone)
        && strcmp(text, "20261007-214526") == 0, "half-hour timezone is not rounded");
    zone = {};
    zone.Bias = 300;
    zone.DaylightBias = -60;
    zone.DaylightDate.wMonth = 3; zone.DaylightDate.wDay = 2;
    zone.DaylightDate.wDayOfWeek = 0; zone.DaylightDate.wHour = 2;
    zone.StandardDate.wMonth = 11; zone.StandardDate.wDay = 1;
    zone.StandardDate.wDayOfWeek = 0; zone.StandardDate.wHour = 2;
    Expect(hbs::detail::FormatLocalArchiveStamp(UtcStamp_(2026, 7, 1, 12, 0, 0), text, sizeof(text), &zone)
        && strcmp(text, "20260701-080000") == 0, "summer daylight saving offset");
    Expect(hbs::detail::FormatLocalArchiveStamp(UtcStamp_(2026, 1, 1, 12, 0, 0), text, sizeof(text), &zone)
        && strcmp(text, "20260101-070000") == 0, "winter standard offset");
    SYSTEMTIME expected = {};
    Expect(hbs::detail::UtcMsToLocalSystemTime(stamp, expected), "system local conversion");
    char expectedText[32] = {};
    _snprintf_s(expectedText, sizeof(expectedText), _TRUNCATE, "%04u%02u%02u-%02u%02u%02u",
        (unsigned)expected.wYear, (unsigned)expected.wMonth, (unsigned)expected.wDay,
        (unsigned)expected.wHour, (unsigned)expected.wMinute, (unsigned)expected.wSecond);
    Expect(hbs::detail::FormatLocalArchiveStamp(stamp, text, sizeof(text), nullptr)
        && strcmp(text, expectedText) == 0, "display uses host timezone when zone is null");
    const std::string key = Key('a', 'b');
    std::wstring name = hbs::detail::MakeArchiveName(stamp, key, 1);
    std::wstring filenameTime = name.substr(65, 15);
    filenameTime[8] = L'-';
    Expect(filenameTime == std::wstring(text, text + strlen(text)), "filename and UI share local conversion");
    char tinyBuffer[4] = {};
    Expect(!hbs::detail::FormatLocalArchiveStamp(stamp, tinyBuffer, sizeof(tinyBuffer), nullptr)
        && tinyBuffer[3] == 0, "short display buffer is terminated");
    Expect(!hbs::detail::FormatLocalArchiveStamp(~uint64_t(0), text, sizeof(text), nullptr)
        && text[0] == 0, "overflow timestamp rejected instead of wrapping");
    Expect(!hbs::detail::FormatLocalArchiveStamp(stamp, nullptr, 0, nullptr), "null output rejected");
}

int main()
{
    TestLocalStamp();
    TestCodec();
    TestRejects();
    TestStoreKeepsAllArchives();
    TestHiddenArchivesStayOnDisk();
    TestExistingTemporaryFileIsPreserved();
    printf(g_failed ? "FAILED %d\n" : "ALL PASSED\n", g_failed);
    return g_failed ? 1 : 0;
}
