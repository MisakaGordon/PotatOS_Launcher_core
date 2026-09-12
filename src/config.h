/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * config.h - layered JSON configuration
 *
 * Settings can be given on the command line or through JSON files. Layers are
 * applied low to high: built-in defaults < global config < per-instance config
 * < explicit --config files < command-line flags. Secrets (passwords, access
 * tokens) are intentionally not read from JSON.
 */
#pragma once

#include "launcher.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace pl {

// All launcher settings resolved from the command line and/or JSON files.
struct CliOptions {
    std::string game_dir;
    std::string version;

    LaunchOptions launch;

    // auth
    std::string login_method;      // "" | "offline" | "yggdrasil"
    std::string username;
    std::string password;          // CLI only, never read from JSON
    std::string uuid;              // explicit offline uuid
    std::string account_id;        // --account: use a stored account
    std::string account_store;     // accounts.json path
    bool save_account = false;
    std::string auth_server = "https://authserver.mojang.com";
    std::string session_server = "https://sessionserver.mojang.com";

    // legacy manual AuthInfo mode (CLI only)
    std::string access_token_manual;
    std::string user_type_manual = "mojang";

    // offline skin (needs authlib-injector)
    std::string skin_file;
    std::string skin_model = "wide";
    std::string authlib_injector;

    std::string script_path;       // --launch-script
    bool print_command = false;    // --print-command
    std::string join_server;       // --server
    bool quick_play = false;

    bool java_explicit = false;    // java was set explicitly (CLI or JSON)
};

// Resolve the global config path: $XDG_CONFIG_HOME/potato-launcher/config.json,
// else $HOME/.config/potato-launcher/config.json. Empty when no home is known.
std::string global_config_path();

// Path of the per-instance config: <game-dir>/versions/<id>/potato.json.
std::string instance_config_path(const std::string& game_dir, const std::string& version);

// Overwrite `o` with every key present in `j`. Throws std::runtime_error on
// malformed values. Secrets (password/accessToken/userType/proxy password) are
// only applied when `allow_secrets` is set (i.e. for command-line overrides).
void apply_config_json(CliOptions& o, const nlohmann::json& j, bool allow_secrets = false);

// Serialize the non-secret settings of `o` (used by --init-config).
nlohmann::json config_to_json(const CliOptions& o);

// Read and parse a config file. Returns false and fills `error` on failure.
bool load_config_file(const std::string& path, nlohmann::json& out, std::string* error);

// Write a default config to `path`, refusing to overwrite an existing file.
bool write_default_config(const std::string& path, std::string* error);

} // namespace pl
