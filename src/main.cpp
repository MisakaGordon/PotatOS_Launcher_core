/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * main.cpp - command line interface
 *
 * Usage examples:
 *   offline: potato-launcher --game-dir ~/.minecraft --version 1.20.4 \
 *              --login offline --username Player --max-mem 2048
 *   yggdrasil: potato-launcher --game-dir ~/.minecraft --version 1.20.4 \
 *              --login yggdrasil --username email@example.com --password secret
 *   stored:    potato-launcher --game-dir ~/.minecraft --version 1.20.4 \
 *              --account account:xxxx --max-mem 2048
 */
#include "auth/accountstore.h"
#include "auth/authserver.h"
#include "auth/offline.h"
#include "auth/yggdrasil.h"
#include "config.h"
#include "download.h"
#include "launcher.h"
#include "manifest.h"
#include "platform.h"
#include "process.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace pl;

namespace {

// Raw command-line parse: JSON overrides for the settings the user typed, plus
// the meta flags that must be handled before the configuration is merged.
struct ParsedArgs {
    nlohmann::json overrides = nlohmann::json::object();
    std::vector<std::string> configs;   // --config FILE, in order
    bool no_config = false;             // --no-config
    bool show_help = false;             // --help
    bool init_config = false;           // --init-config
    std::string init_config_path;       // optional argument of --init-config
};

void print_help(const char* prog) {
    std::cout <<
        "potato-launcher - a minimal Minecraft launcher\n"
        "\n"
        "Usage: " << prog << " --game-dir DIR --version ID [options]\n"
        "\n"
        "Required:\n"
        "  --game-dir DIR       path to the .minecraft directory\n"
        "  --version ID         version id (versions/<id>/<id>.json is read)\n"
        "\n"
        "Login:\n"
        "  --login METHOD       offline | yggdrasil\n"
        "  --username NAME      player / login name\n"
        "  --password PASS      yggdrasil password (required with --login yggdrasil)\n"
        "  --uuid UUID          offline: explicit player uuid (default: derived)\n"
        "  --account ID         use a stored account instead of logging in\n"
        "  --account-store PATH accounts.json (default: <game-dir>/potato-accounts.json)\n"
        "  --save-account       persist the account after login\n"
        "  --auth-server URL    yggdrasil auth base url (default: mojang authserver)\n"
        "  --session-server URL yggdrasil session base url\n"
        "\n"
        "  The legacy flags --username/--uuid/--access-token/--user-type also work\n"
        "  as a manual AuthInfo (no account management).\n"
        "\n"
        "Offline skin (optional):\n"
        "  --skin FILE          png skin to serve (needs authlib-injector.jar)\n"
        "  --skin-model MODEL   wide | slim\n"
        "  --authlib-injector P path to authlib-injector.jar\n"
        "                       (auto-downloaded when omitted)\n"
        "\n"
        "Memory & JVM:\n"
        "  --max-mem MB          -Xmx (e.g. 2048)\n"
        "  --min-mem MB          -Xms\n"
        "  --metaspace MB        -XX:MetaspaceSize\n"
        "  --java PATH           java binary (default: java)\n"
        "  --java-arg ARG        extra JVM argument (repeatable)\n"
        "  --override-java-arg A replace launcher generated argument (repeatable)\n"
        "  --priority LEVEL      high|abovenormal|normal|belownormal|low\n"
        "  --no-generated-jvm-args\n"
        "  --no-optimizing-jvm-args\n"
        "\n"
        "Window:\n"
        "  --width W --height H  custom resolution\n"
        "  --fullscreen\n"
        "\n"
        "Game:\n"
        "  --game-arg ARG        extra game argument (repeatable)\n"
        "  --server HOST[:PORT]  join a server on launch\n"
        "  --quick-play          use the --quickPlayMultiplayer argument (1.20.5+)\n"
        "\n"
        "Natives:\n"
        "  --natives-dir DIR     override the natives directory\n"
        "  --use-custom-natives  skip native library extraction\n"
        "\n"
        "Downloads (missing libraries are fetched before launch):\n"
        "  --no-download         do not auto-complete missing library files\n"
        "  --verify-files        verify SHA-1 of present libraries and refetch\n"
        "  --download-source SRC mirror (default) | mojang\n"
        "  --download-server URL mirror root (default: BMCLAPI)\n"
        "\n"
        "Process:\n"
        "  --env VAR=VAL         set an environment variable (repeatable)\n"
        "  --wrapper CMD         wrap the java command (e.g. gamemoderun)\n"
        "  --pre-launch-command CMD\n"
        "  --post-exit-command CMD\n"
        "\n"
        "Proxy:\n"
        "  --proxy-host HOST --proxy-port PORT [--proxy-user U --proxy-pass P]\n"
        "\n"
        "Config (JSON; command-line flags always win):\n"
        "  --config FILE         load settings from FILE (repeatable, applied in order)\n"
        "  --no-config           ignore the global and per-instance config files\n"
        "  --init-config [PATH]  write a default config and exit\n"
        "                        (default: ~/.config/potato-launcher/config.json)\n"
        "\n"
        "Modes:\n"
        "  --launch-script PATH  write a bash launch script and exit\n"
        "  --print-command       print the generated command line and exit\n"
        "  --help                show this help\n";
}

std::string join_server_host(const std::string& address, std::string* port_out) {
    size_t colon = address.find(':');
    if (colon == std::string::npos) {
        *port_out = "25565";
        return address;
    }
    *port_out = address.substr(colon + 1);
    return address.substr(0, colon);
}

// crude numeric version compare for quick-play detection (1.20.5+)
bool version_at_least(const std::string& id, int min_minor, int min_patch) {
    std::string v = id;
    if (v.rfind("v", 0) == 0) v = v.substr(1);
    size_t d1 = v.find('.');
    if (d1 == std::string::npos) return false;
    int minor = 0;
    try { minor = std::stoi(v.substr(d1 + 1)); } catch (...) { return false; }
    if (minor != min_minor) return minor > min_minor;
    size_t d2 = v.find('.', d1 + 1);
    if (d2 == std::string::npos) return true;
    int patch = 0;
    try { patch = std::stoi(v.substr(d2 + 1)); } catch (...) { return false; }
    return patch >= min_patch;
}

ParsedArgs parse_args(int argc, char** argv) {
    ParsedArgs o;
    nlohmann::json& ov = o.overrides;

    auto set = [&](const char* sec, const char* key, nlohmann::json v) {
        ov[sec][key] = std::move(v);
    };
    auto set_top = [&](const char* key, nlohmann::json v) {
        ov[key] = std::move(v);
    };
    auto push = [&](const char* sec, const char* key, const std::string& v) {
        auto& arr = ov[sec][key];
        if (!arr.is_array())
            arr = nlohmann::json::array();
        arr.push_back(v);
    };

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc)
                throw std::runtime_error("missing value for " + name);
            return argv[++i];
        };
        auto next_int = [&](const std::string& name) -> int {
            std::string v = next(name);
            try {
                return std::stoi(v);
            } catch (...) {
                throw std::runtime_error(name + " expects an integer, got: " + v);
            }
        };

        if (a == "--help" || a == "-h") {
            o.show_help = true;
        } else if (a == "--config") {
            o.configs.push_back(next(a));
        } else if (a == "--no-config") {
            o.no_config = true;
        } else if (a == "--init-config") {
            o.init_config = true;
            if (i + 1 < argc && argv[i + 1][0] != '-')
                o.init_config_path = argv[++i];
        } else if (a == "--game-dir") {
            set_top("gameDir", next(a));
        } else if (a == "--version") {
            set_top("version", next(a));
        } else if (a == "--login") {
            set("auth", "login", next(a));
        } else if (a == "--username") {
            set("auth", "username", next(a));
        } else if (a == "--password") {
            set("auth", "password", next(a));
        } else if (a == "--uuid") {
            set("auth", "uuid", next(a));
        } else if (a == "--access-token") {
            set("auth", "accessToken", next(a));
        } else if (a == "--user-type") {
            set("auth", "userType", next(a));
        } else if (a == "--account") {
            set("auth", "account", next(a));
        } else if (a == "--account-store") {
            set("auth", "accountStore", next(a));
        } else if (a == "--save-account") {
            set("auth", "saveAccount", true);
        } else if (a == "--auth-server") {
            set("auth", "authServer", next(a));
        } else if (a == "--session-server") {
            set("auth", "sessionServer", next(a));
        } else if (a == "--skin") {
            set("skin", "file", next(a));
        } else if (a == "--skin-model") {
            set("skin", "model", next(a));
        } else if (a == "--authlib-injector") {
            set("skin", "authlibInjector", next(a));
        } else if (a == "--max-mem") {
            set("launch", "maxMemory", next_int(a));
        } else if (a == "--min-mem") {
            set("launch", "minMemory", next_int(a));
        } else if (a == "--metaspace") {
            set("launch", "metaspace", next_int(a));
        } else if (a == "--java") {
            set("launch", "java", next(a));
        } else if (a == "--java-arg") {
            push("launch", "javaArgs", next(a));
        } else if (a == "--override-java-arg") {
            push("launch", "overrideJavaArgs", next(a));
        } else if (a == "--game-arg") {
            push("launch", "gameArgs", next(a));
        } else if (a == "--priority") {
            set("launch", "priority", next(a));
        } else if (a == "--no-generated-jvm-args") {
            set("launch", "noGeneratedJvmArgs", true);
        } else if (a == "--no-optimizing-jvm-args") {
            set("launch", "noOptimizingJvmArgs", true);
        } else if (a == "--width") {
            set("launch", "width", next_int(a));
        } else if (a == "--height") {
            set("launch", "height", next_int(a));
        } else if (a == "--fullscreen") {
            set("launch", "fullscreen", true);
        } else if (a == "--server") {
            set("launch", "server", next(a));
        } else if (a == "--quick-play") {
            set("launch", "quickPlay", true);
        } else if (a == "--natives-dir") {
            set("launch", "nativesDir", next(a));
        } else if (a == "--use-custom-natives") {
            set("launch", "useCustomNatives", true);
        } else if (a == "--no-download") {
            set("launch", "noDownload", true);
        } else if (a == "--verify-files") {
            set("launch", "verifyFiles", true);
        } else if (a == "--download-source") {
            set("launch", "downloadSource", next(a));
        } else if (a == "--download-server") {
            set("launch", "downloadServer", next(a));
        } else if (a == "--env") {
            std::string kv = next(a);
            size_t eq = kv.find('=');
            if (eq == std::string::npos)
                throw std::runtime_error("--env expects VAR=VAL");
            ov["launch"]["env"][kv.substr(0, eq)] = kv.substr(eq + 1);
        } else if (a == "--wrapper") {
            set("launch", "wrapper", next(a));
        } else if (a == "--pre-launch-command") {
            set("launch", "preLaunchCommand", next(a));
        } else if (a == "--post-exit-command") {
            set("launch", "postExitCommand", next(a));
        } else if (a == "--proxy-host") {
            set("proxy", "host", next(a));
        } else if (a == "--proxy-port") {
            set("proxy", "port", next_int(a));
        } else if (a == "--proxy-user") {
            set("proxy", "username", next(a));
        } else if (a == "--proxy-pass") {
            set("proxy", "password", next(a));
        } else if (a == "--launch-script") {
            set("mode", "launchScript", next(a));
        } else if (a == "--print-command") {
            set("mode", "printCommand", true);
        } else if (a == "--debug-log") {
            set("launch", "debugLog", true);
        } else {
            throw std::runtime_error("unknown option: " + a);
        }
    }

    return o;
}

// Validate the merged settings and mirror the resolved paths into LaunchOptions.
void finalize(CliOptions& o) {
    if (o.game_dir.empty())
        throw std::runtime_error("--game-dir is required (or set \"gameDir\" in a config file)");
    if (o.version.empty())
        throw std::runtime_error("--version is required (or set \"version\" in a config file)");
    if (!o.login_method.empty() && o.login_method != "offline" && o.login_method != "yggdrasil")
        throw std::runtime_error("--login must be offline or yggdrasil");
    o.launch.game_dir = o.game_dir;
}

// ---------------------------------------------------------------------------
// Auth resolution
// ---------------------------------------------------------------------------

// Read the skin file into memory.
LoadedSkin load_skin(const CliOptions& o) {
    LoadedSkin skin;
    if (o.skin_file.empty()) return skin;
    auto data = read_small_file(o.skin_file);
    if (!data)
        throw std::runtime_error("cannot read skin file: " + o.skin_file);
    skin.png_data = *data;
    skin.slim = (o.skin_model == "slim");
    return skin;
}

// Ensure the authlib-injector jar is available for offline skins, downloading
// it (mirror/official per the download settings) when it is missing.
std::string resolve_authlib_injector(CliOptions& o) {
    std::string injector = o.authlib_injector;
    if (injector.empty())
        injector = join_path(o.game_dir, "authlib-injector.jar");

    if (file_exists(injector)) {
        o.authlib_injector = injector;
        return injector;
    }

    if (o.launch.no_download)
        throw std::runtime_error("authlib-injector not found: " + injector +
                                 " (auto-download disabled by --no-download)");

    std::string proxy;
    if (!o.launch.proxy_host.empty() && o.launch.proxy_port > 0) {
        proxy = o.launch.proxy_host + ":" + std::to_string(o.launch.proxy_port);
        if (!o.launch.proxy_username.empty())
            proxy = o.launch.proxy_username + ":" + o.launch.proxy_password + "@" + proxy;
    }
    std::string root = o.launch.download_mirror.empty()
        ? std::string(kDefaultMirrorRoot)
        : o.launch.download_mirror;

    std::string err;
    if (!download_authlib_injector(injector, o.launch.download_mirror_first, root, proxy, &err))
        throw std::runtime_error("failed to download authlib-injector: " + err);

    o.authlib_injector = injector;
    return injector;
}

// Resolve the AuthInfo + extra jvm args for this launch.
// `store` may be null when no account management is used.
// `skin_server` out-param is set when a local yggdrasil server must stay alive
// for the whole game session (offline + skin).
AuthResult resolve_auth(CliOptions& o,
                        AccountStore* store,
                        std::shared_ptr<YggdrasilServer>* skin_server) {    AuthResult result;

    if (!o.account_id.empty()) {
        if (!store)
            throw std::runtime_error("no account store available");
        auto acc = store->find(o.account_id);
        if (!acc)
            throw std::runtime_error("account not found: " + o.account_id);
        result.info = acc->log_in();  // yggdrasil: validate/refresh; offline: direct
        return result;
    }

    if (o.login_method == "offline") {
        if (!store)
            throw std::runtime_error("--login offline requires an account store");
        std::string uuid = o.uuid.empty() ? offline_uuid_for(o.username) : o.uuid;
        auto acc = store->create_offline(o.username, uuid);
        result.info = acc->log_in();

        if (!o.skin_file.empty()) {
            std::string injector = resolve_authlib_injector(o);

            auto server = std::make_shared<YggdrasilServer>();
            if (!server->start(0))
                throw std::runtime_error("cannot start the local yggdrasil server "
                                         "(openssl is required)");
            server->add_character(acc->profile_id(), acc->profile_name(), load_skin(o));

            result.extra_jvm_args.push_back(server->authlib_injector_agent(injector));
            result.extra_jvm_args.push_back("-Dauthlibinjector.side=client");
            if (skin_server)
                *skin_server = server;
        }

        if (o.save_account)
            store->add(acc);
        return result;
    }

    if (o.login_method == "yggdrasil") {
        if (!store)
            throw std::runtime_error("--login yggdrasil requires an account store");
        if (o.username.empty() || o.password.empty())
            throw std::runtime_error("--login yggdrasil requires --username and --password");
        // yggdrasil login needs the curl binary (or $POTATO_CURL).
        if (resolve_tool(get_env("POTATO_CURL").value_or("curl")).empty())
            throw std::runtime_error(
                "yggdrasil login needs the curl binary. Install curl, or point "
                "POTATO_CURL at it (e.g. export POTATO_CURL=/path/to/curl)");
        YggdrasilProvider provider{o.auth_server, o.session_server};
        auto acc = store->create_yggdrasil(provider, o.username, o.password);
        result.info = acc->log_in();
        if (o.save_account)
            store->add(acc);
        return result;
    }

    // Legacy manual AuthInfo (no account management).
    result.info.username = o.username;
    result.info.uuid = o.uuid;
    result.info.access_token = o.access_token_manual;
    result.info.user_type = o.user_type_manual;
    return result;
}

} // namespace

int main(int argc, char** argv) {
    try {
        ParsedArgs args = parse_args(argc, argv);
        if (args.show_help) {
            print_help(argv[0]);
            return 0;
        }

        if (args.init_config) {
            std::string path = args.init_config_path.empty()
                ? global_config_path()
                : args.init_config_path;
            std::string err;
            if (!write_default_config(path, &err)) {
                std::cerr << "error: " << err << "\n";
                return 1;
            }
            std::cout << "default config written to " << path << "\n";
            return 0;
        }

        CliOptions o;

        // Apply a JSON file on top of the settings accumulated so far. `optional`
        // files (global / per-instance) are skipped when absent; explicit
        // --config files must exist.
        auto apply_file = [&](const std::string& path, bool optional) {
            if (optional && !file_exists(path))
                return;
            nlohmann::json j;
            std::string err;
            if (!load_config_file(path, j, &err))
                throw std::runtime_error(err);
            try {
                apply_config_json(o, j);
            } catch (const std::exception& e) {
                throw std::runtime_error("invalid config " + path + ": " + e.what());
            }
        };

        // 1. global config
        if (!args.no_config) {
            std::string gp = global_config_path();
            if (!gp.empty())
                apply_file(gp, true);
        }

        // 2. per-instance config; its path needs the effective gameDir/version,
        //    which may come from the global config or a command-line flag.
        if (!args.no_config) {
            std::string gd = o.game_dir;
            std::string ver = o.version;
            if (args.overrides.contains("gameDir") && args.overrides["gameDir"].is_string())
                gd = args.overrides["gameDir"].get<std::string>();
            if (args.overrides.contains("version") && args.overrides["version"].is_string())
                ver = args.overrides["version"].get<std::string>();
            std::string ip = instance_config_path(gd, ver);
            if (!ip.empty())
                apply_file(ip, true);
        }

        // 3. explicit --config files, in the given order
        for (const auto& path : args.configs)
            apply_file(path, false);

        // 4. command-line flags win; secrets are only accepted from here
        try {
            apply_config_json(o, args.overrides, /*allow_secrets=*/true);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("invalid option: ") + e.what());
        }

        finalize(o);

        std::string manifest_path = join_path(
            join_path(join_path(o.game_dir, "versions"), o.version), o.version + ".json");
        auto json = read_small_file(manifest_path);
        if (!json)
            throw std::runtime_error("cannot read version manifest: " + manifest_path);

        VersionManifest manifest = VersionManifest::parse(*json);

        // Auto-detect a java matching the manifest requirement when the user
        // did not pick one explicitly (--java). Scans JAVA_HOME / PATH / common
        // install locations, preferring an exact major-version match.
        if (!o.java_explicit) {
            std::string j = find_java(manifest.java_version);
            if (!j.empty())
                o.launch.java_path = j;
            else
                std::cout << "[launcher] warning: no java runtime found; "
                          << "using \"java\" from PATH (expected major version "
                          << manifest.java_version << ")\n";
        }

        if (!o.join_server.empty()) {
            std::string port;
            std::string host = join_server_host(o.join_server, &port);
            if (o.quick_play || version_at_least(manifest.id, 20, 5)) {
                o.launch.game_arguments.push_back("--quickPlayMultiplayer");
                o.launch.game_arguments.push_back(host + ":" + port);
            } else {
                o.launch.game_arguments.push_back("--server");
                o.launch.game_arguments.push_back(host);
                o.launch.game_arguments.push_back("--port");
                o.launch.game_arguments.push_back(port);
            }
        }

        // ---- auth ----
        std::string store_path = o.account_store.empty()
            ? join_path(o.game_dir, "potato-accounts.json")
            : o.account_store;

        AccountStore store;
        store.load(store_path);

        std::shared_ptr<YggdrasilServer> skin_server;
        AuthResult auth;
        bool used_account_store = !o.login_method.empty() || !o.account_id.empty();
        if (used_account_store || o.save_account) {
            auth = resolve_auth(o, &store, &skin_server);
            if (o.save_account)
                store.save(store_path);
        } else {
            auth = resolve_auth(o, nullptr, &skin_server);
        }

        // extra jvm args from the auth method (e.g. -javaagent for skins)
        for (const auto& arg : auth.extra_jvm_args)
            o.launch.java_arguments.push_back(arg);

        DefaultLauncher launcher(manifest, o.launch, auth.info);

        if (o.print_command) {
            std::cout << render_command_line(launcher.generate_command_line()) << "\n";
            return 0;
        }

        if (!o.script_path.empty()) {
            std::string err;
            if (!launcher.make_launch_script(o.script_path, &err)) {
                std::cerr << "error: " << err << "\n";
                return 1;
            }
            std::cout << "launch script written to " << o.script_path << "\n";
            return 0;
        }

        ConsoleProcessListener listener;
        launcher.launch(&listener);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
