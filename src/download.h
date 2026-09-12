/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * download.h - missing library download helpers
 *
 * Mirrors HMCL's DownloadProvider / FileDownloadTask: resolves mirror URLs,
 * fetches a file through the system `curl` binary and verifies its SHA-1.
 */
#pragma once

#include <string>
#include <vector>

namespace pl {

// Default mirror root used when the user picks mirror-first downloads.
extern const char* const kDefaultMirrorRoot;

// Official authlib-injector metadata endpoint (the mirror mirrors this path).
extern const char* const kAuthlibInjectorLatest;

// Hash algorithm used to verify a downloaded file.
enum class HashKind { Sha1, Sha256 };

// Rewrite a Mojang/Forge/Maven URL onto the given mirror root, following the
// same host mapping HMCL's BMCLAPIDownloadProvider uses. Unrecognized hosts are
// returned unchanged.
std::string mirror_url(const std::string& url, const std::string& mirror_root);

// Download `urls` (tried in order) to `dest`. When `hash` is non-empty the
// downloaded bytes are verified against it before `dest` is committed, so a
// partially written or corrupt file never replaces a good one. `proxy` is an
// optional "host:port" forwarded to curl. Returns true on success.
bool download_file(const std::vector<std::string>& urls,
                   const std::string& dest,
                   const std::string& hash,
                   const std::string& proxy,
                   std::string* error,
                   HashKind kind = HashKind::Sha1);

// Whether the file at `path` hashes to `hex` (case-insensitive).
bool sha1_matches(const std::string& path, const std::string& hex);
bool sha256_matches(const std::string& path, const std::string& hex);

// Fetch the latest authlib-injector metadata (mirror/official per mirror_first)
// and download the artifact to `dest`, verifying its SHA-256. Used to complete
// the resource set for offline skin support.
bool download_authlib_injector(const std::string& dest,
                               bool mirror_first,
                               const std::string& mirror_root,
                               const std::string& proxy,
                               std::string* error);

} // namespace pl
