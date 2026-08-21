#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Utf8.h>
#include <Xtc.h>

#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "companion/CompanionRenderer.h"
#include "companion/CompanionState.h"
#include "companion/CompanionTracker.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HomeAppOrder.h"

namespace {
// The home entry each app opens. Kept here rather than in the app table so
// util/HomeAppOrder.h does not have to include the activity layer.
HomeMenuItem homeMenuItemFor(const homeAppOrder::AppId id) {
  switch (id) {
    case homeAppOrder::AppId::Read:
      return HomeMenuItem::READ_MENU;
    case homeAppOrder::AppId::Tasks:
      return HomeMenuItem::TASKS;
    case homeAppOrder::AppId::Calendar:
      return HomeMenuItem::CALENDAR;
    case homeAppOrder::AppId::Budget:
      return HomeMenuItem::BUDGET;
    case homeAppOrder::AppId::Habits:
      return HomeMenuItem::HABITS;
  }
  return HomeMenuItem::NONE;
}
}  // namespace

int HomeActivity::getMenuItemCount() const { return static_cast<int>(entries.size()); }

int HomeActivity::leadingRecentCount() const {
  return UITheme::getInstance().getMetrics().homeContinueReadingInMenu ? 0 : static_cast<int>(recentBooks.size());
}

void HomeActivity::buildEntries() {
  entries.clear();
  entries.reserve(8);

  const auto& metrics = UITheme::getInstance().getMetrics();
  if (!metrics.homeContinueReadingInMenu) {
    // Cover-tile themes: the recent books own the leading slots and are drawn
    // by the tile rather than the menu, but they are still entries, so one
    // list describes the whole screen.
    for (int i = 0; i < static_cast<int>(recentBooks.size()); i++) {
      entries.push_back({nullptr, Book, HomeMenuItem::NONE, i});
    }
  }

  // Themes without a cover tile carry reading itself as the leading entry, which
  // stands in for the Read tile rather than sitting beside it.
  const bool resumeLeads = metrics.homeContinueReadingInMenu && !recentBooks.empty();
  if (resumeLeads) {
    entries.push_back({tr(STR_MENU_READ), Book, HomeMenuItem::NONE, 0});
  }

  // The tiles, in the order the user arranged them on the App Order screen. Read
  // is one of them, so it moves with the rest - the cover card above opens the
  // last book, and the Read tile opens everything else about books, which is a
  // subject like any other.
  int order[homeAppOrder::APP_COUNT];
  homeAppOrder::parse(SETTINGS.homeAppOrder, order);
  for (const int index : order) {
    const auto& app = homeAppOrder::appAt(index);
    // Skipped only when the resume entry above already speaks for reading;
    // adding it again would draw it twice.
    if (resumeLeads && app.id == homeAppOrder::AppId::Read) continue;
    entries.push_back({homeAppOrder::displayName(app.id), app.icon, homeMenuItemFor(app.id), -1});
  }
}

void HomeActivity::drawCompanion(const Rect region) const {
  if (!SETTINGS.companionEnabled || !SETTINGS.companionOnHome) return;
  if (region.width <= 0 || region.height <= 0) return;

  // Ported from JoshuaMillerCode/crosspoint-reader-companion's
  // drawCompanionColumn. Only the column form is kept: on this home screen the
  // companion always gets the tall gap beside the cover, never a strip under a
  // menu, so the fork's compact and side-by-side fallbacks have nothing to pick
  // between.
  constexpr int PAD = 6;  // bubble inner padding
  constexpr int TAIL_LENGTH = 12;
  constexpr int BUBBLE_GAP = 2;  // between the tail tip and the character's head
  constexpr int LABEL_GAP = 2;
  constexpr int SUBLABEL_GAP = 0;
  // getTextHeight() reports the ascender only, but drawText() takes y as the top
  // and descenders hang below it.
  constexpr int DESCENDER_ALLOWANCE = 3;
  constexpr int MARGIN = 4;
  constexpr int WALK_STEPS = 6;
  // Kept to roughly the tail's reach: the character has to pace clear of the
  // bubble's tail.
  constexpr int WALK_TRAVEL = 14;
  constexpr int BOB_HEIGHT = 3;
  constexpr int MAX_SCALE = 4;
  constexpr int MIN_BUBBLE_W = 90;

  const int colX = region.x + MARGIN;
  const int colW = region.width - MARGIN * 2;
  const int colTop = region.y + MARGIN;
  const int colH = region.height - MARGIN * 2;
  if (colW < MIN_BUBBLE_W) return;

  const int labelH = renderer.getTextHeight(UI_10_FONT_ID) + DESCENDER_ALLOWANCE;
  const int subH = renderer.getTextHeight(SMALL_FONT_ID) + DESCENDER_ALLOWANCE;
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int textW = colW - PAD * 2;

  // The mood label, and under it the line answering "why?" and "what next?". A
  // reachable target beats a tally, so progress toward Thriving wins when there
  // is progress to report.
  const auto id = CompanionTracker::activeId();
  const auto mood = COMPANION.currentMood();
  const char* label = companion::moodLabel(mood);
  char sub[40] = "";
  const uint16_t minutes = COMPANION.minutesToday();
  const companion::MoodThresholds thresholds;
  if (minutes >= thresholds.contentMinutes && minutes < thresholds.thrivingMinutes) {
    snprintf(sub, sizeof(sub), tr(STR_COMPANION_TO_THRIVING_FORMAT), thresholds.thrivingMinutes - minutes);
  } else if (COMPANION.hasValidClock() && COMPANION_STATE.ledger.streakDays > 0) {
    snprintf(sub, sizeof(sub), tr(STR_COMPANION_STREAK_FORMAT), COMPANION_STATE.ledger.streakDays);
  }

  const char* quote = COMPANION_STATE.milestonePending ? companion::milestoneQuoteFor(id, companionQuoteIndex)
                                                       : companion::quoteFor(id, mood, companionQuoteIndex);

  // The bubble is measured before the character is sized: the quote needs however
  // many lines it needs, and the character takes what is left.
  std::vector<std::string> lines;
  if (quote != nullptr) {
    if (renderer.getTextWidth(UI_10_FONT_ID, quote) <= textW) {
      lines.emplace_back(quote);
    } else {
      lines = renderer.wrappedText(UI_10_FONT_ID, quote, textW, 3);
    }
  }
  const int bubbleH = lines.empty() ? 0 : static_cast<int>(lines.size()) * lineH + PAD * 2;
  const int bubbleBlock = lines.empty() ? 0 : bubbleH + TAIL_LENGTH + BUBBLE_GAP;
  const int statusBlock = LABEL_GAP + labelH + SUBLABEL_GAP + (sub[0] != '\0' ? subH : 0);

  // Whole-pixel scales only: fractional scaling would smear the baked dither.
  int scale = 0;
  for (int candidate = MAX_SCALE; candidate >= 1; candidate--) {
    if (companion::poseWidth(candidate) + WALK_TRAVEL > colW) continue;
    if (bubbleBlock + companion::poseHeight(candidate) + BOB_HEIGHT + statusBlock <= colH) {
      scale = candidate;
      break;
    }
  }
  // Nothing fits: better an empty column than a clipped character.
  if (scale == 0) return;

  const int spriteW = companion::poseWidth(scale);
  const int spriteH = companion::poseHeight(scale);
  const int blockH = bubbleBlock + spriteH + BOB_HEIGHT + statusBlock;
  const int blockTop = colTop + (colH - blockH) / 2;

  // Bubble across the column with its tail pointing down at the character below,
  // so nothing reaches sideways toward the cover.
  if (!lines.empty()) {
    companion::drawSpeechBubble(renderer, colX, blockTop, colW, bubbleH, TAIL_LENGTH, companion::TailSide::Bottom);
    const Rect textBounds{colX + PAD, blockTop, textW, bubbleH};
    int textY = blockTop + PAD;
    for (const auto& line : lines) {
      UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, textY, line.c_str());
      textY += lineH;
    }
  }

  const uint32_t phase = companionFrame % (WALK_STEPS * 2);
  const bool walkingBack = phase >= WALK_STEPS;
  const uint32_t step = walkingBack ? (WALK_STEPS * 2 - 1 - phase) : phase;
  const int walkX = static_cast<int>(step) * WALK_TRAVEL / (WALK_STEPS - 1);
  const int bob = (companionFrame % 2) ? BOB_HEIGHT : 0;

  // A neglected companion stops pacing, which is most of what says so.
  const bool restless = mood != companion::Mood::Neglected;
  // Centred on the range it walks rather than on its own width, so it does not
  // appear to drift.
  const int laneX = colX + (colW - spriteW - WALK_TRAVEL) / 2;
  const int spriteTop = blockTop + bubbleBlock;
  companion::drawPose(renderer, id, mood, laneX + (restless ? walkX : WALK_TRAVEL / 2),
                      restless ? spriteTop + bob : spriteTop, scale, restless && walkingBack);

  const int labelW = renderer.getTextWidth(UI_10_FONT_ID, label, EpdFontFamily::BOLD);
  const int subW = sub[0] != '\0' ? renderer.getTextWidth(SMALL_FONT_ID, sub) : 0;
  const int labelY = spriteTop + spriteH + BOB_HEIGHT + LABEL_GAP;
  const int centreX = colX + colW / 2;
  renderer.drawText(UI_10_FONT_ID, centreX - labelW / 2, labelY, label, true, EpdFontFamily::BOLD);
  if (subW > 0) {
    renderer.drawText(SMALL_FONT_ID, centreX - subW / 2, labelY + labelH + SUBLABEL_GAP, sub);
  }
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (RecentBooksStore::isMissing(book)) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // If epub, try to load the metadata for title/author and cover
        if (FsHelpers::hasEpubExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  hasOpdsServers = OPDS_STORE.hasServers();

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);

  buildEntries();

  // One I2C read to resolve the calendar day, so currentMood() is cheap from the
  // render path. Here rather than in render() for exactly that reason.
  COMPANION.refreshForDisplay();
  // A different line each visit, stable while the cursor moves around the menu.
  const uint8_t quoteCount = companion::quoteCountFor(CompanionTracker::activeId(), COMPANION.currentMood());
  companionQuoteIndex = quoteCount > 0 ? (companionQuoteIndex + 1) % quoteCount : 0;

  selectorIndex = 0;
  if (initialMenuItem != HomeMenuItem::NONE) {
    for (int i = 0; i < static_cast<int>(entries.size()); i++) {
      if (entries[i].item == initialMenuItem) {
        selectorIndex = i;
        break;
      }
    }
  }

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  // render() must have already set the cover rect; without it we'd be back to
  // cloning the whole framebuffer.
  if (coverRectW <= 0 || coverRectH <= 0) return false;
  freeCoverBuffer();
  const size_t needed = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (needed == 0) return false;
  coverBuffer = static_cast<uint8_t*>(malloc(needed));
  if (!coverBuffer) {
    LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", (unsigned)needed);
    return false;
  }
  coverBufferSize = needed;
  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize)) {
    free(coverBuffer);
    coverBuffer = nullptr;
    coverBufferSize = 0;
    return false;
  }
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer || coverRectW <= 0 || coverRectH <= 0) return false;
  return renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize);
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferSize = 0;
  coverBufferStored = false;
}

void HomeActivity::loop() {
  const int menuCount = getMenuItemCount();
  const auto& metrics = UITheme::getInstance().getMetrics();

  auto activateSelection = [this] {
    if (selectorIndex < 0 || selectorIndex >= static_cast<int>(entries.size())) return;
    const HomeEntry& entry = entries[selectorIndex];

    if (entry.recentIndex >= 0 && entry.recentIndex < static_cast<int>(recentBooks.size())) {
      onSelectBook(recentBooks[entry.recentIndex].path);
      return;
    }
    switch (entry.item) {
      case HomeMenuItem::READ_MENU:
        activityManager.goToReadMenu();
        break;
      case HomeMenuItem::TASKS:
        activityManager.goToTasks();
        break;
      case HomeMenuItem::CALENDAR:
        activityManager.goToCalendar();
        break;
      case HomeMenuItem::BUDGET:
        activityManager.goToBudget();
        break;
      case HomeMenuItem::HABITS:
        activityManager.goToHabits();
        break;
      case HomeMenuItem::FILE_BROWSER:
        onFileBrowserOpen();
        break;
      case HomeMenuItem::RECENTS:
        onRecentsOpen();
        break;
      case HomeMenuItem::OPDS_BROWSER:
        onOpdsBrowserOpen();
        break;
      case HomeMenuItem::FILE_TRANSFER:
        onFileTransferOpen();
        break;
      default:
        break;
    }
  };

  buttonNavigator.onNext([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) backPressSeen = true;

  // Back is otherwise unused on the home menu, and Settings no longer has a row
  // of its own, so it lives here. Resuming moved to the Read entry, which is a
  // press away in the menu itself. backPressSeen guards against the stale
  // release of the Back press that closed the previous activity.
  //
  // Held, the same button syncs every configured integration instead. It goes
  // here rather than on a tile of its own because it is an action on all of them
  // at once, so no single app owns it - and because holding a button for "the
  // heavier version of this" is the convention the organizer tab bars already
  // use for their own syncs.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && backPressSeen) {
    if (mappedInput.getHeldTime() >= SYNC_ALL_HOLD_MS) {
      activityManager.goToSyncAll();
    } else {
      onSettingsOpen();
    }
    return;
  }

  int tx = 0;
  int ty = 0;
  if (!recentBooks.empty() && mappedInput.wasScreenTouchDown(tx, ty) && tx >= 0 && tx < renderer.getScreenWidth() &&
      ty >= metrics.homeTopPadding && ty < metrics.homeTopPadding + metrics.homeCoverTileHeight) {
    if (selectorIndex != 0) {
      selectorIndex = 0;
      requestUpdate();
    }
    return;
  }

  if (!recentBooks.empty() &&
      mappedInput.wasTapInRect(0, metrics.homeTopPadding, renderer.getScreenWidth(), metrics.homeCoverTileHeight)) {
    selectorIndex = 0;
    activateSelection();
    return;
  }

  const int menuTop = metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset;
  const int leadingRecents = leadingRecentCount();
  const int renderedMenuCount = menuCount - leadingRecents;

  // Down highlights the entry under the finger, a tap opens it. Shared by both
  // layouts so the grid and the list behave identically to the touch.
  auto handleMenuTouch = [this, leadingRecents, &activateSelection](MappedInputManager::RowTouch touch,
                                                                    int renderedIndex) {
    const int touchedIndex = renderedIndex + leadingRecents;
    if (touch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != touchedIndex) {
        selectorIndex = touchedIndex;
        requestUpdate();
      }
      return;
    }
    selectorIndex = touchedIndex;
    activateSelection();
  };

  if (metrics.homeGridColumns > 0) {
    // Tiles: one hit-test per column band, since the shared helper walks rows.
    const int columns = metrics.homeGridColumns;
    const int tileWidth = renderer.getScreenWidth() / columns;
    const int gridRows = (renderedMenuCount + columns - 1) / columns;
    const int menuHeight = renderer.getScreenHeight() - menuTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
    // The same step the theme drew with: the rows share whatever height is left.
    const int tileStep = GUI.getGridRowStep(menuHeight, renderedMenuCount);
    for (int column = 0; column < columns; column++) {
      int gridRow = -1;
      const auto tileTouch = mappedInput.rowTouch(gridRow, menuTop, tileStep, gridRows, column * tileWidth,
                                                  (column + 1) * tileWidth, tileStep);
      if (tileTouch == MappedInputManager::RowTouch::None) continue;
      const int renderedIndex = gridRow * columns + column;
      // The last row can be short; its empty cells are not entries.
      if (renderedIndex >= renderedMenuCount) return;
      handleMenuTouch(tileTouch, renderedIndex);
      return;
    }
  } else {
    int menuRow = -1;
    const auto menuTouch = mappedInput.rowTouch(menuRow, menuTop, metrics.menuRowHeight + metrics.menuSpacing,
                                                renderedMenuCount, 0, INT32_MAX, metrics.menuRowHeight);
    if (menuTouch != MappedInputManager::RowTouch::None) {
      handleMenuTouch(menuTouch, menuRow);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding},
                 metrics.homeContinueReadingInMenu && !recentBooks.empty() ? recentBooks[0].title.c_str() : nullptr);

  // Record the tile rect so storeCoverBuffer (called from the theme) knows
  // which sub-region of the framebuffer to snapshot. ~16 KB in Portrait
  // instead of the 48 KB full framebuffer the previous bind captured.
  coverRectX = 0;
  coverRectY = metrics.homeTopPadding;
  coverRectW = pageWidth;
  coverRectH = metrics.homeCoverTileHeight;

  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this));

  // After the card, deliberately: storeCoverBuffer() runs inside that call, so
  // the cached snapshot holds the cover alone. Restoring it each paint is what
  // erases the previous companion frame before this one is drawn.
  drawCompanion(GUI.getHomeCompanionRect(Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight}));
  companionFrame++;

  // A beaten personal best takes over the bubble once. Cleared after the paint
  // that showed it, so a repaint mid-visit cannot swallow it unseen.
  if (SETTINGS.companionEnabled && COMPANION_STATE.milestonePending) {
    COMPANION_STATE.milestonePending = false;
    COMPANION_STATE.saveToFile();
  }

  // The menu draws the entries the cover tile does not own.
  const int leadingRecents = leadingRecentCount();
  const int renderedCount = static_cast<int>(entries.size()) - leadingRecents;
  const auto& rows = entries;

  // The cover card's height was missing from this sum, so the menu was handed a
  // taller rect than the screen has under the card - the tiles bunched at the
  // top of it with a phantom row's worth of space below.
  const int menuTop = metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset;
  const int menuHeight = pageHeight - menuTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  GUI.drawButtonGrid(
      renderer, Rect{0, menuTop, pageWidth, menuHeight}, renderedCount, selectorIndex - leadingRecents,
      [&rows, leadingRecents](int index) {
        const char* label = rows[index + leadingRecents].label;
        return std::string(label != nullptr ? label : "");
      },
      [&rows, leadingRecents](int index) { return rows[index + leadingRecents].icon; });

  const auto labels = mappedInput.mapLabels(tr(STR_SETTINGS_TITLE), tr(STR_SELECT), tr(STR_DIR_PREV), tr(STR_DIR_NEXT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }
