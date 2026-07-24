// Copyright 2018 Google LLC
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.
#ifndef SkPDFSubsetFont_DEFINED
#define SkPDFSubsetFont_DEFINED

#include "include/core/SkData.h" // IWYU pragma: keep
#include "include/core/SkRefCnt.h"
#include "include/core/SkSpan.h"
#include "include/core/SkTypes.h"

class SkPDFGlyphUse;
class SkTypeface;

/** Subset the typeface's data to only include the glyphs used.
 *  The glyph ids will remain the same.
 *
 *  @return The subset font data, or nullptr if it cannot be subset.
 */
sk_sp<SkData> SkPDFSubsetFont(const SkTypeface& typeface, const SkPDFGlyphUse& glyphUsage);

bool SkPDFCanSubsetTableBasedFonts();

/** Compute the glyph -> unicode map directly from the typeface's 'cmap' table, without
 *  calling into the platform font host.
 *
 *  Each glyph maps to the lowest codepoint that produces it; unmapped glyphs stay 0.
 *  glyphToUnicode must be sized to the typeface's glyph count.
 *
 *  @return false if no mappings could be read; the caller should then fall back to
 *          SkTypeface::getGlyphToUnicodeMap.
 */
bool SkPDFComputeGlyphToUnicodeMap(const SkTypeface& typeface, SkSpan<SkUnichar> glyphToUnicode);

#endif  // SkPDFSubsetFont_DEFINED
