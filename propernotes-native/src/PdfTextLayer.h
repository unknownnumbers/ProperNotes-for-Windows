// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "InkData.h"

namespace propernotes {

// Offsets and lengths are UTF-16 indices in PdfTextPage::text. Quadrilaterals
// are in canonical PDF coordinates and don't change with zoom or page rotation.
struct PdfTextGlyph {
    int offset = 0;
    int length = 0;
    TextMarkupQuad quad;
    bool reviewSupported = false;
    int line = 0;
};

struct PdfTextSelection {
    QString text;
    // The copy/selection preview includes the whole range. Review quads are
    // the supported subset and never join across a skipped glyph.
    QVector<TextMarkupQuad> quads;
    QVector<TextMarkupQuad> reviewQuads;
    int skippedReviewGlyphs = 0;
    bool reviewSupported = false;
    QString reviewIssue;
};

struct PdfTextMatch {
    int offset = 0;
    int length = 0;
    QVector<TextMarkupQuad> quads;
};

struct PdfTextPage {
    QString text;
    QVector<PdfTextGlyph> glyphs;
    QString error;
    bool truncated = false;

    // Use a finite distance for selection start and infinity for drag endpoints.
    int hitTest(const QPointF& point, qreal maxDistance = 12) const;
    PdfTextSelection select(int firstGlyph, int lastGlyph) const;
    QVector<PdfTextMatch> search(const QString& query, bool caseSensitive = false,
                                int maximumMatches = 1000) const;
};

} // namespace propernotes
