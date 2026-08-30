/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * auth/yggdrasil.cpp
 */
#include "yggdrasil.h"
#include "crypto.h"
#include "http.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace pl {

static json parse_json(const std::string& text) {
    return json::parse(text, nullptr, false);
}

// ---------------------------------------------------------------------------
// YggdrasilSession
// ---------------------------------------------------------------------------
AuthInfo YggdrasilSession::to_auth_info() const {
    if (!selected_profile)
        throw std::logic_error("no character is selected");

    // user properties {"key": ["value"]}
    json props = json::object();
    for (const auto& kv : user_properties)
        props[kv.first] = json::array({kv.second});

    AuthInfo info;
    info.username = selected_profile->name;
    info.uuid = selected_profile->id;
    info.access_token = access_token;
    info.user_type = AuthInfo::USER_TYPE_MSA;
    info.user_properties = props.dump();
    return info;
}

// ---------------------------------------------------------------------------
// YggdrasilService
// ---------------------------------------------------------------------------
YggdrasilService::YggdrasilService(YggdrasilProvider provider) : provider_(std::move(provider)) {
}

std::string YggdrasilService::request(const std::string& url, const std::string& json_payload) const {
    HttpResponse resp;
    if (json_payload.empty())
        resp = http_request(HttpMethod::Get, url);
    else
        resp = http_request(HttpMethod::Post, url, json_payload,
                            {{"Content-Type", "application/json"}});

    if (!resp.ok)
        throw ServerDisconnectException("cannot reach auth server at " + url);
    return resp.body;
}

// Parse an authentication/refresh response into a session, checking the
// structured error object and the clientToken round-trip.
YggdrasilSession YggdrasilService::handle_auth_response(const std::string& body,
                                                         const std::string& client_token) const {
    json j = parse_json(body);
    if (j.is_discarded())
        throw ServerResponseMalformedException("auth server returned invalid JSON");

    // {error, errorMessage, cause}
    if (j.contains("error") && !j.at("error").is_null()) {
        std::string name = j.value("error", "");
        std::string message = j.value("errorMessage", "");
        std::string cause = j.contains("cause") && j.at("cause").is_string()
            ? j.at("cause").get<std::string>() : "";
        throw RemoteAuthenticationException(name, message, cause);
    }

    YggdrasilSession s;
    if (!j.contains("accessToken") || !j.at("accessToken").is_string())
        throw ServerResponseMalformedException("accessToken is missing");
    s.access_token = j.at("accessToken").get<std::string>();
    s.client_token = j.value("clientToken", "");

    if (!s.client_token.empty() && s.client_token != client_token)
        throw AuthException("client token changed from " + client_token + " to " + s.client_token);

    if (j.contains("selectedProfile") && j.at("selectedProfile").is_object()) {
        GameProfile gp;
        gp.id = j.at("selectedProfile").value("id", "");
        gp.name = j.at("selectedProfile").value("name", "");
        s.selected_profile = gp;
    }
    if (j.contains("availableProfiles") && j.at("availableProfiles").is_array()) {
        for (const auto& e : j.at("availableProfiles")) {
            if (!e.is_object()) continue;
            GameProfile gp;
            gp.id = e.value("id", "");
            gp.name = e.value("name", "");
            if (!gp.id.empty() && !gp.name.empty())
                s.available_profiles.push_back(std::move(gp));
        }
    }
    // user.properties is an array of {name, value} objects.
    if (j.contains("user") && j.at("user").is_object()) {
        const json& user = j.at("user");
        if (user.contains("properties") && user.at("properties").is_array()) {
            for (const auto& e : user.at("properties")) {
                if (e.is_object() && e.contains("name") && e.contains("value"))
                    s.user_properties[e.at("name").get<std::string>()] = e.at("value").get<std::string>();
            }
        }
    }

    return s;
}

YggdrasilSession YggdrasilService::authenticate(const std::string& username,
                                                 const std::string& password,
                                                 const std::string& client_token) {
    json req = {
        {"agent", {{"name", "Minecraft"}, {"version", 1}}},
        {"username", username},
        {"password", password},
        {"clientToken", client_token},
        {"requestUser", true},
    };
    std::string body = request(provider_.authentication_url(), req.dump());
    return handle_auth_response(body, client_token);
}

YggdrasilSession YggdrasilService::refresh(const std::string& access_token,
                                           const std::string& client_token,
                                           const std::optional<GameProfile>& to_select) {
    json req = {
        {"accessToken", access_token},
        {"clientToken", client_token},
        {"requestUser", true},
    };
    if (to_select) {
        req["selectedProfile"] = {
            {"id", to_select->id},
            {"name", to_select->name},
        };
    }
    std::string body = request(provider_.refreshment_url(), req.dump());
    YggdrasilSession s = handle_auth_response(body, client_token);

    if (to_select &&
        (!s.selected_profile || s.selected_profile->id != to_select->id)) {
        throw ServerResponseMalformedException("failed to select character");
    }
    return s;
}

bool YggdrasilService::validate(const std::string& access_token, const std::string& client_token) {
    json req = {
        {"accessToken", access_token},
        {"clientToken", client_token},
    };
    std::string body = request(provider_.validation_url(), req.dump());
    json j = parse_json(body);
    if (j.contains("error") && !j.at("error").is_null()) {
        std::string name = j.value("error", "");
        if (name == "ForbiddenOperationException")
            return false;
        std::string message = j.value("errorMessage", "");
        std::string cause = j.contains("cause") && j.at("cause").is_string()
            ? j.at("cause").get<std::string>() : "";
        throw RemoteAuthenticationException(name, message, cause);
    }
    return true;
}

void YggdrasilService::invalidate(const std::string& access_token, const std::string& client_token) {
    json req = {
        {"accessToken", access_token},
        {"clientToken", client_token},
    };
    request(provider_.invalidation_url(), req.dump());
}

std::optional<std::map<std::string, std::string>>
YggdrasilService::get_profile_properties(const std::string& uuid) {
    std::string body = request(provider_.profile_url(uuid), {});
    json j = parse_json(body);
    if (j.is_discarded() || !j.is_object())
        return std::nullopt;

    std::map<std::string, std::string> props;
    if (j.contains("properties") && j.at("properties").is_array()) {
        for (const auto& e : j.at("properties")) {
            if (e.is_object() && e.contains("name") && e.contains("value"))
                props[e.at("name").get<std::string>()] = e.at("value").get<std::string>();
        }
    }
    return props;
}

// ---------------------------------------------------------------------------
// YggdrasilAccount
// ---------------------------------------------------------------------------
YggdrasilAccount::YggdrasilAccount(std::string account_id,
                                   std::shared_ptr<YggdrasilService> service,
                                   std::string login_name,
                                   YggdrasilSession session)
    : account_id_(std::move(account_id)),
      service_(std::move(service)),
      login_name_(std::move(login_name)),
      session_(std::move(session)) {
    if (!session_.selected_profile)
        throw std::logic_error("yggdrasil account requires a selected profile");
}

std::string YggdrasilAccount::profile_name() const {
    return session_.selected_profile ? session_.selected_profile->name : "";
}

std::string YggdrasilAccount::profile_id() const {
    return session_.selected_profile ? session_.selected_profile->id : "";
}

AuthInfo YggdrasilAccount::log_in() {
    if (!authenticated_ || !session_.has_profile_name()) {
        if (session_.has_profile_name() &&
            service_->validate(session_.access_token, session_.client_token)) {
            authenticated_ = true;
        } else {
            YggdrasilSession refreshed;
            try {
                refreshed = service_->refresh(session_.access_token,
                                              session_.client_token, std::nullopt);
            } catch (const RemoteAuthenticationException& e) {
                if (e.remote_name() == "ForbiddenOperationException")
                    throw CredentialExpiredException();
                throw;
            }
            if (!refreshed.selected_profile ||
                refreshed.selected_profile->id != session_.selected_profile->id) {
                throw ServerResponseMalformedException("selected profile changed");
            }
            if (!refreshed.has_profile_name())
                throw ServerResponseMalformedException("profile name is missing");
            session_ = std::move(refreshed);
            authenticated_ = true;
        }
    }
    return session_.to_auth_info();
}

AuthInfo YggdrasilAccount::play_offline() {
    if (!session_.has_profile_name())
        throw CredentialExpiredException("profile name is missing");
    return session_.to_auth_info();
}

void YggdrasilAccount::log_in_with_password(const std::string& password) {
    YggdrasilSession acquired = service_->authenticate(login_name_, password, random_uuid(true));

    if (!acquired.selected_profile) {
        // re-select the stored profile from the available list
        for (const auto& p : acquired.available_profiles) {
            if (p.id == session_.selected_profile->id) {
                acquired = service_->refresh(acquired.access_token,
                                             acquired.client_token,
                                             GameProfile{p.id, p.name});
                break;
            }
        }
        if (!acquired.selected_profile ||
            acquired.selected_profile->id != session_.selected_profile->id) {
            throw CharacterDeletedException();
        }
    } else if (acquired.selected_profile->id != session_.selected_profile->id) {
        throw CharacterDeletedException();
    }

    session_ = std::move(acquired);
    authenticated_ = true;
}

} // namespace pl
