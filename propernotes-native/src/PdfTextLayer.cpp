// SPDX-License-Identifier: GPL-3.0-only
#include "PdfTextLayer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace propernotes {
namespace {

qreal squaredLength(const QPointF& vector)
{
    return QPointF::dotProduct(vector, vector);
}

std::array<QPointF, 4> vertices(const TextMarkupQuad& quad)
{
    return {quad.topLeft, quad.topRight, quad.bottomRight, quad.bottomLeft};
}

qreal distanceToQuadSquared(const QPointF& point, const TextMarkupQuad& quad)
{
    const auto corners = vertices(quad);
    bool positive = false;
    bool negative = false;
    qreal minimum = std::numeric_limits<qreal>::infinity();
    for (size_t i = 0; i < corners.size(); ++i) {
        const QPointF edge = corners[(i + 1) % corners.size()] - corners[i];
        const QPointF delta = point - corners[i];
        const qreal cross = edge.x() * delta.y() - edge.y() * delta.x();
        positive |= cross > 0;
        negative |= cross < 0;
        const qreal divisor = squaredLength(edge);
        const qreal fraction = divisor > 0
            ? std::clamp(QPointF::dotProduct(delta, edge) / divisor, qreal(0), qreal(1)) : 0;
        minimum = std::min(minimum, squaredLength(delta - edge * fraction));
    }
    return positive && negative ? minimum : 0;
}

// Merge adjacent glyph boxes along the same typographic line. Keep the four
// named corners in their original orientation for the PDF markup writer.
bool mergeQuad(TextMarkupQuad& left, const TextMarkupQuad& right)
{
    const QPointF baseline = left.topRight - left.topLeft;
    const QPointF rightBaseline = right.topRight - right.topLeft;
    const qreal width = std::sqrt(squaredLength(baseline));
    const qreal nextWidth = std::sqrt(squaredLength(rightBaseline));
    if (width <= 0 || nextWidth <= 0)
        return false;
    const QPointF along = baseline / width;
    if (QPointF::dotProduct(along, rightBaseline / nextWidth) < 0.99985)
        return false;
    const QPointF perpendicular(-along.y(), along.x());
    const qreal height = std::abs(QPointF::dotProduct(left.bottomLeft - left.topLeft, perpendicular));
    const qreal nextHeight = std::abs(QPointF::dotProduct(right.bottomLeft - right.topLeft, perpendicular));
    const qreal tolerance = std::max(qreal(0.08), std::min(height, nextHeight) * 0.25);
    if (std::abs(QPointF::dotProduct(right.topLeft - left.topLeft, perpendicular)) > tolerance
        || std::abs(QPointF::dotProduct(right.bottomLeft - left.bottomLeft, perpendicular)) > tolerance)
        return false;
    const qreal start = QPointF::dotProduct(right.topLeft - left.topLeft, along);
    const qreal end = QPointF::dotProduct(right.topRight - left.topLeft, along);
    const qreal allowance = std::max(height, nextHeight) * 0.7;
    if (start > width + allowance || end < -allowance)
        return false;

    const QPointF origin = left.topLeft;
    qreal first = 0;
    qreal last = width;
    qreal top = 0;
    qreal bottom = QPointF::dotProduct(left.bottomLeft - origin, perpendicular);
    const bool topAbove = bottom >= 0;
    const std::array<const TextMarkupQuad*, 2> boxes{&left, &right};
    for (const auto* quad : boxes) {
        for (const QPointF& corner : vertices(*quad)) {
            const QPointF delta = corner - origin;
            const qreal alongValue = QPointF::dotProduct(delta, along);
            const qreal downValue = QPointF::dotProduct(delta, perpendicular);
            first = std::min(first, alongValue);
            last = std::max(last, alongValue);
            top = topAbove ? std::min(top, downValue) : std::max(top, downValue);
            bottom = topAbove ? std::max(bottom, downValue) : std::min(bottom, downValue);
        }
    }
    left = {origin + along * first + perpendicular * top,
            origin + along * last + perpendicular * top,
            origin + along * first + perpendicular * bottom,
            origin + along * last + perpendicular * bottom};
    return true;
}

QVector<TextMarkupQuad> quadsForGlyphs(const QVector<PdfTextGlyph>& glyphs, int first, int pastLast,
                                      bool* reviewSupported = nullptr)
{
    QVector<TextMarkupQuad> result;
    int previousLine = -1;
    bool review = true;
    for (int i = first; i < pastLast; ++i) {
        const PdfTextGlyph& glyph = glyphs[i];
        review &= glyph.reviewSupported;
        if (!result.isEmpty() && glyph.line == previousLine && mergeQuad(result.back(), glyph.quad))
            continue;
        if (result.size() == MaxTextMarkupQuads) {
            review = false;
            break;
        }
        result.append(glyph.quad);
        previousLine = glyph.line;
    }
    if (reviewSupported)
        *reviewSupported = review && !result.isEmpty();
    return result;
}

} // namespace

int PdfTextPage::hitTest(const QPointF& point, qreal maxDistance) const
{
    if (!std::isfinite(point.x()) || !std::isfinite(point.y()) || std::isnan(maxDistance) || maxDistance < 0)
        return -1;
    qreal bestDistance = maxDistance * maxDistance;
    qreal bestCenterDistance = std::numeric_limits<qreal>::infinity();
    int best = -1;
    for (int i = 0; i < glyphs.size(); ++i) {
        const auto& quad = glyphs[i].quad;
        const auto corners = vertices(quad);
        const auto x = [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); };
        const auto y = [](const QPointF& a, const QPointF& b) { return a.y() < b.y(); };
        const auto [left, right] = std::minmax_element(corners.cbegin(), corners.cend(), x);
        const auto [bottom, top] = std::minmax_element(corners.cbegin(), corners.cend(), y);
        const qreal dx = std::max({left->x() - point.x(), point.x() - right->x(), qreal(0)});
        const qreal dy = std::max({bottom->y() - point.y(), point.y() - top->y(), qreal(0)});
        if (dx * dx + dy * dy > bestDistance)
            continue;
        const qreal distance = distanceToQuadSquared(point, quad);
        if (distance > bestDistance)
            continue;
        const QPointF center = (quad.topLeft + quad.topRight + quad.bottomLeft + quad.bottomRight) / 4;
        const qreal centerDistance = squaredLength(point - center);
        if (distance < bestDistance || (distance == bestDistance && centerDistance < bestCenterDistance)) {
            best = i;
            bestDistance = distance;
            bestCenterDistance = centerDistance;
        }
    }
    return best;
}

PdfTextSelection PdfTextPage::select(int firstGlyph, int lastGlyph) const
{
    if (firstGlyph > lastGlyph)
        std::swap(firstGlyph, lastGlyph);
    PdfTextSelection result;
    if (firstGlyph < 0 || lastGlyph >= glyphs.size())
        return result;
    const int start = glyphs[firstGlyph].offset;
    const int end = glyphs[lastGlyph].offset + glyphs[lastGlyph].length;
    result.text = text.mid(start, end - start);
    result.quads = quadsForGlyphs(glyphs, firstGlyph, lastGlyph + 1, &result.reviewSupported);
    return result;
}

QVector<PdfTextMatch> PdfTextPage::search(const QString& query, bool caseSensitive, int maximumMatches) const
{
    QVector<PdfTextMatch> matches;
    const QString needle = query.simplified();
    if (needle.isEmpty() || needle.size() > 512 || maximumMatches <= 0 || glyphs.isEmpty())
        return matches;

    // An inferred line break is a space for literal search. Page boundaries are
    // separate; offsets are preserved for snippets and result geometry.
    QString searchable = text;
    searchable.replace(QLatin1Char('\n'), QLatin1Char(' '));
    const auto sensitivity = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    // Allow one extra probe match so the caller can report the document limit.
    const int limit = std::min(maximumMatches, 1001);
    qsizetype nextOffset = 0;
    while (matches.size() < limit) {
        const qsizetype offset = searchable.indexOf(needle, nextOffset, sensitivity);
        if (offset < 0)
            break;
        const qsizetype end = offset + needle.size();
        nextOffset = end;
        const auto first = std::lower_bound(glyphs.cbegin(), glyphs.cend(), offset,
            [](const PdfTextGlyph& glyph, qsizetype index) { return glyph.offset + glyph.length <= index; });
        const auto last = std::lower_bound(first, glyphs.cend(), end,
            [](const PdfTextGlyph& glyph, qsizetype index) { return glyph.offset < index; });
        if (first != last)
            matches.append({int(offset), int(needle.size()),
                quadsForGlyphs(glyphs, int(first - glyphs.cbegin()), int(last - glyphs.cbegin()))});
    }
    return matches;
}

} // namespace propernotes
