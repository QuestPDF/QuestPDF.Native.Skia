/*
 * Copyright 2020 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkString.h"
#include "include/utils/SkNoDrawCanvas.h"
#include "modules/skresources/include/SkResources.h"
#include "modules/skshaper/utils/FactoryHelpers.h"
#include "modules/svg/include/SkSVGIDMapper.h"
#include "modules/svg/include/SkSVGRenderContext.h"
#include "modules/svg/include/SkSVGText.h"
#include "modules/svg/include/SkSVGTypes.h"
#include "modules/svg/src/SkSVGTextPriv.h"
#include "tests/Test.h"
#include "tools/fonts/FontToolUtils.h"

#include <vector>

DEF_TEST(Svg_Text_PosProvider, r) {
    const auto L = [](float x) { return SkSVGLength(x); };
    const float N = SkSVGTextContext::PosAttrs()[SkSVGTextContext::PosAttrs::kX];

    static const struct PosTestDesc {
        size_t                   offseta;
        std::vector<SkSVGLength> xa, ya;

        size_t                   offsetb;
        std::vector<SkSVGLength> xb, yb;

        std::vector<SkPoint>     expected;
    } gTests[] = {
        {
            0, {}, {},
            0, {}, {},

            { {N,N} }
        },

        {
            0, { L(1) }, {},
            0, {      }, {},

            { {1,N}, {N,N} }
        },
        {
            0, {       }, {},
            0, { L(10) }, {},

            { {10,N}, {N,N} }
        },
        {
            0, { L( 1) }, {},
            0, { L(10) }, {},

            { {10,N}, {N,N} }
        },
        {
            0, { L( 1), L(2) }, {},
            0, { L(10)       }, {},

            { {10,N}, {2,N}, {N,N} }
        },
        {
            0, { L(1), L( 2) }, {},
            1, {       L(20) }, {},

            { {1,N}, {20,N}, {N,N} }
        },
        {
            0, { L(1), L( 2), L(3) }, {},
            1, {       L(20)       }, {},

            { {1,N}, {20,N}, {3,N}, {N,N} }
        },
        {
            0, { L(1), L(2), L( 3) }, {},
            2, {             L(30) }, {},

            { {1,N}, {2,N}, {30,N}, {N,N} }
        },
        {
            0, { L(1)              }, {},
            2, {             L(30) }, {},

            { {1,N}, {N,N}, {30,N}, {N,N} }
        },


        {
            0, {}, { L(4) },
            0, {}, {      },

            { {N,4}, {N,N} }
        },
        {
            0, {}, {       },
            0, {}, { L(40) },

            { {N,40}, {N,N} }
        },
        {
            0, {}, { L( 4) },
            0, {}, { L(40) },

            { {N,40}, {N,N} }
        },
        {
            0, {}, { L( 4), L(5) },
            0, {}, { L(40)       },

            { {N,40}, {N,5}, {N,N} }
        },
        {
            0, {}, { L(4), L( 5) },
            1, {}, {       L(50) },

            { {N,4}, {N,50}, {N,N} }
        },
        {
            0, {}, { L(4), L( 5), L(6) },
            1, {}, {       L(50)       },

            { {N,4}, {N,50}, {N,6}, {N,N} }
        },
        {
            0, {}, { L(4), L(5), L( 6) },
            2, {}, {             L(60) },

            { {N,4}, {N,5}, {N,60}, {N,N} }
        },
        {
            0, {}, { L(4)              },
            2, {}, {             L(60) },

            { {N,4}, {N,N}, {N,60}, {N,N} }
        },

        {
            0, { L( 1), L(2)}, { L( 4)        },
            0, { L(10)      }, { L(40), L(50) },

            { {10,40}, {2,50}, {N,N} }
        },
        {
            0, { L(1), L( 2), L(3) }, { L(4), L( 5)        },
            1, {       L(20)       }, {       L(50), L(60) },

            { {1,4}, {20,50}, {3,60}, {N,N} }
        },
    };

    const SkSVGTextContext::ShapedTextCallback mock_cb =
        [](const SkSVGRenderContext&, const sk_sp<SkTextBlob>&, const SkPaint*, const SkPaint*) {};

    auto test = [&](const PosTestDesc& tst) {
        auto a = SkSVGText::Make();
        auto b = SkSVGTSpan::Make();
        a->appendChild(b);

        a->setX(tst.xa);
        a->setY(tst.ya);
        b->setX(tst.xb);
        b->setY(tst.yb);

        const SkSVGIDMapper mapper;
        const SkSVGLengthContext lctx({0,0});
        const SkSVGPresentationContext pctx;
        SkNoDrawCanvas canvas(0, 0);
        sk_sp<SkFontMgr> fmgr = ToolUtils::TestFontMgr();
        sk_sp<skresources::ResourceProvider> rp;
        sk_sp<SkShapers::Factory> shaping = SkShapers::BestAvailable();
        const SkSVGRenderContext ctx(&canvas,
                                     fmgr,
                                     rp,
                                     mapper,
                                     lctx,
                                     pctx,
                                     {nullptr, nullptr},
                                     shaping);

        SkSVGTextContext tctx(ctx, mock_cb);
        SkSVGTextContext::ScopedPosResolver pa(*a, lctx, &tctx, tst.offseta);
        SkSVGTextContext::ScopedPosResolver pb(*b, lctx, &tctx, tst.offsetb);

        for (size_t i = 0; i < tst.expected.size(); ++i) {
            const auto& exp = tst.expected[i];
            auto pos = i >= tst.offsetb ? pb.resolve(i) : pa.resolve(i);

            REPORTER_ASSERT(r, pos[SkSVGTextContext::PosAttrs::kX] == exp.fX);
            REPORTER_ASSERT(r, pos[SkSVGTextContext::PosAttrs::kY] == exp.fY);
        }
    };

    for (const auto& tst : gTests) {
        test(tst);
    }
}

DEF_TEST(Svg_Text_DominantBaseline, r) {
    using Type = SkSVGDominantBaseline::Type;

    static const struct {
        const char* fValue;
        Type        fExpected;
    } gValidTests[] = {
        { "auto"            , Type::kAuto           },
        { "alphabetic"      , Type::kAlphabetic     },
        { "middle"          , Type::kMiddle         },
        { "central"         , Type::kCentral        },
        { "hanging"         , Type::kHanging        },
        { "mathematical"    , Type::kMathematical   },
        { "ideographic"     , Type::kIdeographic    },
        { "text-before-edge", Type::kTextBeforeEdge },
        { "text-after-edge" , Type::kTextAfterEdge  },
        { "use-script"      , Type::kUseScript      },
        { "no-change"       , Type::kNoChange       },
        { "reset-size"      , Type::kResetSize      },
        // CSS Inline Layout 3 spellings of the two edge values.
        { "text-top"        , Type::kTextBeforeEdge },
        { "text-bottom"     , Type::kTextAfterEdge  },
    };

    for (const auto& tst : gValidTests) {
        auto text = SkSVGText::Make();
        REPORTER_ASSERT(r, text->setAttribute("dominant-baseline", tst.fValue));
        REPORTER_ASSERT(r, text->getDominantBaseline().isValue());
        REPORTER_ASSERT(r, text->getDominantBaseline()->type() == tst.fExpected);

        // The property is not text-specific; it resolves on any node, and via 'style'.
        auto tspan = SkSVGTSpan::Make();
        REPORTER_ASSERT(r, tspan->setAttribute("dominant-baseline", tst.fValue));
        REPORTER_ASSERT(r, tspan->getDominantBaseline()->type() == tst.fExpected);

        auto styled = SkSVGText::Make();
        styled->setAttribute("style", SkStringPrintf("dominant-baseline: %s", tst.fValue).c_str());
        REPORTER_ASSERT(r, styled->getDominantBaseline().isValue());
        REPORTER_ASSERT(r, styled->getDominantBaseline()->type() == tst.fExpected);
    }

    // Values which must not set the property, leaving it to inherit.
    static const char* gUnsetTests[] = {
        "not-a-value",
        // 'alignment-baseline'-only spellings are not 'dominant-baseline' values.
        "baseline",
        "before-edge",
        "after-edge",
        // Trailing garbage must be rejected, not silently truncated.
        "middle bogus",
    };

    for (const char* value : gUnsetTests) {
        auto text = SkSVGText::Make();
        REPORTER_ASSERT(r, !text->setAttribute("dominant-baseline", value));
        REPORTER_ASSERT(r, !text->getDominantBaseline().isValue());
    }

    // 'alignment-baseline' is not supported at all.
    for (const char* attrValue : {"middle", "hanging", "central"}) {
        auto text = SkSVGText::Make();
        REPORTER_ASSERT(r, !text->setAttribute("alignment-baseline", attrValue));
        REPORTER_ASSERT(r, !text->getDominantBaseline().isValue());

        auto tspan = SkSVGTSpan::Make();
        REPORTER_ASSERT(r, !tspan->setAttribute("alignment-baseline", attrValue));
        REPORTER_ASSERT(r, !tspan->getDominantBaseline().isValue());
    }

    // Explicit 'inherit' is handled generically.
    {
        auto text = SkSVGText::Make();
        REPORTER_ASSERT(r, text->setAttribute("dominant-baseline", "inherit"));
        REPORTER_ASSERT(r, !text->getDominantBaseline().isValue());
    }
}
