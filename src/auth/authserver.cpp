/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * auth/authserver.cpp
 */
#include "authserver.h"
#include "crypto.h"
#include "../platform.h"

#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

using json = nlohmann::json;

namespace pl {

// ---------------------------------------------------------------------------
// openssl helpers (RSA keygen / sign via the CLI)
// ---------------------------------------------------------------------------
namespace {

bool run_openssl(const std::vector<std::string>& args, std::string* out) {
    return run_for_output(args).has_value()
        ? (*out = *run_for_output(args), true)
        : false;
}

std::string temp_file(const std::string& content) {
    std::string path = "/tmp/potato-launcher-rsa-" + random_suffix();
    write_file(path, content);
    return path;
}

// Returns the private key PEM, or empty on failure.
std::string generate_rsa_key() {
    std::string pem;
    if (run_openssl({"openssl", "genrsa", "2048"}, &pem) && !pem.empty())
        return pem;
    return {};
}

std::string rsa_public_pem(const std::string& private_pem) {
    std::string keyfile = temp_file(private_pem);
    std::string pub;
    run_openssl({"openssl", "rsa", "-in", keyfile, "-pubout"}, &pub);
    remove(keyfile.c_str());
    return pub;
}

// SHA1withRSA via openssl CLI. OpenSSL 3.x refuses `dgst -sha1 -sign`, and
// `pkeyutl -sign -rawin` hashes the input with SHA-256 in this build, so we
// build the SHA-1 DigestInfo ourselves and sign it with `rsautl -pkcs`
// (raw PKCS#1 v1.5, no digest) — which Java's SHA1withRSA accepts.
std::string rsa_sign_sha1(const std::string& private_pem, const std::string& data) {
    // DigestInfo ::= SEQUENCE { SEQUENCE { OID sha1, NULL }, OCTET STRING digest }
    static const char SHA1_DIGEST_INFO_PREFIX[] = {
        0x30, 0x21, 0x30, 0x09, 0x06, 0x05, 0x2b, 0x0e, 0x03, 0x02, 0x1a, 0x05, 0x00, 0x04, 0x14,
    };
    std::string digest_info(reinterpret_cast<const char*>(SHA1_DIGEST_INFO_PREFIX),
                            sizeof(SHA1_DIGEST_INFO_PREFIX));
    digest_info += sha1_raw(data);

    std::string keyfile = temp_file(private_pem);
    std::string datafile = temp_file(digest_info);
    std::string sigfile = "/tmp/potato-launcher-sig-" + random_suffix();
    bool ok = run_for_output({"openssl", "rsautl", "-sign", "-inkey", keyfile,
                              "-in", datafile, "-pkcs", "-out", sigfile}).has_value();
    remove(keyfile.c_str());
    remove(datafile.c_str());
    if (!ok) {
        remove(sigfile.c_str());
        return {};
    }
    auto sig = read_small_file(sigfile);
    remove(sigfile.c_str());
    if (!sig) return {};
    return base64_encode(*sig);
}

} // namespace

// Internal HTTP response struct (declared in authserver.h).
struct HttpResponse {
    int status = 200;
    std::string content_type = "application/json";
    std::string body;
    std::map<std::string, std::string> extra_headers;
};

// ---------------------------------------------------------------------------
// minimal HTTP server
// ---------------------------------------------------------------------------
namespace {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string query;
    std::string body;
    std::map<std::string, std::string> query_params;
};

std::string url_decode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            auto val = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = val(s[i + 1]), lo = val(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

bool read_fully(int fd, std::string& buf, size_t n) {
    size_t have = buf.size();
    buf.resize(have + n);
    size_t got = 0;
    while (got < n) {
        ssize_t r = recv(fd, &buf[have + got], n - got, 0);
        if (r <= 0) return false;
        got += static_cast<size_t>(r);
    }
    return true;
}

bool parse_request(int fd, HttpRequest& req) {
    std::string head;
    char c;
    size_t body_pos = std::string::npos;
    while ((body_pos = head.find("\r\n\r\n")) == std::string::npos) {
        ssize_t r = recv(fd, &c, 1, 0);
        if (r != 1) return false;
        head += c;
        if (head.size() > 65536) return false;
    }

    std::istringstream lines(head.substr(0, body_pos));
    std::string request_line;
    std::getline(lines, request_line);
    if (request_line.size() > 1 && request_line.back() == '\r')
        request_line.pop_back();
    std::istringstream rl(request_line);
    rl >> req.method >> req.path;

    size_t q = req.path.find('?');
    if (q != std::string::npos) {
        req.query = req.path.substr(q + 1);
        req.path = req.path.substr(0, q);
        // parse query params
        std::istringstream qs(req.query);
        std::string kv;
        while (std::getline(qs, kv, '&')) {
            size_t eq = kv.find('=');
            if (eq == std::string::npos)
                req.query_params[url_decode(kv)] = "";
            else
                req.query_params[url_decode(kv.substr(0, eq))] =
                    url_decode(kv.substr(eq + 1));
        }
    }

    size_t content_length = 0;
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        while (!value.empty() && value.front() == ' ') value.erase(0, 1);
        if (name == "Content-Length")
            content_length = static_cast<size_t>(std::atoll(value.c_str()));
    }

    if (content_length > 0 && !read_fully(fd, req.body, content_length))
        return false;
    return true;
}

void send_response(int fd, const HttpResponse& resp) {
    std::ostringstream out;
    const char* status_text = resp.status == 200 ? "OK"
        : resp.status == 204 ? "No Content"
        : resp.status == 404 ? "Not Found"
        : resp.status == 400 ? "Bad Request"
        : "OK";
    out << "HTTP/1.1 " << resp.status << " " << status_text << "\r\n";
    if (resp.status != 204) {
        out << "Content-Type: " << resp.content_type << "\r\n";
        out << "Content-Length: " << resp.body.size() << "\r\n";
    }
    for (const auto& kv : resp.extra_headers)
        out << kv.first << ": " << kv.second << "\r\n";
    out << "Connection: close\r\n\r\n";
    std::string header = out.str();
    send(fd, header.data(), header.size(), 0);
    if (resp.status != 204 && !resp.body.empty())
        send(fd, resp.body.data(), resp.body.size(), 0);
}

} // namespace

// ---------------------------------------------------------------------------
// YggdrasilServer
// ---------------------------------------------------------------------------
bool YggdrasilServer::start(int port) {
    private_pem_ = generate_rsa_key();
    if (private_pem_.empty())
        return false;
    public_pem_ = rsa_public_pem(private_pem_);
    if (public_pem_.empty())
        return false;

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return false;
    int one = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    socklen_t len = sizeof(addr);
    if (getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len) == 0)
        port_ = ntohs(addr.sin_port);
    if (listen(listen_fd_, 8) != 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    running_ = true;
    thread_ = std::thread([this] { accept_loop(); });
    return true;
}

void YggdrasilServer::stop() {
    running_ = false;
    if (listen_fd_ >= 0) {
        shutdown(listen_fd_, SHUT_RDWR);
        close(listen_fd_);
        listen_fd_ = -1;
    }
    if (thread_.joinable())
        thread_.join();
}

void YggdrasilServer::accept_loop() {
    while (running_) {
        int conn = accept(listen_fd_, nullptr, nullptr);
        if (conn < 0) break;
        handle_connection(conn);
        close(conn);
    }
}

void YggdrasilServer::add_character(const std::string& profile_id,
                                    const std::string& profile_name,
                                    const LoadedSkin& skin) {
    profile_id_ = profile_id;
    profile_name_ = profile_name;
    skin_ = skin;
    if (!skin_.png_data.empty())
        skin_hash_ = sha256_hex(skin_.png_data);
}

std::string YggdrasilServer::root_url() const {
    return "http://127.0.0.1:" + std::to_string(port_);
}

std::string YggdrasilServer::authlib_injector_agent(const std::string& injector_jar) const {
    return "-javaagent:" + injector_jar + "=" + root_url();
}

void YggdrasilServer::handle_connection(int fd) {
    HttpRequest req;
    if (!parse_request(fd, req))
        return;

    HttpResponse resp;
    bool found = false;

    if (req.method == "GET" && req.path == "/") {
        found = true;
        json meta = {
            {"signaturePublickey", public_pem_},
            {"skinDomains", json::array({"127.0.0.1", "localhost"})},
            {"meta", {
                {"serverName", "potato-launcher"},
                {"implementationName", "potato-launcher"},
                {"implementationVersion", "1.0"},
                {"feature.non_email_login", true},
            }},
        };
        resp.body = meta.dump();
    } else if (req.method == "GET" && req.path == "/status") {
        found = true;
        resp.body = json{
            {"user.count", 1},
            {"token.count", 0},
            {"pendingAuthentication.count", 0},
        }.dump();
    } else if (req.method == "POST" && req.path == "/api/profiles/minecraft") {
        found = true;
        json names = json::parse(req.body, nullptr, false);
        json result = json::array();
        if (names.is_array()) {
            for (const auto& n : names) {
                if (!n.is_string()) continue;
                if (n.get<std::string>() == profile_name_)
                    result.push_back({{"id", profile_id_}, {"name", profile_name_}});
            }
        }
        resp.body = result.dump();
    } else if (req.method == "GET" &&
               req.path == "/sessionserver/session/minecraft/hasJoined") {
        auto it = req.query_params.find("username");
        if (it == req.query_params.end()) {
            resp.status = 400;
        } else if (it->second == profile_name_) {
            resp = complete_response();
        } else {
            resp.status = 204;
            resp.body.clear();
        }
        found = true;
    } else if (req.method == "POST" &&
               req.path == "/sessionserver/session/minecraft/join") {
        resp.status = 204;
        resp.body.clear();
        found = true;
    } else if (req.method == "GET" &&
               req.path.rfind("/sessionserver/session/minecraft/profile/", 0) == 0) {
        std::string uuid = req.path.substr(
            std::string("/sessionserver/session/minecraft/profile/").size());
        if (uuid == profile_id_)
            resp = complete_response();
        else {
            resp.status = 204;
            resp.body.clear();
        }
        found = true;
    } else if (req.method == "GET" && req.path.rfind("/textures/", 0) == 0) {
        std::string hash = req.path.substr(std::string("/textures/").size());
        if (!skin_hash_.empty() && hash == skin_hash_ && !skin_.png_data.empty()) {
            resp.status = 200;
            resp.content_type = "image/png";
            resp.body = skin_.png_data;
            resp.extra_headers["Cache-Control"] = "max-age=2592000, public";
        } else {
            resp.status = 404;
            resp.body = "{}";
        }
        found = true;
    }

    if (!found) {
        resp.status = 404;
        resp.body = "{}";
    }

    send_response(fd, resp);
}

HttpResponse YggdrasilServer::complete_response() {
    json texture_payload = {
        {"timestamp", static_cast<int64_t>(time(nullptr) * 1000)},
        {"profileId", profile_id_},
        {"profileName", profile_name_},
    };
    if (!skin_hash_.empty()) {
        json skin = {
            {"url", root_url() + "/textures/" + skin_hash_},
        };
        if (skin_.slim)
            skin["metadata"] = {{"model", "slim"}};
        texture_payload["textures"] = {{"SKIN", skin}};
    } else {
        texture_payload["textures"] = json::object();
    }

    std::string value = base64_encode(texture_payload.dump());
    std::string signature = rsa_sign_sha1(private_pem_, value);

    json property = {
        {"name", "textures"},
        {"value", value},
    };
    if (!signature.empty())
        property["signature"] = signature;

    HttpResponse resp;
    resp.status = 200;
    resp.body = json{
        {"id", profile_id_},
        {"name", profile_name_},
        {"properties", json::array({property})},
    }.dump();
    return resp;
}

} // namespace pl
