// Copyright 2019 Google LLC
#include "include/core/SkFontMetrics.h"
#include "include/core/SkTextBlob.h"
#include "include/private/SkFloatingPoint.h"
#include "include/private/SkMalloc.h"
#include "include/private/SkTo.h"
#include "modules/skparagraph/include/DartTypes.h"
#include "modules/skparagraph/include/TextStyle.h"
#include "modules/skparagraph/src/ParagraphImpl.h"
#include "modules/skparagraph/src/Run.h"
#include "modules/skshaper/include/SkShaper.h"
#include "src/core/SkUTF.h"

#include <algorithm>

namespace skia {
namespace textlayout {

Run::Run(ParagraphImpl* owner,
         const SkShaper::RunHandler::RunInfo& info,
         size_t firstChar,
         SkScalar heightMultiplier,
         bool useHalfLeading,
         SkScalar baselineShift,
         size_t index,
         SkScalar offsetX)
    : fOwner(owner)
    , fTextRange(firstChar + info.utf8Range.begin(), firstChar + info.utf8Range.end())
    , fClusterRange(EMPTY_CLUSTERS)
    , fFont(info.fFont)
    , fClusterStart(firstChar)
    , fGlyphData(std::make_shared<GlyphData>())
    , fGlyphs(fGlyphData->glyphs)
    , fPositions(fGlyphData->positions)
    , fOffsets(fGlyphData->offsets)
    , fClusterIndexes(fGlyphData->clusterIndexes)
    , fHeightMultiplier(heightMultiplier)
    , fUseHalfLeading(useHalfLeading)
    , fBaselineShift(baselineShift)
{
    fBidiLevel = info.fBidiLevel;
    fScript = info.fScript;
    fLanguage = info.fLanguage;
    fAdvance = info.fAdvance;
    fIndex = index;
    fUtf8Range = info.utf8Range;
    fOffset = SkVector::Make(offsetX, 0);

    fGlyphs.push_back_n(info.glyphCount);
    fPositions.push_back_n(info.glyphCount + 1);
    fOffsets.push_back_n(info.glyphCount + 1);
    fClusterIndexes.push_back_n(info.glyphCount + 1);
    info.fFont.getMetrics(&fFontMetrics);

    this->calculateMetrics();

    // To make edge cases easier:
    fPositions[info.glyphCount] = fOffset + fAdvance;
    fOffsets[info.glyphCount] = {0, 0};
    fClusterIndexes[info.glyphCount] = this->leftToRight() ? info.utf8Range.end() : info.utf8Range.begin();
    fEllipsis = false;
    fPlaceholderIndex = std::numeric_limits<size_t>::max();
}

void Run::calculateMetrics() {
    fCorrectAscent = fFontMetrics.fAscent - fFontMetrics.fLeading * 0.5;
    fCorrectDescent = fFontMetrics.fDescent + fFontMetrics.fLeading * 0.5;
    fCorrectLeading = 0;
    if (SkScalarNearlyZero(fHeightMultiplier)) {
        return;
    }
    const auto runHeight = fHeightMultiplier * fFont.getSize();
    const auto fontIntrinsicHeight = fCorrectDescent - fCorrectAscent;
    if (fUseHalfLeading) {
        const auto extraLeading = (runHeight - fontIntrinsicHeight) / 2;
        fCorrectAscent -= extraLeading;
        fCorrectDescent += extraLeading;
    } else {
        const auto multiplier = runHeight / fontIntrinsicHeight;
        fCorrectAscent *= multiplier;
        fCorrectDescent *= multiplier;
    }
    // fCorrectAscent and fCorrectDescent stay unshifted; correctAscent() and correctDescent()
    // apply fBaselineShift on read
}

SkShaper::RunHandler::Buffer Run::newRunBuffer() {
    return {fGlyphs.data(), fPositions.data(), fOffsets.data(), fClusterIndexes.data(), fOffset};
}

TextRange Run::sourceTextRange(size_t pos, size_t size) const {
    // Ellipsis and hyphen runs come from synthetic strings, not the paragraph text.
    if (fOwner == nullptr || fEllipsis || size == 0) {
        return EMPTY_TEXT;
    }
    // Clusters are monotonic in glyph order (increasing for LTR, decreasing for RTL),
    // with a sentinel past the last glyph — so the clusters just outside the glyph
    // range give the text boundaries.
    TextRange text;
    if (this->leftToRight()) {
        text.start = fClusterIndexes[pos];
        text.end = fClusterIndexes[pos + size];
    } else {
        text.start = fClusterIndexes[pos + size - 1];
        text.end = pos > 0 ? fClusterIndexes[pos - 1] : fUtf8Range.end();
    }
    if (text.start >= text.end || fClusterStart + text.end > fOwner->text().size()) {
        return EMPTY_TEXT;
    }
    return text;
}

namespace {
// Invisible characters that only steer line breaking and carry no meaning for the text.
bool is_line_break_hint(SkUnichar u) {
    return u == 0x00AD    // soft hyphen
        || u == 0x200B    // zero-width space
        || u == 0x2060    // word joiner
        || u == 0xFEFF;   // zero-width no-break space (BOM)
}
}  // namespace

// HarfBuzz hides default-ignorable codepoints by shaping each to the font's space glyph with a
// zero advance. Painting that glyph shows nothing, but PDF text extraction reads it as U+0020
// (the space glyph's cmap entry), which splits words for search, copy-paste and screen readers.
// Only line-break hints are handled; joiners, bidi marks and variation selectors carry meaning
// and keep their glyph, which the PDF backend maps back to them through /ActualText.
TextRange Run::hiddenBreakHintText(size_t glyph, SkGlyphID spaceGlyph) const {
    if (fOwner == nullptr || fEllipsis || spaceGlyph == 0 || fGlyphs[glyph] != spaceGlyph) {
        return EMPTY_TEXT;
    }

    // The glyph's text runs from its cluster to the next larger cluster index; clusters
    // grow in glyph order for LTR and in reverse glyph order for RTL.
    const size_t start = fClusterIndexes[glyph];
    size_t end = EMPTY_INDEX;
    if (this->leftToRight()) {
        for (size_t k = glyph + 1; k <= this->size(); ++k) {
            if (fClusterIndexes[k] > start) {
                end = fClusterIndexes[k];
                break;
            }
        }
    } else {
        end = fUtf8Range.end();
        for (size_t k = glyph; k > 0; --k) {
            if (fClusterIndexes[k - 1] > start) {
                end = fClusterIndexes[k - 1];
                break;
            }
        }
    }
    if (end == EMPTY_INDEX || end <= start || fClusterStart + end > fOwner->text().size()) {
        return EMPTY_TEXT;
    }

    // All line-break hints are outside ASCII, so ASCII text (a real space) never qualifies.
    const char* p = fOwner->text().data() + fClusterStart + start;
    const char* stop = fOwner->text().data() + fClusterStart + end;
    if (static_cast<unsigned char>(*p) < 0x80) {
        return EMPTY_TEXT;
    }
    while (p < stop) {
        if (!is_line_break_hint(SkUTF::NextUTF8(&p, stop))) {
            return EMPTY_TEXT;
        }
    }
    return TextRange(start, end);
}

void Run::copyTo(SkTextBlobBuilder& builder, size_t pos, size_t size, bool attachText) const {
    SkASSERT(pos + size <= this->size());

    // Leave out the placeholder glyphs of hidden line-break hints, and their text from the
    // attached text, so they reach neither the canvas nor the PDF text layer.
    // Layout (positions, widths, clusters, line breaks) is unaffected.
    skia_private::STArray<64, size_t, true> kept;
    skia_private::STArray<4, TextRange, true> hiddenText;
    if (fOwner != nullptr && !fEllipsis) {
        const SkGlyphID spaceGlyph = fFont.unicharToGlyph(' ');
        for (size_t i = 0; i < size; ++i) {
            TextRange hidden = this->hiddenBreakHintText(pos + i, spaceGlyph);
            if (hidden == EMPTY_TEXT) {
                kept.push_back(pos + i);
            } else {
                hiddenText.push_back(hidden);
            }
        }
    }
    const bool filtering = !hiddenText.empty();
    const size_t count = filtering ? kept.size() : size;
    if (count == 0) {
        return;
    }
    auto glyphAt = [&](size_t i) { return filtering ? kept[i] : pos + i; };

    // Attached text and clusters let the PDF backend emit correct ToUnicode and
    // /ActualText for glyphs without cmap entries (ligatures, Arabic contextual forms).
    const TextRange text = attachText ? this->sourceTextRange(pos, size) : EMPTY_TEXT;

    // Hidden text inside the attached range, in text order, so it can be cut out.
    // Several glyphs of one cluster share its text, so overlapping ranges are merged.
    skia_private::STArray<4, TextRange, true> cuts;
    size_t cutBytes = 0;
    if (!text.empty()) {
        skia_private::STArray<4, TextRange, true> clipped;
        for (const auto& h : hiddenText) {
            TextRange c(std::max(h.start, text.start), std::min(h.end, text.end));
            if (c.start < c.end) {
                clipped.push_back(c);
            }
        }
        std::sort(clipped.begin(), clipped.end(),
                  [](const TextRange& a, const TextRange& b) { return a.start < b.start; });
        for (const auto& c : clipped) {
            if (!cuts.empty() && c.start <= cuts.back().end) {
                cuts.back().end = std::max(cuts.back().end, c.end);
            } else {
                cuts.push_back(c);
            }
        }
        for (const auto& c : cuts) {
            cutBytes += c.width();
        }
    }
    const size_t textBytes = text.empty() ? 0 : text.width() - cutBytes;

    const auto& blobBuffer = textBytes == 0
        ? builder.allocRunPos(fFont, SkToInt(count))
        : builder.allocRunTextPos(fFont, SkToInt(count), SkToInt(textBytes));

    for (size_t i = 0; i < count; ++i) {
        blobBuffer.glyphs[i] = fGlyphs[glyphAt(i)];
    }

    // allocRunTextPos allocated two extra buffers that must be filled: utf8text gets
    // the source text, and clusters[i] gets the offset in that text where glyph i's
    // source character starts.
    if (textBytes != 0) {
        const char* source = fOwner->text().data() + fClusterStart;
        char* out = blobBuffer.utf8text;
        size_t from = text.start;
        for (const auto& c : cuts) {
            sk_careful_memcpy(out, source + from, c.start - from);
            out += c.start - from;
            from = c.end;
        }
        sk_careful_memcpy(out, source + from, text.end - from);

        for (size_t i = 0; i < count; ++i) {
            // Rebase onto the attached text; clamping matters only when a blob
            // boundary splits a cluster.
            size_t cluster = fClusterIndexes[glyphAt(i)];
            cluster = std::min(std::max(cluster, text.start), text.end);
            size_t removed = 0;
            for (const auto& c : cuts) {
                if (c.end <= cluster) {
                    removed += c.width();
                }
            }
            blobBuffer.clusters[i] = SkToU32(cluster - text.start - removed);
        }
    }

    // Position = shaped position + justification shift + per-glyph offset.
    for (size_t i = 0; i < count; ++i) {
        const size_t g = glyphAt(i);
        auto point = fPositions[g];
        if (!fJustificationShifts.empty()) {
            point.fX += fJustificationShifts[g].fX;
        }
        point += fOffsets[g];
        blobBuffer.points()[i] = point;
    }
}

// Find a cluster range from text range (within one run)
// Cluster range is normalized ([start:end) start < end regardless of TextDirection
// Boolean value in triple indicates whether the cluster range was found or not
std::tuple<bool, ClusterIndex, ClusterIndex> Run::findLimitingClusters(TextRange text) const {
    if (text.width() == 0) {
        // Special Flutter case for "\n" and "...\n"
        if (text.end > this->fTextRange.start) {
            ClusterIndex index = fOwner->clusterIndex(text.end - 1);
            return std::make_tuple(true, index, index);
        } else {
            return std::make_tuple(false, 0, 0);
        }
    }

    ClusterRange clusterRange;
    bool found = true;
    // Deal with the case when either start or end are not align with glyph cluster edge
    // In such case we shift the text range to the right
    // (cutting from the left and adding to the right)
    if (leftToRight()) {
        // LTR: [start:end)
        found = clusterRange.start != fClusterRange.end;
        clusterRange.start = fOwner->clusterIndex(text.start);
        clusterRange.end = fOwner->clusterIndex(text.end - 1);
    } else {
        // RTL: (start:end]
        clusterRange.start = fOwner->clusterIndex(text.end);
        clusterRange.end = fOwner->clusterIndex(text.start + 1);
        found = clusterRange.end != fClusterRange.start;
    }

    return std::make_tuple(
            found,
            clusterRange.start,
            clusterRange.end);
}

std::tuple<bool, TextIndex, TextIndex> Run::findLimitingGlyphClusters(TextRange text) const {
    TextIndex start = fOwner->findPreviousGlyphClusterBoundary(text.start);
    TextIndex end = fOwner->findNextGlyphClusterBoundary(text.end);
    return std::make_tuple(true, start, end);
}

// Adjust the text to grapheme edges so the first grapheme start is in the text and the last grapheme start is in the text
// It actually means that the first grapheme is entirely in the text and the last grapheme does not have to be
// 12345 234 2:2 -> 2,5 4:4
std::tuple<bool, TextIndex, TextIndex> Run::findLimitingGraphemes(TextRange text) const {
    TextIndex start = fOwner->findPreviousGraphemeBoundary(text.start);
    TextIndex end = fOwner->findNextGraphemeBoundary(text.end);
    return std::make_tuple(true, start, end);
}

void Run::iterateThroughClusters(const ClusterVisitor& visitor) {

    for (size_t index = 0; index < fClusterRange.width(); ++index) {
        auto correctIndex = leftToRight() ? fClusterRange.start + index : fClusterRange.end - index - 1;
        auto cluster = &fOwner->cluster(correctIndex);
        visitor(cluster);
    }
}

void Run::addSpacesAtTheEnd(SkScalar space, Cluster* cluster) {
    // Increment the run width
    fAdvance.fX += space;
    // Increment the cluster width
    cluster->space(space);
}

SkScalar Run::addLetterSpacesEvenly(SkScalar space) {
    SkScalar shift = 0;
    for (size_t i = 0; i < this->size(); ++i) {
        fPositions[i].fX += shift;
        shift += space;
    }
    fPositions[this->size()].fX += shift;
    fAdvance.fX += shift;
    return shift;
}

SkScalar Run::addLetterSpacesEvenly(SkScalar space, Cluster* cluster) {
    // Offset all the glyphs in the cluster
    SkScalar shift = 0;
    for (size_t i = cluster->startPos(); i < cluster->endPos(); ++i) {
        fPositions[i].fX += shift;
        shift += space;
    }
    if (this->size() == cluster->endPos()) {
        // To make calculations easier
        fPositions[cluster->endPos()].fX += shift;
    }
    // Increment the run width
    fAdvance.fX += shift;
    // Increment the cluster width
    cluster->space(shift);
    cluster->setHalfLetterSpacing(space / 2);

    return shift;
}

bool Run::isCursiveScript() const {
    switch (this->fScript) {
        case SkSetFourByteTag('A', 'r', 'a', 'b'): // ARABIC
        case SkSetFourByteTag('R', 'o', 'h', 'g'): // HANIFI_ROHINGYA
        case SkSetFourByteTag('M', 'a', 'n', 'd'): // MANDAIC
        case SkSetFourByteTag('M', 'o', 'n', 'g'): // MONGOLIAN
        case SkSetFourByteTag('N', 'k', 'o', 'o'): // NKO
        case SkSetFourByteTag('P', 'h', 'a', 'g'): // PHAGS_PA
        case SkSetFourByteTag('S', 'y', 'r', 'c'): // SYRIAC
            return true;
        default:
            return false;
    }
}

void Run::shift(const Cluster* cluster, SkScalar offset) {
    if (offset == 0) {
        return;
    }
    for (size_t i = cluster->startPos(); i < cluster->endPos(); ++i) {
        fPositions[i].fX += offset;
    }
    if (this->size() == cluster->endPos()) {
        // To make calculations easier
        fPositions[cluster->endPos()].fX += offset;
    }
}

void Run::extend(const Cluster* cluster, SkScalar offset) {
    // Extend the cluster at the end
    fPositions[cluster->endPos()].fX += offset;
}

void Run::updateMetrics(InternalLineMetrics* endlineMetrics) {

    SkASSERT(isPlaceholder());
    auto placeholderStyle = this->placeholderStyle();
    // Difference between the placeholder baseline and the line bottom
    SkScalar baselineAdjustment = 0;
    switch (placeholderStyle->fBaseline) {
        case TextBaseline::kAlphabetic:
            break;

        case TextBaseline::kIdeographic:
            baselineAdjustment = endlineMetrics->deltaBaselines() / 2;
            break;
    }

    auto height = placeholderStyle->fHeight;
    auto offset = placeholderStyle->fBaselineOffset;

    fFontMetrics.fLeading = 0;
    switch (placeholderStyle->fAlignment) {
        case PlaceholderAlignment::kBaseline:
            fFontMetrics.fAscent = baselineAdjustment - offset;
            fFontMetrics.fDescent = baselineAdjustment + height - offset;
            break;

        case PlaceholderAlignment::kAboveBaseline:
            fFontMetrics.fAscent = baselineAdjustment - height;
            fFontMetrics.fDescent = baselineAdjustment;
            break;

        case PlaceholderAlignment::kBelowBaseline:
            fFontMetrics.fAscent = baselineAdjustment;
            fFontMetrics.fDescent = baselineAdjustment + height;
            break;

        case PlaceholderAlignment::kTop:
            fFontMetrics.fDescent = height + fFontMetrics.fAscent;
            break;

        case PlaceholderAlignment::kBottom:
            fFontMetrics.fAscent = fFontMetrics.fDescent - height;
            break;

        case PlaceholderAlignment::kMiddle:
            auto mid = (-fFontMetrics.fDescent - fFontMetrics.fAscent)/2.0;
            fFontMetrics.fDescent = height/2.0 - mid;
            fFontMetrics.fAscent =  - height/2.0 - mid;
            break;
    }

    this->calculateMetrics();

    // Make sure the placeholder can fit the line
    endlineMetrics->add(this);
}

SkScalar Cluster::sizeToChar(TextIndex ch) const {
    if (ch <= fTextRange.start || ch >= fTextRange.end) {
        return 0;
    }
    if (fOwner != nullptr && fRunIndex != EMPTY_RUN && fWidth > 0) {
        // Scale cluster width by standalone glyph advances so multi-character
        // ligatures (such as "ffi") split proportionally to their component characters.
        // Combining marks lack kGraphemeStart and contribute zero horizontal advance
        const Run& run = fOwner->run(fRunIndex);
        const SkFont& font = run.font();
        const char* text = fOwner->text().data();
        const char* ptr = text + fTextRange.start;
        const char* end = text + fTextRange.end;
        const char* split = text + ch;

        SkScalar prefixWidth = 0;
        SkScalar totalWidth = 0;
        while (ptr < end) {
            const char* charStart = ptr;
            SkUnichar u = SkUTF::NextUTF8(&ptr, end);
            if (u < 0) {
                break;
            }
            if (!fOwner->codeUnitHasProperty(charStart - text,
                                             SkUnicode::CodeUnitFlags::kGraphemeStart)) {
                continue;
            }
            SkScalar w = font.measureText(charStart, ptr - charStart, SkTextEncoding::kUTF8);
            if (charStart < split) {
                prefixWidth += w;
            }
            totalWidth += w;
        }

        if (totalWidth > 0) {
            return SkDoubleToScalar(fWidth * (prefixWidth / totalWidth));
        }
        return 0;
    }

    auto shift = ch - fTextRange.start;
    auto ratio = shift * 1.0 / fTextRange.width();

    return SkDoubleToScalar(fWidth * ratio);
}

SkScalar Cluster::sizeFromChar(TextIndex ch) const {
    if (ch < fTextRange.start || ch + 1 >= fTextRange.end) {
        return 0;
    }
    return fWidth - sizeToChar(ch + 1);
}

size_t Cluster::roundPos(SkScalar s) const {
    auto ratio = (s * 1.0) / fWidth;
    return sk_double_floor2int(ratio * size());
}

SkScalar Cluster::trimmedWidth(size_t pos) const {
    // Find the width until the pos and return the min between trimmedWidth and the width(pos)
    // We don't have to take in account cluster shift since it's the same for 0 and for pos
    auto& run = fOwner->run(fRunIndex);
    return std::min(run.positionX(pos) - run.positionX(fStart), fWidth);
}

SkScalar Run::positionX(size_t pos) const {
    return posX(pos) + (fJustificationShifts.empty() ? 0 : fJustificationShifts[pos].fY);
}

PlaceholderStyle* Run::placeholderStyle() const {
    if (isPlaceholder()) {
        return &fOwner->placeholders()[fPlaceholderIndex].fStyle;
    } else {
        return nullptr;
    }
}

bool Run::isResolved() const {
    for (auto& glyph :fGlyphs) {
        if (glyph == 0) {
            return false;
        }
    }
    return true;
}

Run* Cluster::runOrNull() const {
    if (fRunIndex >= fOwner->runs().size()) {
        return nullptr;
    }
    return &fOwner->run(fRunIndex);
}

Run& Cluster::run() const {
    SkASSERT(fRunIndex < fOwner->runs().size());
    return fOwner->run(fRunIndex);
}

SkFont Cluster::font() const {
    SkASSERT(fRunIndex < fOwner->runs().size());
    return fOwner->run(fRunIndex).font();
}

bool Cluster::isSoftBreak() const {
    return fOwner->codeUnitHasProperty(fTextRange.end,
                                       SkUnicode::CodeUnitFlags::kSoftLineBreakBefore);
}

bool Cluster::isSoftHyphen() const {
    auto text = fOwner->text();
    const char* p = text.begin() + fTextRange.start;
    const char* end = text.begin() + fTextRange.end;
    if (p >= end) {
        return false;
    }
    SkUnichar cp = SkUTF::NextUTF8(&p, end);
    return cp == 0x00AD && p == end;
}

bool Cluster::isGraphemeBreak() const {
    return fOwner->codeUnitHasProperty(fTextRange.end, SkUnicode::CodeUnitFlags::kGraphemeStart);
}
}  // namespace textlayout
}  // namespace skia
