/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * manifest.h - version.json data model
 *
 * Mirrors HMCL's GameVersionManifest / Library / Argument / Rule classes.
 */
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pl {

// A single os/feature rule from a library or argument.
struct Rule {
    enum class Action { Allow, Disallow };
    Action action = Action::Allow;

    std::optional<std::string> os_name;      // "windows" | "osx" | "linux"
    std::optional<std::string> os_version;   // regex matched against OS version
    std::optional<std::string> os_arch;      // "x86" | "x86_64" | ...
    std::string feature;                     // feature name when rules target features
};

// extract.exclude entries of a native library.
struct ExtractRule {
    std::vector<std::string> exclude;
};

// A downloadable artifact (mirrors HMCL's DownloadInfo / LibraryDownloadInfo).
struct DownloadInfo {
    std::string path;       // relative path inside libraries/ (optional)
    std::string url;        // direct download url
    std::string sha1;       // expected SHA-1 (empty when unknown)
    long long size = 0;
};

struct Library {
    std::string group;
    std::string name;       // artifact
    std::string version;
    std::string classifier; // e.g. "natives-linux" or "installer"

    std::vector<Rule> rules;
    std::optional<ExtractRule> extract;

    // native classifier map: os -> classifier suffix (e.g. linux -> "natives-linux")
    std::map<std::string, std::string> natives;

    bool native = false;

    // Download metadata from version.json.
    std::string url;                                // repository base url ("url")
    std::optional<DownloadInfo> artifact;           // downloads.artifact
    std::map<std::string, DownloadInfo> classifiers;// downloads.classifiers
    std::vector<std::string> checksums;             // "checksums"

    // group:name:version[:classifier]
    static Library parse(const std::string& name);

    // File name of the library jar, e.g. "name-1.0.0-natives-linux.jar".
    std::string file_name() const;

    // Relative path inside .minecraft/libraries, e.g.
    // "org/lwjgl/lwjgl/3.3.1/lwjgl-3.3.1-natives-linux.jar".
    std::string relative_path() const;

    // Download metadata selected for this library (artifact, or the classifier
    // entry for a native library). Mirrors HMCL Library.getRawDownloadInfo().
    std::optional<DownloadInfo> raw_download() const;

    // On-disk path relative to libraries/, preferring the manifest's
    // downloads path when present (mirrors HMCL Library.getPath()).
    std::string download_path() const;

    // URL to fetch this library from: the download entry's url, else the
    // library's repository base ("url") + path, else the Mojang default repo.
    std::string download_url() const;

    // Expected SHA-1: download entry, else the first "checksums" entry, else "".
    std::string download_sha1() const;

    // Whether this library is selected for the current OS/arch and optional features.
    bool applies(const std::map<std::string, bool>& features) const;
};

// An argument that may carry rules. "value" may be a single string or a list.
struct Argument {
    std::vector<Rule> rules;
    std::vector<std::string> values;
    bool has_rules = false;
};

struct VersionManifest {
    std::string id;
    std::string type;
    std::string main_class;

    // Legacy single-string arguments (used by pre-1.13 manifests).
    std::string minecraft_arguments;

    std::vector<Library> libraries;

    // 1.13+ structured arguments
    std::vector<Argument> jvm_arguments;
    std::vector<Argument> game_arguments;

    std::string asset_index_id;
    std::string asset_index_url;
    int java_version = 8;

    // Parse the JSON content of version.json.
    static VersionManifest parse(const std::string& json);

    // Select the jvm/game argument list after applying rules against features.
    std::vector<std::string> resolve_jvm_arguments(const std::map<std::string, bool>& features) const;
    std::vector<std::string> resolve_game_arguments(const std::map<std::string, bool>& features) const;
};

// Evaluate a list of rules. Empty list -> allow (true).
bool check_rules(const std::vector<Rule>& rules, const std::map<std::string, bool>& features);

// Test whether a feature map contains a given boolean feature.
bool has_feature(const std::map<std::string, bool>& features, const std::string& name, bool value);

} // namespace pl
