/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * platform.h - OS / filesystem / environment abstractions
 *
 * Mirrors the role of HMCL's util.platform package.
 */
#pragma once

#include <map>
#include <string>
#include <vector>
#include <optional>

namespace pl {

enum class Os {
    Windows,
    Linux,
    MacOs,
    Unknown
};

// Current operating system, decided at compile time on Linux/macOS/Windows.
Os current_os();

bool is_windows();
bool is_linux();
bool is_macos();

// "windows", "osx", "linux" (the values Minecraft version manifests use in rules)
std::string os_name();

// Pointer width, i.e. the JVM "bits" we will assume for the default java.
bool is_64bit();

// Value used for the "arch" field of os rules: "x86", "x86_64", "arm64"...
std::string os_arch();

// Classifier suffix appended to native library names.
// e.g. linux -> "natives-linux", macos arm64 -> "natives-osx-arm64".
std::string native_platform();

char path_separator();
std::string path_separator_str();

// Absolute normalized path, or "" when the input is empty.
std::string absolute_path(const std::string& path);
std::string parent_dir(const std::string& path);
std::string file_name(const std::string& path);
std::string join_path(const std::string& a, const std::string& b);

bool file_exists(const std::string& path);
bool is_directory(const std::string& path);
bool create_directories(const std::string& path);
bool clean_directory(const std::string& path);
bool delete_file(const std::string& path);
bool set_executable(const std::string& path);
long long file_size(const std::string& path);

std::optional<std::string> read_small_file(const std::string& path);
bool write_file(const std::string& path, const std::string& content);

// Environment variables
std::optional<std::string> get_env(const std::string& name);
std::map<std::string, std::string> get_all_env(); // defined in platform.cpp

// Split a command line string honoring double quotes.
std::vector<std::string> tokenize_command(const std::string& cmd);

// POSIX single-quote escaping for shell scripts.
std::string shell_quote(const std::string& s);

// Run "cmd -version" style programs, returning the whole stdout.
std::optional<std::string> run_for_output(const std::vector<std::string>& argv);

// Like run_for_output but merges stderr into the captured output
// (needed for programs like `java -version` that print to stderr).
std::optional<std::string> run_for_output_merged(const std::vector<std::string>& argv);

// Parse the major java version out of `java -version` output.
// "version \"1.8.0_412\"" -> 8 ; "version \"17.0.11\"" -> 17
int parse_java_major_version(const std::string& output);

// Directory for temporary files, honoring TMPDIR/TEMP/TMP before falling back
// to the platform default (e.g. /tmp, or %TEMP% on Windows).
std::string temp_directory();

// Locate an executable by name through PATH, or an absolute path.
// Returns the resolved path, or empty when not found.
std::string find_in_path(const std::string& program);

// Detect the major java version of a candidate binary ("" -> not runnable).
int detect_java_major(const std::string& java_binary);

// Auto-detect a java runtime matching `required_major` (0 = any).
// Checks, in order: PATH "java", JAVA_HOME, and common install locations
// (/usr/lib/jvm, /usr/java, /opt/java, macOS /Library/Java/..., Windows
// Program Files). Prefers an exact major-version match, else the newest one.
// Returns "" when nothing usable was found.
std::string find_java(int required_major);

// Resolve a runtime tool like curl/openssl. An absolute path or a bare name
// found on PATH is returned as-is; otherwise "".
std::string resolve_tool(const std::string& name);

} // namespace pl
