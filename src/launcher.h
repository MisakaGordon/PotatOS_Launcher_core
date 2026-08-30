/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * launcher.h - launch options, auth info, and the default launcher
 *
 * Mirrors HMCL's LaunchOptions / AuthInfo / DefaultLauncher.
 */
#pragma once

#include "auth/auth.h"
#include "command.h"
#include "manifest.h"
#include "process.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pl {

enum class ProcessPriority {
    High,
    AboveNormal,
    Normal,
    BelowNormal,
    Low,
};

struct LaunchOptions {
    // Java runtime
    std::string java_path = "java";
    std::optional<int> max_memory;   // MB
    std::optional<int> min_memory;   // MB
    std::optional<int> metaspace;    // MB
    std::vector<std::string> java_arguments;        // extra jvm args
    std::vector<std::string> override_java_arguments;
    std::vector<std::string> game_arguments;        // extra game args
    std::string wrapper;                            // optional wrapper command (e.g. "nice -n -5")

    // Instance
    std::string game_dir;               // .minecraft
    std::string version_name;           // overrides ${version_name}
    std::string profile_name = "potato";
    std::string version_type;

    // Resolution (enables the has_custom_resolution feature)
    std::optional<int> width;
    std::optional<int> height;

    bool fullscreen = false;

    // Natives
    std::string natives_dir;            // empty -> auto under versions/<id>/
    bool use_custom_natives = false;

    // JVM argument generation
    bool no_generated_jvm_args = false;
    bool no_generated_optimizing_jvm_args = false;

    ProcessPriority process_priority = ProcessPriority::Normal;

    std::map<std::string, std::string> environment_variables;

    std::string pre_launch_command;
    std::string post_exit_command;

    bool enable_debug_log_output = false;

    // HTTP proxy forwarded to the JVM
    std::string proxy_host;
    int proxy_port = -1;
    std::string proxy_username;
    std::string proxy_password;
};

// The default launcher: builds the java command line from a version manifest
// and launch options, prepares natives/log4j files and starts the game.
class DefaultLauncher {
public:
    DefaultLauncher(VersionManifest manifest, LaunchOptions options, AuthInfo auth);

    // Build the full java command line for this launch.
    std::vector<std::string> generate_command_line();

    // Resolve the natives directory (options override or versions/<id>-natives-<platform>).
    std::string natives_dir() const;

    // Classpath: all applicable non-native libraries + the main jar.
    std::vector<std::string> classpath() const;

    // The ${placeholder} configuration map passed into argument substitution.
    Configurations configurations() const;

    // Extract native libraries from their jars into the natives directory.
    bool decompress_natives(std::string* error);

    // Write the log4j2.xml configuration next to the version manifest (1.7+).
    void extract_log4j_config();

    // Start the game and monitor it until exit. Blocks until the game exits.
    void launch(ProcessListener* listener);

    // Generate a bash launch script. Returns false and fills error on failure.
    bool make_launch_script(const std::string& script_path, std::string* error);

    // Environment variables exposed to the game process (INST_*, ...).
    std::map<std::string, std::string> environment_variables() const;

private:
    // Detect the java major version by running `java -version`.
    int java_major_version() const;

    std::map<std::string, bool> features() const;

    bool using_log4j() const;
    std::string log4j_config_path() const;

    void append_default_jvm_args(CommandBuilder& res);
    void append_jvm_args(CommandBuilder& res);

    // Remove "--flag <empty>" pairs and stray empty tokens from a command line.
    static std::vector<std::string> filter_empty_argument_pairs(std::vector<std::string> cmd);

    VersionManifest manifest_;
    LaunchOptions options_;
    AuthInfo auth_;

    mutable int java_major_version_cache_ = -1;
};

} // namespace pl
