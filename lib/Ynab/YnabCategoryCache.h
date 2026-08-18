#pragma once
#include <ArduinoJson.h>
#include <CivilTime.h>
#include <PersistableStore.h>

#include <vector>

#include "YnabCategory.h"

/**
 * Singleton holding the balances from the last sync.
 *
 * The Budget tab renders straight from here, so opening it needs no Wi-Fi: a
 * sync is an explicit user action, and the cached balances stay readable in
 * between. Nothing is ever pushed back - the device does not spend money.
 */
class YnabCategoryCache : public PersistableStore<YnabCategoryCache> {
 private:
  std::vector<YnabCategory> categories;  // In the plan's own order
  uint16_t syncMonth = civil::NO_DATE;   // First of the month the amounts are for

  YnabCategoryCache() = default;
  ~YnabCategoryCache() = default;

  friend class PersistableStore<YnabCategoryCache>;

 public:
  static constexpr size_t MAX_CATEGORIES = YNAB_MAX_CATEGORIES;

  static const char* getFilePath() { return "/.crosspoint/ynab_categories.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  const std::vector<YnabCategory>& getCategories() const { return categories; }
  uint16_t getSyncMonth() const { return syncMonth; }
  bool hasSynced() const { return syncMonth != civil::NO_DATE; }

  /**
   * Replaces the list after a successful sync.
   *
   * `month` is the month the balances belong to. NO_DATE leaves the stored
   * month untouched, so a sync whose response carried no month keeps showing
   * the last one that did.
   */
  void setCategories(std::vector<YnabCategory>&& fetched, uint16_t month);

  void clear();
};

#define YNAB_CATEGORIES YnabCategoryCache::getInstance()
