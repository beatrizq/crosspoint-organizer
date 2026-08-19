#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

#include "YnabAccount.h"
#include "YnabCategory.h"

/**
 * Singleton holding the YNAB credentials and category selection.
 *
 * YNAB is reached with a Personal Access Token the owner generates in their own
 * account (Account Settings -> Developer Settings), so there is no OAuth dance
 * and nothing to refresh: one token, entered once, used as a bearer credential
 * on every request. It is XOR-obfuscated with the device's hardware MAC and
 * base64-encoded before it is written, the same scheme as the Todoist, Google,
 * KOReader and OPDS credentials: not cryptographically secure, but it keeps a
 * long-lived token out of plain sight on a card that gets mounted on a PC, and
 * it cannot be decoded on another chip.
 *
 * The plan (budget) id is not a secret and is stored in the clear, so a
 * mistyped id can be corrected from a PC without retyping the token.
 */
/**
 * A short label the user typed for one account.
 *
 * YNAB has no short-name field - an account is named whatever the owner typed in
 * the app, which is routinely too long for a tab ("Barclays Current Account") -
 * so the label is entered on the device and stored against the account id.
 */
struct AccountNickname {
  std::string id;
  std::string nickname;
};

class YnabStore : public PersistableStore<YnabStore> {
 private:
  std::string accessToken;
  // The plan id, called the budget id in the YNAB app and in the older API
  // paths. A UUID, or the literal "last-used" for the most recently used plan.
  std::string budgetId;
  // Category ids the user ticked. Empty means "not chosen yet"; the sync treats
  // that as nothing to fetch rather than silently showing every category.
  std::vector<std::string> selectedCategories;
  // Short labels for the Budget screen's account tabs, keyed by account id. User
  // config, so they live here beside the credentials rather than in the account
  // cache: a cache clear must not lose a label the user typed.
  std::vector<AccountNickname> accountNicknames;

  YnabStore() = default;
  ~YnabStore() = default;

  friend class PersistableStore<YnabStore>;

 public:
  // Personal access tokens are 64 hex characters; the cap leaves room for the
  // longer OAuth access tokens without letting a corrupt file allocate freely.
  static constexpr size_t MAX_TOKEN_LEN = 128;
  // A UUID is 36 characters; "last-used" and "default" are shorter.
  static constexpr size_t MAX_BUDGET_ID_LEN = 48;
  static constexpr size_t MAX_CATEGORIES = YNAB_MAX_CATEGORIES;
  static constexpr size_t MAX_CATEGORY_ID_LEN = YNAB_CATEGORY_ID_MAX_LEN;

  static const char* getFilePath() { return "/.crosspoint/ynab.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  void setAccessToken(const std::string& value);
  const std::string& getAccessToken() const { return accessToken; }
  bool hasToken() const { return !accessToken.empty(); }

  void setBudgetId(const std::string& value);
  const std::string& getBudgetId() const { return budgetId; }
  bool hasBudgetId() const { return !budgetId.empty(); }

  // Both halves are needed before anything can be fetched.
  bool isConfigured() const { return hasToken() && hasBudgetId(); }

  // Drops the token and the category selection, keeping the plan id: clearing a
  // token is about the credential, and the id is not one.
  void clearToken();

  const std::vector<std::string>& getSelectedCategories() const { return selectedCategories; }
  bool isCategorySelected(const std::string& id) const;
  // Adds or removes the id, capped at MAX_CATEGORIES. No-op past the cap.
  void toggleCategory(const std::string& id);
  void clearCategories() { selectedCategories.clear(); }

  static constexpr size_t MAX_ACCOUNT_ID_LEN = YNAB_ACCOUNT_ID_MAX_LEN;
  static constexpr size_t MAX_NICKNAME_LEN = YNAB_ACCOUNT_NICKNAME_MAX_LEN;

  // The label typed for this account, or "" when none was. Callers fall back to
  // the account's own name.
  const std::string& getAccountNickname(const std::string& id) const;
  // Sets or, with an empty value, clears the label. Capped at MAX_ACCOUNTS, which
  // is also what the tab bar and the cache hold.
  void setAccountNickname(const std::string& id, const std::string& nickname);
};

#define YNAB_STORE YnabStore::getInstance()
