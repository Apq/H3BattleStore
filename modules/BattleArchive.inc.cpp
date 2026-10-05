// ========== BattleArchive.inc.cpp ==========
// 战斗时刻档的可复用文件层：编码、原子落盘、扫描索引和保留策略。
// 本文件不读取或修改游戏战斗对象；快照内容只作为不透明 section 字节。
//
// 插件按 /Zp1 编译，本文件的 STL 对象必须回到默认对齐，否则 vector/string
// 布局和调用方不一致。磁盘格式全部手写小端，不依赖结构体布局。

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#pragma pack(push, 8)

namespace hbs {

static const uint32_t kMagic = 0x31425348u; // "HBS1" little-endian
static const uint16_t kFormatVersion = 1;
static const uint32_t kMaxRecordsPerBattle = 30;
static const uint32_t kMaxBattlesOnDisk = 30;
static const uint32_t kMaxSections = 16;
static const uint32_t kMinSectionId = 1;
static const uint32_t kMaxSectionId = 16;
static const uint64_t kMaxTotalBytes = 64ull * 1024ull * 1024ull;
static const uint32_t kKeyHexChars = 64;
static const uint32_t kHeaderBytes = 4 + 2 + 2 + 64 + 64 + 8 + 4 + 4; // 152
static const uint32_t kSectionHeaderBytes = 4 + 4 + 4;                 // id, length, crc
static const uint32_t kTrailerBytes = 4;

struct ArchiveSection {
    uint32_t id;
    std::vector<uint8_t> bytes;
};

struct ArchiveDocument {
    std::string battleKey;
    std::string targetKey;
    uint64_t timestampUtcMs;
    uint32_t sequence;
    std::vector<ArchiveSection> sections;
};

struct ArchiveRecord {
    std::wstring path;
    std::string battleKey;
    std::string targetKey;
    uint64_t timestampUtcMs;
    uint32_t sequence;
};

class ArchiveStore {
public:
    explicit ArchiveStore(std::wstring root);

    bool Save(const ArchiveDocument& document, std::wstring& error);
    bool List(const std::string& battleKey,
              const std::string& targetKey,
              std::vector<ArchiveRecord>& records,
              std::wstring& error);
    bool Load(const ArchiveRecord& record, ArchiveDocument& document, std::wstring& error);
    bool Delete(const ArchiveRecord& record, std::wstring& error);

private:
    std::wstring root_;
};

namespace detail {

inline void SetError(std::wstring& error, const wchar_t* text)
{
    error = text ? text : L"";
}

inline void AppendError(std::wstring& error, const std::wstring& text)
{
    if (!error.empty()) error += L"; ";
    error += text;
}

inline bool IsHexChar(char c)
{
    return (c >= '0' && c <= '9')
        || (c >= 'a' && c <= 'f')
        || (c >= 'A' && c <= 'F');
}

inline bool IsKey(const std::string& key)
{
    if (key.size() != kKeyHexChars) return false;
    for (size_t i = 0; i < key.size(); ++i) {
        if (!IsHexChar(key[i])) return false;
    }
    return true;
}

inline char HexNibble(unsigned value)
{
    return static_cast<char>(value < 10 ? ('0' + value) : ('a' + (value - 10)));
}

inline int HexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

inline void AppendHex8(std::wstring& out, uint32_t value)
{
    for (int shift = 28; shift >= 0; shift -= 4)
        out.push_back(static_cast<wchar_t>(HexNibble((value >> shift) & 0xF)));
}

inline void AppendHex16(std::wstring& out, uint64_t value)
{
    AppendHex8(out, static_cast<uint32_t>(value >> 32));
    AppendHex8(out, static_cast<uint32_t>(value));
}

inline void AppendDec(std::wstring& out, uint32_t value, int width)
{
    wchar_t digits[16] = {};
    int n = 0;
    do {
        digits[n++] = static_cast<wchar_t>(L'0' + (value % 10u));
        value /= 10u;
    } while (value && n < 16);
    while (n < width && n < 16) digits[n++] = L'0';
    while (n > 0) out.push_back(digits[--n]);
}

inline bool Hex8(const wchar_t* text, uint32_t& value)
{
    uint32_t out = 0;
    for (int i = 0; i < 8; ++i) {
        const int nibble = HexValue(static_cast<char>(text[i]));
        if (nibble < 0 || text[i] > 0x7F) return false;
        out = (out << 4) | static_cast<uint32_t>(nibble);
    }
    value = out;
    return true;
}

inline bool Hex16(const wchar_t* text, uint64_t& value)
{
    uint32_t hi = 0;
    uint32_t lo = 0;
    if (!Hex8(text, hi) || !Hex8(text + 8, lo)) return false;
    value = (static_cast<uint64_t>(hi) << 32) | lo;
    return true;
}

inline uint32_t Crc32(const uint8_t* data, size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

inline void PutU16(std::vector<uint8_t>& out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

inline void PutU32(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 24));
}

inline void PutU64(std::vector<uint8_t>& out, uint64_t value)
{
    PutU32(out, static_cast<uint32_t>(value));
    PutU32(out, static_cast<uint32_t>(value >> 32));
}

inline bool GetU16(const uint8_t* data, size_t size, size_t& cursor, uint16_t& value, std::wstring& error)
{
    if (cursor > size || size - cursor < 2) {
        SetError(error, L"truncated u16");
        return false;
    }
    value = static_cast<uint16_t>(data[cursor] | (static_cast<uint16_t>(data[cursor + 1]) << 8));
    cursor += 2;
    return true;
}

inline bool GetU32(const uint8_t* data, size_t size, size_t& cursor, uint32_t& value, std::wstring& error)
{
    if (cursor > size || size - cursor < 4) {
        SetError(error, L"truncated u32");
        return false;
    }
    value = static_cast<uint32_t>(data[cursor])
        | (static_cast<uint32_t>(data[cursor + 1]) << 8)
        | (static_cast<uint32_t>(data[cursor + 2]) << 16)
        | (static_cast<uint32_t>(data[cursor + 3]) << 24);
    cursor += 4;
    return true;
}

inline bool GetU64(const uint8_t* data, size_t size, size_t& cursor, uint64_t& value, std::wstring& error)
{
    uint32_t lo = 0;
    uint32_t hi = 0;
    if (!GetU32(data, size, cursor, lo, error) || !GetU32(data, size, cursor, hi, error))
        return false;
    value = static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
    return true;
}

inline bool ValidateSections(const std::vector<ArchiveSection>& sections, std::wstring& error)
{
    if (sections.empty()) {
        SetError(error, L"archive requires at least one section");
        return false;
    }
    if (sections.size() > kMaxSections) {
        SetError(error, L"too many sections");
        return false;
    }
    uint64_t payload = 0;
    bool seen[kMaxSectionId + 1] = {};
    for (size_t i = 0; i < sections.size(); ++i) {
        const uint32_t id = sections[i].id;
        if (id < kMinSectionId || id > kMaxSectionId) {
            SetError(error, L"section id out of range");
            return false;
        }
        if (seen[id]) {
            SetError(error, L"duplicate section id");
            return false;
        }
        seen[id] = true;
        payload += sections[i].bytes.size();
        if (payload > kMaxTotalBytes) {
            SetError(error, L"archive exceeds 64MB");
            return false;
        }
    }
    const uint64_t total = static_cast<uint64_t>(kHeaderBytes)
        + static_cast<uint64_t>(sections.size()) * kSectionHeaderBytes
        + payload
        + kTrailerBytes;
    if (total > kMaxTotalBytes) {
        SetError(error, L"archive exceeds 64MB");
        return false;
    }
    return true;
}

inline bool Encode(const ArchiveDocument& document, std::vector<uint8_t>& out, std::wstring& error)
{
    out.clear();
    if (!IsKey(document.battleKey) || !IsKey(document.targetKey)) {
        SetError(error, L"battleKey and targetKey must be 64 hex characters");
        return false;
    }
    if (!ValidateSections(document.sections, error)) return false;

    out.reserve(kHeaderBytes + document.sections.size() * kSectionHeaderBytes + kTrailerBytes);
    PutU32(out, kMagic);
    PutU16(out, kFormatVersion);
    PutU16(out, static_cast<uint16_t>(kHeaderBytes));
    out.insert(out.end(), document.battleKey.begin(), document.battleKey.end());
    out.insert(out.end(), document.targetKey.begin(), document.targetKey.end());
    PutU64(out, document.timestampUtcMs);
    PutU32(out, document.sequence);
    PutU32(out, static_cast<uint32_t>(document.sections.size()));
    if (out.size() != kHeaderBytes) {
        SetError(error, L"internal header size mismatch");
        out.clear();
        return false;
    }

    for (size_t i = 0; i < document.sections.size(); ++i) {
        const ArchiveSection& section = document.sections[i];
        if (section.bytes.size() > 0xFFFFFFFFull) {
            SetError(error, L"section too large");
            out.clear();
            return false;
        }
        const uint32_t length = static_cast<uint32_t>(section.bytes.size());
        const uint32_t crc = Crc32(section.bytes.empty() ? nullptr : section.bytes.data(), section.bytes.size());
        PutU32(out, section.id);
        PutU32(out, length);
        PutU32(out, crc);
        out.insert(out.end(), section.bytes.begin(), section.bytes.end());
    }
    const uint32_t fileCrc = Crc32(out.data(), out.size());
    PutU32(out, fileCrc);
    if (out.size() > kMaxTotalBytes) {
        SetError(error, L"archive exceeds 64MB");
        out.clear();
        return false;
    }
    return true;
}

inline bool Decode(const uint8_t* data, size_t size, ArchiveDocument& document, std::wstring& error)
{
    document = ArchiveDocument();
    if (!data || size < kHeaderBytes + kSectionHeaderBytes + kTrailerBytes) {
        SetError(error, L"archive shorter than minimum frame");
        return false;
    }
    if (size > kMaxTotalBytes) {
        SetError(error, L"archive exceeds 64MB");
        return false;
    }
    if (size < 4) {
        SetError(error, L"truncated archive");
        return false;
    }
    const uint32_t storedCrc = static_cast<uint32_t>(data[size - 4])
        | (static_cast<uint32_t>(data[size - 3]) << 8)
        | (static_cast<uint32_t>(data[size - 2]) << 16)
        | (static_cast<uint32_t>(data[size - 1]) << 24);
    const uint32_t actualCrc = Crc32(data, size - 4);
    if (storedCrc != actualCrc) {
        SetError(error, L"archive CRC mismatch");
        return false;
    }

    size_t cursor = 0;
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t headerSize = 0;
    if (!GetU32(data, size - 4, cursor, magic, error)
        || !GetU16(data, size - 4, cursor, version, error)
        || !GetU16(data, size - 4, cursor, headerSize, error)) {
        return false;
    }
    if (magic != kMagic) {
        SetError(error, L"bad archive magic");
        return false;
    }
    if (version != kFormatVersion) {
        SetError(error, L"unsupported archive version");
        return false;
    }
    if (headerSize != kHeaderBytes) {
        SetError(error, L"unexpected header size");
        return false;
    }
    if ((size - 4) - cursor < kKeyHexChars * 2) {
        SetError(error, L"truncated keys");
        return false;
    }
    document.battleKey.assign(reinterpret_cast<const char*>(data + cursor), kKeyHexChars);
    cursor += kKeyHexChars;
    document.targetKey.assign(reinterpret_cast<const char*>(data + cursor), kKeyHexChars);
    cursor += kKeyHexChars;
    if (!IsKey(document.battleKey) || !IsKey(document.targetKey)) {
        SetError(error, L"archive key is not 64 hex characters");
        return false;
    }
    uint32_t sectionCount = 0;
    if (!GetU64(data, size - 4, cursor, document.timestampUtcMs, error)
        || !GetU32(data, size - 4, cursor, document.sequence, error)
        || !GetU32(data, size - 4, cursor, sectionCount, error)) {
        return false;
    }
    if (cursor != kHeaderBytes) {
        SetError(error, L"header cursor mismatch");
        return false;
    }
    if (sectionCount < 1 || sectionCount > kMaxSections) {
        SetError(error, L"section count out of range");
        return false;
    }

    bool seen[kMaxSectionId + 1] = {};
    const size_t bodyEnd = size - 4;
    for (uint32_t i = 0; i < sectionCount; ++i) {
        uint32_t id = 0;
        uint32_t length = 0;
        uint32_t crc = 0;
        if (!GetU32(data, bodyEnd, cursor, id, error)
            || !GetU32(data, bodyEnd, cursor, length, error)
            || !GetU32(data, bodyEnd, cursor, crc, error)) {
            SetError(error, L"truncated section header");
            return false;
        }
        if (id < kMinSectionId || id > kMaxSectionId) {
            SetError(error, L"unknown section id");
            return false;
        }
        if (seen[id]) {
            SetError(error, L"duplicate section id");
            return false;
        }
        if (cursor > bodyEnd || bodyEnd - cursor < length) {
            SetError(error, L"section length exceeds file");
            return false;
        }
        const uint8_t* bytes = data + cursor;
        if (Crc32(bytes, length) != crc) {
            SetError(error, L"section CRC mismatch");
            return false;
        }
        seen[id] = true;
        ArchiveSection section;
        section.id = id;
        section.bytes.assign(bytes, bytes + length);
        document.sections.push_back(std::move(section));
        cursor += length;
    }
    if (cursor != bodyEnd) {
        SetError(error, L"trailing bytes after sections");
        return false;
    }
    return true;
}

inline bool IsReservedName(const std::wstring& name)
{
    return name.empty()
        || name == L"."
        || name == L".."
        || name.find(L'\\') != std::wstring::npos
        || name.find(L'/') != std::wstring::npos;
}

inline bool HasHbsExtension(const std::wstring& name)
{
    if (name.size() < 4) return false;
    const wchar_t* ext = name.c_str() + name.size() - 4;
    return _wcsicmp(ext, L".hbs") == 0;
}

inline std::wstring FileNameOf(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

inline std::wstring JoinRoot(const std::wstring& root, const std::wstring& name)
{
    if (root.empty()) return name;
    const wchar_t tail = root[root.size() - 1];
    if (tail == L'\\' || tail == L'/') return root + name;
    return root + L"\\" + name;
}

inline bool GetFinalPath(HANDLE file, std::wstring& path, std::wstring& error)
{
    const DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
    DWORD needed = GetFinalPathNameByHandleW(file, nullptr, 0, flags);
    if (needed == 0) {
        SetError(error, L"GetFinalPathNameByHandleW failed");
        return false;
    }
    std::vector<wchar_t> buffer(needed + 1, L'\0');
    const DWORD written = GetFinalPathNameByHandleW(file, buffer.data(), needed + 1, flags);
    if (written == 0 || written >= needed + 1) {
        SetError(error, L"GetFinalPathNameByHandleW truncated");
        return false;
    }
    path.assign(buffer.data(), written);
    return true;
}

inline bool OpenFinalPath(const std::wstring& path, DWORD access, DWORD share, DWORD disposition,
                          std::wstring& finalPath, HANDLE& handle, std::wstring& error)
{
    const DWORD attrs = GetFileAttributesW(path.c_str());
    const DWORD flags = (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
        ? FILE_FLAG_BACKUP_SEMANTICS
        : FILE_ATTRIBUTE_NORMAL;
    handle = CreateFileW(path.c_str(), access, share, nullptr, disposition, flags, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        SetError(error, L"CreateFileW failed");
        return false;
    }
    if (!GetFinalPath(handle, finalPath, error)) {
        CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
        return false;
    }
    return true;
}

inline std::wstring StripPrefix(const std::wstring& path)
{
    const wchar_t* prefix = L"\\\\?\\";
    if (path.size() >= 4 && path.compare(0, 4, prefix) == 0) return path.substr(4);
    return path;
}

inline bool SameDirectory(const std::wstring& finalPath, const std::wstring& rootFinal, std::wstring& error)
{
    const std::wstring file = StripPrefix(finalPath);
    const std::wstring root = StripPrefix(rootFinal);
    const size_t slash = file.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        SetError(error, L"archive path has no directory");
        return false;
    }
    std::wstring dir = file.substr(0, slash);
    while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) dir.pop_back();
    std::wstring rootDir = root;
    while (!rootDir.empty() && (rootDir.back() == L'\\' || rootDir.back() == L'/')) rootDir.pop_back();
    if (_wcsicmp(dir.c_str(), rootDir.c_str()) != 0) {
        SetError(error, L"archive path is outside the store root");
        return false;
    }
    return true;
}

inline bool CreateDirectories(const std::wstring& root)
{
    std::wstring path;
    path.reserve(root.size());
    size_t index = 0;
    if (root.size() >= 2 && root[1] == L':') {
        path.append(root, 0, 2);
        index = 2;
    }
    while (index < root.size()) {
        while (index < root.size() && (root[index] == L'\\' || root[index] == L'/')) {
            path.push_back(root[index]);
            ++index;
        }
        const size_t start = index;
        while (index < root.size() && root[index] != L'\\' && root[index] != L'/') ++index;
        if (start == index) break;
        path.append(root, start, index - start);
        if (path.size() == 2 && path[1] == L':') continue;
        const DWORD attrs = GetFileAttributesW(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            if (!CreateDirectoryW(path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
                return false;
        } else if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            return false;
        }
    }
    return true;
}

inline bool EnsureRoot(const std::wstring& root, std::wstring& rootFinal, std::wstring& error)
{
    if (root.empty()) {
        SetError(error, L"archive root is empty");
        return false;
    }
    if (!CreateDirectories(root)) {
        SetError(error, L"cannot create archive root");
        return false;
    }
    const DWORD attrs = GetFileAttributesW(root.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        SetError(error, L"archive root is not a directory");
        return false;
    }

    HANDLE handle = INVALID_HANDLE_VALUE;
    if (!OpenFinalPath(root, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       OPEN_EXISTING, rootFinal, handle, error)) {
        return false;
    }
    CloseHandle(handle);
    return true;
}

inline bool ReadWholeFile(const std::wstring& path, std::vector<uint8_t>& bytes, std::wstring& error)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        SetError(error, L"cannot open archive");
        return false;
    }
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0) {
        CloseHandle(file);
        SetError(error, L"cannot read archive size");
        return false;
    }
    if (static_cast<uint64_t>(size.QuadPart) > kMaxTotalBytes) {
        CloseHandle(file);
        SetError(error, L"archive exceeds 64MB");
        return false;
    }
    bytes.resize(static_cast<size_t>(size.QuadPart));
    size_t done = 0;
    while (done < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes.size() - done, 1u << 20));
        DWORD got = 0;
        if (!ReadFile(file, bytes.data() + done, chunk, &got, nullptr)) {
            CloseHandle(file);
            SetError(error, L"ReadFile failed");
            return false;
        }
        if (got == 0) break;
        done += got;
    }
    CloseHandle(file);
    if (done != bytes.size()) {
        SetError(error, L"short archive read");
        bytes.clear();
        return false;
    }
    return true;
}

inline bool WriteNewFile(const std::wstring& path, const std::vector<uint8_t>& bytes, std::wstring& error)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        SetError(error, GetLastError() == ERROR_FILE_EXISTS
            ? L"temporary archive already exists"
            : L"cannot create temporary archive");
        return false;
    }
    size_t done = 0;
    bool ok = true;
    while (done < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes.size() - done, 1u << 20));
        DWORD wrote = 0;
        if (!WriteFile(file, bytes.data() + done, chunk, &wrote, nullptr) || wrote != chunk) {
            ok = false;
            SetError(error, L"WriteFile failed");
            break;
        }
        done += wrote;
    }
    if (ok && !FlushFileBuffers(file)) {
        ok = false;
        SetError(error, L"FlushFileBuffers failed");
    }
    CloseHandle(file);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}

struct Scanned {
    ArchiveRecord record;
    std::wstring name;
    std::vector<uint8_t> bytes;
};

inline bool Newer(const Scanned& a, const Scanned& b)
{
    if (a.record.timestampUtcMs != b.record.timestampUtcMs)
        return a.record.timestampUtcMs > b.record.timestampUtcMs;
    if (a.record.sequence != b.record.sequence)
        return a.record.sequence > b.record.sequence;
    return _wcsicmp(a.name.c_str(), b.name.c_str()) > 0;
}

inline bool WideHexEquals(const wchar_t* text, const std::string& key)
{
    if (key.size() != kKeyHexChars) return false;
    for (size_t i = 0; i < key.size(); ++i) {
        const wchar_t ch = text[i];
        if (ch > 0x7F) return false;
        const int left = HexValue(static_cast<char>(ch));
        const int right = HexValue(key[i]);
        if (left < 0 || left != right) return false;
    }
    return true;
}

inline bool ParseGeneratedName(const std::wstring& name,
                               uint64_t timestamp,
                               uint32_t sequence,
                               const std::string& battleKey,
                               const std::string& targetKey)
{
    // <16 hex ts><8 hex seq>-<64 hex battle>-<64 hex target>.hbs
    const size_t expected = 16 + 8 + 1 + kKeyHexChars + 1 + kKeyHexChars + 4;
    if (name.size() != expected) return false;
    uint64_t fileTs = 0;
    uint32_t fileSeq = 0;
    if (name[24] != L'-' || name[25 + kKeyHexChars] != L'-') return false;
    if (!Hex16(name.c_str(), fileTs) || !Hex8(name.c_str() + 16, fileSeq)) return false;
    if (!WideHexEquals(name.c_str() + 25, battleKey)) return false;
    if (!WideHexEquals(name.c_str() + 26 + kKeyHexChars, targetKey)) return false;
    if (_wcsicmp(name.c_str() + name.size() - 4, L".hbs") != 0) return false;
    return fileTs == timestamp && fileSeq == sequence;
}

inline std::wstring MakeArchiveName(uint64_t timestamp,
                                    uint32_t sequence,
                                    const std::string& battleKey,
                                    const std::string& targetKey)
{
    std::wstring name;
    name.reserve(16 + 8 + 1 + kKeyHexChars + 1 + kKeyHexChars + 4);
    AppendHex16(name, timestamp);
    AppendHex8(name, sequence);
    name.push_back(L'-');
    for (size_t i = 0; i < battleKey.size(); ++i)
        name.push_back(static_cast<wchar_t>(static_cast<unsigned char>(battleKey[i])));
    name.push_back(L'-');
    for (size_t i = 0; i < targetKey.size(); ++i)
        name.push_back(static_cast<wchar_t>(static_cast<unsigned char>(targetKey[i])));
    name += L".hbs";
    return name;
}

inline bool Scan(const std::wstring& root,
                 std::vector<Scanned>& records,
                 std::wstring& error)
{
    records.clear();
    error.clear();
    std::wstring rootFinal;
    if (!EnsureRoot(root, rootFinal, error)) return false;

    const std::wstring pattern = JoinRoot(root, L"*.hbs");
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(pattern.c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return true;
        SetError(error, L"cannot scan archive root");
        return false;
    }

    do {
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const std::wstring name = found.cFileName;
        if (IsReservedName(name) || !HasHbsExtension(name)) continue;
        const std::wstring path = JoinRoot(root, name);
        std::wstring finalPath;
        HANDLE opened = INVALID_HANDLE_VALUE;
        std::wstring openError;
        if (!OpenFinalPath(path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, finalPath, opened, openError)) {
            AppendError(error, name + L": " + openError);
            continue;
        }
        CloseHandle(opened);
        if (!SameDirectory(finalPath, rootFinal, openError) || !HasHbsExtension(FileNameOf(finalPath))) {
            AppendError(error, name + L": skipped path outside generated archives");
            continue;
        }

        std::vector<uint8_t> bytes;
        std::wstring readError;
        if (!ReadWholeFile(path, bytes, readError)) {
            AppendError(error, name + L": " + readError);
            continue;
        }
        ArchiveDocument document;
        std::wstring decodeError;
        if (!Decode(bytes.data(), bytes.size(), document, decodeError)) {
            AppendError(error, name + L": " + decodeError);
            continue;
        }
        if (!ParseGeneratedName(name, document.timestampUtcMs, document.sequence,
                                document.battleKey, document.targetKey)) {
            AppendError(error, name + L": filename does not match archive identity");
            continue;
        }
        Scanned item;
        item.record.path = path;
        item.record.battleKey = document.battleKey;
        item.record.targetKey = document.targetKey;
        item.record.timestampUtcMs = document.timestampUtcMs;
        item.record.sequence = document.sequence;
        item.name = name;
        item.bytes = std::move(bytes);
        records.push_back(std::move(item));
    } while (FindNextFileW(search, &found));
    const DWORD scanError = GetLastError();
    FindClose(search);
    if (scanError != ERROR_NO_MORE_FILES && scanError != ERROR_SUCCESS) {
        AppendError(error, L"archive scan ended early");
        return false;
    }
    std::sort(records.begin(), records.end(), Newer);
    return true;
}

inline bool DeleteVerifiedBytes(const std::wstring& root,
                                const std::wstring& path,
                                const std::vector<uint8_t>& bytes,
                                std::wstring& error)
{
    std::wstring rootFinal;
    if (!EnsureRoot(root, rootFinal, error)) return false;
    const std::wstring name = FileNameOf(path);
    if (IsReservedName(name) || !HasHbsExtension(name) || path != JoinRoot(root, name)) {
        SetError(error, L"refusing archive path outside the store root");
        return false;
    }
    ArchiveDocument document;
    if (!Decode(bytes.data(), bytes.size(), document, error)) return false;
    if (!ParseGeneratedName(name, document.timestampUtcMs, document.sequence,
                            document.battleKey, document.targetKey)) {
        SetError(error, L"refusing archive whose filename does not match its contents");
        return false;
    }
    std::wstring finalPath;
    HANDLE opened = INVALID_HANDLE_VALUE;
    if (!OpenFinalPath(path, DELETE, FILE_SHARE_READ | FILE_SHARE_DELETE,
                       OPEN_EXISTING, finalPath, opened, error)) {
        return false;
    }
    CloseHandle(opened);
    if (!SameDirectory(finalPath, rootFinal, error) || !HasHbsExtension(FileNameOf(finalPath))) {
        SetError(error, L"refusing archive path outside the store root");
        return false;
    }
    if (!DeleteFileW(path.c_str())) {
        SetError(error, L"DeleteFileW failed");
        return false;
    }
    return true;
}

inline bool DeleteOwnedFile(const std::wstring& root, const std::wstring& path, std::wstring& error)
{
    std::wstring rootFinal;
    if (!EnsureRoot(root, rootFinal, error)) return false;
    const std::wstring name = FileNameOf(path);
    if (IsReservedName(name) || !HasHbsExtension(name)) {
        SetError(error, L"refusing path that is not a generated .hbs name");
        return false;
    }
    if (path != JoinRoot(root, name)) {
        SetError(error, L"refusing archive path outside the store root");
        return false;
    }

    std::wstring finalPath;
    HANDLE opened = INVALID_HANDLE_VALUE;
    if (!OpenFinalPath(path, DELETE | GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                       OPEN_EXISTING, finalPath, opened, error)) {
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
            SetError(error, L"DeleteFileW failed");
        return false;
    }
    if (!SameDirectory(finalPath, rootFinal, error) || !HasHbsExtension(FileNameOf(finalPath))) {
        CloseHandle(opened);
        SetError(error, L"refusing archive path outside the store root");
        return false;
    }

    std::vector<uint8_t> bytes;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(opened, &size) || size.QuadPart < 0
        || static_cast<uint64_t>(size.QuadPart) > kMaxTotalBytes) {
        CloseHandle(opened);
        SetError(error, L"cannot validate archive before delete");
        return false;
    }
    bytes.resize(static_cast<size_t>(size.QuadPart));
    size_t done = 0;
    while (done < bytes.size()) {
        DWORD got = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes.size() - done, 1u << 20));
        if (!ReadFile(opened, bytes.data() + done, chunk, &got, nullptr)) {
            CloseHandle(opened);
            SetError(error, L"cannot read archive before delete");
            return false;
        }
        if (got == 0) break;
        done += got;
    }
    CloseHandle(opened);
    if (done != bytes.size()) {
        SetError(error, L"short read before delete");
        return false;
    }
    ArchiveDocument document;
    if (!Decode(bytes.data(), bytes.size(), document, error)) return false;
    if (!ParseGeneratedName(name, document.timestampUtcMs, document.sequence,
                            document.battleKey, document.targetKey)) {
        SetError(error, L"refusing archive whose filename does not match its contents");
        return false;
    }
    if (!DeleteFileW(path.c_str())) {
        SetError(error, L"DeleteFileW failed");
        return false;
    }
    return true;
}

inline bool KeysMatch(const Scanned& item, const std::string& battleKey, const std::string& targetKey)
{
    if (!battleKey.empty() && item.record.battleKey != battleKey) return false;
    if (!targetKey.empty() && item.record.targetKey != targetKey) return false;
    return true;
}

} // namespace detail

inline ArchiveStore::ArchiveStore(std::wstring root)
    : root_(std::move(root))
{
}

inline bool ArchiveStore::Save(const ArchiveDocument& document, std::wstring& error)
{
    error.clear();
    if (!detail::IsKey(document.battleKey) || !detail::IsKey(document.targetKey)) {
        detail::SetError(error, L"battleKey and targetKey must be 64 hex characters");
        return false;
    }
    if (!detail::ValidateSections(document.sections, error)) return false;

    std::wstring rootFinal;
    if (!detail::EnsureRoot(root_, rootFinal, error)) return false;

    std::vector<detail::Scanned> existing;
    std::wstring scanError;
    if (!detail::Scan(root_, existing, scanError)) {
        error = scanError;
        return false;
    }

    uint64_t sequence = document.sequence;
    for (;;) {
        if (sequence > 0xFFFFFFFFull) {
            detail::SetError(error, L"sequence space exhausted for this timestamp");
            return false;
        }
        const std::wstring candidate = detail::MakeArchiveName(
            document.timestampUtcMs, static_cast<uint32_t>(sequence), document.battleKey, document.targetKey);
        bool taken = GetFileAttributesW(detail::JoinRoot(root_, candidate).c_str()) != INVALID_FILE_ATTRIBUTES;
        if (!taken) {
            for (size_t i = 0; i < existing.size(); ++i) {
                const detail::Scanned& item = existing[i];
                if (item.record.battleKey == document.battleKey
                    && item.record.targetKey == document.targetKey
                    && item.record.timestampUtcMs == document.timestampUtcMs
                    && item.record.sequence == static_cast<uint32_t>(sequence)) {
                    taken = true;
                    break;
                }
            }
        }
        if (!taken) break;
        ++sequence;
    }

    ArchiveDocument stamped = document;
    stamped.sequence = static_cast<uint32_t>(sequence);
    std::vector<uint8_t> encoded;
    if (!detail::Encode(stamped, encoded, error)) return false;

    const std::wstring name = detail::MakeArchiveName(
        stamped.timestampUtcMs, stamped.sequence, stamped.battleKey, stamped.targetKey);
    const std::wstring finalPath = detail::JoinRoot(root_, name);
    const std::wstring tempPath = finalPath + L".tmp";
    if (GetFileAttributesW(finalPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        detail::SetError(error, L"archive filename collision");
        return false;
    }
    if (!detail::WriteNewFile(tempPath, encoded, error)) return false;

    if (!MoveFileExW(tempPath.c_str(), finalPath.c_str(), MOVEFILE_WRITE_THROUGH)) {
        const DWORD moveError = GetLastError();
        DeleteFileW(tempPath.c_str());
        detail::SetError(error, moveError == ERROR_ALREADY_EXISTS
            ? L"archive filename collision"
            : L"atomic move failed");
        return false;
    }

    std::vector<detail::Scanned> after;
    std::wstring trimError;
    if (!detail::Scan(root_, after, trimError)) {
        detail::AppendError(trimError, L"saved but retention scan failed");
        error = trimError;
        return true;
    }

    std::vector<detail::Scanned*> mine;
    for (size_t i = 0; i < after.size(); ++i) {
        if (after[i].record.battleKey == stamped.battleKey)
            mine.push_back(&after[i]);
    }
    for (size_t i = kMaxRecordsPerBattle; i < mine.size(); ++i) {
        std::wstring deleteError;
        if (!detail::DeleteVerifiedBytes(root_, mine[i]->record.path, mine[i]->bytes, deleteError))
            detail::AppendError(trimError, mine[i]->name + L": trim failed: " + deleteError);
    }

    std::vector<std::string> battles;
    std::vector<uint64_t> newest;
    for (size_t i = 0; i < after.size(); ++i) {
        const std::string& key = after[i].record.battleKey;
        size_t slot = battles.size();
        for (size_t b = 0; b < battles.size(); ++b) {
            if (battles[b] == key) {
                slot = b;
                break;
            }
        }
        if (slot == battles.size()) {
            battles.push_back(key);
            newest.push_back(after[i].record.timestampUtcMs);
        } else if (after[i].record.timestampUtcMs > newest[slot]) {
            newest[slot] = after[i].record.timestampUtcMs;
        }
    }
    std::vector<size_t> order(battles.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (newest[a] != newest[b]) return newest[a] > newest[b];
        return battles[a] > battles[b];
    });
    for (size_t n = kMaxBattlesOnDisk; n < order.size(); ++n) {
        const std::string& drop = battles[order[n]];
        for (size_t i = 0; i < after.size(); ++i) {
            if (after[i].record.battleKey != drop) continue;
            std::wstring deleteError;
            if (!detail::DeleteVerifiedBytes(root_, after[i].record.path, after[i].bytes, deleteError))
                detail::AppendError(trimError, after[i].name + L": battle trim failed: " + deleteError);
        }
    }
    error = trimError;
    return true;
}

inline bool ArchiveStore::List(const std::string& battleKey,
                               const std::string& targetKey,
                               std::vector<ArchiveRecord>& records,
                               std::wstring& error)
{
    records.clear();
    if (!battleKey.empty() && !detail::IsKey(battleKey)) {
        detail::SetError(error, L"battleKey filter is not 64 hex characters");
        return false;
    }
    if (!targetKey.empty() && !detail::IsKey(targetKey)) {
        detail::SetError(error, L"targetKey filter is not 64 hex characters");
        return false;
    }
    std::vector<detail::Scanned> scanned;
    if (!detail::Scan(root_, scanned, error)) return false;
    for (size_t i = 0; i < scanned.size(); ++i) {
        if (detail::KeysMatch(scanned[i], battleKey, targetKey))
            records.push_back(scanned[i].record);
    }
    return true;
}

inline bool ArchiveStore::Load(const ArchiveRecord& record, ArchiveDocument& document, std::wstring& error)
{
    document = ArchiveDocument();
    std::wstring rootFinal;
    if (!detail::EnsureRoot(root_, rootFinal, error)) return false;
    const std::wstring name = detail::FileNameOf(record.path);
    if (detail::IsReservedName(name) || !detail::HasHbsExtension(name)
        || record.path != detail::JoinRoot(root_, name)) {
        detail::SetError(error, L"refusing archive path outside the store root");
        return false;
    }
    std::wstring finalPath;
    HANDLE opened = INVALID_HANDLE_VALUE;
    if (!detail::OpenFinalPath(record.path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING,
                               finalPath, opened, error)) {
        return false;
    }
    CloseHandle(opened);
    if (!detail::SameDirectory(finalPath, rootFinal, error)) return false;

    std::vector<uint8_t> bytes;
    if (!detail::ReadWholeFile(record.path, bytes, error)) return false;
    if (!detail::Decode(bytes.data(), bytes.size(), document, error)) return false;
    if (!detail::ParseGeneratedName(name, document.timestampUtcMs, document.sequence,
                                    document.battleKey, document.targetKey)) {
        document = ArchiveDocument();
        detail::SetError(error, L"filename does not match archive identity");
        return false;
    }
    if (record.battleKey != document.battleKey
        || record.targetKey != document.targetKey
        || record.timestampUtcMs != document.timestampUtcMs
        || record.sequence != document.sequence) {
        document = ArchiveDocument();
        detail::SetError(error, L"record metadata does not match archive");
        return false;
    }
    return true;
}

inline bool ArchiveStore::Delete(const ArchiveRecord& record, std::wstring& error)
{
    error.clear();
    if (!detail::DeleteOwnedFile(root_, record.path, error)) return false;
    if (GetFileAttributesW(record.path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        detail::SetError(error, L"archive still present after delete");
        return false;
    }
    return true;
}

} // namespace hbs

#pragma pack(pop)
