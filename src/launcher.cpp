/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * launcher.cpp
 *
 * Mirrors HMCL's DefaultLauncher: command line generation, native extraction,
 * log4j configuration, process launch and launch-script generation.
 */
#include "launcher.h"
#include "platform.h"
#include "zip.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

#if !defined(_WIN32)
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace pl {

// ---------------------------------------------------------------------------
// Embedded log4j2 configuration files (HMCL ships these next to the manifest).
// ---------------------------------------------------------------------------
static const char* LOG4J2_1_7_XML = R"(<?xml version="1.0" encoding="UTF-8"?>
<Configuration status="WARN" packages="com.mojang.util.Log4jLevelPatch">
    <Appenders>
        <Console name="SysOut" target="SYSTEM_OUT">
            <PatternLayout pattern="[%d{HH:mm:ss}] [%t/%level]: %msg%n" />
        </Console>
        <RollingRandomAccessFile name="File" fileName="logs/latest.log" filePattern="logs/%d{yyyy-MM-dd}-%i.log.gz">
            <PatternLayout pattern="[%d{HH:mm:ss}] [%t/%level]: %msg%n" />
            <Policies>
                <TimeBasedTriggeringPolicy />
                <OnStartupTriggeringPolicy />
            </Policies>
        </RollingRandomAccessFile>
    </Appenders>
    <Loggers>
        <Root level="info">
            <filters>
                <MarkerFilter marker="NETWORK_PACKETS" onMatch="DENY" onMismatch="NEUTRAL" />
            </filters>
            <AppenderRef ref="SysOut" />
            <AppenderRef ref="File" />
        </Root>
    </Loggers>
</Configuration>
)";

static const char* LOG4J2_1_12_XML = R"(<?xml version="1.0" encoding="UTF-8"?>
<Configuration status="WARN" packages="com.mojang.util">
    <Appenders>
        <Console name="SysOut" target="SYSTEM_OUT">
            <PatternLayout pattern="[%d{HH:mm:ss}] [%t/%level]: %msg%n" />
        </Console>
        <RollingRandomAccessFile name="File" fileName="logs/latest.log" filePattern="logs/%d{yyyy-MM-dd}-%i.log.gz">
            <PatternLayout pattern="[%d{HH:mm:ss}] [%t/%level]: %msg%n" />
            <Policies>
                <TimeBasedTriggeringPolicy />
                <OnStartupTriggeringPolicy />
            </Policies>
        </RollingRandomAccessFile>
    </Appenders>
    <Loggers>
        <Root level="info">
            <filters>
                <MarkerFilter marker="NETWORK_PACKETS" onMatch="DENY" onMismatch="NEUTRAL" />
            </filters>
            <AppenderRef ref="SysOut" />
            <AppenderRef ref="File" />
        </Root>
        <Logger name="com.mojang.authlib" level="error" />
        <Logger name="net.minecraft.launchwrapper" level="error" />
    </Loggers>
</Configuration>
)";

static std::string instance_dir(const std::string& game_dir, const std::string& id) {
    return join_path(join_path(game_dir, "versions"), id);
}

static std::string main_jar_path(const std::string& game_dir, const std::string& id) {
    return join_path(instance_dir(game_dir, id), id + ".jar");
}

static std::string libraries_dir(const std::string& game_dir) {
    return join_path(game_dir, "libraries");
}

static std::string assets_dir(const std::string& game_dir) {
    return join_path(game_dir, "assets");
}

// Numeric major.minor compare of a version id like "1.20.4", "v1.7.10" or
// "26.2-snapshot-2". Non-numeric ids (e.g. "24w14a") compare as (0, 0).
static bool version_ge(const std::string& id, int req_major, int req_minor) {
    std::string v = id;
    if (v.rfind("v", 0) == 0) v = v.substr(1);
    size_t d1 = v.find('.');
    int major = 0, minor = 0;
    try {
        major = d1 == std::string::npos ? std::stoi(v) : std::stoi(v.substr(0, d1));
    } catch (...) {
        return false;
    }
    if (d1 != std::string::npos) {
        std::string rest = v.substr(d1 + 1);
        std::string minor_part = rest.substr(0, rest.find_first_of(".-"));
        try { minor = std::stoi(minor_part); } catch (...) { minor = 0; }
    }
    if (major != req_major) return major > req_major;
    return minor >= req_minor;
}

// Render argv as a bash command line, single-quoting each token so paths with
// spaces survive (render_command_line is only for display).
static std::string quoted_command_line(const std::vector<std::string>& args) {
    std::string out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) out += ' ';
        out += shell_quote(args[i]);
    }
    return out;
}

// The main class used by Forge / NeoForge 1.17+.
static const char kBootstraplauncherMain[] = "cpw.mods.bootstraplauncher.BootstrapLauncher";

// Split on ',' (the -DignoreList value format).
static std::vector<std::string> split_comma(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t comma = s.find(',', start);
        if (comma == std::string::npos) {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, comma - start));
        start = comma + 1;
    }
    return out;
}

// Forge / NeoForge 1.17+ launch via BootstrapLauncher refuses to start when a
// classpath jar shadows a module that is already loaded from the JVM module path
// (e.g. the ASM copy a game manifest ships alongside the module-path copy the
// bootstrapper itself needs). Jars whose file name starts with one of the
// -DignoreList tokens are exempt from that check. The property's built-in
// defaults ("asm","securejarhandler") are lost whenever the manifest overrides
// it, so merge them back in, and make sure the primary jar is listed too so that
// it is kept out of the legacy-classpath scan and handled as the game module
// (mirrors HMCL's LaunchManifestNormalizer.repairBootstrapLauncher).
static void repair_bootstraplauncher_ignore_list(std::vector<std::string>& args,
                                                 const Configurations& config) {
    for (std::string& arg : args) {
        static const char kPrefix[] = "-DignoreList=";
        if (arg.rfind(kPrefix, 0) != 0)
            continue;
        std::vector<std::string> tokens = split_comma(arg.substr(sizeof(kPrefix) - 1));
        auto has = [&tokens](const std::string& t) {
            for (const auto& s : tokens)
                if (s == t) return true;
            return false;
        };
        if (!has("asm"))
            tokens.push_back("asm");
        if (!has("securejarhandler"))
            tokens.push_back("securejarhandler");
        auto it = config.find("primary_jar_name");
        if (it != config.end() && !it->second.empty() && !has(it->second))
            tokens.push_back(it->second);

        std::string value;
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (i) value += ',';
            value += tokens[i];
        }
        arg = std::string(kPrefix) + value;
    }
}

// ---------------------------------------------------------------------------

DefaultLauncher::DefaultLauncher(VersionManifest manifest, LaunchOptions options, AuthInfo auth)
    : manifest_(std::move(manifest)),
      options_(std::move(options)),
      auth_(std::move(auth)) {
}

int DefaultLauncher::java_major_version() const {
    if (java_major_version_cache_ >= 0)
        return java_major_version_cache_;
    int v = -1;
    // `java -version` writes to stderr; capture both streams.
    auto out = run_for_output_merged({options_.java_path, "-version"});
    if (out)
        v = parse_java_major_version(*out);
    if (v < 0)
        v = manifest_.java_version;
    if (v < 0)
        v = 8;
    java_major_version_cache_ = v;
    return v;
}

std::map<std::string, bool> DefaultLauncher::features() const {
    std::map<std::string, bool> f;
    if (options_.width && *options_.width != 0 && options_.height && *options_.height != 0)
        f["has_custom_resolution"] = true;
    return f;
}

bool DefaultLauncher::using_log4j() const {
    // log4j2 is used by Minecraft 1.7+; base the decision on the actual game
    // version id (the assetIndex id is just a number and must not be used here).
    return version_ge(manifest_.id, 1, 7);
}

std::string DefaultLauncher::log4j_config_path() const {
    return join_path(instance_dir(options_.game_dir, manifest_.id), "log4j2.xml");
}

void DefaultLauncher::extract_log4j_config() {
    // HMCL splits the bundled config at 1.12: 1.7..1.11 get the "1.7" variant,
    // 1.12+ the "1.12" one. (Never called for < 1.7, which has no log4j2.)
    bool modern = version_ge(manifest_.id, 1, 12);
    write_file(log4j_config_path(), modern ? LOG4J2_1_12_XML : LOG4J2_1_7_XML);
}

std::string DefaultLauncher::natives_dir() const {
    if (!options_.natives_dir.empty())
        return options_.natives_dir;
    return join_path(instance_dir(options_.game_dir, manifest_.id),
                     manifest_.id + "-natives-" + native_platform());
}

std::vector<std::string> DefaultLauncher::classpath() const {
    std::vector<std::string> out;
    for (const Library& lib : manifest_.libraries) {
        if (lib.native)
            continue;
        if (!lib.applies(features()))
            continue;
        std::string path = absolute_path(join_path(libraries_dir(options_.game_dir), lib.relative_path()));
        if (!file_exists(path))
            continue;
        // Installer-generated manifests (e.g. NeoForge) can list the same library
        // twice; a duplicated -cp entry makes BootstrapLauncher's UnionFileSystem
        // crash on a duplicate path key, so de-duplicate like HMCL.
        if (std::find(out.begin(), out.end(), path) == out.end())
            out.push_back(path);
    }
    return out;
}

Configurations DefaultLauncher::configurations() const {
    std::string id = manifest_.id;
    std::string version_name = options_.version_name.empty() ? id : options_.version_name;

    std::string uuid = auth_.uuid;
    uuid.erase(std::remove_if(uuid.begin(), uuid.end(), [](char c) { return c == '-'; }), uuid.end());

    Configurations c;
    c["auth_player_name"] = auth_.username;
    c["auth_session"] = auth_.access_token;
    c["auth_access_token"] = auth_.access_token;
    c["auth_uuid"] = uuid;
    c["version_name"] = version_name;
    c["profile_name"] = options_.profile_name;
    c["version_type"] = options_.version_type.empty()
        ? (manifest_.type.empty() ? "unknown" : manifest_.type)
        : options_.version_type;
    c["game_directory"] = absolute_path(options_.game_dir);
    c["user_type"] = auth_.user_type;
    c["assets_index_name"] = manifest_.asset_index_id.empty() ? id : manifest_.asset_index_id;
    c["user_properties"] = auth_.user_properties;
    c["resolution_width"] = options_.width ? std::to_string(*options_.width) : std::string();
    c["resolution_height"] = options_.height ? std::to_string(*options_.height) : std::string();
    c["library_directory"] = absolute_path(libraries_dir(options_.game_dir));
    c["libraries_directory"] = c["library_directory"];
    c["classpath_separator"] = path_separator_str();
    c["primary_jar"] = absolute_path(main_jar_path(options_.game_dir, id));
    c["primary_jar_name"] = file_name(c["primary_jar"]);
    c["file_separator"] = "/";
    c["language"] = "en_us";
    c["game_assets"] = absolute_path(assets_dir(options_.game_dir));
    c["assets_root"] = c["game_assets"];
    c["natives_directory"] = absolute_path(natives_dir());

    // classpath = applicable libraries + main jar
    std::string cp;
    for (const auto& lib : classpath())
        cp += lib + path_separator_str();
    cp += c["primary_jar"];
    c["classpath"] = cp;

    // Official launcher placeholders; we provide benign defaults.
    c["launcher_name"] = "potato-launcher";
    c["launcher_version"] = "1.0.0";
    c["clientid"] = "";
    c["auth_xuid"] = "";
    c["auth_session"] = c["auth_access_token"];

    return c;
}

// User-supplied jvm arguments hook (extension point, mirrors appendJvmArgs).
void DefaultLauncher::append_jvm_args(CommandBuilder& res) {
    (void)res;
}

void DefaultLauncher::append_default_jvm_args(CommandBuilder& res) {
    append_jvm_args(res);

    int java_version = java_major_version();

    res.add_default("-Dminecraft.client.jar=", absolute_path(main_jar_path(options_.game_dir, manifest_.id)));

    if (is_macos())
        res.add_default("-Xdock:name=", "Minecraft " + manifest_.id);

    res.add_default("-Duser.home=", parent_dir(absolute_path(options_.game_dir)));

    // proxy
    if (options_.proxy_host.empty() || options_.proxy_port <= 0) {
        res.add_default("-Djava.net.useSystemProxies=", "true");
    } else {
        res.add("-Dhttp.proxyHost=" + options_.proxy_host);
        res.add("-Dhttp.proxyPort=" + std::to_string(options_.proxy_port));
        res.add("-Dhttps.proxyHost=" + options_.proxy_host);
        res.add("-Dhttps.proxyPort=" + std::to_string(options_.proxy_port));
        if (!options_.proxy_username.empty()) {
            res.add("-Dhttp.proxyUser=" + options_.proxy_username);
            res.add("-Dhttp.proxyPassword=" + options_.proxy_password);
            res.add("-Dhttps.proxyUser=" + options_.proxy_username);
            res.add("-Dhttps.proxyPassword=" + options_.proxy_password);
        }
    }

    if (!options_.no_generated_optimizing_jvm_args) {
        // G1GC tuning flags are experimental; unlock them first (matching HMCL).
        if (!res.has_prefix("-XX:+UnlockExperimentalVMOptions"))
            res.add("-XX:+UnlockExperimentalVMOptions");
        if (!res.has_prefix("-XX:+UnlockDiagnosticVMOptions"))
            res.add("-XX:+UnlockDiagnosticVMOptions");

        // Use G1GC unless the user picked another collector.
        bool custom_gc = false;
        for (const auto& a : res.args()) {
            if (a == "-XX:-UseG1GC" ||
                (a.rfind("-XX:+Use", 0) == 0 && a.size() > 7 &&
                 a.substr(a.size() - 2) == "GC")) {
                custom_gc = true;
                break;
            }
        }
        if (java_version >= 8 && !custom_gc) {
            res.add_unstable_default("-XX:+UseG1GC", "");
            res.add_unstable_default("-XX:G1MixedGCCountTarget=", "5");
            res.add_unstable_default("-XX:G1NewSizePercent=", "20");
            res.add_unstable_default("-XX:G1ReservePercent=", "20");
            res.add_unstable_default("-XX:MaxGCPauseMillis=", "50");
            res.add_unstable_default("-XX:G1HeapRegionSize=", "32m");
        }

        res.add_unstable_default("-XX:-OmitStackTraceInFastThrow", "");
        if (java_version <= 8)
            res.add_unstable_default("-XX:MaxInlineLevel=", "15");

        // On 64-bit JVMs with plenty of RAM, raise the JIT / code-cache budget
        // (mirrors HMCL's defaults).
        if (is_64bit() && total_memory_bytes() > 4LL * 1024 * 1024 * 1024) {
            res.add_unstable_default("-XX:-DontCompileHugeMethods", "");
            res.add_unstable_default("-XX:MaxNodeLimit=", "240000");
            res.add_unstable_default("-XX:NodeLimitFudgeFactor=", "8000");
            res.add_unstable_default("-XX:TieredCompileTaskTimeout=", "10000");
            res.add_unstable_default("-XX:ReservedCodeCacheSize=", "400M");
            if (java_version >= 9) {
                res.add_unstable_default("-XX:NonNMethodCodeHeapSize=", "12M");
                res.add_unstable_default("-XX:ProfiledCodeHeapSize=", "194M");
            }
            if (java_version >= 8)
                res.add_unstable_default("-XX:NmethodSweepActivity=", "1");
        }

        // JDK 25/26 enable compact object headers by default; pin the flag so
        // tuned heaps behave consistently.
        if (is_64bit() && java_version >= 25 && java_version <= 26)
            res.add_unstable_default("-XX:+UseCompactObjectHeaders", "");

        // 32-bit JVMs allocate 320KB stacks by default rather than the 1MB of
        // 64-bit JVMs, crashing Minecraft 1.13+ with StackOverflowError.
        if (!is_64bit())
            res.add_default("-Xss", "1m");
    }

    // Java 16 enforces strong encapsulation by default; MC 1.17+ needs this.
    if (java_version == 16)
        res.add_default("--illegal-access=", "permit");

    // JDK 24/25 restrict sun.misc.Unsafe memory-access; LWJGL-based games need it.
    if (java_version == 24 || java_version == 25)
        res.add_default("--sun-misc-unsafe-memory-access=", "allow");

    res.add_default("-Dfml.ignoreInvalidMinecraftCertificates=", "true");
    res.add_default("-Dfml.ignorePatchDiscrepancies=", "true");
}

std::vector<std::string> DefaultLauncher::generate_command_line() {
    CommandBuilder res;

    // Process priority via nice (POSIX). Windows uses start /ABOVENORMAL etc.
    std::string nice_value;
    switch (options_.process_priority) {
        case ProcessPriority::High: nice_value = "-5"; break;
        case ProcessPriority::AboveNormal: nice_value = "-1"; break;
        case ProcessPriority::Normal: break;
        case ProcessPriority::BelowNormal: nice_value = "1"; break;
        case ProcessPriority::Low: nice_value = "5"; break;
    }
    if (!nice_value.empty()) {
        res.add("nice");
        res.add("-n");
        res.add(nice_value);
    }

    // Optional wrapper (e.g. "flatpak run org.freedesktop.Sdk", "gamemoderun").
    if (!options_.wrapper.empty())
        res.add_all(tokenize_command(options_.wrapper));

    res.add(options_.java_path);

    res.add_all(options_.override_java_arguments);

    int java_version = java_major_version();

    if (options_.max_memory && *options_.max_memory > 0)
        res.add_default("-Xmx", std::to_string(*options_.max_memory) + "m");
    if (options_.min_memory && *options_.min_memory > 0 &&
        (!options_.max_memory || *options_.min_memory <= *options_.max_memory))
        res.add_default("-Xms", std::to_string(*options_.min_memory) + "m");

    if (options_.metaspace && *options_.metaspace > 0) {
        if (java_version < 8)
            res.add_default("-XX:PermSize=", std::to_string(*options_.metaspace) + "m");
        else
            res.add_default("-XX:MetaspaceSize=", std::to_string(*options_.metaspace) + "m");
    }

    res.add_all(options_.java_arguments);

    std::string encoding = "UTF-8";
    res.add_default("-Dfile.encoding=", encoding);
    if (java_version < 19) {
        res.add_default("-Dsun.stdout.encoding=", encoding);
        res.add_default("-Dsun.stderr.encoding=", encoding);
    } else {
        res.add_default("-Dstdout.encoding=", encoding);
        res.add_default("-Dstderr.encoding=", encoding);
    }

    // log4j2 RCE mitigations (CVE-2021-44228 and friends)
    res.add_default("-Djava.rmi.server.useCodebaseOnly=", "true");
    res.add_default("-Dcom.sun.jndi.rmi.object.trustURLCodebase=", "false");
    res.add_default("-Dcom.sun.jndi.cosnaming.object.trustURLCodebase=", "false");
    res.add_default("-Dlog4j2.formatMsgNoLookups=", "true");

    if (using_log4j())
        res.add_default("-Dlog4j.configurationFile=", absolute_path(log4j_config_path()));

    if (!options_.no_generated_jvm_args)
        append_default_jvm_args(res);

    // Manifest jvm arguments (with ${...} placeholders resolved).
    Configurations config = configurations();
    std::vector<std::string> manifest_jvm =
        substitute_all(manifest_.resolve_jvm_arguments(features()), config);
    if (manifest_.main_class == kBootstraplauncherMain)
        repair_bootstraplauncher_ignore_list(manifest_jvm, config);
    res.add_all(std::move(manifest_jvm));

    // Remove -Xincgc on Java 9+ (deprecated/removed flag).
    if (java_version >= 9)
        res.remove_starts_with("-Xincgc");

    if (manifest_.main_class.empty())
        throw std::runtime_error("mainClass is null for instance " + manifest_.id);
    res.add(manifest_.main_class);

    if (!manifest_.minecraft_arguments.empty()) {
        // Legacy arguments (pre-1.13): split on whitespace, substitute placeholders.
        res.add_all(substitute_all(tokenize_command(manifest_.minecraft_arguments), config));
    } else {
        // Structured game arguments.
        res.add_all(substitute_all(manifest_.resolve_game_arguments(features()), config));
    }

    if (options_.fullscreen)
        res.add("--fullscreen");

    if (!options_.proxy_host.empty() && options_.proxy_port > 0) {
        res.add("--proxyHost");
        res.add(options_.proxy_host);
        res.add("--proxyPort");
        res.add(std::to_string(options_.proxy_port));
        if (!options_.proxy_username.empty()) {
            res.add("--proxyUser");
            res.add(options_.proxy_username);
            res.add("--proxyPass");
            res.add(options_.proxy_password);
        }
    }

    res.add_all(substitute_all(options_.game_arguments, config));

    std::vector<std::string> cmd = res.take_args();
    // Placeholders that resolved to empty values (e.g. --clientId ${clientid})
    // must not remain on the command line. When a bare switch precedes an
    // empty value, the whole "--flag <empty>" pair is dropped.
    cmd = filter_empty_argument_pairs(std::move(cmd));
    return cmd;
}

std::vector<std::string> DefaultLauncher::filter_empty_argument_pairs(std::vector<std::string> cmd) {
    std::vector<bool> drop(cmd.size(), false);
    auto is_bare_switch = [](const std::string& s) {
        return !s.empty() && s[0] == '-' && s.find('=') == std::string::npos;
    };
    for (size_t i = 0; i < cmd.size(); ++i) {
        if (!cmd[i].empty())
            continue;
        drop[i] = true;
        if (i > 0 && !drop[i - 1] && is_bare_switch(cmd[i - 1]))
            drop[i - 1] = true;
    }
    std::vector<std::string> out;
    for (size_t i = 0; i < cmd.size(); ++i)
        if (!drop[i])
            out.push_back(std::move(cmd[i]));
    return out;
}

bool DefaultLauncher::decompress_natives(std::string* error) {
    std::string target = natives_dir();
    create_directories(target);
    clean_directory(target);

    for (const Library& lib : manifest_.libraries) {
        if (!lib.native || !lib.applies(features()))
            continue;
        ZipExtractOptions opts;
        if (lib.extract)
            opts.exclude = lib.extract->exclude;

        std::string jar = absolute_path(join_path(libraries_dir(options_.game_dir), lib.relative_path()));
        if (!file_exists(jar)) {
            if (error) *error = "native library not found: " + jar;
            return false;
        }
        if (!extract_zip(jar, target, opts, error))
            return false;
    }
    return true;
}

std::map<std::string, std::string> DefaultLauncher::environment_variables() const {
    std::string version_name = options_.version_name.empty() ? manifest_.id : options_.version_name;

    std::map<std::string, std::string> env;
    env["INST_NAME"] = version_name;
    env["INST_ID"] = version_name;
    env["INST_DIR"] = absolute_path(instance_dir(options_.game_dir, manifest_.id));
    env["INST_MC_DIR"] = absolute_path(options_.game_dir);
    env["INST_JAVA"] = options_.java_path;

    env.insert(options_.environment_variables.begin(), options_.environment_variables.end());
    return env;
}

void DefaultLauncher::launch(ProcessListener* listener) {
    std::vector<std::string> command = generate_command_line();
    for (const auto& a : command)
        if (a.empty())
            throw std::runtime_error("Illegal command line: contains a blank argument");

    if (!options_.use_custom_natives) {
        std::string err;
        if (!decompress_natives(&err))
            throw std::runtime_error("failed to decompress natives: " + err);
    }

    if (using_log4j())
        extract_log4j_config();

    std::string run_dir = absolute_path(options_.game_dir);
    create_directories(run_dir);

    if (!options_.pre_launch_command.empty()) {
        auto pre = tokenize_command(options_.pre_launch_command);
        auto res = spawn_process(pre, environment_variables(), run_dir);
        if (res) {
            int status = 0;
            waitpid(res->process->pid(), &status, 0);
            close(res->stdout_fd);
            close(res->stderr_fd);
            if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
                throw std::runtime_error("pre-launch command exited with code " +
                                         std::to_string(WEXITSTATUS(status)));
        }
    }

    std::vector<std::string> post_exit = tokenize_command(options_.post_exit_command);
    launch_and_monitor(command, environment_variables(), run_dir, listener, post_exit);
}

bool DefaultLauncher::make_launch_script(const std::string& script_path, std::string* error) {
    std::vector<std::string> command = generate_command_line();

    if (!options_.use_custom_natives) {
        std::string err;
        if (!decompress_natives(&err)) {
            if (error) *error = "failed to decompress natives: " + err;
            return false;
        }
    }
    if (using_log4j())
        extract_log4j_config();

    std::string out = "#!/usr/bin/env bash\n";
    for (const auto& kv : environment_variables())
        out += "export " + kv.first + "=" + shell_quote(kv.second) + "\n";
    out += "cd " + shell_quote(absolute_path(options_.game_dir)) + "\n";
    if (!options_.pre_launch_command.empty())
        out += options_.pre_launch_command + "\n";
    out += quoted_command_line(command) + "\n";
    if (!options_.post_exit_command.empty())
        out += options_.post_exit_command + "\n";

    if (!write_file(script_path, out)) {
        if (error) *error = "cannot write " + script_path;
        return false;
    }
    if (!set_executable(script_path)) {
        if (error) *error = "cannot mark " + script_path + " executable";
        return false;
    }
    return true;
}

} // namespace pl
