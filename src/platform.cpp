/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * platform.cpp
 */
#include "platform.h"

#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <dirent.h>
#include <sys/wait.h>
#include <cerrno>
#endif

namespace pl {

Os current_os() {
#if defined(_WIN32)
    return Os::Windows;
#elif defined(__APPLE__)
    return Os::MacOs;
#elif defined(__linux__)
    return Os::Linux;
#else
    return Os::Unknown;
#endif
}

bool is_windows() { return current_os() == Os::Windows; }
bool is_linux() { return current_os() == Os::Linux; }
bool is_macos() { return current_os() == Os::MacOs; }

std::string os_name() {
    switch (current_os()) {
        case Os::Windows: return "windows";
        case Os::MacOs: return "osx";
        case Os::Linux: return "linux";
        default: return "unknown";
    }
}

bool is_64bit() {
    return sizeof(void*) == 8;
}

std::string os_arch() {
#if defined(_MSC_VER)
#if defined(_M_ARM64)
    return "arm64";
#elif defined(_M_X64)
    return "x86_64";
#elif defined(_M_IX86)
    return "x86";
#elif defined(_M_ARM)
    return "arm";
#endif
#elif defined(__GNUC__) || defined(__clang__)
#if defined(__aarch64__)
    return "arm64";
#elif defined(__x86_64__)
    return "x86_64";
#elif defined(__i386__)
    return "x86";
#elif defined(__arm__)
    return "arm";
#elif defined(__riscv)
    return "riscv64";
#endif
#endif
    return "unknown";
}

// Classifier used by most Minecraft native library manifests.
// e.g. "linux", "osx", "windows", "windows-x86", "osx-arm64", "linux-arm64".
std::string native_platform() {
    std::string os = os_name();
    if (!is_64bit() && os == "windows") {
        return os + "-x86";
    }
    if (os == "osx" && os_arch() == "arm64") {
        return os + "-arm64";
    }
    if (os == "linux" && os_arch() == "arm64") {
        return os + "-arm64";
    }
    if (os == "linux" && os_arch() == "arm") {
        return os + "-arm32";
    }
    return os;
}

char path_separator() {
#if defined(_WIN32)
    return ';';
#else
    return ':';
#endif
}

std::string path_separator_str() {
    return std::string(1, path_separator());
}

std::string absolute_path(const std::string& path) {
    if (path.empty()) return "";
#if defined(_WIN32)
    char buf[MAX_PATH];
    if (GetFullPathNameA(path.c_str(), MAX_PATH, buf, nullptr) == 0)
        return path;
    return buf;
#else
    if (!path.empty() && path[0] == '/')
        return path;
    char buf[4096];
    if (getcwd(buf, sizeof(buf)) == nullptr)
        return path;
    std::string sep = path.rfind('/', 0) == 0 ? "" : "/";
    return std::string(buf) + sep + path;
#endif
}

std::string parent_dir(const std::string& path) {
    std::string abs = absolute_path(path);
    size_t pos = abs.find_last_of('/');
    if (pos == std::string::npos) return ".";
    if (pos == 0) return "/";
    return abs.substr(0, pos);
}

std::string file_name(const std::string& path) {
    std::string p = path;
    while (!p.empty() && p.back() == '/')
        p.pop_back();
    size_t pos = p.find_last_of('/');
    if (pos == std::string::npos) return p;
    return p.substr(pos + 1);
}

std::string join_path(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}

bool file_exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool is_directory(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool create_directories(const std::string& path) {
    if (path.empty()) return false;
    std::string cur;
    std::string p = path;
    if (p[0] == '/') {
        cur = "/";
        p = p.substr(1);
    }
    size_t pos = 0;
    while (pos <= p.size()) {
        size_t nxt = p.find('/', pos);
        std::string part = p.substr(pos, nxt == std::string::npos ? std::string::npos : nxt - pos);
        if (!part.empty()) {
            if (cur.empty() || cur == "/") cur = cur + part;
            else if (cur.back() == '/') cur = cur + part;
            else cur = cur + "/" + part;
            if (!file_exists(cur)) {
#if defined(_WIN32)
                if (!CreateDirectoryA(cur.c_str(), nullptr)) return false;
#else
                if (mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) return false;
#endif
            }
        }
        if (nxt == std::string::npos) break;
        pos = nxt + 1;
    }
    return true;
}

bool clean_directory(const std::string& path) {
    if (!is_directory(path)) {
        return create_directories(path);
    }
#if defined(_WIN32)
    return true; // best effort on windows; native extraction re-creates files anyway
#else
    DIR* dir = opendir(path.c_str());
    if (!dir) return false;
    struct dirent* ent;
    bool ok = true;
    while ((ent = readdir(dir)) != nullptr) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        std::string full = join_path(path, name);
        struct stat st;
        if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            ok = clean_directory(full) && ok;
        else
            ok = (remove(full.c_str()) == 0) && ok;
    }
    closedir(dir);
    return ok;
#endif
}

bool delete_file(const std::string& path) {
    return remove(path.c_str()) == 0 || errno == ENOENT;
}

bool set_executable(const std::string& path) {
#if defined(_WIN32)
    return true;
#else
    return chmod(path.c_str(), S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) == 0;
#endif
}

long long file_size(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return -1;
    return st.st_size;
}

std::optional<std::string> read_small_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool write_file(const std::string& path, const std::string& content) {
    create_directories(parent_dir(path));
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return out.good();
}

std::optional<std::string> get_env(const std::string& name) {
    const char* v = std::getenv(name.c_str());
    if (!v) return std::nullopt;
    return std::string(v);
}

#if defined(_WIN32)
std::map<std::string, std::string> get_all_env() {
    std::map<std::string, std::string> out;
    LPCH env = GetEnvironmentStrings();
    if (!env) return out;
    for (LPCH p = env; *p; p += strlen(p) + 1) {
        std::string entry(p);
        size_t eq = entry.find('=');
        if (eq != std::string::npos)
            out[entry.substr(0, eq)] = entry.substr(eq + 1);
    }
    FreeEnvironmentStrings(env);
    return out;
}
#else
std::map<std::string, std::string> get_all_env() {
    std::map<std::string, std::string> out;
    if (!environ) return out;
    for (char** p = environ; *p; ++p) {
        std::string entry(*p);
        size_t eq = entry.find('=');
        if (eq != std::string::npos)
            out[entry.substr(0, eq)] = entry.substr(eq + 1);
    }
    return out;
}
#endif

std::vector<std::string> tokenize_command(const std::string& cmd) {
    std::vector<std::string> out;
    std::string cur;
    bool in_quote = false;
    for (size_t i = 0; i < cmd.size(); ++i) {
        char c = cmd[i];
        if (c == '"') {
            in_quote = !in_quote;
        } else if (c == ' ' && !in_quote) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    out += "'";
    return out;
}

std::optional<std::string> run_for_output(const std::vector<std::string>& argv) {
#if defined(_WIN32)
    return std::nullopt;
#else
    int pipefd[2];
    if (pipe(pipefd) != 0) return std::nullopt;
    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return std::nullopt;
    }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        std::vector<char*> cargv;
        for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
        cargv.push_back(nullptr);
        execvp(cargv[0], cargv.data());
        _exit(127);
    }
    close(pipefd[1]);
    std::string out;
    char buf[4096];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0)
        out.append(buf, static_cast<size_t>(n));
    close(pipefd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return std::nullopt;
    return out;
#endif
}

int parse_java_major_version(const std::string& output) {
    // "version \"1.8.0_412\"", "version \"17.0.11\"", "openjdk version \"21.0.3\""
    size_t pos = output.find("version \"");
    if (pos == std::string::npos) return -1;
    pos += std::string("version \"").size();
    size_t end = output.find('"', pos);
    std::string ver = output.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    if (ver.rfind("1.", 0) == 0) {
        // legacy 1.x -> x
        return std::atoi(ver.substr(2).c_str());
    }
    size_t dot = ver.find('.');
    if (dot == std::string::npos) return std::atoi(ver.c_str());
    return std::atoi(ver.substr(0, dot).c_str());
}

} // namespace pl
