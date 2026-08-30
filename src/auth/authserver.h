/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * auth/authserver.h - local yggdrasil server for offline skins
 *
 * Mirrors HMCL's offline.YggdrasilServer. When an offline account has a skin,
 * the game is launched with `-javaagent:authlib-injector.jar=<local url>` and
 * this tiny HTTP server answers the yggdrasil API so the game can log in
 * offline and fetch the skin. Skins are served as /textures/<sha256>.
 *
 * RSA signing is delegated to the `openssl` command line tool (no crypto
 * library is linked).
 */
#pragma once

#include <atomic>
#include <map>
#include <string>
#include <thread>

namespace pl {

// Internal HTTP response struct, defined in authserver.cpp.
struct HttpResponse;

struct LoadedSkin {
    bool slim = false;
    std::string png_data;      // raw png bytes (used to compute sha256 and serve)
};

class YggdrasilServer {
public:
    YggdrasilServer() = default;
    ~YggdrasilServer() { stop(); }
    YggdrasilServer(const YggdrasilServer&) = delete;
    YggdrasilServer& operator=(const YggdrasilServer&) = delete;

    // Starts the server on 127.0.0.1 at `port` (0 = pick a free port).
    // Returns false if it could not start (e.g. openssl missing when signing).
    bool start(int port);
    void stop();

    // Register the offline player served by this server.
    void add_character(const std::string& profile_id, const std::string& profile_name,
                       const LoadedSkin& skin);

    int listening_port() const { return port_; }
    std::string root_url() const;

    // The -javaagent launcher argument (authlib-injector.jar=local url) and
    // the extra JVM args returned to the caller.
    std::string authlib_injector_agent(const std::string& injector_jar) const;

private:
    void accept_loop();
    void handle_connection(int fd);
    HttpResponse complete_response();

    int port_ = 0;
    int listen_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::string private_pem_;
    std::string public_pem_;
    std::string profile_id_;
    std::string profile_name_;
    std::string skin_hash_;
    LoadedSkin skin_;
};

} // namespace pl
