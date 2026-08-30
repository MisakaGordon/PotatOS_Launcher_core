/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * auth/offline.cpp
 */
#include "offline.h"
#include "crypto.h"

namespace pl {

std::string offline_uuid_for(const std::string& username) {
    return offline_uuid(username);
}

OfflineAccount::OfflineAccount(std::string account_id,
                               std::string profile_name,
                               std::string profile_id)
    : account_id_(std::move(account_id)),
      profile_name_(std::move(profile_name)),
      profile_id_(std::move(profile_id)) {
    if (profile_name_.empty())
        throw std::invalid_argument("offline profile name cannot be blank");
}

AuthInfo OfflineAccount::log_in() {
    // The "msa" user type avoids "invalid session" errors when joining
    // offline-mode servers (mirrors HMCL's OfflineAccount.logInWithoutSkin).
    AuthInfo info;
    info.username = profile_name_;
    info.uuid = profile_id_;
    info.access_token = random_uuid(true);
    info.user_type = AuthInfo::USER_TYPE_MSA;
    info.user_properties = "{}";
    return info;
}

AuthInfo OfflineAccount::play_offline() {
    return log_in();
}

} // namespace pl
