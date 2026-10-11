// ========== BattleArchive.inc.cpp ==========
// 战斗时刻档的可复用文件层：编码、原子新写入和扫描索引；所有档案存盘保留。
// 本文件不读取或修改游戏战斗对象；快照内容只作为不透明 section 字节。
//
// 2026-10-11 用户裁定（docs/02 §2/§3）：双格式可切换（binary|json）+ 指纹目录
// 布局。目录：<存档根>\<64hex 指纹>\手动\<回合>_<编号>.hbs|.json；自动\ 预留
// （首存建齐，本轮只建不写）。旧平铺 .hbs 与一切旧后缀档留盘不删、不进当前
// 配置的读档视野；两种后缀互不可见；列表按 (回合, 编号) 两段数值倒序，禁止
// 整串字符串排序；JSON 帧版本从 1 起，不沿用二进制 codec 版本。
//
// 插件按 /Zp1 编译，本文件的 STL/nlohmann 对象必须回到默认对齐，否则
// vector/string/json 布局和调用方不一致。磁盘格式全部手写小端，不依赖结构体布局。

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#pragma pack(push, 8)

#include "BattleStoreJsonCore.inc.hpp"

namespace hbs {

static const uint32_t kMagic = 0x31425348u; // 小端字节序 48 53 42 31 = "HSB1"（2026-10-06 实测盘上四档核验）
// v2（2026-10-10 用户裁定升版）：battle 段去掉与 heroes 段重复的法力字段；
// 不再要求键格式与列表记录四元组一致。旧版 v1 文件全部拒绝，玩家重新存档。
static const uint16_t kFormatVersion = 2;
static const uint32_t kMaxSections = 16;
static const uint32_t kMinSectionId = 1;
static const uint32_t kMaxSectionId = 16;
static const uint64_t kMaxTotalBytes = 64ull * 1024ull * 1024ull;
static const uint32_t kKeyHexChars = 64;
static const uint32_t kHeaderBytes = 4 + 2 + 2 + 64 + 64 + 8 + 4 + 4; // 152
static const uint32_t kSectionHeaderBytes = 4 + 4 + 4;                 // id, length, crc
static const uint32_t kTrailerBytes = 4;

// JSON 帧标识与版本（2026-10-11 20:4x 用户裁定：JSON 是全新格式，版本从 1
// 起、后续演进自行递增，不沿用二进制 codec 版本号）。
static const char kJsonFormatTag[] = "H3BS";
static const int kJsonFrameVersion = 1;

enum class ArchiveFormat {
    Binary,
    Json,
};

inline const wchar_t* ArchiveExtension_(ArchiveFormat format)
{
    return format == ArchiveFormat::Json ? L".json" : L".hbs";
}

struct ArchiveSection {
    uint32_t id;
    std::vector<uint8_t> bytes;
};

struct ArchiveDocument {
    std::string battleKey;
    std::string targetKey;
    uint64_t timestampUtcMs;
    uint32_t sequence;
    // 回合用于目录内命名与 JSON 帧顶层冗余校验；二进制帧头不写该字段。
    int32_t turn = 0;
    // JSON 模式的段负载：{"turn":..., "sections":{...}}（CodecEncodeJson 输出）。
    BattleJson json;
    std::vector<ArchiveSection> sections;
};

struct ArchiveRecord {
    std::wstring path;
    std::string battleKey;
    std::string targetKey;
    uint64_t timestampUtcMs;
    uint32_t sequence;
    int32_t turn = 0;
    uint64_t number = 0; // 同回合内存档序号，从 1 起，自然位数无上限
};

class ArchiveStore {
public:
    ArchiveStore(std::wstring root, ArchiveFormat format);

    bool Save(const ArchiveDocument& document, std::wstring& error, ArchiveRecord* committed = nullptr);
    bool List(const std::string& battleKey,
              const std::string& targetKey,
              std::vector<ArchiveRecord>& records,
              std::wstring& error);
    bool Load(const ArchiveRecord& record, ArchiveDocument& document, std::wstring& error);

private:
    std::wstring root_;
    ArchiveFormat format_;
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

inline int HexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
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
    // 2026-10-10 用户裁定：不单独校验键格式——非 64hex 的 battleKey 不可能等于
    // 当前战斗指纹，后续键比较自然拒绝；错误定位交给那一步。
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

// ===== JSON 帧层（2026-10-11 用户裁定）：schema 级字段校验取代帧尾 CRC =====
// 顶层 {"format":"H3BS","version":1,"battleKey":...,"timestampUtcMs":...,
// "turn":...,"sections":{...}}；targetKey 不进 JSON 帧（生产恒为 64 个 '0'
// 占位），读档侧只比较 battleKey。段内容校验在 CodecDecodeJson。
inline bool EncodeJsonDocument(const ArchiveDocument& document, std::vector<uint8_t>& out, std::wstring& error)
{
    out.clear();
    if (!IsKey(document.battleKey) || !IsKey(document.targetKey)) {
        SetError(error, L"battleKey and targetKey must be 64 hex characters");
        return false;
    }
    if (!document.json.is_object() || !document.json.contains("sections")
        || !document.json.contains("turn") || !document.json["sections"].is_object()) {
        SetError(error, L"json document payload is missing sections");
        return false;
    }
    int32_t payloadTurn = 0;
    if (!JsonI32_(document.json, "turn", &payloadTurn) || payloadTurn != document.turn) {
        SetError(error, L"json turn does not match document turn");
        return false;
    }
    BattleJson frame;
    frame["format"] = kJsonFormatTag;
    frame["version"] = kJsonFrameVersion;
    frame["battleKey"] = document.battleKey;
    frame["timestampUtcMs"] = document.timestampUtcMs;
    frame["turn"] = document.turn;
    frame["sections"] = document.json["sections"];
    // dump(2) 可读可 diff；replace 兜底仅防未来字段漏网（当前所有字符串都经
    // Latin-1/ACP 映射产出合法 UTF-8）。体积/速度代价为用户知情裁定。
    const std::string text = frame.dump(2, ' ', false, BattleJson::error_handler_t::replace);
    if (text.size() > kMaxTotalBytes) {
        SetError(error, L"archive exceeds 64MB");
        return false;
    }
    out.assign(text.begin(), text.end());
    return true;
}

inline bool DecodeJsonDocument(const uint8_t* data, size_t size, ArchiveDocument& document, std::wstring& error)
{
    document = ArchiveDocument();
    if (!data || size < 2) {
        SetError(error, L"archive shorter than minimum frame");
        return false;
    }
    if (size > kMaxTotalBytes) {
        SetError(error, L"archive exceeds 64MB");
        return false;
    }
    const BattleJson root = BattleJson::parse(data, data + size, nullptr, false, true);
    if (root.is_discarded() || !root.is_object()) {
        SetError(error, L"archive JSON parse failed");
        return false;
    }
    if (!root.contains("format") || !root["format"].is_string()
        || root["format"].get<std::string>() != kJsonFormatTag) {
        SetError(error, L"bad archive magic");
        return false;
    }
    if (!root.contains("version") || !root["version"].is_number_integer()
        || root["version"].get<long long>() != kJsonFrameVersion) {
        SetError(error, L"unsupported archive version");
        return false;
    }
    if (!root.contains("battleKey") || !root["battleKey"].is_string()) {
        SetError(error, L"archive JSON schema invalid");
        return false;
    }
    document.battleKey = root["battleKey"].get<std::string>();
    if (!root.contains("timestampUtcMs") || !root["timestampUtcMs"].is_number_unsigned()) {
        SetError(error, L"archive JSON schema invalid");
        return false;
    }
    document.timestampUtcMs = root["timestampUtcMs"].get<uint64_t>();
    if (!JsonI32_(root, "turn", &document.turn)
        || !root.contains("sections") || !root["sections"].is_object()) {
        SetError(error, L"archive JSON schema invalid");
        return false;
    }
    document.json["turn"] = root["turn"];
    document.json["sections"] = root["sections"];
    // JSON 帧不含 sequence；读回恒 0，记录匹配两侧同源。
    document.sequence = 0;
    document.targetKey.assign(64, '0');
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

inline bool HasArchiveExtension(const std::wstring& name, ArchiveFormat format)
{
    const wchar_t* ext = ArchiveExtension_(format);
    const size_t len = wcslen(ext);
    if (name.size() < len) return false;
    return _wcsicmp(name.c_str() + name.size() - len, ext) == 0;
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

// ===== 指纹目录布局（2026-10-11 用户裁定，docs/02 §3）=====
// <存档根>\<64hex 指纹>\手动\<回合>_<编号>.hbs|.json；自动\ 预留只建不写。
// 首次存档创建指纹目录并建齐两个子目录；目录创建中断下次重建，无残留状态。

inline std::wstring KeyToWide(const std::string& key)
{
    std::wstring wide;
    wide.reserve(key.size());
    for (size_t i = 0; i < key.size(); ++i)
        wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(key[i])));
    return wide;
}

inline bool WideKeyToString(const std::wstring& wide, std::string* key)
{
    if (wide.size() != kKeyHexChars) return false;
    std::string narrow;
    narrow.reserve(wide.size());
    for (size_t i = 0; i < wide.size(); ++i) {
        if (wide[i] == 0 || wide[i] > 0x7F) return false;
        narrow.push_back(static_cast<char>(wide[i]));
    }
    if (!IsKey(narrow)) return false;
    *key = narrow;
    return true;
}

inline std::wstring BattleDirOf(const std::wstring& root, const std::string& battleKey)
{
    return JoinRoot(root, KeyToWide(battleKey));
}

inline std::wstring ManualDirOf(const std::wstring& battleDir)
{
    return JoinRoot(battleDir, L"手动");
}

// 建齐 指纹目录/手动/自动；已存在视为成功（幂等重建）。
inline bool EnsureBattleTree(const std::wstring& root, const std::string& battleKey, std::wstring& error)
{
    const std::wstring battleDir = BattleDirOf(root, battleKey);
    if (!CreateDirectories(battleDir) || !CreateDirectories(ManualDirOf(battleDir))
        || !CreateDirectories(JoinRoot(battleDir, L"自动"))) {
        SetError(error, L"cannot create archive root");
        return false;
    }
    return true;
}

// ===== 命名层：<回合>_<编号>.hbs|.json =====
// 编号 = 同回合内存档序号，从 1 起、取同回合已有最大编号 +1 递增、自然位数
// 不前补零、无上限；(回合, 编号) 唯一。回合可负（存档侧不校验 turn 值域，
// 08:0x 裁定），负回合档照写照列。文件名仅作标识与人工辨认，身份与冗余
// 校验以帧内 battleKey/timestampUtcMs/turn 为准。

inline std::wstring MakeNumberedName(int32_t turn, uint64_t number, ArchiveFormat format)
{
    wchar_t name[48] = {};
    _snwprintf_s(name, _TRUNCATE, L"%d_%llu%s", turn,
        static_cast<unsigned long long>(number), ArchiveExtension_(format));
    return name;
}

// 严格两段解析：<int32 回合>_<uint64 编号，≥1>.<指定后缀>；
// 拒绝空段、前导 '+'、超宽数字与多余后缀。
inline bool ParseNumberedName(const std::wstring& name, ArchiveFormat format,
                              int32_t* turn, uint64_t* number)
{
    if (turn) *turn = 0;
    if (number) *number = 0;
    const wchar_t* ext = ArchiveExtension_(format);
    const size_t extLen = wcslen(ext);
    if (name.size() < 3 + extLen) return false;
    if (_wcsicmp(name.c_str() + name.size() - extLen, ext) != 0) return false;
    const wchar_t* p = name.c_str();
    bool negative = false;
    if (*p == L'-') {
        negative = true;
        ++p;
    }
    if (*p < L'0' || *p > L'9') return false;
    long long turnValue = 0;
    int digits = 0;
    while (*p >= L'0' && *p <= L'9') {
        if (++digits > 10) return false;
        turnValue = turnValue * 10 + (*p - L'0');
        if (turnValue > 2147483647LL) return false;
        ++p;
    }
    if (negative) turnValue = -turnValue;
    if (*p != L'_') return false;
    ++p;
    if (*p < L'1' || *p > L'9') return false; // 编号从 1 起，无前导零
    unsigned long long numberValue = 0;
    digits = 0;
    while (*p >= L'0' && *p <= L'9') {
        if (++digits > 20) return false;
        numberValue = numberValue * 10 + (*p - L'0');
        if (numberValue > 18446744073709551615ULL) return false;
        ++p;
    }
    if (p != name.c_str() + name.size() - extLen) return false;
    if (turn) *turn = static_cast<int32_t>(turnValue);
    if (number) *number = numberValue;
    return true;
}

// 同回合已有最大编号（仅统计能解析的当前后缀文件）；空回合从 1 起。
inline uint64_t NextNumberInTurn(const std::wstring& manualDir, int32_t turn, ArchiveFormat format)
{
    wchar_t turnText[16] = {};
    _snwprintf_s(turnText, _TRUNCATE, L"%d", turn);
    const std::wstring pattern = JoinRoot(manualDir, std::wstring(turnText) + L"_*" + ArchiveExtension_(format));
    uint64_t maxNumber = 0;
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(pattern.c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return 1;
    do {
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        int32_t fileTurn = 0;
        uint64_t fileNumber = 0;
        if (!ParseNumberedName(found.cFileName, format, &fileTurn, &fileNumber)) continue;
        if (fileTurn != turn) continue;
        if (fileNumber > maxNumber) maxNumber = fileNumber;
    } while (FindNextFileW(search, &found));
    FindClose(search);
    return maxNumber == 0 ? 1 : maxNumber + 1;
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
};

// 列表排序（2026-10-11 用户裁定，取代按 mtime/timestamp 读最新）：
// 按 (回合数, 编号) 两段数值倒序——比较必须把文件名拆成两段各自解析为整数，
// 禁止整串字符串排序（字典序在 7134_* vs 25_*、25_10 vs 25_2 上会错乱）。
// 读档回退后回合回跳（7134 回 25 再存）时旧高回合档仍排在前——接受该语义。
inline bool NewerRecord(const ArchiveRecord& a, const ArchiveRecord& b)
{
    if (a.turn != b.turn) return a.turn > b.turn;
    return a.number > b.number;
}

// ===== 本地时间显示（文件头 UTC 毫秒为身份权威；文件名时间标签已随布局
// 淘汰，此函数仅供 UI/日志呈现，禁止用 gmtime 或固定加 8 小时）=====

inline bool UtcMsToLocalSystemTime(uint64_t utcMs, SYSTEMTIME& local,
                                    const TIME_ZONE_INFORMATION* zone)
{
    local = {};
    if (utcMs > (0x7FFFFFFFFFFFFFFFull - 116444736000000000ull) / 10000ull) return false;
    const uint64_t ftv = utcMs * 10000ull + 116444736000000000ull;
    FILETIME ft = {};
    ft.dwLowDateTime = static_cast<DWORD>(ftv & 0xFFFFFFFFull);
    ft.dwHighDateTime = static_cast<DWORD>(ftv >> 32);
    SYSTEMTIME utc = {};
    if (!FileTimeToSystemTime(&ft, &utc)) return false;
    return SystemTimeToTzSpecificLocalTime(zone, &utc, &local) != FALSE;
}

inline bool UtcMsToLocalSystemTime(uint64_t utcMs, SYSTEMTIME& local)
{
    return UtcMsToLocalSystemTime(utcMs, local, nullptr);
}

inline bool FormatLocalArchiveStamp(uint64_t utcMs, char* out, size_t cap,
                                   const TIME_ZONE_INFORMATION* zone)
{
    if (!out || cap == 0) return false;
    out[0] = 0;
    SYSTEMTIME local = {};
    if (!UtcMsToLocalSystemTime(utcMs, local, zone)) return false;
    return _snprintf_s(out, cap, _TRUNCATE, "%04u%02u%02u-%02u%02u%02u",
        (unsigned)local.wYear, (unsigned)local.wMonth, (unsigned)local.wDay,
        (unsigned)local.wHour, (unsigned)local.wMinute, (unsigned)local.wSecond) >= 0;
}

// UI 显示标签：本地时间 + 同回合编号后缀（编号无上限，自然位数；旧撞名
// 后缀的 1..999 容量限制随新命名层取消）。
inline bool FormatNumberedArchiveStamp(uint64_t utcMs, uint64_t number, char* out, size_t cap,
                                       const TIME_ZONE_INFORMATION* zone)
{
    if (!out || cap == 0) return false;
    out[0] = 0;
    if (number < 1) return false;
    char stamp[32] = {};
    if (!FormatLocalArchiveStamp(utcMs, stamp, sizeof(stamp), zone)) return false;
    return _snprintf_s(out, cap, _TRUNCATE, "%s_%llu", stamp,
        static_cast<unsigned long long>(number)) >= 0;
}

// ===== 扫描：按指纹目录枚举 手动\ 下当前后缀档 =====
// battleKey 空 = 枚举存档根下全部 64hex 指纹目录；非空 = 只看该指纹目录。
// 指纹目录/手动 子目录缺失（尚无存档或玩家删除）= 该指纹无档，不报错。
// 坏档（解析/校验失败、文件名与身份不符）跳过并写错误聚合，留盘不删。
inline bool ScanOneBattleDir(const std::wstring& root, const std::string& battleKey,
                             ArchiveFormat format, std::vector<Scanned>& records,
                             std::wstring& error)
{
    const std::wstring manualDir = ManualDirOf(BattleDirOf(root, battleKey));
    const DWORD dirAttrs = GetFileAttributesW(manualDir.c_str());
    if (dirAttrs == INVALID_FILE_ATTRIBUTES || (dirAttrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return true; // 该指纹尚无手动目录：无档
    std::wstring manualFinal;
    {
        HANDLE dirHandle = INVALID_HANDLE_VALUE;
        std::wstring dirError;
        if (!OpenFinalPath(manualDir, FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           OPEN_EXISTING, manualFinal, dirHandle, dirError)) {
            AppendError(error, KeyToWide(battleKey) + L": " + dirError);
            return true;
        }
        CloseHandle(dirHandle);
    }

    const std::wstring pattern = JoinRoot(manualDir, std::wstring(L"*") + ArchiveExtension_(format));
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(pattern.c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return true;
        AppendError(error, KeyToWide(battleKey) + L": cannot scan archive root");
        return true;
    }

    do {
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const std::wstring name = found.cFileName;
        if (IsReservedName(name) || !HasArchiveExtension(name, format)) continue;
        int32_t turn = 0;
        uint64_t number = 0;
        if (!ParseNumberedName(name, format, &turn, &number)) {
            AppendError(error, name + L": filename does not match archive identity");
            continue;
        }
        const std::wstring path = JoinRoot(manualDir, name);
        std::wstring finalPath;
        HANDLE opened = INVALID_HANDLE_VALUE;
        std::wstring openError;
        if (!OpenFinalPath(path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, finalPath, opened, openError)) {
            AppendError(error, name + L": " + openError);
            continue;
        }
        CloseHandle(opened);
        if (!SameDirectory(finalPath, manualFinal, openError)
            || !HasArchiveExtension(FileNameOf(finalPath), format)) {
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
        bool decoded = false;
        if (format == ArchiveFormat::Json)
            decoded = DecodeJsonDocument(bytes.data(), bytes.size(), document, decodeError);
        else
            decoded = Decode(bytes.data(), bytes.size(), document, decodeError);
        if (!decoded) {
            AppendError(error, name + L": " + decodeError);
            continue;
        }
        if (document.battleKey != battleKey) {
            AppendError(error, name + L": filename does not match archive identity");
            continue;
        }
        if (format == ArchiveFormat::Json && document.turn != turn) {
            // §2：turn 与文件名回合互为冗余校验。
            AppendError(error, name + L": filename does not match archive identity");
            continue;
        }
        Scanned item;
        item.record.path = path;
        item.record.battleKey = document.battleKey;
        item.record.targetKey = document.targetKey;
        item.record.timestampUtcMs = document.timestampUtcMs;
        item.record.sequence = document.sequence;
        item.record.turn = turn;
        item.record.number = number;
        item.name = name;
        records.push_back(std::move(item));
    } while (FindNextFileW(search, &found));
    const DWORD scanError = GetLastError();
    FindClose(search);
    if (scanError != ERROR_NO_MORE_FILES && scanError != ERROR_SUCCESS) {
        AppendError(error, L"archive scan ended early");
        return false;
    }
    return true;
}

inline bool Scan(const std::wstring& root, ArchiveFormat format,
                 const std::string& battleKeyFilter,
                 std::vector<Scanned>& records, std::wstring& error)
{
    records.clear();
    error.clear();
    std::wstring rootFinal;
    if (!EnsureRoot(root, rootFinal, error)) return false;

    if (!battleKeyFilter.empty())
        return ScanOneBattleDir(root, battleKeyFilter, format, records, error);

    // 枚举存档根下的全部 64hex 指纹目录（旧平铺档在根目录，天然不进视野）。
    const std::wstring pattern = JoinRoot(root, L"*");
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(pattern.c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return true;
        SetError(error, L"cannot scan archive root");
        return false;
    }
    std::vector<std::string> battles;
    do {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        std::string key;
        if (!WideKeyToString(found.cFileName, &key)) continue;
        battles.push_back(key);
    } while (FindNextFileW(search, &found));
    const DWORD scanError = GetLastError();
    FindClose(search);
    if (scanError != ERROR_NO_MORE_FILES && scanError != ERROR_SUCCESS) {
        SetError(error, L"archive scan ended early");
        return false;
    }
    for (size_t i = 0; i < battles.size(); ++i) {
        if (!ScanOneBattleDir(root, battles[i], format, records, error)) return false;
    }
    // 排序统一在 ArchiveStore::List 出口做（NewerRecord），此处不预排。
    return true;
}

inline bool KeysMatch(const Scanned& item, const std::string& battleKey, const std::string& targetKey)
{
    if (!battleKey.empty() && item.record.battleKey != battleKey) return false;
    if (!targetKey.empty() && item.record.targetKey != targetKey) return false;
    return true;
}

} // namespace detail

inline ArchiveStore::ArchiveStore(std::wstring root, ArchiveFormat format)
    : root_(std::move(root))
    , format_(format)
{
}

inline bool ArchiveStore::Save(const ArchiveDocument& document, std::wstring& error, ArchiveRecord* committed)
{
    error.clear();
    if (committed) *committed = ArchiveRecord();
    if (!detail::IsKey(document.battleKey) || !detail::IsKey(document.targetKey)) {
        detail::SetError(error, L"battleKey and targetKey must be 64 hex characters");
        return false;
    }
    if (document.turn < 0) {
        // 文件名回合段解析为 int32；负回合档会立即失配列表身份（存档成功但
        // 不可见），直接拒绝更诚实。战斗回合从 1 起，负值只可能来自内存损坏。
        detail::SetError(error, L"battle turn is negative");
        return false;
    }
    std::vector<uint8_t> encoded;
    if (format_ == ArchiveFormat::Json) {
        if (!detail::EncodeJsonDocument(document, encoded, error)) return false;
    } else {
        if (!detail::ValidateSections(document.sections, error)) return false;
        if (!detail::Encode(document, encoded, error)) return false;
    }

    std::wstring rootFinal;
    if (!detail::EnsureRoot(root_, rootFinal, error)) return false;
    // 首次存档创建指纹目录并建齐 手动/自动 两个子目录（自动预留只建不写）。
    if (!detail::EnsureBattleTree(root_, document.battleKey, error)) return false;
    const std::wstring manualDir = detail::ManualDirOf(detail::BattleDirOf(root_, document.battleKey));

    // 编号 = 同回合已有最大编号 + 1（无上限自然位数）；(回合, 编号) 唯一。
    const uint64_t number = detail::NextNumberInTurn(manualDir, document.turn, format_);
    const std::wstring name = detail::MakeNumberedName(document.turn, number, format_);

    const std::wstring finalPath = detail::JoinRoot(manualDir, name);
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

    if (committed) {
        committed->path = finalPath;
        committed->battleKey = document.battleKey;
        committed->targetKey = document.targetKey;
        committed->timestampUtcMs = document.timestampUtcMs;
        committed->sequence = document.sequence;
        committed->turn = document.turn;
        committed->number = number;
    }

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
    if (!detail::Scan(root_, format_, battleKey, scanned, error)) return false;
    for (size_t i = 0; i < scanned.size(); ++i) {
        if (detail::KeysMatch(scanned[i], battleKey, targetKey))
            records.push_back(scanned[i].record);
    }
    // 两段数值倒序是唯一列表序（单指纹目录路径不经过 Scan 的全局排序，
    // 在此统一出口排序，保证两条路径一致）。
    std::sort(records.begin(), records.end(), detail::NewerRecord);
    return true;
}

inline bool ArchiveStore::Load(const ArchiveRecord& record, ArchiveDocument& document, std::wstring& error)
{
    document = ArchiveDocument();
    if (!detail::IsKey(record.battleKey)) {
        detail::SetError(error, L"refusing archive path outside the store root");
        return false;
    }
    std::wstring rootFinal;
    if (!detail::EnsureRoot(root_, rootFinal, error)) return false;
    const std::wstring manualDir = detail::ManualDirOf(detail::BattleDirOf(root_, record.battleKey));
    const std::wstring name = detail::FileNameOf(record.path);
    if (detail::IsReservedName(name) || !detail::HasArchiveExtension(name, format_)
        || record.path != detail::JoinRoot(manualDir, name)) {
        detail::SetError(error, L"refusing archive path outside the store root");
        return false;
    }
    int32_t turn = 0;
    uint64_t number = 0;
    if (!detail::ParseNumberedName(name, format_, &turn, &number)) {
        detail::SetError(error, L"filename does not match archive identity");
        return false;
    }
    std::wstring manualFinal;
    {
        HANDLE dirHandle = INVALID_HANDLE_VALUE;
        std::wstring dirError;
        if (!detail::OpenFinalPath(manualDir, FILE_READ_ATTRIBUTES,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   OPEN_EXISTING, manualFinal, dirHandle, error)) {
            return false;
        }
        CloseHandle(dirHandle);
    }
    std::wstring finalPath;
    HANDLE opened = INVALID_HANDLE_VALUE;
    if (!detail::OpenFinalPath(record.path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING,
                               finalPath, opened, error)) {
        return false;
    }
    CloseHandle(opened);
    if (!detail::SameDirectory(finalPath, manualFinal, error)) return false;

    std::vector<uint8_t> bytes;
    if (!detail::ReadWholeFile(record.path, bytes, error)) return false;
    if (format_ == ArchiveFormat::Json) {
        if (!detail::DecodeJsonDocument(bytes.data(), bytes.size(), document, error)) return false;
        // §2：turn 与文件名回合互为冗余校验；battleKey 与指纹目录名互验。
        if (document.turn != turn || document.battleKey != record.battleKey) {
            document = ArchiveDocument();
            detail::SetError(error, L"filename does not match archive identity");
            return false;
        }
    } else {
        if (!detail::Decode(bytes.data(), bytes.size(), document, error)) return false;
        if (document.battleKey != record.battleKey) {
            document = ArchiveDocument();
            detail::SetError(error, L"filename does not match archive identity");
            return false;
        }
        document.turn = turn; // 二进制帧头不含 turn，以文件名回合为准
    }
    // 2026-10-10 用户裁定：记录与文档的四元组一致性比较已删除——文件名↔内容
    // 身份（指纹目录）与 battleKey 比较已绑定"文件属于当前战斗"。
    return true;
}

} // namespace hbs

#pragma pack(pop)
