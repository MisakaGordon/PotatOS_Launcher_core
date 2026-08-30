/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * auth/http.h - minimal HTTP client
 *
 * Shells out to the system `curl` binary so no HTTP/TLS library is required.
 * This mirrors HMCL's NetworkUtils.doGet / doPost.
 */
#pragma once

#include <map>
#include <string>

namespace pl {

struct HttpResponse {
    int status = 0;         // 0 when the request itself failed
    std::string body;
    bool ok = false;        // network/transport success (status may still be >= 400)
};

enum class HttpMethod { Get, Post, Put };

// Perform an HTTP request.
// headers: extra request headers. Content-Type is set automatically for
//          Post/Put when body is non-empty unless overridden.
// Returns the response, or ok=false on transport errors (no curl, no network...).
HttpResponse http_request(HttpMethod method,
                          const std::string& url,
                          const std::string& body = {},
                          const std::map<std::string, std::string>& headers = {},
                          int timeout_seconds = 30);

} // namespace pl
