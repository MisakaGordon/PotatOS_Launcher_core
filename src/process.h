/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * process.h - process spawn, output pumping, exit handling
 *
 * Mirrors HMCL's ManagedProcess / StreamPump / ExitWaiter / ProcessListener.
 */
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace pl {

enum class ExitType {
    JVM_ERROR,        // java itself failed to start
    APPLICATION_ERROR,// game crashed / exited abnormally
    KILLED,           // killed by signal 9 (137) on Linux
    NORMAL,
    INTERRUPTED,
};

const char* exit_type_name(ExitType t);

// Client callback for process output and termination.
class ProcessListener {
public:
    virtual void on_log(const std::string& line, bool is_error_stream) = 0;
    virtual void on_exit(int exit_code, ExitType type) = 0;
    virtual ~ProcessListener() = default;
};

// Default listener that forwards logs to stdout/stderr and prints the exit code.
class ConsoleProcessListener : public ProcessListener {
public:
    void on_log(const std::string& line, bool is_error_stream) override;
    void on_exit(int exit_code, ExitType type) override;
};

// Captures all lines produced by a launched game for later crash analysis.
class ManagedProcess {
public:
    explicit ManagedProcess(int pid) : pid_(pid) {}

    int pid() const { return pid_; }

    void add_line(const std::string& line) {
        std::lock_guard<std::mutex> lock(mutex_);
        lines_.push_back(line);
    }

    std::vector<std::string> lines() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return lines_;
    }

private:
    int pid_;
    mutable std::mutex mutex_;
    std::vector<std::string> lines_;
};

struct SpawnResult {
    std::shared_ptr<ManagedProcess> process;
    int stdout_fd = -1;
    int stderr_fd = -1;
};

// fork() + exec() the given argv with env and workdir.
// On failure returns std::nullopt.
std::optional<SpawnResult> spawn_process(
    const std::vector<std::string>& argv,
    const std::map<std::string, std::string>& env,
    const std::string& workdir);

// Classify a terminated process by exit code and captured error lines.
ExitType determine_exit_type(int exit_code, const std::vector<std::string>& error_lines);

// Launch a process and monitor it:
//   - pumps stdout/stderr lines into listener->on_log and ManagedProcess::add_line
//   - waits for exit, then runs post_exit_command (if any) and calls
//     listener->on_exit(exit_code, exit_type)
// This call blocks until the process exits.
void launch_and_monitor(
    const std::vector<std::string>& argv,
    const std::map<std::string, std::string>& env,
    const std::string& workdir,
    ProcessListener* listener,
    const std::vector<std::string>& post_exit_command);

} // namespace pl
