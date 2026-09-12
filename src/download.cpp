/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * download.cpp
 *
 * Resolves mirror URLs and fetches files with the system `curl` binary.
 * Mirrors HMCL's BMCLAPIDownloadProvider.injectURL and FileDownloadTask.
 */
#include "download.h"
#include "auth/crypto.h"
#include "auth/http.h"
#include "platform.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <iostream>
#include <utility>

namespace pl {

const char* const kDefaultMirrorRoot = "https://bmclapi2.bangbang93.com";
const char* const kAuthlibInjectorLatest = "https://authlib-injector.yushi.moe/artifact/latest.json";

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
        {"https://authlib-injector.yushi.moe", "/mirrors/authlib-injector"},
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

static bool hex_equal_ci(const std::string& a, const std::string& b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y)
            return false;
    }
    return true;
}

static bool hash_matches(const std::string& path, const std::string& hex, HashKind kind) {
    if (hex.empty())
        return true;
    auto data = read_small_file(path);
    if (!data)
        return false;
    std::string raw = kind == HashKind::Sha256 ? sha256_raw(*data) : sha1_raw(*data);
    std::string actual = bytes_to_hex(reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
    return hex_equal_ci(actual, hex);
}

bool sha1_matches(const std::string& path, const std::string& hex) {
    return hash_matches(path, hex, HashKind::Sha1);
}

bool sha256_matches(const std::string& path, const std::string& hex) {
    return hash_matches(path, hex, HashKind::Sha256);
}

bool download_file(const std::vector<std::string>& urls,
                   const std::string& dest,
                   const std::string& hash,
                   const std::string& proxy,
                   std::string* error,
                   HashKind kind) {
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
        if (!hash_matches(tmp, hash, kind)) {
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

bool download_authlib_injector(const std::string& dest,
                               bool mirror_first,
                               const std::string& mirror_root,
                               const std::string& proxy,
                               std::string* error) {
    std::string official = kAuthlibInjectorLatest;
    std::string mirrored = mirror_url(official, mirror_root);

    std::vector<std::string> meta_urls;
    if (mirror_first) {
        if (mirrored != official) meta_urls.push_back(mirrored);
        meta_urls.push_back(official);
    } else {
        meta_urls.push_back(official);
        if (mirrored != official) meta_urls.push_back(mirrored);
    }

    std::string download_url;
    std::string sha256;
    std::string version;
    std::string last_error;
    for (const std::string& url : meta_urls) {
        HttpResponse resp = http_request(HttpMethod::Get, url);
        if (!resp.ok || resp.status < 200 || resp.status >= 300) {
            last_error = "cannot fetch authlib-injector metadata: " + url;
            continue;
        }
        nlohmann::json meta = nlohmann::json::parse(resp.body, nullptr, false);
        if (meta.is_discarded() || !meta.is_object()) {
            last_error = "invalid authlib-injector metadata from " + url;
            continue;
        }
        download_url = meta.value("download_url", std::string());
        version = meta.value("version", std::string());
        if (meta.contains("checksums") && meta.at("checksums").is_object())
            sha256 = meta.at("checksums").value("sha256", std::string());
        if (!download_url.empty())
            break;
    }

    if (download_url.empty()) {
        if (error) *error = last_error.empty() ? "no authlib-injector download url" : last_error;
        return false;
    }

    std::string mirrored_dl = mirror_url(download_url, mirror_root);
    std::vector<std::string> urls;
    if (mirror_first) {
        if (mirrored_dl != download_url) urls.push_back(mirrored_dl);
        urls.push_back(download_url);
    } else {
        urls.push_back(download_url);
        if (mirrored_dl != download_url) urls.push_back(mirrored_dl);
    }

    std::cerr << "[download] authlib-injector"
              << (version.empty() ? "" : " " + version)
              << " -> " << dest << "\n";
    return download_file(urls, dest, sha256, proxy, error, HashKind::Sha256);
}

} // namespace pl
