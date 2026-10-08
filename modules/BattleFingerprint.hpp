#pragma once

#include <stdint.h>
#include <string>
#include <vector>

static void FingerprintPut32_(std::vector<uint8_t>& bytes, uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes.push_back((uint8_t)(value >> shift));
}

// Length framing preserves empty names, non-ASCII bytes and field boundaries.
static void FingerprintAppendMap_(std::vector<uint8_t>& bytes,
    const std::string& title)
{
    FingerprintPut32_(bytes, 0x3250414D); // MAP2: title-only map identity.
    FingerprintPut32_(bytes, (uint32_t)title.size());
    bytes.insert(bytes.end(), title.begin(), title.end());
}
