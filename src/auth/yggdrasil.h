/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * auth/yggdrasil.h - yggdrasil authentication
 *
 * Mirrors HMCL's YggdrasilService / YggdrasilAccount / YggdrasilSession.
 * Talks to the Mojang auth server (or any third-party yggdrasil server).
 */
#pragma once

#include "auth.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pl {

struct GameProfile {
    std::string id;    // compact uuid (32 hex chars)
    std::string name;
};

// A stored/acquired authentication session.
struct YggdrasilSession {
    std::string client_token;
    std::string access_token;
    std::optional<GameProfile> selected_profile;
    std::vector<GameProfile> available_profiles;
    std::map<std::string, std::string> user_properties;

    bool has_profile_name() const {
        return selected_profile && !selected_profile->name.empty();
    }

    AuthInfo to_auth_info() const;
};

// Base urls of a yggdrasil-compatible server.
struct YggdrasilProvider {
    std::string auth_url;       // e.g. https://authserver.mojang.com
    std::string session_url;    // e.g. https://sessionserver.mojang.com

    std::string authentication_url() const { return auth_url + "/authenticate"; }
    std::string refreshment_url() const { return auth_url + "/refresh"; }
    std::string validation_url() const { return auth_url + "/validate"; }
    std::string invalidation_url() const { return auth_url + "/invalidate"; }
    std::string profile_url(const std::string& uuid) const {
        return session_url + "/session/minecraft/profile/" + uuid;
    }
};

// Perform the yggdrasil protocol over HTTP.
class YggdrasilService {
public:
    explicit YggdrasilService(YggdrasilProvider provider);

    // POST /authenticate with username+password.
    YggdrasilSession authenticate(const std::string& username,
                                  const std::string& password,
                                  const std::string& client_token);

    // POST /refresh with an existing token pair; optionally re-select a character.
    YggdrasilSession refresh(const std::string& access_token,
                             const std::string& client_token,
                             const std::optional<GameProfile>& to_select = std::nullopt);

    // POST /validate. Returns false when the token is simply invalid
    // (ForbiddenOperationException), throws on other errors.
    bool validate(const std::string& access_token, const std::string& client_token);

    // POST /invalidate.
    void invalidate(const std::string& access_token, const std::string& client_token);

    // GET the profile properties (e.g. the "textures" base64 payload).
    std::optional<std::map<std::string, std::string>> get_profile_properties(const std::string& uuid);

    const YggdrasilProvider& provider() const { return provider_; }

private:
    std::string request(const std::string& url, const std::string& json_payload) const;
    YggdrasilSession handle_auth_response(const std::string& body, const std::string& client_token) const;

    YggdrasilProvider provider_;
};

// An account bound to a yggdrasil profile. Can log in with stored tokens
// (refreshing automatically) or offline.
class YggdrasilAccount : public Account {
public:
    YggdrasilAccount(std::string account_id,
                     std::shared_ptr<YggdrasilService> service,
                     std::string login_name,
                     YggdrasilSession session);

    std::string account_type() const override { return "yggdrasil"; }
    std::string account_id() const override { return account_id_; }
    std::string profile_name() const override;
    std::string profile_id() const override;

    // Validate stored tokens; refresh them when the session needs a profile
    // name. Throws CredentialExpiredException when a password is required.
    AuthInfo log_in() override;

    // Login without touching the network (uses cached profile name).
    AuthInfo play_offline() override;

    const std::string& login_name() const { return login_name_; }
    const YggdrasilSession& session() const { return session_; }
    std::shared_ptr<YggdrasilService> service() const { return service_; }

    // Re-login with a password (character selection must match the profile).
    void log_in_with_password(const std::string& password);

    // Mark the stored tokens as freshly verified (used right after login).
    void mark_authenticated() { authenticated_ = true; }

private:
    std::string account_id_;
    std::shared_ptr<YggdrasilService> service_;
    std::string login_name_;
    YggdrasilSession session_;
    bool authenticated_ = false;
};

} // namespace pl
