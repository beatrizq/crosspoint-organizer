#pragma once
#include <CompanionMood.h>
#include <GfxRenderer.h>

#include <string>
#include <vector>

#include "CompanionSprites.generated.h"

namespace companion {

// Logical footprint of a pose at the given integer scale, for layout maths.
constexpr int poseWidth(const int scale) { return SPRITE_WIDTH * scale; }
constexpr int poseHeight(const int scale) { return SPRITE_HEIGHT * scale; }

/**
 * Draws one companion pose with its top-left at (x, y), magnified by an integer
 * scale (1 = one framebuffer pixel per sprite pixel).
 *
 * Every pixel goes through GfxRenderer::drawPixel, so the renderer's own
 * coordinate transform handles all four screen orientations and no sprite data
 * has to be pre-rotated. Pixels outside the screen are dropped here rather than
 * by drawPixel, which logs an error per out-of-bounds write.
 *
 * Nothing is scaled by fractions: e-ink at this size needs hard edges, and
 * integer scaling keeps the baked dither pattern intact.
 */
// `mirrored` flips the sprite horizontally so a walking character can face the
// way it is travelling. The sprites are symmetric enough to read either way, so
// no second set of art is needed.
void drawPose(const GfxRenderer& renderer, CompanionId id, Mood mood, int x, int y, int scale, bool mirrored = false);

// Which edge the tail hangs off, so the bubble can point at a companion beside
// it or below it.
enum class TailSide : uint8_t { Left, Bottom };

/**
 * Draws a rounded speech bubble with a tail pointing at the speaker.
 *
 * The interior is cleared to paper before the outline is stroked, so callers can
 * draw text straight afterwards without worrying about what was underneath.
 * `tailLength` is how far the tail reaches beyond the bubble body. `lineWidth`
 * defaults to the same thickness a selected tile's outline uses elsewhere
 * (see LyraTheme's selectionLineWidth) so the two selection-adjacent strokes
 * read as the same weight.
 */
void drawSpeechBubble(const GfxRenderer& renderer, int x, int y, int w, int h, int tailLength,
                      TailSide side = TailSide::Left, int lineWidth = 2);

// Wrapped lines plus the text column width the bubble should actually use --
// see fitBubbleText().
struct BubbleFit {
  std::vector<std::string> lines;
  int textWidth;
};

/**
 * Wraps `text` at `fontId` against `maxTextWidth`, and reports how wide the
 * bubble's text column actually needs to be: the text's own single-line
 * width when it fits without wrapping, so a short line's bubble does not
 * stretch to fill whatever room happened to be available, or `maxTextWidth`
 * itself once wrapping kicks in, since that is what the wrap points were
 * chosen against. Never narrower than `minTextWidth`, so a one-word line
 * still gets a bubble with room for the tail and rounded corners.
 */
BubbleFit fitBubbleText(const GfxRenderer& renderer, int fontId, const std::string& text, int maxTextWidth,
                        int minTextWidth, int maxLines);

// Translated name of a mood, for the status label under the character. Unlike
// the character quotes this is UI chrome, so it comes from the string table.
const char* moodLabel(Mood mood);

}  // namespace companion
