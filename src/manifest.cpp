/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * manifest.cpp - version.json parser
 */
#include "manifest.h"
#include "platform.h"

#include <nlohmann/json.hpp>

#include <sstream>
#include <regex>

using json = nlohmann::json;

namespace pl {

static Rule parse_rule(const json& j) {
    Rule r;
    if (j.contains("action")) {
        std::string a = j.at("action").get<std::string>();
        r.action = a == "disallow" ? Rule::Action::Disallow : Rule::Action::Allow;
    }
    if (j.contains("os")) {
        const json& os = j.at("os");
        if (os.contains("name")) r.os_name = os.at("name").get<std::string>();
        if (os.contains("version")) r.os_version = os.at("version").get<std::string>();
        if (os.contains("arch")) r.os_arch = os.at("arch").get<std::string>();
    }
    if (j.contains("features")) {
        const json& f = j.at("features");
        if (!f.empty()) {
            // Features are "name": true in practice; store the first key.
            r.feature = f.begin().key();
        }
    }
    return r;
}

static std::vector<Rule> parse_rules(const json& j) {
    std::vector<Rule> out;
    if (j.is_array()) {
        for (const auto& e : j)
            out.push_back(parse_rule(e));
    }
    return out;
}

Library Library::parse(const std::string& name) {
    Library lib;
    // group:artifact:version[:classifier]
    std::vector<std::string> parts;
    std::stringstream ss(name);
    std::string tok;
    while (std::getline(ss, tok, ':'))
        parts.push_back(tok);
    if (parts.size() >= 3) {
        lib.group = parts[0];
        lib.name = parts[1];
        lib.version = parts[2];
    }
    if (parts.size() >= 4)
        lib.classifier = parts[3];
    return lib;
}

std::string Library::file_name() const {
    std::string f = name + "-" + version;
    if (!classifier.empty())
        f += "-" + classifier;
    return f + ".jar";
}

std::string Library::relative_path() const {
    std::string dir = group;
    for (auto& c : dir)
        if (c == '.') c = '/';
    return dir + "/" + name + "/" + version + "/" + file_name();
}

std::optional<DownloadInfo> Library::raw_download() const {
    if (native) {
        auto it = classifiers.find(classifier);
        if (it != classifiers.end())
            return it->second;
        return std::nullopt;
    }
    return artifact;
}

std::string Library::download_path() const {
    auto info = raw_download();
    if (info && !info->path.empty())
        return info->path;
    return relative_path();
}

std::string Library::download_url() const {
    auto info = raw_download();
    if (info && !info->url.empty())
        return info->url;
    std::string base = url.empty() ? "https://libraries.minecraft.net/" : url;
    if (!base.empty() && base.back() != '/')
        base += '/';
    return base + download_path();
}

std::string Library::download_sha1() const {
    auto info = raw_download();
    if (info && !info->sha1.empty())
        return info->sha1;
    if (!checksums.empty())
        return checksums.front();
    return {};
}

static bool match_os_arch(const std::string& pattern, const std::string& arch) {
    if (pattern == "x86" && (arch == "x86" || arch == "i386")) return true;
    if (pattern == "x86_64" && (arch == "x86_64" || arch == "amd64")) return true;
    return pattern == arch;
}

bool check_rules(const std::vector<Rule>& rules, const std::map<std::string, bool>& features) {
    if (rules.empty()) return true;

    std::string os = os_name();
    std::string arch = os_arch();
    std::optional<std::string> os_version = get_env("OS_VERSION");

    bool allowed = false;
    for (const Rule& r : rules) {
        bool match = true;
        if (r.os_name && *r.os_name != os) match = false;
        if (match && r.os_arch && !match_os_arch(*r.os_arch, arch)) match = false;
        if (match && r.os_version) {
            // The "version" rule value is a regex against the OS version; we only
            // have a sensible value on Windows, so on other platforms just skip
            // these rules (they never match, matching the reference launcher).
            match = false;
        }
        if (match && !r.feature.empty())
            match = has_feature(features, r.feature, true);

        if (match)
            allowed = (r.action == Rule::Action::Allow);
    }
    return allowed;
}

bool has_feature(const std::map<std::string, bool>& features, const std::string& name, bool value) {
    auto it = features.find(name);
    return it != features.end() && it->second == value;
}

bool Library::applies(const std::map<std::string, bool>& features) const {
    if (native) {
        // A native library: only applies when its platform classifier exists.
        auto it = natives.find(native_platform());
        if (it == natives.end())
            return false;
    }
    return check_rules(rules, features);
}

static std::vector<std::string> parse_value(const json& v) {
    std::vector<std::string> out;
    if (v.is_string()) {
        out.push_back(v.get<std::string>());
    } else if (v.is_array()) {
        for (const auto& e : v)
            if (e.is_string())
                out.push_back(e.get<std::string>());
    }
    return out;
}

// ---------------------------------------------------------------------------
// HMCL NativePatcher parity.
//
// Mojang's version.json only ships x86-64 Linux natives for LWJGL, so on an
// aarch64 host the game starts with an x86-64 natives jar on the classpath and
// LWJGL bails out with
//     [LWJGL] Platform/architecture mismatch detected for module: org.lwjgl
//     Platform available on classpath: linux/x64
//     java.lang.UnsatisfiedLinkError: Failed to locate library: liblwjgl.so
// HMCL fixes this in NativePatcher.patchNative, logging
//     Replace org.lwjgl:lwjgl-opengl:3.4.1:natives-linux with
//             org.lwjgl:lwjgl-opengl:3.4.1:natives-linux-arm64
// We rewrite the exact same set (the org.lwjgl natives) here. The artifact URL
// stays on libraries.minecraft.net because BMCLAPI's /libraries route mirrors it
// to the real arm64 jar (Mojang's own host answers 404 for the arm64 artifact);
// the x86-64 sha-1 is dropped since it does not describe the arm64 file.
// ---------------------------------------------------------------------------
static void patch_org_lwjgl_natives(Library& lib) {
    const std::string base = os_name();          // e.g. "linux"
    const std::string plat = native_platform();  // e.g. "linux-arm64"
    if (plat != base + "-arm64") return;         // only swap x86-64 -> arm64
    if (lib.group != "org.lwjgl") return;        // same library set as HMCL
    if (lib.classifier != "natives-" + base) return;

    const std::string from = "-natives-" + base + ".jar";
    const std::string to = "-natives-" + plat + ".jar";
    lib.classifier = "natives-" + plat;

    auto rewrite = [&](DownloadInfo& info) {
        for (std::string* s : {&info.path, &info.url}) {
            size_t pos = s->find(from);
            if (pos != std::string::npos)
                s->replace(pos, from.size(), to);
        }
        info.sha1.clear(); // unknown for the arm64 artifact
        info.size = 0;
    };
    if (lib.artifact) rewrite(*lib.artifact);
    for (auto& kv : lib.classifiers) rewrite(kv.second);
}

VersionManifest VersionManifest::parse(const std::string& json_str) {
    VersionManifest m;
    json root = json::parse(json_str);

    if (root.contains("id")) m.id = root.at("id").get<std::string>();
    if (root.contains("type")) m.type = root.at("type").get<std::string>();
    if (root.contains("mainClass")) m.main_class = root.at("mainClass").get<std::string>();
    if (root.contains("minecraftArguments"))
        m.minecraft_arguments = root.at("minecraftArguments").get<std::string>();

    if (root.contains("assetIndex")) {
        const json& ai = root.at("assetIndex");
        if (ai.contains("id")) m.asset_index_id = ai.at("id").get<std::string>();
        if (ai.contains("url")) m.asset_index_url = ai.at("url").get<std::string>();
    }
    if (root.contains("javaVersion") && root.at("javaVersion").contains("majorVersion"))
        m.java_version = root.at("javaVersion").at("majorVersion").get<int>();

    if (root.contains("libraries")) {
        for (const auto& e : root.at("libraries")) {
            if (!e.contains("name")) continue;
            Library lib = Library::parse(e.at("name").get<std::string>());
            if (e.contains("url") && e.at("url").is_string())
                lib.url = e.at("url").get<std::string>();
            if (e.contains("rules"))
                lib.rules = parse_rules(e.at("rules"));
            if (e.contains("checksums") && e.at("checksums").is_array())
                for (const auto& c : e.at("checksums"))
                    if (c.is_string())
                        lib.checksums.push_back(c.get<std::string>());
            if (e.contains("extract")) {
                ExtractRule ex;
                if (e.at("extract").contains("exclude"))
                    for (const auto& x : e.at("extract").at("exclude"))
                        ex.exclude.push_back(x.get<std::string>());
                lib.extract = ex;
            }
            if (e.contains("downloads") && e.at("downloads").is_object()) {
                const json& dl = e.at("downloads");
                auto parse_info = [](const json& j) {
                    DownloadInfo info;
                    if (j.contains("path") && j.at("path").is_string())
                        info.path = j.at("path").get<std::string>();
                    if (j.contains("url") && j.at("url").is_string())
                        info.url = j.at("url").get<std::string>();
                    if (j.contains("sha1") && j.at("sha1").is_string())
                        info.sha1 = j.at("sha1").get<std::string>();
                    if (j.contains("size") && j.at("size").is_number())
                        info.size = j.at("size").get<long long>();
                    return info;
                };
                if (dl.contains("artifact") && dl.at("artifact").is_object())
                    lib.artifact = parse_info(dl.at("artifact"));
                if (dl.contains("classifiers") && dl.at("classifiers").is_object())
                    for (auto it = dl.at("classifiers").begin(); it != dl.at("classifiers").end(); ++it)
                        if (it.value().is_object())
                            lib.classifiers[it.key()] = parse_info(it.value());
            }
            if (e.contains("natives")) {
                for (auto it = e.at("natives").begin(); it != e.at("natives").end(); ++it)
                    lib.natives[it.key()] = it.value().get<std::string>();
                lib.native = true;
                // The actual artifact classifier comes from the natives map,
                // e.g. org.lwjgl:lwjgl:3.3.3 -> lwjgl-3.3.3-natives-linux.jar
                auto n = lib.natives.find(native_platform());
                if (n != lib.natives.end())
                    lib.classifier = n->second;
            }
            patch_org_lwjgl_natives(lib);
            m.libraries.push_back(std::move(lib));
        }
    }

    if (root.contains("arguments")) {
        const json& args = root.at("arguments");
        if (args.contains("jvm")) {
            for (const auto& a : args.at("jvm")) {
                Argument arg;
                if (a.is_string()) {
                    arg.values.push_back(a.get<std::string>());
                } else if (a.is_object()) {
                    arg.has_rules = true;
                    if (a.contains("rules"))
                        arg.rules = parse_rules(a.at("rules"));
                    arg.values = parse_value(a.value("value", json()));
                }
                m.jvm_arguments.push_back(std::move(arg));
            }
        }
        if (args.contains("game")) {
            for (const auto& a : args.at("game")) {
                Argument arg;
                if (a.is_string()) {
                    arg.values.push_back(a.get<std::string>());
                } else if (a.is_object()) {
                    arg.has_rules = true;
                    if (a.contains("rules"))
                        arg.rules = parse_rules(a.at("rules"));
                    arg.values = parse_value(a.value("value", json()));
                }
                m.game_arguments.push_back(std::move(arg));
            }
        }
    }

    return m;
}

std::vector<std::string> VersionManifest::resolve_jvm_arguments(const std::map<std::string, bool>& features) const {
    std::vector<std::string> out;
    for (const Argument& a : jvm_arguments) {
        if (!a.has_rules || check_rules(a.rules, features))
            out.insert(out.end(), a.values.begin(), a.values.end());
    }
    return out;
}

std::vector<std::string> VersionManifest::resolve_game_arguments(const std::map<std::string, bool>& features) const {
    std::vector<std::string> out;
    for (const Argument& a : game_arguments) {
        if (!a.has_rules || check_rules(a.rules, features))
            out.insert(out.end(), a.values.begin(), a.values.end());
    }
    return out;
}

} // namespace pl
