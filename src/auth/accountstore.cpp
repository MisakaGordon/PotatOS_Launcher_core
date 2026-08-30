/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * auth/accountstore.cpp
 */
#include "accountstore.h"
#include "crypto.h"
#include "../platform.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace pl {

static std::string new_account_id() {
    return "account:" + random_uuid(true);
}

void AccountStore::load(const std::string& path) {
    accounts_.clear();
    auto text = read_small_file(path);
    if (!text)
        return;
    json arr = json::parse(*text, nullptr, false);
    if (arr.is_discarded() || !arr.is_array())
        return;
    for (const auto& rec : arr) {
        auto acc = from_storage(rec);
        if (acc)
            accounts_.push_back(std::move(acc));
    }
}

void AccountStore::save(const std::string& path) const {
    json arr = json::array();
    for (const auto& acc : accounts_) {
        json rec = json::object();
        rec["accountID"] = acc->account_id();
        rec["type"] = acc->account_type();
        rec["profileName"] = acc->profile_name();
        rec["profileID"] = acc->profile_id();

        if (auto* yd = dynamic_cast<YggdrasilAccount*>(acc.get())) {
            rec["loginName"] = yd->login_name();
            rec["clientToken"] = yd->session().client_token;
            rec["accessToken"] = yd->session().access_token;
            rec["authURL"] = yd->service()->provider().auth_url;
            rec["sessionURL"] = yd->service()->provider().session_url;
            if (!yd->session().user_properties.empty()) {
                json props = json::object();
                for (const auto& kv : yd->session().user_properties)
                    props[kv.first] = kv.second;
                rec["userProperties"] = props;
            }
        }
        arr.push_back(std::move(rec));
    }
    write_file(path, arr.dump(2));
}

std::shared_ptr<Account> AccountStore::find(const std::string& account_id) const {
    for (const auto& acc : accounts_)
        if (acc->account_id() == account_id)
            return acc;
    return nullptr;
}

void AccountStore::add(std::shared_ptr<Account> account) {
    accounts_.push_back(std::move(account));
}

void AccountStore::remove(const std::string& account_id) {
    for (auto it = accounts_.begin(); it != accounts_.end(); ++it) {
        if ((*it)->account_id() == account_id) {
            accounts_.erase(it);
            return;
        }
    }
}

std::shared_ptr<OfflineAccount> AccountStore::create_offline(const std::string& username,
                                                             const std::string& uuid) {
    std::string id = uuid.empty() ? offline_uuid_for(username) : uuid;
    return std::make_shared<OfflineAccount>(new_account_id(), username, id);
}

std::shared_ptr<YggdrasilService> AccountStore::service_for(const YggdrasilProvider& provider) {
    std::string key = provider.auth_url + "|" + provider.session_url;
    for (const auto& kv : services_)
        if (kv.first == key)
            return kv.second;
    auto svc = std::make_shared<YggdrasilService>(provider);
    services_.emplace_back(key, svc);
    return svc;
}

std::shared_ptr<YggdrasilAccount> AccountStore::create_yggdrasil(const YggdrasilProvider& provider,
                                                                 const std::string& username,
                                                                 const std::string& password) {
    auto service = service_for(provider);
    YggdrasilSession session =
        service->authenticate(username, password, random_uuid(true));
    if (!session.selected_profile) {
        if (session.available_profiles.empty())
            throw NoCharacterException();
        // pick the first available character (HMCL lets the user choose)
        session = service->refresh(session.access_token, session.client_token,
                                   session.available_profiles.front());
    }
    if (!session.selected_profile)
        throw NoCharacterException();

    auto acc = std::make_shared<YggdrasilAccount>(new_account_id(), service,
                                                  username, session);
    acc->mark_authenticated();
    return acc;
}

std::shared_ptr<Account> AccountStore::from_storage(const json& record) {
    if (!record.is_object())
        return nullptr;
    std::string type = record.value("type", "");
    std::string account_id = record.value("accountID", "");
    std::string profile_name = record.value("profileName", "");
    std::string profile_id = record.value("profileID", "");
    if (account_id.empty() || profile_name.empty() || profile_id.empty())
        return nullptr;

    if (type == "offline") {
        return std::make_shared<OfflineAccount>(account_id, profile_name, profile_id);
    }

    if (type == "yggdrasil") {
        std::string client_token = record.value("clientToken", "");
        std::string access_token = record.value("accessToken", "");
        if (client_token.empty() || access_token.empty())
            return nullptr;
        YggdrasilProvider provider;
        provider.auth_url = record.value("authURL", "https://authserver.mojang.com");
        provider.session_url = record.value("sessionURL", "https://sessionserver.mojang.com");

        YggdrasilSession session;
        session.client_token = client_token;
        session.access_token = access_token;
        session.selected_profile = GameProfile{profile_id, profile_name};
        if (record.contains("userProperties") && record.at("userProperties").is_object()) {
            for (auto it = record.at("userProperties").begin();
                 it != record.at("userProperties").end(); ++it)
                session.user_properties[it.key()] = it.value().get<std::string>();
        }
        auto service = service_for(provider);
        return std::make_shared<YggdrasilAccount>(account_id, service,
                                                  record.value("loginName", profile_name),
                                                  session);
    }
    return nullptr;
}

} // namespace pl
