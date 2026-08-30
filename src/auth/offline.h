/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * auth/offline.h - offline authentication
 *
 * Mirrors HMCL's OfflineAccount / OfflineAccountFactory. The offline player
 * UUID is a name-based MD5 of "OfflinePlayer:" + username.
 */
#pragma once

#include "auth.h"

#include <string>

namespace pl {

// Derive the offline uuid for a player name (Java UUID.nameUUIDFromBytes of
// "OfflinePlayer:" + name, version 3). Returns a compact (no-dash) uuid.
std::string offline_uuid_for(const std::string& username);

// An offline account. logIn() and playOffline() both return the same AuthInfo
// with a fresh random access token (so the game always has a "valid" token).
class OfflineAccount : public Account {
public:
    OfflineAccount(std::string account_id,
                   std::string profile_name,
                   std::string profile_id);

    std::string account_type() const override { return "offline"; }
    std::string account_id() const override { return account_id_; }
    std::string profile_name() const override { return profile_name_; }
    std::string profile_id() const override { return profile_id_; }

    // Offline login never needs the network.
    AuthInfo log_in() override;
    AuthInfo play_offline() override;

private:
    std::string account_id_;
    std::string profile_name_;
    std::string profile_id_;
};

} // namespace pl
