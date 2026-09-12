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

// Rewrite a Mojang/Forge/Maven URL onto the given mirror root, following the
// same host mapping HMCL's BMCLAPIDownloadProvider uses. Unrecognized hosts are
// returned unchanged.
std::string mirror_url(const std::string& url, const std::string& mirror_root);

// Download `urls` (tried in order) to `dest`. When `sha1` is non-empty the
// downloaded bytes are verified against it before `dest` is committed, so a
// partially written or corrupt file never replaces a good one. `proxy` is an
// optional "host:port" forwarded to curl. Returns true on success.
bool download_file(const std::vector<std::string>& urls,
                   const std::string& dest,
                   const std::string& sha1,
                   const std::string& proxy,
                   std::string* error);

// Whether the file at `path` hashes to `sha1` (case-insensitive hex).
bool sha1_matches(const std::string& path, const std::string& sha1);

} // namespace pl
