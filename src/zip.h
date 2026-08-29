/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * zip.h - native library extraction (miniz wrapper)
 *
 * Mirrors HMCL's Unzipper usage in DefaultLauncher.decompressNatives.
 */
#pragma once

#include <string>
#include <vector>

namespace pl {

struct ZipExtractOptions {
    // Prefixes (relative paths) to skip, from a library's extract.exclude.
    std::vector<std::string> exclude;

    bool skip_git = true;
    bool skip_sha1 = true;
    bool skip_symlinks = true;

    // Keep existing files that already have the same size (HMCL default).
    bool skip_if_same_size = true;

    // Set when the user wants the system's native GLFW/SDL/OpenAL.
    bool skip_glfw = false;
    bool skip_sdl = false;
    bool skip_openal = false;
};

// Extract the given zip archive into dest_dir.
// Returns true on success; on failure sets `error` and returns false.
bool extract_zip(const std::string& zip_path,
                 const std::string& dest_dir,
                 const ZipExtractOptions& opts,
                 std::string* error);

} // namespace pl
