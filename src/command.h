/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * command.h - command line assembly and placeholder substitution
 *
 * Mirrors HMCL's CommandBuilder plus the ${placeholder} handling of
 * Arguments.parseArguments / getConfigurations.
 */
#pragma once

#include <map>
#include <string>
#include <vector>

namespace pl {

// Ordered command line builder with HMCL-style "default" semantics:
// add_default() only appends when no existing argument starts with the key,
// so user supplied arguments always win over generated ones.
class CommandBuilder {
public:
    // Append an argument verbatim.
    void add(const std::string& arg);
    void add_all(const std::vector<std::string>& args);

    // Append key+value as a single token; skipped if another token starts with key.
    void add_default(const std::string& key, const std::string& value);
    // Same as add_default but never adds when the same key was explicitly banned.
    void add_unstable_default(const std::string& key, const std::string& value);

    // Remove the first argument that starts with `prefix`.
    void remove_starts_with(const std::string& prefix);

    const std::vector<std::string>& args() const { return args_; }
    std::vector<std::string> take_args() { return std::move(args_); }

    // Whether any stored argument starts with the given prefix.
    bool has_prefix(const std::string& prefix) const;

private:
    std::vector<std::string> args_;
};

// Configurations is the ${key} -> value map of the launch (getConfigurations in HMCL).
using Configurations = std::map<std::string, std::string>;

// Substitute ${placeholders} in a single argument string.
// Unknown placeholders are left untouched (matching HMCL's tolerant behavior).
std::string substitute_placeholders(const std::string& arg, const Configurations& config);

// Substitute a list of raw argument strings.
std::vector<std::string> substitute_all(const std::vector<std::string>& args, const Configurations& config);

// Join arguments into a single line (used for generated scripts / display).
std::string render_command_line(const std::vector<std::string>& args);

} // namespace pl
