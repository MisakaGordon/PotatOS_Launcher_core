/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * command.cpp
 */
#include "command.h"

#include <sstream>

namespace pl {

void CommandBuilder::add(const std::string& arg) {
    args_.push_back(arg);
}

void CommandBuilder::add_all(const std::vector<std::string>& args) {
    args_.insert(args_.end(), args.begin(), args.end());
}

void CommandBuilder::add_default(const std::string& key, const std::string& value) {
    if (has_prefix(key))
        return;
    args_.push_back(key + value);
}

void CommandBuilder::add_unstable_default(const std::string& key, const std::string& value) {
    if (has_prefix(key))
        return;
    args_.push_back(key + value);
}

bool CommandBuilder::has_prefix(const std::string& prefix) const {
    for (const auto& a : args_)
        if (a.rfind(prefix, 0) == 0)
            return true;
    return false;
}

void CommandBuilder::remove_starts_with(const std::string& prefix) {
    for (auto it = args_.begin(); it != args_.end(); ++it) {
        if (it->rfind(prefix, 0) == 0) {
            args_.erase(it);
            return;
        }
    }
}

std::string substitute_placeholders(const std::string& arg, const Configurations& config) {
    std::string out;
    out.reserve(arg.size() + 16);
    size_t i = 0;
    while (i < arg.size()) {
        if (arg[i] == '$' && i + 1 < arg.size() && arg[i + 1] == '{') {
            size_t end = arg.find('}', i + 2);
            if (end != std::string::npos) {
                std::string key = arg.substr(i + 2, end - i - 2);
                auto it = config.find(key);
                if (it != config.end()) {
                    out += it->second;
                    i = end + 1;
                    continue;
                }
            }
        }
        out += arg[i];
        ++i;
    }
    return out;
}

std::vector<std::string> substitute_all(const std::vector<std::string>& args, const Configurations& config) {
    std::vector<std::string> out;
    out.reserve(args.size());
    for (const auto& a : args)
        out.push_back(substitute_placeholders(a, config));
    return out;
}

std::string render_command_line(const std::vector<std::string>& args) {
    std::ostringstream ss;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) ss << ' ';
        ss << args[i];
    }
    return ss.str();
}

} // namespace pl
