/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * zip.cpp
 */
#include "zip.h"
#include "platform.h"

#include <miniz.h>

#include <cstring>
#include <filesystem>

namespace pl {

static std::string lowercase(const std::string& s) {
    std::string out = s;
    for (auto& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

static bool has_extension(const std::string& name, const char* ext) {
    size_t pos = name.rfind('.');
    if (pos == std::string::npos) return false;
    return lowercase(name.substr(pos + 1)) == ext;
}

static bool excluded(const std::string& rel, const std::vector<std::string>& prefixes) {
    for (const auto& p : prefixes) {
        if (rel.rfind(p, 0) == 0)
            return true;
    }
    return false;
}

bool extract_zip(const std::string& zip_path,
                 const std::string& dest_dir,
                 const ZipExtractOptions& opts,
                 std::string* error) {

    if (!file_exists(zip_path)) {
        if (error) *error = "native library archive not found: " + zip_path;
        return false;
    }

    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, zip_path.c_str(), 0)) {
        if (error) *error = "cannot open zip archive: " + zip_path;
        return false;
    }

    create_directories(dest_dir);
    bool ok = true;

    mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st))
            continue;

        std::string name = st.m_filename;
        // normalize backslashes
        for (auto& c : name)
            if (c == '\\') c = '/';

        if (st.m_is_directory)
            continue;
        if (opts.skip_symlinks && ((st.m_external_attr >> 16) & 0170000) == 0120000)
            continue;
        if (opts.skip_git && has_extension(name, "git"))
            continue;
        if (opts.skip_sha1 && has_extension(name, "sha1"))
            continue;
        if (excluded(name, opts.exclude))
            continue;

        std::string lower = lowercase(pl::file_name(name));
        if (opts.skip_glfw && lower.find("glfw") != std::string::npos)
            continue;
        if (opts.skip_sdl && lower.find("sdl") != std::string::npos)
            continue;
        if (opts.skip_openal && lower.find("openal") != std::string::npos)
            continue;

        std::string dest = join_path(dest_dir, name);
        if (!file_exists(parent_dir(dest)))
            create_directories(parent_dir(dest));

        // Skip when an existing regular file has exactly the same size.
        if (opts.skip_if_same_size && file_exists(dest) && is_directory(dest) == false) {
            if (file_size(dest) == static_cast<long long>(st.m_uncomp_size))
                continue;
        }

        if (!delete_file(dest))
            continue;

        size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        if (!data) {
            ok = false;
            if (error) *error = "failed to extract " + name + " from " + zip_path;
            break;
        }
        bool written = write_file(dest, std::string(static_cast<const char*>(data), size));
        MZ_FREE(data);
        if (!written) {
            ok = false;
            if (error) *error = "failed to write " + dest;
            break;
        }
    }

    mz_zip_reader_end(&zip);
    return ok;
}

} // namespace pl
