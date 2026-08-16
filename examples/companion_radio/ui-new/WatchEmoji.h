#pragma once
// WatchEmoji.h -- inline colour emoji for node names on the T-Watch LCD.
//
// Display-only: node names arrive from adverts / the companion app carrying
// UTF-8 emoji, and this draws them as small colour images beside the text.
// There is no typing path here.
//
// Format: 10x10 logical pixels (20x20 on the 240px panel after UI_ZOOM),
// one byte per pixel in RGB332 (RRRGGGBB) -- the exact depth of the LGFX
// sprite buffer, so pushImage332() is a raw copy with colour-key skip.
// Baked from Twemoji (jdecked) 72x72 PNGs, Lanczos-downsampled to 10x10,
// semi-transparent edge pixels composited onto black (the watch background)
// before quantising so edges don't grey out, alpha < 50% -> key.
//
// Graphics (c) Twitter/Twemoji contributors, CC BY 4.0.
//
// Assets live in the generated WatchEmojiData.h (tools/bake_watch_emoji.py,
// driven by tools/watch_emoji_registry.txt): the NSW/ACT repeater-owner
// registry, the watch's previous 79-emoji set, and common message faces.
// Flags and ZWJ sequences are matched whole via the sequence table.

#include <stdint.h>
#include <string.h>

#define WEMOJI_W    10
#define WEMOJI_H    10
#define WEMOJI_KEY  0xE3    // RGB332 magenta (R=7,G=0,B=3): never emitted by the baker for opaque pixels
#define WEMOJI_VS16 0xFE0F  // variation selector-16: skipped when it follows an emoji
#define WEMOJI_RUN_MAX 160   // longest text run buffered between emoji (>= the 160-byte channel message cap)

#include "WatchEmojiData.h"   // generated: WatchEmoji struct, WATCH_EMOJI[], WATCH_EMOJI_COUNT

// Decode one UTF-8 sequence. Mirrors emojiDecodeUtf8() in EmojiSprites.h;
// duplicated here so this header stands alone.
static inline uint32_t watchEmojiDecode(const uint8_t* s, int remaining, int* used) {
  uint8_t b0 = s[0];
  if (b0 < 0x80) { *used = 1; return b0; }
  if ((b0 & 0xE0) == 0xC0 && remaining >= 2) { *used = 2; return ((uint32_t)(b0 & 0x1F) << 6) | (s[1] & 0x3F); }
  if ((b0 & 0xF0) == 0xE0 && remaining >= 3) { *used = 3; return ((uint32_t)(b0 & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F); }
  if ((b0 & 0xF8) == 0xF0 && remaining >= 4) { *used = 4; return ((uint32_t)(b0 & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) | ((uint32_t)(s[2] & 0x3F) << 6) | (s[3] & 0x3F); }
  *used = 1;
  return 0xFFFD;
}

// Match the emoji (if any) starting at s[0..remaining). Handles single
// codepoints, regional-indicator pairs and ZWJ sequences by comparing the
// decoded codepoint run (fe0f skipped) against WATCH_EMOJI, longest sequence
// first (the generated table is sorted that way). On a match, *used is the
// number of bytes consumed -- including any fe0f inside or right after the
// sequence -- and the pixel data is returned. On no match, *used is the byte
// length of just the first codepoint (plus a trailing fe0f) so the caller
// can skip that one codepoint and continue.
static inline const uint8_t* watchEmojiMatch(const uint8_t* s, int remaining, int* used) {
  uint32_t cps[WEMOJI_SEQ_MAX];
  int      ends[WEMOJI_SEQ_MAX];   // byte offset just past cps[i] (fe0f included)
  int n = 0, i = 0;
  while (n < WEMOJI_SEQ_MAX && i < remaining) {
    int u = 1;
    uint32_t cp = watchEmojiDecode(s + i, remaining - i, &u);
    if (cp < 0x80 || cp == 0xFFFD) break;   // sequences never contain ASCII / invalid
    if (cp == WEMOJI_VS16) { if (n) ends[n - 1] = i + u; i += u; continue; }
    cps[n] = cp; i += u; ends[n] = i; n++;
  }
  if (n == 0) { *used = 1; return NULL; }
  for (int e = 0; e < WATCH_EMOJI_COUNT; e++) {
    const WatchEmoji& w = WATCH_EMOJI[e];
    if (w.n > n) continue;
    bool ok = true;
    for (int k = 0; k < w.n; k++) { if (w.cp[k] != cps[k]) { ok = false; break; } }
    if (ok) { *used = ends[w.n - 1]; return w.px; }
  }
  *used = ends[0];   // no match: skip just the first codepoint (+ its fe0f)
  return NULL;
}

// Draw `name` at (x,y) with a max width, drawing any known emoji inline as
// 10x10 colour images and everything else as text through `disp`. Unknown
// non-ASCII (emoji without an asset, selectors, combining marks) is skipped
// silently -- the same outcome as translateUTF8ToBlocks() gives today, so
// unsupported names degrade exactly as before. Text is drawn in whatever
// colour/size the caller has set. Returns the x after the last thing drawn.
//
// The emoji cell is 10 wide + 1 gap; text is measured with getTextWidth() so
// the run stops cleanly at max_w rather than being clipped mid-glyph. The
// 10px cell sits on the same row as 8px text with 1px above the glyph line
// (y-1) so its visual centre lines up with lowercase text.
template <typename DISP>
static int watchEmojiDrawName(DISP& disp, int x, int y, int max_w, const char* name) {
  const int x_end = x + max_w;
  const uint8_t* s = (const uint8_t*)name;
  int len = (int)strlen(name);
  int i = 0;
  char text[64];   // pending ASCII run
  int  tl = 0;

  // flush the pending ASCII run, truncating to fit; returns false if out of room
  auto flush = [&]() -> bool {
    if (tl == 0) return true;
    text[tl] = 0;
    // shorten until it fits
    while (tl > 0 && x + (int)disp.getTextWidth(text) > x_end) { text[--tl] = 0; }
    if (tl == 0) return false;
    disp.setCursor(x, y);
    disp.print(text);
    x += disp.getTextWidth(text);
    tl = 0;
    return true;
  };

  while (i < len) {
    int used = 1;
    uint32_t cp = watchEmojiDecode(s + i, len - i, &used);
    if (cp < 0x80) {
      if (tl < (int)sizeof(text) - 1) text[tl++] = (char)cp;
      i += used;
      continue;
    }
    // non-ASCII: emit any pending text first
    if (!flush()) return x;
    int mu = 1;
    const uint8_t* px = watchEmojiMatch(s + i, len - i, &mu);
    if (px) {
      if (x + WEMOJI_W > x_end) return x;
      disp.pushImage332(x, y - 1, WEMOJI_W, WEMOJI_H, px, WEMOJI_KEY);
      x += WEMOJI_W + 1;
    }
    // else: unknown -> skipped
    i += mu;
  }
  flush();
  return x;
}

// Width in logical pixels that watchEmojiDrawRun() will occupy for `text`:
// text runs via getTextWidth(), each known emoji as a WEMOJI_W + 1 cell,
// unknown non-ASCII contributing nothing (it is skipped when drawn).
template <typename DISP>
static int watchEmojiTextWidth(DISP& disp, const char* text) {
  const uint8_t* s = (const uint8_t*)text;
  int len = (int)strlen(text);
  int i = 0, w = 0;
  char run[WEMOJI_RUN_MAX + 1];
  int rl = 0;
  while (i < len) {
    int used = 1;
    uint32_t cp = watchEmojiDecode(s + i, len - i, &used);
    if (cp < 0x80) {
      if (rl < (int)sizeof(run) - 1) run[rl++] = (char)cp;
      i += used;
      continue;
    }
    if (rl) { run[rl] = 0; w += disp.getTextWidth(run); rl = 0; }
    int mu = 1;
    if (watchEmojiMatch(s + i, len - i, &mu)) w += WEMOJI_W + 1;
    i += mu;
  }
  if (rl) { run[rl] = 0; w += disp.getTextWidth(run); }
  return w;
}

// Draw `text` starting at x (which may be negative or off-screen: used by the
// channel-view marquee) with no width limit -- the sprite clips whatever
// falls outside. Does NOT touch the text-wrap state: callers that draw past
// the screen edge (marquees, right-aligned overflow) must setTextWrap(false)
// around the call so a long line clips instead of wrapping onto the rows
// below, and restore it after; callers already running with wrap off (the
// settings row loop) just call it. Emoji cells clip via pushImage itself.
// Returns the x after the last thing drawn.
template <typename DISP>
static int watchEmojiDrawRun(DISP& disp, int x, int y, const char* text) {
  const uint8_t* s = (const uint8_t*)text;
  int len = (int)strlen(text);
  int i = 0;
  char run[WEMOJI_RUN_MAX + 1];
  int rl = 0;
  auto flush = [&]() {
    if (!rl) return;
    run[rl] = 0;
    disp.setCursor(x, y);
    disp.print(run);
    x += disp.getTextWidth(run);
    rl = 0;
  };
  while (i < len) {
    int used = 1;
    uint32_t cp = watchEmojiDecode(s + i, len - i, &used);
    if (cp < 0x80) {
      if (rl < (int)sizeof(run) - 1) run[rl++] = (char)cp;
      i += used;
      continue;
    }
    flush();
    int mu = 1;
    const uint8_t* px = watchEmojiMatch(s + i, len - i, &mu);
    if (px) {
      disp.pushImage332(x, y - 1, WEMOJI_W, WEMOJI_H, px, WEMOJI_KEY);
      x += WEMOJI_W + 1;
    }
    i += mu;
  }
  flush();
  return x;
}