

#pragma once

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

namespace Lyra3CoversMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = LyraMetrics::values;
  v.homeCoverTileHeight = 300;
  v.homeRecentBooksCount = 3;
  // No height for a grid under 300px of cover, so this variant keeps rows.
  v.homeGridColumns = 0;
  // Unlike plain LyraTheme, this variant's own drawRecentBookCover() (three
  // covers, not one) still lives on Home -- see Lyra3CoversTheme.cpp.
  v.homeShowsCoverCard = true;
  return v;
}();
}  // namespace Lyra3CoversMetrics

class Lyra3CoversTheme : public LyraTheme {
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
  // No room: three covers already fill the width LyraTheme's own version
  // shares with the companion column. Explicit rather than inherited, so
  // this doesn't depend on LyraTheme's own cover-width bookkeeping, which
  // this theme's drawRecentBookCover() above never touches.
  Rect getHomeCompanionRect(Rect) const override { return Rect{}; }
};
