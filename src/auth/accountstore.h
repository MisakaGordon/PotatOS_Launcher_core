/*
 * potato's launcher - a minimal Minecraft launcher in C++
 * auth/accountstore.h - persistent account storage
 *
 * Mirrors HMCL's accounts.json handling (simplified: a single JSON array;
 * credentials and cached data live in the same record for this learning
 * implementation instead of a separate private store).
 */
#pragma once

#include "auth.h"
#include "offline.h"
#include "yggdrasil.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace pl {

class AccountStore {
public:
    // Load accounts from `path` (silently ignoring malformed entries).
    void load(const std::string& path);
    // Write all accounts to `path`.
    void save(const std::string& path) const;

    const std::vector<std::shared_ptr<Account>>& accounts() const { return accounts_; }
    std::shared_ptr<Account> find(const std::string& account_id) const;
    void add(std::shared_ptr<Account> account);
    void remove(const std::string& account_id);

    // Create a new offline account (uuid derived from name when empty).
    std::shared_ptr<OfflineAccount> create_offline(const std::string& username,
                                                   const std::string& uuid = "");

    // Create and authenticate a yggdrasil account with username+password.
    // Throws AuthException subclasses on failure.
    std::shared_ptr<YggdrasilAccount> create_yggdrasil(const YggdrasilProvider& provider,
                                                       const std::string& username,
                                                       const std::string& password);

    // Get a service bound to the given provider (cached per provider pair).
    std::shared_ptr<YggdrasilService> service_for(const YggdrasilProvider& provider);

    // Rebuild an account from a stored record; returns null for bad records.
    std::shared_ptr<Account> from_storage(const nlohmann::json& record);

private:
    std::vector<std::shared_ptr<Account>> accounts_;
    // provider cache: "auth_url|session_url" -> service
    std::vector<std::pair<std::string, std::shared_ptr<YggdrasilService>>> services_;
};

} // namespace pl
