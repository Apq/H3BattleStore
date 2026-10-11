#pragma once

// ========== BattleStoreJsonCore.inc.hpp ==========
// 生产与独立测试共用的 JSON 核心（2026-10-11 用户裁定：存档双格式可切换，
// JSON 后端沿用 H3Auto BattleStoreJsonCore 的 nlohmann/json 3.11.3 单头 +
// JSON_NOEXCEPTION 禁异常封装与 parse 签名；库文件在 third_party/nlohmann/）。
// 游戏线程用 SEH，库侧关闭 C++ 异常：parse 失败返回 discarded，不抛出。
// 生产 vcxproj 已定义 JSON_NOEXCEPTION；独立测试的 cl 命令未定义，此处兜底，
// 保证两侧编译行为一致（禁止在 include 之后再启用异常）。
// 包含方须已先包含 windows.h（编码转换用 MultiByteToWideChar）。
// 取值辅助为通用字宽界校验（存在/类型/字宽），业务值域校验归调用方防线。

#ifndef JSON_NOEXCEPTION
#define JSON_NOEXCEPTION 1
#endif

#include <stddef.h>
#include <string.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "../third_party/nlohmann/json.hpp"

using BattleJson = nlohmann::json;

inline const long long kJsonI8Min_ = -128;
inline const long long kJsonI8Max_ = 127;
inline const long long kJsonI16Min_ = -32768;
inline const long long kJsonI16Max_ = 32767;
inline const long long kJsonI32Min_ = -2147483648LL;
inline const long long kJsonI32Max_ = 2147483647LL;
inline const long long kJsonU8Max_ = 255;
inline const long long kJsonU16Max_ = 65535;
inline const long long kJsonU32Max_ = 4294967295LL;

// 无符号/有符号双形态整数取值与字宽界（对等二进制按字宽读出；越界拒绝，
// 大无符号不得伪装成合法有符号值）。
inline bool JsonIntRange_(const BattleJson& node, long long lo, long long hi, long long* out)
{
    if (node.is_number_unsigned()) {
        const unsigned long long v = node.get<unsigned long long>();
        if (v > (unsigned long long)hi) return false;
        *out = (long long)v;
        return true;
    }
    if (node.is_number_integer()) {
        const long long v = node.get<long long>();
        if (v < lo) return false;
        *out = v;
        return true;
    }
    return false;
}

inline bool JsonReadInt_(const BattleJson& obj, const char* key, long long lo, long long hi, long long* out)
{
    if (!obj.is_object() || !obj.contains(key)) return false;
    return JsonIntRange_(obj[key], lo, hi, out);
}

inline bool JsonI8_(const BattleJson& obj, const char* key, int8_t* out)
{
    long long v = 0;
    if (!JsonReadInt_(obj, key, kJsonI8Min_, kJsonI8Max_, &v)) return false;
    *out = (int8_t)v;
    return true;
}

inline bool JsonI16_(const BattleJson& obj, const char* key, int16_t* out)
{
    long long v = 0;
    if (!JsonReadInt_(obj, key, kJsonI16Min_, kJsonI16Max_, &v)) return false;
    *out = (int16_t)v;
    return true;
}

inline bool JsonI32_(const BattleJson& obj, const char* key, int32_t* out)
{
    long long v = 0;
    if (!JsonReadInt_(obj, key, kJsonI32Min_, kJsonI32Max_, &v)) return false;
    *out = (int32_t)v;
    return true;
}

inline bool JsonU8_(const BattleJson& obj, const char* key, uint8_t* out)
{
    long long v = 0;
    if (!JsonReadInt_(obj, key, 0, kJsonU8Max_, &v)) return false;
    *out = (uint8_t)v;
    return true;
}

inline bool JsonU16_(const BattleJson& obj, const char* key, uint16_t* out)
{
    long long v = 0;
    if (!JsonReadInt_(obj, key, 0, kJsonU16Max_, &v)) return false;
    *out = (uint16_t)v;
    return true;
}

inline bool JsonU32_(const BattleJson& obj, const char* key, uint32_t* out)
{
    long long v = 0;
    if (!JsonReadInt_(obj, key, 0, kJsonU32Max_, &v)) return false;
    *out = (uint32_t)v;
    return true;
}

// null 是非有限浮点的 dump 形态，读回 NaN（交给读档侧有限性检查拒绝，
// 与二进制存 NaN 位模式后拒绝的路径同口径）。
inline bool JsonF32_(const BattleJson& obj, const char* key, float* out)
{
    if (!obj.is_object() || !obj.contains(key)) return false;
    const BattleJson& node = obj[key];
    if (node.is_null()) {
        const unsigned nanBits = 0x7FC00000u;
        memcpy(out, &nanBits, sizeof(*out));
        return true;
    }
    if (!node.is_number()) return false;
    *out = (float)node.get<double>();
    return true;
}

// 字节数组：每个字节一个数值元素（0..255），尺寸精确校验。
inline bool JsonBytesToArray_(const BattleJson& obj, const char* key, uint8_t* out, size_t size)
{
    if (!obj.is_object() || !obj.contains(key)) return false;
    const BattleJson& arr = obj[key];
    if (!arr.is_array() || arr.size() != size) return false;
    for (size_t i = 0; i < size; ++i) {
        long long v = 0;
        if (!JsonIntRange_(arr[i], 0, kJsonU8Max_, &v)) return false;
        out[i] = (uint8_t)v;
    }
    return true;
}

inline bool JsonIntsToArray_(const BattleJson& obj, const char* key, int32_t* out, size_t count)
{
    if (!obj.is_object() || !obj.contains(key)) return false;
    const BattleJson& arr = obj[key];
    if (!arr.is_array() || arr.size() != count) return false;
    for (size_t i = 0; i < count; ++i) {
        long long v = 0;
        if (!JsonIntRange_(arr[i], kJsonI32Min_, kJsonI32Max_, &v)) return false;
        out[i] = (int32_t)v;
    }
    return true;
}

// 原始字节串 ↔ JSON 字符串的 Latin-1 映射（每字节 0x00..0xFF → 同码点
// U+0000..U+00FF）：产合法 UTF-8 可 dump；纯 ASCII（资源名常态）完全可读；
// 高位字节无损但显示为重音字母。用于 DEF/PCX 资源名等定长 char[] 字段。
inline std::string JsonFromRawBytes_(const char* data, size_t len)
{
    std::string out;
    out.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = (unsigned char)data[i];
        if (c < 0x80) out.push_back((char)c);
        else {
            out.push_back((char)(0xC0 | (c >> 6)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

inline bool JsonLatin1ToBytes_(const std::string& text, std::string* out)
{
    out->clear();
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = (unsigned char)text[i];
        unsigned cp = 0;
        size_t step = 0;
        if (c < 0x80) { cp = c; step = 1; }
        else if ((c & 0xE0) == 0xC0) {
            if (i + 1 >= text.size()) return false;
            const unsigned char c2 = (unsigned char)text[i + 1];
            if ((c2 & 0xC0) != 0x80) return false;
            cp = ((c & 0x1F) << 6) | (c2 & 0x3F);
            step = 2;
        } else return false; // 码点超出 U+00FF：不是本编码写出的字符串
        out->push_back((char)cp);
        i += step;
    }
    return true;
}

inline bool JsonLatin1ToCharArray_(const std::string& text, char* out, size_t capacity, size_t* written)
{
    std::string bytes;
    if (!JsonLatin1ToBytes_(text, &bytes)) return false;
    if (bytes.size() > capacity) return false;
    memcpy(out, bytes.data(), bytes.size());
    *written = bytes.size();
    return true;
}

// H3 战斗日志行是本机 ANSI（中文环境 GBK）字节，JSON 文本必须合法 UTF-8。
// 严格 CP_ACP→UTF-8 转换让中文行以真中文存档（可读可 diff）；任一行含
// 非法本机序列时退化为 Latin-1 字节映射（无损保底，由段级 "encoding"
// 标记区分）；纯 ASCII 两种编码结果相同。
inline bool JsonAcpToUtf8_(const std::string& acp, std::string* utf8)
{
    utf8->clear();
    if (acp.empty()) return true;
    const int wlen = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS,
        acp.data(), (int)acp.size(), nullptr, 0);
    if (wlen <= 0) return false;
    std::vector<wchar_t> wide((size_t)wlen);
    MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, acp.data(), (int)acp.size(),
        wide.data(), wlen);
    const int ulen = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wlen, nullptr, 0,
        nullptr, nullptr);
    if (ulen <= 0) return false;
    utf8->resize((size_t)ulen);
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), wlen, &(*utf8)[0], ulen,
        nullptr, nullptr);
    return true;
}

inline void JsonUtf8ToAcp_(const std::string& utf8, std::string* acp)
{
    acp->clear();
    if (utf8.empty()) return;
    // JSON parse 已保证合法 UTF-8；本机代码页外码点（手改写入）替换 '?'。
    const int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        utf8.data(), (int)utf8.size(), nullptr, 0);
    if (wlen <= 0) { acp->assign(utf8); return; }
    std::vector<wchar_t> wide((size_t)wlen);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), (int)utf8.size(),
        wide.data(), wlen);
    const int alen = WideCharToMultiByte(CP_ACP, 0, wide.data(), wlen, nullptr, 0,
        nullptr, nullptr);
    if (alen <= 0) { acp->assign(utf8); return; }
    acp->resize((size_t)alen);
    WideCharToMultiByte(CP_ACP, 0, wide.data(), wlen, &(*acp)[0], alen,
        nullptr, nullptr);
}

inline BattleJson JsonByteArray_(const uint8_t* data, size_t size)
{
    BattleJson arr = BattleJson::array();
    for (size_t i = 0; i < size; ++i) arr.push_back(data[i]);
    return arr;
}

inline BattleJson JsonInt32Array_(const int32_t* data, size_t count)
{
    BattleJson arr = BattleJson::array();
    for (size_t i = 0; i < count; ++i) arr.push_back(data[i]);
    return arr;
}
