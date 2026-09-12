/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * download.cpp
 *
 * Resolves mirror URLs and fetches files with the system `curl` binary.
 * Mirrors HMCL's BMCLAPIDownloadProvider.injectURL and FileDownloadTask.
 */
#include "download.h"
#include "auth/crypto.h"
#include "platform.h"

#include <cstdio>
#include <utility>

namespace pl {

const char* const kDefaultMirrorRoot = "https://bmclapi2.bangbang93.com";

std::string mirror_url(const std::string& url, const std::string& mirror_root) {
    if (url.empty() || mirror_root.empty())
        return url;

    // (prefix, replacement) pairs, most specific first. Mirrors the subset of
    // HMCL's BMCLAPIDownloadProvider replacement table relevant to libraries.
    static const std::pair<const char*, const char*> kRules[] = {
        {"https://bmclapi2.bangbang93.com", ""},
        {"https://libraries.minecraft.net", "/libraries"},
        {"https://maven.neoforged.net/releases/", "/maven/"},
        {"https://files.minecraftforge.net/maven", "/maven"},
        {"https://maven.minecraftforge.net", "/maven"},
        {"https://launchermeta.mojang.com", ""},
        {"https://piston-meta.mojang.com", ""},
        {"https://piston-data.mojang.com", ""},
        {"https://launcher.mojang.com", ""},
        {"https://repo1.maven.org/maven2",
         "https://mirrors.cloud.tencent.com/nexus/repository/maven-public"},
        {"https://repo.maven.apache.org/maven2",
         "https://mirrors.cloud.tencent.com/nexus/repository/maven-public"},
    };

    for (const auto& rule : kRules) {
        std::string prefix = rule.first;
        if (url.rfind(prefix, 0) != 0)
            continue;
        std::string replacement = rule.second;
        // An empty suffix means the host maps to the mirror root itself.
        if (replacement.empty())
            replacement = mirror_root;
        else
            replacement = mirror_root + replacement;
        return replacement + url.substr(prefix.size());
    }
    return url;
}

bool sha1_matches(const std::string& path, const std::string& sha1) {
    if (sha1.empty())
        return true;
    auto data = read_small_file(path);
    if (!data)
        return false;
    std::string raw = sha1_raw(*data);
    std::string hex = bytes_to_hex(reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
    if (hex.size() != sha1.size())
        return false;
    for (size_t i = 0; i < hex.size(); ++i) {
        char a = hex[i], b = sha1[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b)
            return false;
    }
    return true;
}

bool download_file(const std::vector<std::string>& urls,
                   const std::string& dest,
                   const std::string& sha1,
                   const std::string& proxy,
                   std::string* error) {
    if (urls.empty()) {
        if (error) *error = "no download url for " + dest;
        return false;
    }

    std::string curl = resolve_tool(get_env("POTATO_CURL").value_or("curl"));
    if (curl.empty()) {
        if (error) *error = "curl not found (install curl or set POTATO_CURL)";
        return false;
    }

    create_directories(parent_dir(dest));
    std::string tmp = dest + ".part-" + random_suffix();
    std::string last_error;

    for (const std::string& url : urls) {
        if (url.empty())
            continue;

        std::vector<std::string> args;
        args.push_back(curl);
        args.push_back("-fL");
        args.push_back("--connect-timeout");
        args.push_back("15");
        args.push_back("--retry");
        args.push_back("2");
        args.push_back("--retry-delay");
        args.push_back("1");
        if (!proxy.empty()) {
            args.push_back("-x");
            args.push_back(proxy);
        }
        args.push_back("-o");
        args.push_back(tmp);
        args.push_back(url);

        // run_for_output returns nullopt when curl exits non-zero (e.g. -f
        // turned an HTTP error into a failure), so a successful value means the
        // body was written to `tmp`.
        auto result = run_for_output(args);
        if (!result) {
            last_error = "download failed: " + url;
            std::remove(tmp.c_str());
            continue;
        }
        if (!sha1_matches(tmp, sha1)) {
            last_error = "checksum mismatch: " + url;
            std::remove(tmp.c_str());
            continue;
        }
        if (std::rename(tmp.c_str(), dest.c_str()) != 0) {
            last_error = "cannot move downloaded file into place: " + dest;
            std::remove(tmp.c_str());
            continue;
        }
        return true;
    }

    delete_file(tmp);
    if (error) *error = last_error.empty() ? ("no usable download url for " + dest) : last_error;
    return false;
}

} // namespace pl
