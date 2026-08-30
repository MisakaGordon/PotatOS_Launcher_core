/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * auth/http.cpp
 *
 * Runs `curl` in a subprocess. Request body and response are staged through
 * temp files so larger payloads are handled reliably.
 */
#include "http.h"
#include "crypto.h"
#include "../platform.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

extern char** environ;

namespace pl {

// Run a program, capturing stdout and stderr->/dev/null.
// Returns true when the process exited with status 0.
static bool run_capture_stdout(const std::vector<std::string>& argv, std::string* stdout_out) {
    int out_pipe[2];
    if (pipe(out_pipe) != 0) return false;
    pid_t pid = fork();
    if (pid < 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        return false;
    }
    if (pid == 0) {
        close(out_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(out_pipe[1]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        std::vector<char*> cargv;
        for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
        cargv.push_back(nullptr);
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
        execve(resolved.c_str(), cargv.data(), environ);
        _exit(127);
    }
    close(out_pipe[1]);
    std::string out;
    char buf[4096];
    ssize_t n;
    while ((n = read(out_pipe[0], buf, sizeof(buf))) > 0)
        out.append(buf, static_cast<size_t>(n));
    close(out_pipe[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (stdout_out) *stdout_out = out;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

HttpResponse http_request(HttpMethod method,
                          const std::string& url,
                          const std::string& body,
                          const std::map<std::string, std::string>& headers,
                          int timeout_seconds) {
    HttpResponse resp;

    // The curl binary is looked up from $POTATO_CURL (or PATH). A clear error
    // is reported when it is missing so users can install it or point at one.
    std::string curl = resolve_tool(
        get_env("POTATO_CURL").value_or("curl"));
    if (curl.empty()) {
        resp.ok = false;
        return resp;
    }

    std::string tmp = temp_directory();
    create_directories(tmp);
    std::string body_file = join_path(tmp, "potato-req-" + random_suffix());
    std::string out_file = join_path(tmp, "potato-resp-" + random_suffix());

    if (method != HttpMethod::Get && !body.empty()) {
        std::ofstream out(body_file, std::ios::binary);
        out.write(body.data(), static_cast<std::streamsize>(body.size()));
        out.close();
    }

    std::vector<std::string> args;
    args.push_back(curl);
    args.push_back("-sS");
    args.push_back("--connect-timeout");
    args.push_back("15");
    args.push_back("--max-time");
    args.push_back(std::to_string(timeout_seconds));
    args.push_back("-X");
    switch (method) {
        case HttpMethod::Get: args.push_back("GET"); break;
        case HttpMethod::Post: args.push_back("POST"); break;
        case HttpMethod::Put: args.push_back("PUT"); break;
    }
    for (const auto& kv : headers) {
        args.push_back("-H");
        args.push_back(kv.first + ": " + kv.second);
    }
    if (method != HttpMethod::Get && !body.empty()) {
        bool has_ct = false;
        for (const auto& kv : headers)
            if (kv.first == "Content-Type" || kv.first == "content-type") has_ct = true;
        if (!has_ct) {
            args.push_back("-H");
            args.push_back("Content-Type: application/json");
        }
        args.push_back("--data-binary");
        args.push_back("@" + body_file);
    }
    args.push_back("-o");
    args.push_back(out_file);
    args.push_back("-w");
    args.push_back("%{http_code}");
    args.push_back(url);

    std::string code_text;
    bool ran = run_capture_stdout(args, &code_text);
    if (!ran) {
        resp.status = 0;
        resp.ok = false;
    } else {
        // stdout holds only "%{http_code}" (the body went to -o out_file)
        while (!code_text.empty() &&
               (code_text.back() == '\n' || code_text.back() == '\r'))
            code_text.pop_back();
        resp.status = std::atoi(code_text.c_str());
        resp.ok = true;
        auto body_bytes = read_small_file(out_file);
        if (body_bytes) resp.body = *body_bytes;
    }

    remove(body_file.c_str());
    remove(out_file.c_str());
    return resp;
}

} // namespace pl
