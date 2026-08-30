/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * auth/crypto.h - small self-contained hash / encoding helpers
 *
 * MD5 and SHA-256 are implemented from scratch; base64 / hex encodings too.
 * These power offline UUID derivation (MD5 nameUUID) and the local
 * yggdrasil server's texture hashing.
 */
#pragma once

#include <cstdint>
#include <string>

namespace pl {

// base64 (standard alphabet, with padding)
std::string base64_encode(const std::string& data);
// base64 decode; returns empty string on malformed input
std::string base64_decode(const std::string& text);

// MD5 over the input; returns the 16 raw digest bytes.
std::string md5_raw(const std::string& data);
std::string md5_hex(const std::string& data);

// SHA-256 over the input; returns the 32 raw digest bytes.
std::string sha256_raw(const std::string& data);
std::string sha256_hex(const std::string& data);

// SHA-1 over the input; returns the 20 raw digest bytes.
std::string sha1_raw(const std::string& data);

// Hex helpers.
std::string bytes_to_hex(const uint8_t* data, size_t len);
std::string hex_to_bytes(const std::string& hex);

// UUID helpers.
// Minecraft offline uuid: name-based MD5 (Java UUID.nameUUIDFromBytes)
// over "OfflinePlayer:" + username, version 3, IETF variant.
std::string offline_uuid(const std::string& username);

// Random version-4 UUID, either compact (no dashes) or canonical form.
std::string random_uuid(bool compact = true);

// Format a 32-hex-char uuid (compact) into canonical 8-4-4-4-12 form.
std::string format_uuid(const std::string& compact);

// Random lowercase alphanumeric suffix (for temp file names).
std::string random_suffix();

} // namespace pl
