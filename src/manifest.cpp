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
            if (e.contains("rules"))
                lib.rules = parse_rules(e.at("rules"));
            if (e.contains("extract")) {
                ExtractRule ex;
                if (e.at("extract").contains("exclude"))
                    for (const auto& x : e.at("extract").at("exclude"))
                        ex.exclude.push_back(x.get<std::string>());
                lib.extract = ex;
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
