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
#include "launcher.h"
#include "manifest.h"
#include "platform.h"
#include "process.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace pl;

namespace {

struct CliOptions {
    std::string game_dir;
    std::string version;

    LaunchOptions launch;

    // auth
    std::string login_method;      // "" | "offline" | "yggdrasil"
    std::string username;
    std::string password;
    std::string uuid;              // explicit offline uuid
    std::string account_id;        // --account: use a stored account
    std::string account_store;     // accounts.json path
    bool save_account = false;
    std::string auth_server = "https://authserver.mojang.com";
    std::string session_server = "https://sessionserver.mojang.com";

    // legacy manual AuthInfo mode
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

    bool show_help = false;
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
        "Process:\n"
        "  --env VAR=VAL         set an environment variable (repeatable)\n"
        "  --wrapper CMD         wrap the java command (e.g. gamemoderun)\n"
        "  --pre-launch-command CMD\n"
        "  --post-exit-command CMD\n"
        "\n"
        "Proxy:\n"
        "  --proxy-host HOST --proxy-port PORT [--proxy-user U --proxy-pass P]\n"
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

CliOptions parse_args(int argc, char** argv) {
    CliOptions o;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc)
                throw std::runtime_error("missing value for " + name);
            return argv[++i];
        };

        if (a == "--help" || a == "-h") {
            o.show_help = true;
        } else if (a == "--game-dir") {
            o.game_dir = next(a);
            o.launch.game_dir = o.game_dir;
        } else if (a == "--version") {
            o.version = next(a);
        } else if (a == "--login") {
            o.login_method = next(a);
        } else if (a == "--username") {
            o.username = next(a);
        } else if (a == "--password") {
            o.password = next(a);
        } else if (a == "--uuid") {
            o.uuid = next(a);
        } else if (a == "--access-token") {
            o.access_token_manual = next(a);
        } else if (a == "--user-type") {
            o.user_type_manual = next(a);
        } else if (a == "--account") {
            o.account_id = next(a);
        } else if (a == "--account-store") {
            o.account_store = next(a);
        } else if (a == "--save-account") {
            o.save_account = true;
        } else if (a == "--auth-server") {
            o.auth_server = next(a);
        } else if (a == "--session-server") {
            o.session_server = next(a);
        } else if (a == "--skin") {
            o.skin_file = next(a);
        } else if (a == "--skin-model") {
            o.skin_model = next(a);
        } else if (a == "--authlib-injector") {
            o.authlib_injector = next(a);
        } else if (a == "--max-mem") {
            o.launch.max_memory = std::stoi(next(a));
        } else if (a == "--min-mem") {
            o.launch.min_memory = std::stoi(next(a));
        } else if (a == "--metaspace") {
            o.launch.metaspace = std::stoi(next(a));
        } else if (a == "--java") {
            o.launch.java_path = next(a);
        } else if (a == "--java-arg") {
            o.launch.java_arguments.push_back(next(a));
        } else if (a == "--override-java-arg") {
            o.launch.override_java_arguments.push_back(next(a));
        } else if (a == "--game-arg") {
            o.launch.game_arguments.push_back(next(a));
        } else if (a == "--priority") {
            std::string p = next(a);
            if (p == "high") o.launch.process_priority = ProcessPriority::High;
            else if (p == "abovenormal") o.launch.process_priority = ProcessPriority::AboveNormal;
            else if (p == "normal") o.launch.process_priority = ProcessPriority::Normal;
            else if (p == "belownormal") o.launch.process_priority = ProcessPriority::BelowNormal;
            else if (p == "low") o.launch.process_priority = ProcessPriority::Low;
            else throw std::runtime_error("invalid priority: " + p);
        } else if (a == "--no-generated-jvm-args") {
            o.launch.no_generated_jvm_args = true;
        } else if (a == "--no-optimizing-jvm-args") {
            o.launch.no_generated_optimizing_jvm_args = true;
        } else if (a == "--width") {
            o.launch.width = std::stoi(next(a));
        } else if (a == "--height") {
            o.launch.height = std::stoi(next(a));
        } else if (a == "--fullscreen") {
            o.launch.fullscreen = true;
        } else if (a == "--server") {
            o.join_server = next(a);
        } else if (a == "--quick-play") {
            o.quick_play = true;
        } else if (a == "--natives-dir") {
            o.launch.natives_dir = next(a);
        } else if (a == "--use-custom-natives") {
            o.launch.use_custom_natives = true;
        } else if (a == "--env") {
            std::string kv = next(a);
            size_t eq = kv.find('=');
            if (eq == std::string::npos)
                throw std::runtime_error("--env expects VAR=VAL");
            o.launch.environment_variables[kv.substr(0, eq)] = kv.substr(eq + 1);
        } else if (a == "--wrapper") {
            o.launch.wrapper = next(a);
        } else if (a == "--pre-launch-command") {
            o.launch.pre_launch_command = next(a);
        } else if (a == "--post-exit-command") {
            o.launch.post_exit_command = next(a);
        } else if (a == "--proxy-host") {
            o.launch.proxy_host = next(a);
        } else if (a == "--proxy-port") {
            o.launch.proxy_port = std::stoi(next(a));
        } else if (a == "--proxy-user") {
            o.launch.proxy_username = next(a);
        } else if (a == "--proxy-pass") {
            o.launch.proxy_password = next(a);
        } else if (a == "--launch-script") {
            o.script_path = next(a);
        } else if (a == "--print-command") {
            o.print_command = true;
        } else if (a == "--debug-log") {
            o.launch.enable_debug_log_output = true;
        } else {
            throw std::runtime_error("unknown option: " + a);
        }
    }

    if (!o.show_help) {
        if (o.game_dir.empty())
            throw std::runtime_error("--game-dir is required");
        if (o.version.empty())
            throw std::runtime_error("--version is required");
        if (!o.login_method.empty() && o.login_method != "offline" && o.login_method != "yggdrasil")
            throw std::runtime_error("--login must be offline or yggdrasil");
    }

    return o;
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

// Resolve the AuthInfo + extra jvm args for this launch.
// `store` may be null when no account management is used.
// `skin_server` out-param is set when a local yggdrasil server must stay alive
// for the whole game session (offline + skin).
AuthResult resolve_auth(CliOptions& o,
                        AccountStore* store,
                        std::shared_ptr<YggdrasilServer>* skin_server) {
    AuthResult result;

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
            if (o.authlib_injector.empty())
                throw std::runtime_error(
                    "--skin requires --authlib-injector PATH (download authlib-injector.jar "
                    "from https://authlib-injector.yushi.moe/)");
            if (!file_exists(o.authlib_injector))
                throw std::runtime_error("authlib-injector jar not found: " + o.authlib_injector);

            auto server = std::make_shared<YggdrasilServer>();
            if (!server->start(0))
                throw std::runtime_error("cannot start the local yggdrasil server "
                                         "(openssl is required)");
            server->add_character(acc->profile_id(), acc->profile_name(), load_skin(o));

            result.extra_jvm_args.push_back(server->authlib_injector_agent(o.authlib_injector));
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
        CliOptions o = parse_args(argc, argv);
        if (o.show_help) {
            print_help(argv[0]);
            return 0;
        }

        std::string manifest_path = join_path(
            join_path(join_path(o.game_dir, "versions"), o.version), o.version + ".json");
        auto json = read_small_file(manifest_path);
        if (!json)
            throw std::runtime_error("cannot read version manifest: " + manifest_path);

        VersionManifest manifest = VersionManifest::parse(*json);

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
