/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * config.cpp - layered JSON configuration
 */
#include "config.h"
#include "platform.h"

#include <stdexcept>

using json = nlohmann::json;

namespace pl {

namespace {

const json& section(const json& j, const char* name) {
    static const json empty = json::object();
    if (j.is_object() && j.contains(name) && j.at(name).is_object())
        return j.at(name);
    return empty;
}

std::string priority_to_string(ProcessPriority p) {
    switch (p) {
        case ProcessPriority::High: return "high";
        case ProcessPriority::AboveNormal: return "abovenormal";
        case ProcessPriority::Normal: return "normal";
        case ProcessPriority::BelowNormal: return "belownormal";
        case ProcessPriority::Low: return "low";
    }
    return "normal";
}

ProcessPriority priority_from_string(const std::string& p) {
    if (p == "high") return ProcessPriority::High;
    if (p == "abovenormal") return ProcessPriority::AboveNormal;
    if (p == "normal") return ProcessPriority::Normal;
    if (p == "belownormal") return ProcessPriority::BelowNormal;
    if (p == "low") return ProcessPriority::Low;
    throw std::runtime_error("invalid priority: " + p);
}

void require_string(const json& j, const char* key, std::string& out) {
    if (j.contains(key))
        out = j.at(key).get<std::string>();
}

void require_int(const json& j, const char* key, std::optional<int>& out) {
    if (j.contains(key))
        out = j.at(key).get<int>();
}

void require_int(const json& j, const char* key, int& out) {
    if (j.contains(key))
        out = j.at(key).get<int>();
}

void require_bool(const json& j, const char* key, bool& out) {
    if (j.contains(key))
        out = j.at(key).get<bool>();
}

void require_strings(const json& j, const char* key, std::vector<std::string>& out) {
    if (j.contains(key))
        out = j.at(key).get<std::vector<std::string>>();
}

} // namespace

std::string global_config_path() {
    if (auto xdg = get_env("XDG_CONFIG_HOME"); xdg && !xdg->empty())
        return join_path(join_path(*xdg, "potato-launcher"), "config.json");
    if (auto home = get_env("HOME"); home && !home->empty())
        return join_path(join_path(join_path(*home, ".config"), "potato-launcher"), "config.json");
    return {};
}

std::string instance_config_path(const std::string& game_dir, const std::string& version) {
    if (game_dir.empty() || version.empty())
        return {};
    return join_path(join_path(join_path(game_dir, "versions"), version), "potato.json");
}

void apply_config_json(CliOptions& o, const json& j, bool allow_secrets) {
    if (j.is_null())
        return;
    if (!j.is_object())
        throw std::runtime_error("config root must be a JSON object");

    require_string(j, "gameDir", o.game_dir);
    require_string(j, "version", o.version);

    const json& launch = section(j, "launch");
    if (launch.contains("java")) {
        o.launch.java_path = launch.at("java").get<std::string>();
        o.java_explicit = true;
    }
    require_int(launch, "maxMemory", o.launch.max_memory);
    require_int(launch, "minMemory", o.launch.min_memory);
    require_int(launch, "metaspace", o.launch.metaspace);
    require_strings(launch, "javaArgs", o.launch.java_arguments);
    require_strings(launch, "overrideJavaArgs", o.launch.override_java_arguments);
    require_strings(launch, "gameArgs", o.launch.game_arguments);
    require_string(launch, "wrapper", o.launch.wrapper);
    require_string(launch, "versionName", o.launch.version_name);
    require_string(launch, "profileName", o.launch.profile_name);
    require_string(launch, "versionType", o.launch.version_type);
    require_int(launch, "width", o.launch.width);
    require_int(launch, "height", o.launch.height);
    require_bool(launch, "fullscreen", o.launch.fullscreen);
    require_string(launch, "nativesDir", o.launch.natives_dir);
    require_bool(launch, "useCustomNatives", o.launch.use_custom_natives);
    require_bool(launch, "noDownload", o.launch.no_download);
    require_bool(launch, "verifyFiles", o.launch.verify_files);
    require_string(launch, "downloadServer", o.launch.download_mirror);
    require_bool(launch, "noGeneratedJvmArgs", o.launch.no_generated_jvm_args);
    require_bool(launch, "noOptimizingJvmArgs", o.launch.no_generated_optimizing_jvm_args);
    require_string(launch, "preLaunchCommand", o.launch.pre_launch_command);
    require_string(launch, "postExitCommand", o.launch.post_exit_command);
    require_bool(launch, "debugLog", o.launch.enable_debug_log_output);
    require_string(launch, "server", o.join_server);
    require_bool(launch, "quickPlay", o.quick_play);

    if (launch.contains("priority"))
        o.launch.process_priority = priority_from_string(launch.at("priority").get<std::string>());

    if (launch.contains("downloadSource")) {
        std::string s = launch.at("downloadSource").get<std::string>();
        if (s == "mirror") o.launch.download_mirror_first = true;
        else if (s == "mojang") o.launch.download_mirror_first = false;
        else throw std::runtime_error("invalid downloadSource: " + s + " (expected mirror or mojang)");
    }

    if (launch.contains("env") && launch.at("env").is_object())
        for (auto it = launch.at("env").begin(); it != launch.at("env").end(); ++it)
            o.launch.environment_variables[it.key()] = it.value().get<std::string>();

    const json& auth = section(j, "auth");
    require_string(auth, "login", o.login_method);
    require_string(auth, "username", o.username);
    require_string(auth, "uuid", o.uuid);
    require_string(auth, "account", o.account_id);
    require_string(auth, "accountStore", o.account_store);
    require_bool(auth, "saveAccount", o.save_account);
    require_string(auth, "authServer", o.auth_server);
    require_string(auth, "sessionServer", o.session_server);
    if (allow_secrets) {
        require_string(auth, "password", o.password);
        require_string(auth, "accessToken", o.access_token_manual);
        require_string(auth, "userType", o.user_type_manual);
    }

    const json& skin = section(j, "skin");
    require_string(skin, "file", o.skin_file);
    require_string(skin, "authlibInjector", o.authlib_injector);
    if (skin.contains("model")) {
        std::string m = skin.at("model").get<std::string>();
        if (m != "wide" && m != "slim")
            throw std::runtime_error("invalid skin model: " + m + " (expected wide or slim)");
        o.skin_model = m;
    }

    const json& proxy = section(j, "proxy");
    require_string(proxy, "host", o.launch.proxy_host);
    require_int(proxy, "port", o.launch.proxy_port);
    require_string(proxy, "username", o.launch.proxy_username);
    if (allow_secrets)
        require_string(proxy, "password", o.launch.proxy_password);

    const json& mode = section(j, "mode");
    require_string(mode, "launchScript", o.script_path);
    require_bool(mode, "printCommand", o.print_command);
}

json config_to_json(const CliOptions& o) {
    json j = json::object();
    j["gameDir"] = o.game_dir;
    j["version"] = o.version;

    json launch = json::object();
    launch["java"] = o.launch.java_path;
    launch["maxMemory"] = o.launch.max_memory.value_or(0);
    launch["minMemory"] = o.launch.min_memory.value_or(0);
    launch["metaspace"] = o.launch.metaspace.value_or(0);
    launch["javaArgs"] = o.launch.java_arguments;
    launch["overrideJavaArgs"] = o.launch.override_java_arguments;
    launch["gameArgs"] = o.launch.game_arguments;
    launch["wrapper"] = o.launch.wrapper;
    launch["versionName"] = o.launch.version_name;
    launch["profileName"] = o.launch.profile_name;
    launch["versionType"] = o.launch.version_type;
    launch["priority"] = priority_to_string(o.launch.process_priority);
    launch["noGeneratedJvmArgs"] = o.launch.no_generated_jvm_args;
    launch["noOptimizingJvmArgs"] = o.launch.no_generated_optimizing_jvm_args;
    launch["width"] = o.launch.width.value_or(0);
    launch["height"] = o.launch.height.value_or(0);
    launch["fullscreen"] = o.launch.fullscreen;
    launch["nativesDir"] = o.launch.natives_dir;
    launch["useCustomNatives"] = o.launch.use_custom_natives;
    launch["noDownload"] = o.launch.no_download;
    launch["verifyFiles"] = o.launch.verify_files;
    launch["downloadSource"] = o.launch.download_mirror_first ? "mirror" : "mojang";
    launch["downloadServer"] = o.launch.download_mirror;
    launch["env"] = o.launch.environment_variables;
    launch["preLaunchCommand"] = o.launch.pre_launch_command;
    launch["postExitCommand"] = o.launch.post_exit_command;
    launch["server"] = o.join_server;
    launch["quickPlay"] = o.quick_play;
    launch["debugLog"] = o.launch.enable_debug_log_output;
    j["launch"] = std::move(launch);

    json auth = json::object();
    auth["login"] = o.login_method;
    auth["username"] = o.username;
    auth["uuid"] = o.uuid;
    auth["account"] = o.account_id;
    auth["accountStore"] = o.account_store;
    auth["saveAccount"] = o.save_account;
    auth["authServer"] = o.auth_server;
    auth["sessionServer"] = o.session_server;
    j["auth"] = std::move(auth);

    json skin = json::object();
    skin["file"] = o.skin_file;
    skin["model"] = o.skin_model;
    skin["authlibInjector"] = o.authlib_injector;
    j["skin"] = std::move(skin);

    json proxy = json::object();
    proxy["host"] = o.launch.proxy_host;
    proxy["port"] = o.launch.proxy_port;
    proxy["username"] = o.launch.proxy_username;
    j["proxy"] = std::move(proxy);

    json mode = json::object();
    mode["launchScript"] = o.script_path;
    mode["printCommand"] = o.print_command;
    j["mode"] = std::move(mode);

    return j;
}

bool load_config_file(const std::string& path, json& out, std::string* error) {
    auto text = read_small_file(path);
    if (!text) {
        if (error) *error = "cannot read config file: " + path;
        return false;
    }
    json parsed = json::parse(*text, nullptr, false);
    if (parsed.is_discarded()) {
        if (error) *error = "invalid JSON in config file: " + path;
        return false;
    }
    out = std::move(parsed);
    return true;
}

bool write_default_config(const std::string& path, std::string* error) {
    if (path.empty()) {
        if (error) *error = "no config path (HOME is not set)";
        return false;
    }
    if (file_exists(path)) {
        if (error) *error = "config already exists: " + path;
        return false;
    }
    std::string text = config_to_json(CliOptions{}).dump(2);
    text += "\n";
    if (!write_file(path, text)) {
        if (error) *error = "cannot write config: " + path;
        return false;
    }
    return true;
}

} // namespace pl
