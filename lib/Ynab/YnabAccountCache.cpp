#include "YnabAccountCache.h"

#include <Logging.h>

#include <algorithm>
#include <utility>

void YnabAccountCache::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["accounts"].to<JsonArray>();
  for (const auto& account : accounts) {
    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = account.id;
    obj["name"] = account.name;
    obj["balance"] = account.balance;

    char iso[11];
    civil::isoFromDate(account.transactionsSyncDate, iso, sizeof(iso));
    obj["txDate"] = iso;

    JsonArray txs = obj["tx"].to<JsonArray>();
    for (const auto& transaction : account.transactions) {
      JsonObject tx = txs.add<JsonObject>();
      tx["payee"] = transaction.payee;
      tx["amount"] = transaction.amount;
      char txIso[11];
      civil::isoFromDate(transaction.date, txIso, sizeof(txIso));
      tx["date"] = txIso;
    }
  }
}

bool YnabAccountCache::fromJson(JsonVariantConst doc) {
  accounts.clear();

  JsonArrayConst arr = doc["accounts"].as<JsonArrayConst>();
  accounts.reserve(std::min(arr.size(), MAX_ACCOUNTS));
  for (JsonObjectConst obj : arr) {
    if (accounts.size() >= MAX_ACCOUNTS) break;
    YnabAccount account;
    account.id = obj["id"] | "";
    // An account with no id cannot be matched against a nickname or refetched,
    // so it is not an account as far as this screen is concerned.
    if (account.id.empty()) continue;
    account.name = obj["name"] | "";
    account.balance = obj["balance"] | "";
    account.transactionsSyncDate = civil::dateFromIso(obj["txDate"] | "");

    JsonArrayConst txs = obj["tx"].as<JsonArrayConst>();
    account.transactions.reserve(std::min(txs.size(), MAX_TRANSACTIONS));
    for (JsonObjectConst tx : txs) {
      if (account.transactions.size() >= MAX_TRANSACTIONS) break;
      YnabTransaction transaction;
      transaction.payee = tx["payee"] | "";
      transaction.amount = tx["amount"] | "";
      transaction.date = civil::dateFromIso(tx["date"] | "");
      account.transactions.push_back(std::move(transaction));
    }
    accounts.push_back(std::move(account));
  }

  LOG_DBG("YAC", "Loaded %zu accounts", accounts.size());
  return true;
}

void YnabAccountCache::setAccounts(std::vector<YnabAccount>&& fetched) {
  if (fetched.size() > MAX_ACCOUNTS) fetched.resize(MAX_ACCOUNTS);

  // Transactions are carried across by id. The fetch only knows the account's
  // identity and balance - it does not ask for transactions - so taking its
  // result wholesale would blank every tab each time the picker was opened.
  for (auto& account : fetched) {
    const auto previous = std::find_if(accounts.begin(), accounts.end(),
                                       [&account](const YnabAccount& held) { return held.id == account.id; });
    if (previous == accounts.end()) continue;
    account.transactions = std::move(previous->transactions);
    account.transactionsSyncDate = previous->transactionsSyncDate;
  }

  accounts = std::move(fetched);
}

void YnabAccountCache::setTransactions(const std::string& accountId, std::vector<YnabTransaction>&& fetched,
                                       const uint16_t date) {
  const auto found = std::find_if(accounts.begin(), accounts.end(),
                                  [&accountId](const YnabAccount& held) { return held.id == accountId; });
  if (found == accounts.end()) {
    LOG_ERR("YAC", "No such account %s; transactions dropped", accountId.c_str());
    return;
  }

  // Newest first, which is the order the tab reads in. Stable, so the API's own
  // ordering survives within a day - YNAB returns oldest-first and carries no
  // time of day, so within one date its sequence is all there is to go on.
  std::stable_sort(fetched.begin(), fetched.end(),
                   [](const YnabTransaction& a, const YnabTransaction& b) { return a.date > b.date; });
  if (fetched.size() > MAX_TRANSACTIONS) fetched.resize(MAX_TRANSACTIONS);

  found->transactions = std::move(fetched);
  if (date != civil::NO_DATE) found->transactionsSyncDate = date;
}

const YnabAccount* YnabAccountCache::accountAt(const size_t index) const {
  return index < accounts.size() ? &accounts[index] : nullptr;
}

size_t YnabAccountCache::getTodayTransactionCount() const {
  size_t total = 0;
  for (const auto& account : accounts) {
    if (account.transactionsSyncDate == civil::NO_DATE) continue;
    total += static_cast<size_t>(
        std::count_if(account.transactions.begin(), account.transactions.end(),
                      [&account](const YnabTransaction& t) { return t.date == account.transactionsSyncDate; }));
  }
  return total;
}
