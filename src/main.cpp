/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * main.cpp - command line interface
 *
 * Usage example:
 *   potato-launcher --game-dir ~/.minecraft --version 1.20.4 \
 *       --username Player --uuid 00000000-0000-0000-0000-000000000000 \
 *       --access-token 00000000000000000000000000000000 --max-mem 2048
 */
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
    std::string manifest_path;

    LaunchOptions launch;
    AuthInfo auth;

    std::string script_path;      // --launch-script
    bool print_command = false;   // --print-command
    bool launch_and_wait = true;
    std::string join_server;      // --server
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
        "Account:\n"
        "  --username NAME       in-game player name\n"
        "  --uuid UUID           player uuid (with or without dashes)\n"
        "  --access-token TOKEN  auth access token (offline: any non-empty value)\n"
        "  --user-type TYPE      mojang | offline | msa (default mojang)\n"
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
    if (d2 == std::string::npos) return true; // "1.20" >= "1.20.5"? treat as patch 0 -> false below
    int patch = 0;
    try { patch = std::stoi(v.substr(d2 + 1)); } catch (...) { return false; }
    return patch >= min_patch;
}

CliOptions parse_args(int argc, char** argv) {
    CliOptions o;
    std::vector<std::string> server_port_args;

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
        } else if (a == "--username") {
            o.auth.username = next(a);
        } else if (a == "--uuid") {
            o.auth.uuid = next(a);
        } else if (a == "--access-token") {
            o.auth.access_token = next(a);
        } else if (a == "--user-type") {
            o.auth.user_type = next(a);
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
            o.launch_and_wait = false;
        } else if (a == "--print-command") {
            o.print_command = true;
            o.launch_and_wait = false;
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
    }

    return o;
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

        DefaultLauncher launcher(manifest, o.launch, o.auth);

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
