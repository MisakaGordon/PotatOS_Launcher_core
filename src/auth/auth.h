/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * auth/auth.h - core auth types
 *
 * Mirrors HMCL's org.jackhuang.hmcl.auth package: AuthInfo, the
 * AuthenticationException hierarchy and the Account interface.
 */
#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace pl {

// Authentication result passed to the launcher (mirrors HMCL's AuthInfo).
struct AuthInfo {
    std::string username;
    std::string access_token;
    std::string uuid;              // canonical or compact uuid
    std::string user_type = "mojang";
    std::string user_properties = "{}";  // JSON, e.g. {"key":["value"]}

    static constexpr const char* USER_TYPE_MSA = "msa";
    static constexpr const char* USER_TYPE_MOJANG = "mojang";
    static constexpr const char* USER_TYPE_LEGACY = "legacy";
};

// What a login method hands back: the AuthInfo plus any extra JVM arguments
// the game needs (e.g. the -javaagent for offline skins).
struct AuthResult {
    AuthInfo info;
    std::vector<std::string> extra_jvm_args;
};

// ---------------------------------------------------------------------------
// Exceptions (mirrors the HMCL AuthenticationException hierarchy)
// ---------------------------------------------------------------------------
class AuthException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Stored credentials expired; a fresh password login is required.
class CredentialExpiredException : public AuthException {
public:
    CredentialExpiredException() : AuthException("credentials expired") {}
    explicit CredentialExpiredException(const std::string& msg) : AuthException(msg) {}
};

// Account has no character at all.
class NoCharacterException : public AuthException {
public:
    NoCharacterException() : AuthException("the account has no character") {}
};

// No character was selected among the available ones.
class NoSelectedCharacterException : public AuthException {
public:
    NoSelectedCharacterException() : AuthException("no character selected") {}
};

// The previously selected character no longer exists.
class CharacterDeletedException : public AuthException {
public:
    CharacterDeletedException() : AuthException("the selected character was deleted") {}
};

// The auth server returned something malformed.
class ServerResponseMalformedException : public AuthException {
public:
    ServerResponseMalformedException() : AuthException("malformed server response") {}
    explicit ServerResponseMalformedException(const std::string& msg) : AuthException(msg) {}
};

// Could not reach the auth server.
class ServerDisconnectException : public AuthException {
public:
    explicit ServerDisconnectException(const std::string& msg) : AuthException(msg) {}
};

// A structured error returned by the yggdrasil server.
class RemoteAuthenticationException : public AuthException {
public:
    RemoteAuthenticationException(std::string name, std::string message, std::string cause)
        : AuthException(build_message(name, message, cause)),
          name_(std::move(name)),
          message_(std::move(message)),
          cause_(std::move(cause)) {}

    const std::string& remote_name() const { return name_; }
    const std::string& remote_message() const { return message_; }
    const std::string& remote_cause() const { return cause_; }

private:
    static std::string build_message(const std::string& name,
                                     const std::string& message,
                                     const std::string& cause) {
        std::string out = name;
        if (!message.empty()) out += ": " + message;
        if (!cause.empty()) out += ": " + cause;
        return out;
    }
    std::string name_;
    std::string message_;
    std::string cause_;
};

// ---------------------------------------------------------------------------
// Account
// ---------------------------------------------------------------------------
class Account {
public:
    virtual ~Account() = default;

    // Human-readable account kind: "yggdrasil" or "offline".
    virtual std::string account_type() const = 0;

    // Stable account entry id (account:<uuid>), mirrors HMCL's AccountID.
    virtual std::string account_id() const = 0;

    virtual std::string profile_name() const = 0;
    virtual std::string profile_id() const = 0;  // compact uuid

    // Login with stored credentials (refreshing when expired).
    // Throws CredentialExpiredException when a password is needed.
    virtual AuthInfo log_in() = 0;

    // Login usable offline (no network).
    virtual AuthInfo play_offline() = 0;
};

} // namespace pl
