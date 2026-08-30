/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * auth/crypto.cpp
 */
#include "crypto.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace pl {

// ---------------------------------------------------------------------------
// base64
// ---------------------------------------------------------------------------
std::string base64_encode(const std::string& data) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= data.size()) {
        uint32_t n = (static_cast<uint8_t>(data[i]) << 16) |
                     (static_cast<uint8_t>(data[i + 1]) << 8) |
                     static_cast<uint8_t>(data[i + 2]);
        out += tbl[(n >> 18) & 63];
        out += tbl[(n >> 12) & 63];
        out += tbl[(n >> 6) & 63];
        out += tbl[n & 63];
        i += 3;
    }
    if (data.size() - i == 1) {
        uint32_t n = static_cast<uint8_t>(data[i]) << 16;
        out += tbl[(n >> 18) & 63];
        out += tbl[(n >> 12) & 63];
        out += "==";
    } else if (data.size() - i == 2) {
        uint32_t n = (static_cast<uint8_t>(data[i]) << 16) |
                     (static_cast<uint8_t>(data[i + 1]) << 8);
        out += tbl[(n >> 18) & 63];
        out += tbl[(n >> 12) & 63];
        out += tbl[(n >> 6) & 63];
        out += '=';
    }
    return out;
}

static const int8_t* base64_reverse_table() {
    static int8_t rev[256];
    static bool initialized = false;
    if (!initialized) {
        std::memset(rev, -1, sizeof(rev));
        const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i)
            rev[static_cast<uint8_t>(tbl[i])] = static_cast<int8_t>(i);
        initialized = true;
    }
    return rev;
}

std::string base64_decode(const std::string& text) {
    const int8_t* rev = base64_reverse_table();
    std::string out;
    uint32_t buf = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=' || c == '\n' || c == '\r') continue;
        int8_t v = rev[static_cast<uint8_t>(c)];
        if (v < 0) return {};
        buf = (buf << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((buf >> bits) & 0xff);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// hex
// ---------------------------------------------------------------------------
std::string bytes_to_hex(const uint8_t* data, size_t len) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out += hex[(data[i] >> 4) & 0xf];
        out += hex[data[i] & 0xf];
    }
    return out;
}

std::string hex_to_bytes(const std::string& hex) {
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        int hi = val(hex[i]), lo = val(hex[i + 1]);
        if (hi < 0 || lo < 0) return {};
        out += static_cast<char>((hi << 4) | lo);
    }
    return out;
}

// ---------------------------------------------------------------------------
// MD5 (RFC 1321)
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t MD5_K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};
constexpr uint32_t MD5_S[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

uint32_t rotl(uint32_t x, uint32_t c) { return (x << c) | (x >> (32 - c)); }

void md5_process(std::vector<uint32_t>& state, const uint8_t* block) {
    uint32_t M[16];
    for (int i = 0; i < 16; ++i)
        M[i] = static_cast<uint32_t>(block[i * 4]) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 8) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 3]) << 24);

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    for (int i = 0; i < 64; ++i) {
        uint32_t f;
        int g;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) % 16;
        }
        uint32_t tmp = d;
        d = c;
        c = b;
        b = b + rotl(a + f + MD5_K[i] + M[g], MD5_S[i]);
        a = tmp;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
}
} // namespace

std::string md5_raw(const std::string& data) {
    std::vector<uint32_t> state = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    std::vector<uint8_t> padded;
    padded.reserve(data.size() + 72);
    padded.insert(padded.end(), data.begin(), data.end());
    padded.push_back(0x80);
    while (padded.size() % 64 != 56)
        padded.push_back(0x00);
    uint64_t bitlen = static_cast<uint64_t>(data.size()) * 8;
    for (int i = 0; i < 8; ++i)
        padded.push_back(static_cast<uint8_t>((bitlen >> (8 * i)) & 0xff));

    for (size_t i = 0; i < padded.size(); i += 64)
        md5_process(state, padded.data() + i);

    std::string out;
    out.reserve(16);
    for (uint32_t v : state) {
        out += static_cast<char>(v & 0xff);
        out += static_cast<char>((v >> 8) & 0xff);
        out += static_cast<char>((v >> 16) & 0xff);
        out += static_cast<char>((v >> 24) & 0xff);
    }
    return out;
}

std::string md5_hex(const std::string& data) {
    std::string raw = md5_raw(data);
    return bytes_to_hex(reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
}

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4)
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t SHA256_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

uint32_t rotr(uint32_t x, uint32_t c) { return (x >> c) | (x << (32 - c)); }

void sha256_transform(uint32_t state[8], const uint8_t* block) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
               static_cast<uint32_t>(block[i * 4 + 3]);
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + S1 + ch + SHA256_K[i] + w[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}
} // namespace

std::string sha256_raw(const std::string& data) {
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };
    std::vector<uint8_t> padded;
    padded.reserve(data.size() + 72);
    padded.insert(padded.end(), data.begin(), data.end());
    padded.push_back(0x80);
    while (padded.size() % 64 != 56)
        padded.push_back(0x00);
    uint64_t bitlen = static_cast<uint64_t>(data.size()) * 8;
    for (int i = 7; i >= 0; --i)
        padded.push_back(static_cast<uint8_t>((bitlen >> (8 * i)) & 0xff));

    for (size_t i = 0; i < padded.size(); i += 64)
        sha256_transform(state, padded.data() + i);

    std::string out;
    out.reserve(32);
    for (uint32_t v : state) {
        out += static_cast<char>((v >> 24) & 0xff);
        out += static_cast<char>((v >> 16) & 0xff);
        out += static_cast<char>((v >> 8) & 0xff);
        out += static_cast<char>(v & 0xff);
    }
    return out;
}

std::string sha256_hex(const std::string& data) {
    std::string raw = sha256_raw(data);
    return bytes_to_hex(reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
}

// ---------------------------------------------------------------------------
// SHA-1 (FIPS 180-1)
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t SHA1_K[80] = {
    0x5a827999, 0x5a827999, 0x5a827999, 0x5a827999, 0x5a827999, 0x5a827999, 0x5a827999, 0x5a827999,
    0x5a827999, 0x5a827999, 0x5a827999, 0x5a827999, 0x5a827999, 0x5a827999, 0x5a827999, 0x5a827999,
    0x5a827999, 0x5a827999, 0x5a827999, 0x5a827999,
    0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1,
    0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1,
    0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1,
    0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc,
    0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc,
    0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc,
    0xca62c1d6, 0xca62c1d6, 0xca62c1d6, 0xca62c1d6, 0xca62c1d6, 0xca62c1d6, 0xca62c1d6, 0xca62c1d6,
    0xca62c1d6, 0xca62c1d6, 0xca62c1d6, 0xca62c1d6, 0xca62c1d6, 0xca62c1d6, 0xca62c1d6, 0xca62c1d6,
    0xca62c1d6, 0xca62c1d6, 0xca62c1d6, 0xca62c1d6,
};

uint32_t rotl32(uint32_t x, uint32_t c) { return (x << c) | (x >> (32 - c)); }

void sha1_transform(uint32_t h[5], const uint8_t* block) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i)
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
               static_cast<uint32_t>(block[i * 4 + 3]);
    for (int i = 16; i < 80; ++i) {
        w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) {
        uint32_t f, k = SHA1_K[i];
        if (i < 20) f = (b & c) | (~b & d);
        else if (i < 40) f = b ^ c ^ d;
        else if (i < 60) f = (b & c) | (b & d) | (c & d);
        else f = b ^ c ^ d;
        uint32_t tmp = rotl32(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rotl32(b, 30); b = a; a = tmp;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}
} // namespace

std::string sha1_raw(const std::string& data) {
    uint32_t h[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
    std::vector<uint8_t> padded;
    padded.reserve(data.size() + 72);
    padded.insert(padded.end(), data.begin(), data.end());
    padded.push_back(0x80);
    while (padded.size() % 64 != 56)
        padded.push_back(0x00);
    uint64_t bitlen = static_cast<uint64_t>(data.size()) * 8;
    for (int i = 7; i >= 0; --i)
        padded.push_back(static_cast<uint8_t>((bitlen >> (8 * i)) & 0xff));

    for (size_t i = 0; i < padded.size(); i += 64)
        sha1_transform(h, padded.data() + i);

    std::string out;
    out.reserve(20);
    for (uint32_t v : h) {
        out += static_cast<char>((v >> 24) & 0xff);
        out += static_cast<char>((v >> 16) & 0xff);
        out += static_cast<char>((v >> 8) & 0xff);
        out += static_cast<char>(v & 0xff);
    }
    return out;
}

// ---------------------------------------------------------------------------
// UUID helpers
// ---------------------------------------------------------------------------
std::string format_uuid(const std::string& compact) {
    std::string out = compact;
    if (out.size() != 32) return out;
    out.insert(8, "-");
    out.insert(13, "-");
    out.insert(18, "-");
    out.insert(23, "-");
    return out;
}

std::string offline_uuid(const std::string& username) {
    std::string md5 = md5_raw("OfflinePlayer:" + username);
    uint8_t bytes[16];
    std::memcpy(bytes, md5.data(), 16);
    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0f) | 0x30); // version 3
    bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3f) | 0x80); // IETF variant
    return bytes_to_hex(bytes, 16);
}

std::string random_uuid(bool compact) {
    uint8_t bytes[16];
    FILE* f = fopen("/dev/urandom", "rb");
    if (f) {
        size_t got = fread(bytes, 1, 16, f);
        fclose(f);
        if (got != 16) {
            // deterministic fallback (should not happen)
            static uint64_t counter = 0;
            std::memcpy(bytes, &counter, sizeof(counter));
            std::memcpy(bytes + 8, &counter, sizeof(counter));
            ++counter;
        }
    } else {
        std::memset(bytes, 0, 16);
    }
    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0f) | 0x40); // version 4
    bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3f) | 0x80); // IETF variant
    std::string hex = bytes_to_hex(bytes, 16);
    return compact ? hex : format_uuid(hex);
}

std::string random_suffix() {
    std::string hex = random_uuid(true);
    return hex.substr(0, 8);
}

} // namespace pl
