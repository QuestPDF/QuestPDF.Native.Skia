// Copyright 2026 QuestPDF
//
// Regression tests for the behavior the QuestPDF fork adds to SkParagraph on top of upstream.
// They live in their own file so that upstream changes to SkParagraphTest.cpp never conflict
// with them, and they only use fonts stored in this repository (resources/fonts).
//
// Run with: dm --src tests --match QuestPDF
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkString.h"
#include "include/core/SkTextBlob.h"
#include "include/core/SkTypeface.h"
#include "include/utils/SkNoDrawCanvas.h"
#include "modules/skparagraph/include/FontCollection.h"
#include "modules/skparagraph/include/Paragraph.h"
#include "modules/skparagraph/include/ParagraphBuilder.h"
#include "modules/skparagraph/include/ParagraphPainter.h"
#include "modules/skparagraph/include/ParagraphStyle.h"
#include "modules/skparagraph/include/TextShadow.h"
#include "modules/skparagraph/include/TextStyle.h"
#include "modules/skparagraph/include/TypefaceFontProvider.h"
#include "modules/skparagraph/src/ParagraphImpl.h"
#include "modules/skparagraph/src/TextLine.h"
#include "modules/skshaper/utils/FactoryHelpers.h"
#include "modules/skunicode/include/SkUnicode.h"
#include "src/core/SkTextBlobPriv.h"
#include "tests/Test.h"
#include "tools/fonts/FontToolUtils.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace skia::textlayout;

namespace {

constexpr SkScalar kEpsilon = 0.01f;
constexpr char kFamily[] = "Roboto";
constexpr char kSoftHyphen[] = "\xC2\xAD";

sk_sp<SkUnicode> get_unicode() {
    return sk_ref_sp<SkUnicode>(SkShapers::BestAvailable()->getUnicode());
}

sk_sp<SkTypeface> load_typeface() {
    return ToolUtils::CreateTypefaceFromResource("fonts/Roboto-Regular.ttf");
}

// A font collection set up like QuestPDF's: the fonts are registered by the application.
sk_sp<FontCollection> make_font_collection() {
    sk_sp<SkTypeface> typeface = load_typeface();
    if (!typeface) {
        return nullptr;
    }
    auto provider = sk_make_sp<TypefaceFontProvider>();
    provider->registerTypeface(std::move(typeface), SkString(kFamily));
    auto fontCollection = sk_make_sp<FontCollection>();
    fontCollection->setAssetFontManager(std::move(provider));
    fontCollection->getParagraphCache()->turnOn(false);
    return fontCollection;
}

// The paragraph settings QuestPDF uses.
ParagraphStyle make_paragraph_style() {
    ParagraphStyle style;
    style.setReplaceTabCharacters(true);
    style.turnHintingOff();
    style.setApplyRoundingHack(false);
    style.setRenderSoftHyphens(true);
    style.setMaxLines(std::numeric_limits<int>::max());
    return style;
}

TextStyle make_text_style(SkScalar fontSize) {
    TextStyle style;
    style.setFontFamilies({SkString(kFamily)});
    style.setFontSize(fontSize);
    style.setColor(SK_ColorBLACK);
    style.setHalfLeading(true);
    style.setFontHinting(SkFontHinting::kNone);
    return style;
}

struct Span {
    std::string fText;
    TextStyle fStyle;
};

std::unique_ptr<Paragraph> make_paragraph(const sk_sp<FontCollection>& fontCollection,
                                          const std::vector<Span>& spans,
                                          SkScalar width,
                                          const ParagraphStyle& paragraphStyle = make_paragraph_style()) {
    auto builder = ParagraphBuilder::make(paragraphStyle, fontCollection, get_unicode());
    for (const Span& span : spans) {
        builder->pushStyle(span.fStyle);
        builder->addText(span.fText.c_str(), span.fText.size());
        builder->pop();
    }
    auto paragraph = builder->Build();
    paragraph->layout(width);
    return paragraph;
}

// Records the text blobs a paragraph draws on a canvas.
class BlobCanvas final : public SkNoDrawCanvas {
public:
    struct Blob {
        sk_sp<const SkTextBlob> fBlob;
        SkPoint fOrigin;
    };
    BlobCanvas() : SkNoDrawCanvas(10000, 10000) {}
    std::vector<Blob> fBlobs;

protected:
    void onDrawTextBlob(const SkTextBlob* blob, SkScalar x, SkScalar y, const SkPaint&) override {
        fBlobs.push_back({sk_ref_sp(blob), {x, y}});
    }
};

// Records the filled rectangles solid and double decorations are drawn with.
class DecorationPainter final : public ParagraphPainter {
public:
    std::vector<SkRect> fRects;

    void drawTextBlob(const sk_sp<SkTextBlob>&, SkScalar, SkScalar, const SkPaintOrID&) override {}
    void drawTextShadow(const sk_sp<SkTextBlob>&, SkScalar, SkScalar, SkColor, SkScalar) override {}
    void drawRect(const SkRect&, const SkPaintOrID&) override {}
    void drawFilledRect(const SkRect& rect, const DecorationStyle&) override {
        fRects.push_back(rect.makeOffset(fOrigin));
    }
    void drawPath(const SkPath&, const DecorationStyle&) override {}
    void drawLine(SkScalar, SkScalar, SkScalar, SkScalar, const DecorationStyle&) override {}
    void clipRect(const SkRect&) override {}
    void translate(SkScalar dx, SkScalar dy) override { fOrigin += SkPoint::Make(dx, dy); }
    void save() override { fStack.push_back(fOrigin); }
    void restore() override {
        fOrigin = fStack.back();
        fStack.pop_back();
    }

private:
    SkPoint fOrigin = {0, 0};
    std::vector<SkPoint> fStack;
};

bool is_fractional(SkScalar value) { return std::fabs(value - std::round(value)) > 0.05f; }

}  // namespace

// Paragraph::paintLines() paints exactly the requested lines, at their place in the paragraph.
DEF_TEST(QuestPDF_Paragraph_PaintLines, reporter) {
    auto fontCollection = make_font_collection();
    if (!fontCollection) {
        return;
    }
    auto paragraph = make_paragraph(fontCollection, {{"one\ntwo\nthree", make_text_style(14)}}, 500);
    std::vector<LineMetrics> lines;
    paragraph->getLineMetrics(lines);
    REPORTER_ASSERT(reporter, lines.size() == 3);
    if (lines.size() != 3) {
        return;
    }

    auto painted = [&](int from, int to) {
        BlobCanvas canvas;
        paragraph->paintLines(&canvas, from, to);
        std::vector<SkScalar> baselines;
        for (const auto& blob : canvas.fBlobs) {
            baselines.push_back(blob.fOrigin.fY);
        }
        return baselines;
    };

    auto middle = painted(1, 1);
    REPORTER_ASSERT(reporter, middle.size() == 1);
    REPORTER_ASSERT(reporter, middle.size() == 1 &&
                              SkScalarNearlyEqual(middle[0], (SkScalar)lines[1].fBaseline, kEpsilon));
    REPORTER_ASSERT(reporter, painted(0, 2).size() == 3);
    REPORTER_ASSERT(reporter, painted(1, 2).size() == 2);
    // The range is clamped to the existing lines
    REPORTER_ASSERT(reporter, painted(-5, 100).size() == 3);
    REPORTER_ASSERT(reporter, painted(2, 1).empty());
    paragraph->paintLines(nullptr, 0, 2);
}

// Neither the line height nor the baseline the text is drawn at is rounded to whole units.
DEF_TEST(QuestPDF_Paragraph_LineMetricsAreNotRounded, reporter) {
    auto fontCollection = make_font_collection();
    if (!fontCollection) {
        return;
    }
    auto paragraph = make_paragraph(fontCollection, {{"one\ntwo\nthree", make_text_style(11.3f)}}, 500);
    std::vector<LineMetrics> lines;
    paragraph->getLineMetrics(lines);
    REPORTER_ASSERT(reporter, lines.size() == 3);
    if (lines.size() != 3) {
        return;
    }

    SkScalar lineHeight = (SkScalar)(lines[0].fAscent + lines[0].fDescent);
    REPORTER_ASSERT(reporter, is_fractional(lineHeight), "line height %g", lineHeight);
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(paragraph->getHeight(), 3 * lineHeight, kEpsilon),
                    "height %g, expected %g", paragraph->getHeight(), 3 * lineHeight);

    BlobCanvas canvas;
    paragraph->paint(&canvas, 0, 0);
    REPORTER_ASSERT(reporter, canvas.fBlobs.size() == 3);
    bool anyFractional = false;
    for (size_t i = 0; i < canvas.fBlobs.size() && i < lines.size(); ++i) {
        SkScalar baseline = canvas.fBlobs[i].fOrigin.fY;
        anyFractional |= is_fractional(baseline);
        REPORTER_ASSERT(reporter, SkScalarNearlyEqual(baseline, (SkScalar)lines[i].fBaseline, kEpsilon),
                        "line %zu drawn at %g, baseline %g", i, baseline, lines[i].fBaseline);
    }
    REPORTER_ASSERT(reporter, anyFractional);
}

// A line holding only whitespace must not report a width beyond the layout width.
DEF_TEST(QuestPDF_Paragraph_WhitespaceOnlyLineFitsLayoutWidth, reporter) {
    auto fontCollection = make_font_collection();
    if (!fontCollection) {
        return;
    }
    const SkScalar width = 50;
    auto paragraph = make_paragraph(fontCollection, {{std::string(200, ' '), make_text_style(12)}}, width);
    REPORTER_ASSERT(reporter, paragraph->getLongestLine() <= width + kEpsilon,
                    "longest line %g", paragraph->getLongestLine());
}

// QuestPDF fails the layout when a paragraph is wider than the space it was given, so a line that
// ends in a rendered soft hyphen must still fit the layout width, also when the soft hyphen is
// the only break opportunity on the line and when the text is letter-spaced.
DEF_TEST(QuestPDF_Paragraph_SoftHyphenNeverExceedsLayoutWidth, reporter) {
    auto fontCollection = make_font_collection();
    if (!fontCollection) {
        return;
    }
    const std::string texts[] = {
            std::string("inter") + kSoftHyphen + "national inter" + kSoftHyphen + "national",
            std::string("Donau") + kSoftHyphen + "dampf" + kSoftHyphen + "schiff" + kSoftHyphen +
                    "fahrts" + kSoftHyphen + "gesell" + kSoftHyphen + "schaft",
    };

    size_t hyphenatedLines = 0;
    size_t linesEndedBeforeSoftHyphen = 0;
    for (SkScalar letterSpacing : {0.0f, 2.5f}) {
        for (const std::string& text : texts) {
            for (SkScalar maxWidth = 30; maxWidth <= 220; maxWidth += 1) {
                TextStyle style = make_text_style(20);
                style.setLetterSpacing(letterSpacing);
                auto paragraph = make_paragraph(fontCollection, {{text, style}}, maxWidth);
                REPORTER_ASSERT(reporter, paragraph->getLongestLine() <= maxWidth + kEpsilon,
                                "spacing %g width %g: longest line %g",
                                letterSpacing, maxWidth, paragraph->getLongestLine());

                auto impl = static_cast<ParagraphImpl*>(paragraph.get());
                for (auto& line : impl->lines()) {
                    REPORTER_ASSERT(reporter, line.width() <= maxWidth + kEpsilon,
                                    "spacing %g width %g: line width %g",
                                    letterSpacing, maxWidth, line.width());
                    if (line.hyphen() != nullptr) {
                        ++hyphenatedLines;
                        // The hyphen has no trailing letter spacing, so the shifted line must fit too
                        REPORTER_ASSERT(reporter,
                                        line.offset().fX + line.width() <= maxWidth + kEpsilon,
                                        "spacing %g width %g: hyphenated line ends at %g",
                                        letterSpacing, maxWidth, line.offset().fX + line.width());
                    } else if (text.compare(line.text().end, 2, kSoftHyphen) == 0) {
                        // The hyphen did not fit and there was no earlier break opportunity
                        ++linesEndedBeforeSoftHyphen;
                    }
                }
            }
        }
    }
    // Make sure the sweep exercised both the hyphenated and the fallback breaks
    REPORTER_ASSERT(reporter, hyphenatedLines > 0);
    REPORTER_ASSERT(reporter, linesEndedBeforeSoftHyphen > 0);
}

// TextStyle::setBreakAnywhere() lets a line break inside a word that would otherwise move to the
// next line as a whole, and only inside the styled range.
DEF_TEST(QuestPDF_Paragraph_BreakAnywhere, reporter) {
    auto fontCollection = make_font_collection();
    if (!fontCollection) {
        return;
    }
    TextStyle regular = make_text_style(14);
    TextStyle breakAnywhere = make_text_style(14);
    breakAnywhere.setBreakAnywhere(true);
    REPORTER_ASSERT(reporter, breakAnywhere.getBreakAnywhere() && !regular.getBreakAnywhere());
    // Both comparisons must see the flag: the builder merges equal neighbouring styles and
    // the paragraph cache keys on equalsByFonts()
    REPORTER_ASSERT(reporter, !regular.equals(breakAnywhere));
    REPORTER_ASSERT(reporter, !regular.equalsByFonts(breakAnywhere));

    const std::string first = "aaaa ";
    const std::string second = "bbbbbbbbbb";
    const SkScalar fullWidth =
            make_paragraph(fontCollection, {{first + second, regular}}, 10000)->getLongestLine();
    // The second word fits on a line of its own, but not in what is left of the first line
    const SkScalar width = fullWidth * 0.75f;

    auto firstLineEnd = [&](const TextStyle& firstStyle, const TextStyle& secondStyle) -> size_t {
        auto paragraph =
                make_paragraph(fontCollection, {{first, firstStyle}, {second, secondStyle}}, width);
        REPORTER_ASSERT(reporter, paragraph->getLongestLine() <= width + kEpsilon);
        std::vector<LineMetrics> lines;
        paragraph->getLineMetrics(lines);
        REPORTER_ASSERT(reporter, lines.size() == 2, "%zu lines", lines.size());
        return lines.empty() ? 0 : lines[0].fEndExcludingWhitespaces;
    };

    // Off: the whole second word moves to the next line
    REPORTER_ASSERT(reporter, firstLineEnd(regular, regular) == 4);
    // On: the second word is split to fill the first line
    REPORTER_ASSERT(reporter, firstLineEnd(breakAnywhere, breakAnywhere) > first.size());
    REPORTER_ASSERT(reporter, firstLineEnd(regular, breakAnywhere) > first.size());
    // The flag on a neighbouring range does not leak into the unflagged word
    REPORTER_ASSERT(reporter, firstLineEnd(breakAnywhere, regular) == 4);
}

// Fonts registered by the application are found regardless of the letter case of the family name.
DEF_TEST(QuestPDF_TypefaceFontProvider_FamilyNamesAreCaseInsensitive, reporter) {
    sk_sp<SkTypeface> typeface = load_typeface();
    if (!typeface) {
        return;
    }
    auto provider = sk_make_sp<TypefaceFontProvider>();
    provider->registerTypeface(typeface, SkString("Roboto"));

    for (const char* name : {"Roboto", "roboto", "ROBOTO", "rObOtO"}) {
        sk_sp<SkFontStyleSet> set = provider->matchFamily(name);
        REPORTER_ASSERT(reporter, set && set->count() == 1, "family '%s'", name);
        REPORTER_ASSERT(reporter, provider->matchFamilyStyle(name, SkFontStyle()) != nullptr,
                        "family '%s'", name);
    }
    sk_sp<SkFontStyleSet> unknown = provider->matchFamily("Unknown");
    REPORTER_ASSERT(reporter, !unknown || unknown->count() == 0);
    sk_sp<SkFontStyleSet> null = provider->matchFamily(nullptr);
    REPORTER_ASSERT(reporter, !null || null->count() == 0);

    // A different spelling joins the existing family, which keeps the name it was created with
    provider->registerTypeface(typeface, SkString("ROBOTO"));
    REPORTER_ASSERT(reporter, provider->countFamilies() == 1);
    SkString familyName;
    provider->getFamilyName(0, &familyName);
    REPORTER_ASSERT(reporter, familyName.equals("Roboto"));
    sk_sp<SkFontStyleSet> set = provider->matchFamily("roboto");
    REPORTER_ASSERT(reporter, set && set->count() == 2);
}

// Text blobs carry the source text and the glyph-to-text mapping, which the PDF backend needs to
// make ligatures and contextual forms extractable. Shadow blobs must not carry it, or the text
// would be extracted once more per shadow.
DEF_TEST(QuestPDF_Paragraph_TextBlobsCarrySourceText, reporter) {
    auto fontCollection = make_font_collection();
    if (!fontCollection) {
        return;
    }
    const std::string text = "office waffle";
    TextStyle style = make_text_style(14);
    style.addShadow(TextShadow(SK_ColorGRAY, {1, 1}, 0));
    auto paragraph = make_paragraph(fontCollection, {{text, style}}, 500);

    BlobCanvas canvas;
    paragraph->paint(&canvas, 0, 0);
    REPORTER_ASSERT(reporter, canvas.fBlobs.size() == 2, "%zu blobs", canvas.fBlobs.size());

    int blobsWithText = 0;
    for (const auto& blob : canvas.fBlobs) {
        SkTextBlobRunIterator it(blob.fBlob.get());
        REPORTER_ASSERT(reporter, !it.done());
        if (it.done() || it.textSize() == 0) {
            continue;
        }
        ++blobsWithText;
        REPORTER_ASSERT(reporter, it.textSize() == text.size());
        REPORTER_ASSERT(reporter, it.textSize() == text.size() &&
                                  memcmp(it.text(), text.data(), text.size()) == 0);
        // The ligatures leave fewer glyphs than characters; clusters point back into the text
        REPORTER_ASSERT(reporter, it.glyphCount() < text.size(), "%u glyphs", it.glyphCount());
        REPORTER_ASSERT(reporter, it.clusters() != nullptr && it.clusters()[0] == 0);
        for (uint32_t i = 1; it.clusters() != nullptr && i < it.glyphCount(); ++i) {
            REPORTER_ASSERT(reporter, it.clusters()[i] > it.clusters()[i - 1] &&
                                      it.clusters()[i] < text.size());
        }
        it.next();
        REPORTER_ASSERT(reporter, it.done());
    }
    REPORTER_ASSERT(reporter, blobsWithText == 1);
}

// HarfBuzz shapes a soft hyphen or zero-width space to the font's space glyph with no advance.
// Drawing that glyph puts a space into the PDF text layer and splits the word for search and
// copy-paste (QuestPDF issue #1494), so it is left out of the drawn blobs and their source
// text, while layout keeps it. Other hidden characters are drawn as before.
DEF_TEST(QuestPDF_Paragraph_HiddenCharactersAreNotDrawn, reporter) {
    auto fontCollection = make_font_collection();
    if (!fontCollection) {
        return;
    }
    const SkGlyphID spaceGlyph = load_typeface()->unicharToGlyph(' ');

    struct Drawn {
        std::string fText;
        int fSpaceGlyphs = 0;
    };
    auto draw = [&](const std::string& text, SkScalar width, int line) {
        auto paragraph = make_paragraph(fontCollection, {{text, make_text_style(14)}}, width);
        BlobCanvas canvas;
        paragraph->paintLines(&canvas, line, line);
        Drawn drawn;
        for (const auto& blob : canvas.fBlobs) {
            for (SkTextBlobRunIterator it(blob.fBlob.get()); !it.done(); it.next()) {
                drawn.fText.append(it.text(), it.textSize());
                for (uint32_t i = 0; i < it.glyphCount(); ++i) {
                    drawn.fSpaceGlyphs += it.glyphs()[i] == spaceGlyph;
                }
                for (uint32_t i = 0; it.clusters() != nullptr && i < it.glyphCount(); ++i) {
                    REPORTER_ASSERT(reporter, it.clusters()[i] < it.textSize());
                }
            }
        }
        return drawn;
    };

    // No break at the hidden characters: only the real space is drawn
    const std::string text = std::string("light") + kSoftHyphen + "house light\xE2\x80\x8Bhouse";
    Drawn drawn = draw(text, 500, 0);
    REPORTER_ASSERT(reporter, drawn.fText == "lighthouse lighthouse", "%s", drawn.fText.c_str());
    REPORTER_ASSERT(reporter, drawn.fSpaceGlyphs == 1, "%d space glyphs", drawn.fSpaceGlyphs);

    // Layout is unchanged: the hidden characters still take no room
    auto hidden = make_paragraph(fontCollection, {{text, make_text_style(14)}}, 500);
    auto plain = make_paragraph(fontCollection, {{"lighthouse lighthouse", make_text_style(14)}}, 500);
    REPORTER_ASSERT(reporter, std::fabs(hidden->getMaxIntrinsicWidth() -
                                        plain->getMaxIntrinsicWidth()) < kEpsilon);

    // Break at the soft hyphen: the first line draws "light" and the hyphen, and no space
    const std::string word = std::string("light") + kSoftHyphen + "house";
    auto narrow = make_paragraph(fontCollection, {{word, make_text_style(14)}}, 50);
    REPORTER_ASSERT(reporter, narrow->lineNumber() == 2, "%zu lines", narrow->lineNumber());
    Drawn first = draw(word, 50, 0);
    REPORTER_ASSERT(reporter, first.fText == "light", "%s", first.fText.c_str());
    REPORTER_ASSERT(reporter, first.fSpaceGlyphs == 0, "%d space glyphs", first.fSpaceGlyphs);

    // A zero-width non-joiner changes how letters join (Persian, Indic), so it is not a
    // line-break hint: its glyph and text stay, for the PDF backend to map through /ActualText
    const std::string joiner = "shelf\xE2\x80\x8C" "ful";
    Drawn kept = draw(joiner, 500, 0);
    REPORTER_ASSERT(reporter, kept.fText == joiner, "%s", kept.fText.c_str());
    REPORTER_ASSERT(reporter, kept.fSpaceGlyphs == 1, "%d space glyphs", kept.fSpaceGlyphs);
}

// The underline starts at the distance below the baseline the font asks for, and the two lines of
// a double underline are one line thickness apart.
DEF_TEST(QuestPDF_Paragraph_DecorationGeometry, reporter) {
    auto fontCollection = make_font_collection();
    sk_sp<SkTypeface> typeface = load_typeface();
    if (!fontCollection || !typeface) {
        return;
    }
    const SkScalar fontSize = 40;
    SkFontMetrics metrics;
    SkFont(typeface, fontSize).getMetrics(&metrics);
    SkScalar thickness = fontSize / 14.0f;
    if ((metrics.fFlags & SkFontMetrics::kUnderlineThicknessIsValid_Flag) &&
        metrics.fUnderlineThickness > 0) {
        thickness = metrics.fUnderlineThickness;
    }
    SkScalar position = metrics.fXHeight / 4;
    if ((metrics.fFlags & SkFontMetrics::kUnderlinePositionIsValid_Flag) &&
        metrics.fUnderlinePosition > 0) {
        position = metrics.fUnderlinePosition;
    }

    auto paint = [&](TextDecorationStyle decorationStyle, SkScalar* baseline) {
        TextStyle style = make_text_style(fontSize);
        style.setDecoration(TextDecoration::kUnderline);
        style.setDecorationStyle(decorationStyle);
        style.setDecorationColor(SK_ColorBLACK);
        auto paragraph = make_paragraph(fontCollection, {{"underlined", style}}, 1000);
        std::vector<LineMetrics> lines;
        paragraph->getLineMetrics(lines);
        *baseline = lines.empty() ? 0 : (SkScalar)lines[0].fBaseline;
        DecorationPainter painter;
        paragraph->paint(&painter, 0, 0);
        return painter.fRects;
    };

    SkScalar baseline = 0;
    auto solid = paint(TextDecorationStyle::kSolid, &baseline);
    REPORTER_ASSERT(reporter, solid.size() == 1);
    if (solid.size() == 1) {
        REPORTER_ASSERT(reporter, SkScalarNearlyEqual(solid[0].height(), thickness, kEpsilon));
        REPORTER_ASSERT(reporter, SkScalarNearlyEqual(solid[0].top(), baseline + position, kEpsilon),
                        "underline top %g, expected %g", solid[0].top(), baseline + position);
    }

    auto doubled = paint(TextDecorationStyle::kDouble, &baseline);
    REPORTER_ASSERT(reporter, doubled.size() == 2);
    if (doubled.size() == 2) {
        REPORTER_ASSERT(reporter, SkScalarNearlyEqual(doubled[0].top(), baseline + position, kEpsilon));
        REPORTER_ASSERT(reporter,
                        SkScalarNearlyEqual(doubled[1].top() - doubled[0].bottom(), thickness, kEpsilon),
                        "gap %g, expected %g", doubled[1].top() - doubled[0].bottom(), thickness);
    }
}
