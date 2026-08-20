#pragma once
#include <string>

#include "YnabAccount.h"
#include "YnabStore.h"

/**
 * The short label an account is shown under.
 *
 * Shared by the Budget screen, which draws it on a tab, and the accounts screen,
 * which lets you edit it - so the two cannot disagree about what a tab will say.
 *
 * YNAB has no short-name field: an account is called whatever the owner typed in
 * the app, and "Barclays Current Account" is several times wider than a tab. The
 * label is therefore entered on the device and stored against the account id. The
 * fallback until one is typed is the first word of the account name, which for a
 * bank account is usually the bank - a guess, which is why it is editable.
 *
 * Returns by value rather than by reference: the fallback is a substring that has
 * to outlive the call, and an account name is at most YnabAccount::NAME_MAX_LEN.
 */
inline std::string ynabAccountLabel(const YnabAccount& account) {
  const std::string& nickname = YNAB_STORE.getAccountNickname(account.id);
  if (!nickname.empty()) return nickname;

  const size_t space = account.name.find(' ');
  if (space == std::string::npos) return account.name;
  return account.name.substr(0, space);
}
