#pragma once
#include <ArduinoJson.h>
#include <CivilTime.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

#include "YnabAccount.h"

/**
 * Singleton holding the accounts the Budget screen tabs across, and the
 * transactions behind each.
 *
 * Persisted for the same reason the balances are: the account tabs render
 * straight from here, so opening the screen needs no Wi-Fi. A sync is an
 * explicit user action, and the cached transactions stay readable in between.
 * Nothing is ever pushed back - the device does not spend money.
 *
 * The account list itself comes from the picker in Settings, not from the Budget
 * screen: which accounts exist is a setup question, and fetching the list on
 * every tab sync would spend one of the 200 requests an hour the token is allowed
 * on something that changes once a year.
 */
class YnabAccountCache : public PersistableStore<YnabAccountCache> {
 private:
  std::vector<YnabAccount> accounts;

  YnabAccountCache() = default;
  ~YnabAccountCache() = default;

  friend class PersistableStore<YnabAccountCache>;

 public:
  static constexpr size_t MAX_ACCOUNTS = YNAB_MAX_ACCOUNTS;
  static constexpr size_t MAX_TRANSACTIONS = YNAB_MAX_TRANSACTIONS;

  static const char* getFilePath() { return "/.crosspoint/ynab_accounts.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  const std::vector<YnabAccount>& getAccounts() const { return accounts; }
  bool hasAccounts() const { return !accounts.empty(); }

  /**
   * Replaces the account list after a successful fetch.
   *
   * Transactions already held for an account that survives the fetch are kept:
   * the list is refetched whenever the picker is visited, and dropping a month of
   * transactions because the user opened Settings would empty every tab.
   */
  void setAccounts(std::vector<YnabAccount>&& fetched);

  /**
   * Replaces one account's transactions, newest first. No-op for an unknown id,
   * which is what a fetch for an account since removed from the plan looks like.
   *
   * `date` is the day the fetch happened, for the tab's header.
   */
  void setTransactions(const std::string& accountId, std::vector<YnabTransaction>&& fetched, uint16_t date);

  // The account at `index`, or nullptr when out of range.
  const YnabAccount* accountAt(size_t index) const;

  void clear() { accounts.clear(); }
};

#define YNAB_ACCOUNTS YnabAccountCache::getInstance()
