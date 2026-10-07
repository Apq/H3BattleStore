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

static void TestStoreLimits()
{
    printf("store limits\n");
    const std::wstring root = TempRoot();
    ArchiveStore store(root);
    const std::string battle = Key('d', 'e');
    const std::string target = Key('f', '0');
    const std::string otherTarget = Key('f', '1');
    std::wstring error;

    for (int i = 0; i < 30; ++i) {
        ArchiveDocument document = Doc(battle, target, 1000 + static_cast<uint64_t>(i), static_cast<uint32_t>(i + 1), 2,
                                       static_cast<uint8_t>(i));
        Expect(store.Save(document, error), "save within 30");
    }
    ArchiveDocument overflow = Doc(battle, target, 5000, 31, 2, 99);
    Expect(store.Save(overflow, error), "31st save succeeds");
    std::vector<ArchiveRecord> records;
    Expect(store.List(battle, target, records, error), "list after trim");
    Expect(records.size() == 30, "kept 30 records");
    Expect(!records.empty() && records[0].timestampUtcMs == 5000 && records[0].sequence == 31, "newest first");
    Expect(records.back().timestampUtcMs == 1001, "oldest dropped");

    ArchiveDocument collision = Doc(battle, target, 5000, 31, 4, 7);
    ArchiveRecord committed;
    Expect(store.Save(collision, error, &committed), "same second bumps attempt suffix");
    const std::wstring committedName = committed.path.empty()
        ? std::wstring() : committed.path.substr(committed.path.find_last_of(L'\\') + 1);
    Expect(committedName.size() > 6
        && committedName.compare(committedName.size() - 6, 6, L"_2.hbs") == 0
        && committedName.compare(0, 64, std::wstring(battle.begin(), battle.end())) == 0
        && committedName.compare(64, 9, L"_19700101") == 0,
        "collision filename is <fingerprint>_yyyymmdd_hhmmss_2.hbs");
    Expect(store.List(battle, target, records, error), "list after collision");
    Expect(records.size() == 30 && records[0].sequence == 31 && records[0].timestampUtcMs == 5000,
        "collision keeps document sequence");
    Expect(committed.sequence == 31 && committed.path == records[0].path
        && committed.battleKey == battle && committed.targetKey == target,
        "save returns actual committed identity");
    ArchiveDocument committedReadback;
    Expect(store.Load(committed, committedReadback, error), "readback committed collision");
    Expect(committedReadback.sequence == 31 && committedReadback.sections[0].bytes[0] == 7,
        "committed collision payload");


    ArchiveDocument other = Doc(battle, otherTarget, 1000, 31, 1, 3);
    Expect(store.Save(other, error), "other target does not collide");
    std::vector<ArchiveRecord> battleRecords;
    Expect(store.List(battle, "", battleRecords, error), "list whole battle");
    Expect(battleRecords.size() == 30, "30 records cover every target in one battle");
    int otherKept = 0;
    for (size_t i = 0; i < battleRecords.size(); ++i) {
        if (battleRecords[i].targetKey == otherTarget) ++otherKept;
    }
    Expect(otherKept == 0, "older other target yields to the newer same-battle record");

    ArchiveDocument loaded;
    Expect(store.Load(records[0], loaded, error), "load newest");
    Expect(loaded.sections.size() == 1 && loaded.sections[0].id == 4 && loaded.sections[0].bytes.size() == 1
               && loaded.sections[0].bytes[0] == 7,
           "loaded payload");

    ArchiveDocument invalid = collision;
    invalid.battleKey = "bad";
    Expect(!store.Save(invalid, error, &committed), "invalid save rejected");
    Expect(committed.path.empty() && committed.battleKey.empty() && committed.sequence == 0,
        "failed save clears committed identity");


    ArchiveStore restarted(root);
    std::vector<ArchiveRecord> again;
    Expect(restarted.List(battle, target, again, error), "restart list");
    Expect(again.size() == records.size() && again[0].path == records[0].path && again[0].sequence == 31,
           "restart keeps order");

    std::vector<std::string> battles;
    for (int i = 0; i < 30; ++i) {
        const char fill = "0123456789abcdef"[i % 16];
        const char tail = "0123456789abcdef"[i / 16];
        battles.push_back(Key(fill, tail));
        ArchiveDocument battleDoc = Doc(battles.back(), target, 10000 + static_cast<uint64_t>(i), 1, 1,
                                        static_cast<uint8_t>(i));
        Expect(store.Save(battleDoc, error), "save battle");
    }
    Expect(store.List("", "", records, error), "list all after 31 battles");
    int keptOld = 0;
    int keptNew = 0;
    for (size_t i = 0; i < records.size(); ++i) {
        if (records[i].battleKey == battle) ++keptOld;
        if (records[i].battleKey == battles.back()) ++keptNew;
    }
    Expect(keptOld == 0, "oldest battle removed by retention");
    Expect(keptNew == 1, "newest battle retained");
    Expect(CountHbs(root) == 30, "disk holds 30 battles of one record");

    RemoveTree(root);
}

static void TestDeleteFailure()
{
    printf("delete failure\n");
    const std::wstring root = TempRoot();
    ArchiveStore store(root);
    std::wstring error;
    ArchiveDocument document = Doc(Key('1', '2'), Key('3', '4'), 42, 1, 8, 9);
    Expect(store.Save(document, error), "save for delete");
    std::vector<ArchiveRecord> records;
    Expect(store.List("", "", records, error) && records.size() == 1, "one record");

    HANDLE held = CreateFileW(records[0].path.c_str(), GENERIC_READ, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Expect(held != INVALID_HANDLE_VALUE, "lock archive");
    Expect(!store.Delete(records[0], error), "locked delete fails");
    Expect(error.find(L"failed") != std::wstring::npos, "delete failure diagnostic");
    Expect(GetFileAttributesW(records[0].path.c_str()) != INVALID_FILE_ATTRIBUTES, "file remains");
    CloseHandle(held);

    Expect(store.Delete(records[0], error), "delete succeeds");
    Expect(GetFileAttributesW(records[0].path.c_str()) == INVALID_FILE_ATTRIBUTES, "file removed");

    ArchiveRecord outside = records[0];
    outside.path = root + L"..\\not-owned.hbs";
    Expect(!store.Delete(outside, error), "outside path refused");
    outside.path = root + L"foreign.hbs";
    const HANDLE foreign = CreateFileW(outside.path.c_str(), GENERIC_WRITE, 0, nullptr,
                                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (foreign != INVALID_HANDLE_VALUE) {
        const char text[] = "nope";
        DWORD wrote = 0;
        WriteFile(foreign, text, sizeof(text), &wrote, nullptr);
        CloseHandle(foreign);
    }
    Expect(!store.Delete(outside, error), "foreign hbs refused");
    Expect(GetFileAttributesW(outside.path.c_str()) != INVALID_FILE_ATTRIBUTES, "foreign file remains");

    const std::wstring corrupt = root
        + std::wstring(document.battleKey.begin(), document.battleKey.end())
        + L"_19700101_080000.hbs";
    const HANDLE bad = CreateFileW(corrupt.c_str(), GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (bad != INVALID_HANDLE_VALUE) {
        const char text[] = "truncated";
        DWORD wrote = 0;
        WriteFile(bad, text, sizeof(text) - 1, &wrote, nullptr);
        CloseHandle(bad);
    }
    std::vector<ArchiveRecord> listed;
    Expect(store.List("", "", listed, error), "corrupt scan continues");
    Expect(listed.empty(), "corrupt file skipped");
    Expect(error.find(L"truncated") != std::wstring::npos || error.find(L"CRC") != std::wstring::npos
               || error.find(corrupt.substr(corrupt.find_last_of(L'\\') + 1)) != std::wstring::npos,
           "corrupt diagnostic");

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
            ArchiveDocument loaded;
            Expect(store.Load(record, loaded, error) && loaded.timestampUtcMs == honest.timestampUtcMs,
                "load uses header UTC rather than current timezone interpretation");
        }
        std::wstring invalidDate = std::wstring(document.battleKey.begin(), document.battleKey.end())
            + L"_19700230_080001.hbs";
        Expect(!hbs::detail::ParseGeneratedName(invalidDate, honest.timestampUtcMs, honest.battleKey),
            "invalid filename calendar date still rejected");
    } else {
        Expect(false, "rename for tamper test");
    }

    RemoveTree(root);
}

int main()
{
    TestCodec();
    TestRejects();
    TestStoreLimits();
    TestDeleteFailure();
    printf(g_failed ? "FAILED %d\n" : "ALL PASSED\n", g_failed);
    return g_failed ? 1 : 0;
}
