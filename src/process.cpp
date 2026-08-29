/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * process.cpp
 */
#include "process.h"
#include "platform.h"

#if defined(_WIN32)
// Windows process management is not implemented yet; this file only
// provides the POSIX implementation used on Linux/macOS.
#else

#include <cerrno>
#include <csignal>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <optional>
#include <thread>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

namespace pl {

const char* exit_type_name(ExitType t) {
    switch (t) {
        case ExitType::JVM_ERROR: return "JVM_ERROR";
        case ExitType::APPLICATION_ERROR: return "APPLICATION_ERROR";
        case ExitType::KILLED: return "SIGKILL";
        case ExitType::NORMAL: return "NORMAL";
        case ExitType::INTERRUPTED: return "INTERRUPTED";
    }
    return "UNKNOWN";
}

void ConsoleProcessListener::on_log(const std::string& line, bool is_error_stream) {
    if (is_error_stream)
        std::cerr << line << std::endl;
    else
        std::cout << line << std::endl;
}

void ConsoleProcessListener::on_exit(int exit_code, ExitType type) {
    std::cout << "[launcher] game exited with code " << exit_code
              << " (" << exit_type_name(type) << ")" << std::endl;
}

static bool contains_substring(const std::vector<std::string>& lines,
                               const std::vector<std::string>& needles) {
    for (const auto& line : lines) {
        for (const auto& n : needles) {
            if (line.find(n) != std::string::npos)
                return true;
        }
    }
    return false;
}

ExitType determine_exit_type(int exit_code, const std::vector<std::string>& error_lines) {
    if (exit_code != 0 && contains_substring(error_lines, {
            "Could not create the Java Virtual Machine.",
            "Error occurred during initialization of VM",
            "A fatal exception has occurred. Program will exit."})) {
        return ExitType::JVM_ERROR;
    }
    if (exit_code != 0 || contains_substring(error_lines, {
            "Crash report saved to", "Could not save crash report to",
            "This crash report has been saved to:", "Unable to launch",
            "An exception was thrown, the game will display an error screen and halt."})) {
        if (exit_code == 137 && (is_linux()))
            return ExitType::KILLED;
        return ExitType::APPLICATION_ERROR;
    }
    return ExitType::NORMAL;
}

static std::vector<std::string> build_environ(const std::map<std::string, std::string>& env) {
    auto all = get_all_env();
    for (const auto& kv : env)
        all[kv.first] = kv.second;
    std::vector<std::string> out;
    for (const auto& kv : all)
        out.push_back(kv.first + "=" + kv.second);
    return out;
}

std::optional<SpawnResult> spawn_process(
    const std::vector<std::string>& argv,
    const std::map<std::string, std::string>& env,
    const std::string& workdir) {

    int out_pipe[2], err_pipe[2];
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0)
        return std::nullopt;

    std::vector<std::string> env_vec = build_environ(env);
    std::vector<char*> cargv, cenv;
    for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
    cargv.push_back(nullptr);
    for (auto& e : env_vec) cenv.push_back(const_cast<char*>(e.c_str()));
    cenv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        return std::nullopt;
    }

    if (pid == 0) {
        // child
        close(out_pipe[0]);
        close(err_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[1]);
        close(err_pipe[1]);
        if (!workdir.empty())
            chdir(workdir.c_str());
        // keep stdin /dev/null so the game never blocks reading the terminal
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }
        // execve does not search PATH; resolve bare program names manually.
        std::string program = argv[0];
        std::string resolved = program;
        if (program.find('/') == std::string::npos) {
            const char* path = getenv("PATH");
            if (path) {
                std::string pathenv(path);
                size_t pos = 0;
                while (pos <= pathenv.size()) {
                    size_t nxt = pathenv.find(':', pos);
                    std::string dir = pathenv.substr(
                        pos, nxt == std::string::npos ? std::string::npos : nxt - pos);
                    if (dir.empty()) dir = ".";
                    std::string candidate = dir + "/" + program;
                    if (access(candidate.c_str(), X_OK) == 0) {
                        resolved = candidate;
                        break;
                    }
                    if (nxt == std::string::npos) break;
                    pos = nxt + 1;
                }
            }
        }
        execve(resolved.c_str(), cargv.data(), cenv.data());
        // exec failed - report through stderr so the launcher can diagnose
        std::string msg = "failed to execute '" + argv[0] + "': " + strerror(errno) + "\n";
        write(STDERR_FILENO, msg.data(), msg.size());
        _exit(127);
    }

    // parent
    close(out_pipe[1]);
    close(err_pipe[1]);
    SpawnResult res;
    res.process = std::make_shared<ManagedProcess>(pid);
    res.stdout_fd = out_pipe[0];
    res.stderr_fd = err_pipe[0];
    return res;
}

static void pump_fd(int fd, ProcessListener* listener, ManagedProcess* proc,
                    bool is_error, std::vector<std::string>* errors) {
    FILE* fp = fdopen(fd, "r");
    if (!fp) {
        close(fd);
        return;
    }
    char* line = nullptr;
    size_t cap = 0;
    ssize_t n;
    while ((n = getline(&line, &cap, fp)) != -1) {
        std::string s(line, static_cast<size_t>(n));
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
            s.pop_back();
        if (s.empty()) continue;
        if (proc) proc->add_line(s);
        if (is_error && errors) errors->push_back(s);
        if (listener) listener->on_log(s, is_error);
    }
    free(line);
    fclose(fp);
}

void launch_and_monitor(
    const std::vector<std::string>& argv,
    const std::map<std::string, std::string>& env,
    const std::string& workdir,
    ProcessListener* listener,
    const std::vector<std::string>& post_exit_command) {

    auto spawn = spawn_process(argv, env, workdir);
    if (!spawn) {
        if (listener) listener->on_exit(-1, ExitType::INTERRUPTED);
        return;
    }

    auto proc = spawn->process;
    std::vector<std::string> error_lines;
    std::thread stdout_pump(pump_fd, spawn->stdout_fd, listener, proc.get(), false, nullptr);
    std::thread stderr_pump(pump_fd, spawn->stderr_fd, listener, proc.get(), true, &error_lines);

    int status = 0;
    waitpid(proc->pid(), &status, 0);

    stdout_pump.join();
    stderr_pump.join();

    int exit_code;
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exit_code = 128 + WTERMSIG(status);
    } else {
        exit_code = 1;
    }

    ExitType type = determine_exit_type(exit_code, error_lines);

    if (!post_exit_command.empty()) {
        // run the post-exit command inside the game directory
        auto run = [&]() {
            auto res = spawn_process(post_exit_command, env, workdir);
            if (!res) return;
            int s = 0;
            waitpid(res->process->pid(), &s, 0);
            close(res->stdout_fd);
            close(res->stderr_fd);
        };
        try {
            run();
        } catch (...) {
            // never let a post-exit command failure hide the game result
        }
    }

    if (listener) listener->on_exit(exit_code, type);
}

} // namespace pl

#endif // _WIN32
