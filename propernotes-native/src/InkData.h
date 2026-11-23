// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QColor>
#include <QImage>
#include <QMetaType>
#include <QPointF>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPolygonF>
#include <QPainter>
#include <QFont>
#include <QFontMetricsF>
#include <QRectF>
#include <QString>
#include <QTextLayout>
#include <QTextOption>
#include <QTransform>
#include <QUuid>
#include <QVector>
#include <algorithm>
#include <cmath>

namespace propernotes {

// Canonical PDF user-space coordinates, in PDF points. The render transform
// supplied by PdfDocument handles the crop origin, page rotation and Y axis.
struct InkPoint {
    QPointF position;
    qreal pressure = 0.6;
};

enum class InkStrokeKind { Pen, Highlighter };

inline constexpr qreal MinInkStrokeWidthPt = 0.05;
inline constexpr qreal MaxInkStrokeWidthPt = 1000;
inline constexpr qreal MaxInkCoordinate = 1e8;

struct InkStroke {
    QUuid id = QUuid::createUuid();
    QColor color = QColor(25, 55, 95);
    qreal widthPt = 2.0;
    QVector<InkPoint> points;
    InkStrokeKind kind = InkStrokeKind::Pen;
};

// Rect uses canonical PDF coordinates (Y increases upwards). Text starts at
// (rect.left(), rect.bottom()) and is laid out downwards inside that rectangle.
// Text is plain Unicode, with no rich text, external resources or embedded font.
struct TextBox {
    QUuid id = QUuid::createUuid();
    QRectF rect;
    QString text;
    QString fontFamily = QStringLiteral("Segoe UI");
    qreal fontSizePt = 12.0;
    QColor color = QColor(25, 55, 95);
};

enum class TextMarkupKind { Highlight, Underline, StrikeOut, Squiggly };

// Canonical PDF coordinates, in typographic order along the text direction.
// The top and bottom edges can be rotated relative to the PDF page axes.
struct TextMarkupQuad {
    QPointF topLeft;
    QPointF topRight;
    QPointF bottomLeft;
    QPointF bottomRight;

    bool operator==(const TextMarkupQuad&) const = default;
};

struct TextMarkup {
    QUuid id = QUuid::createUuid();
    TextMarkupKind kind = TextMarkupKind::Highlight;
    QColor color = QColor(255, 224, 64, 136);
    QVector<TextMarkupQuad> quads;

    bool operator==(const TextMarkup&) const = default;
};

inline constexpr qsizetype MaxTextMarkupQuads = 2048;
inline constexpr qsizetype MaxDocumentTextMarkupQuads = 100'000;
inline constexpr qsizetype MaxDocumentTextMarkups = 10'000;
inline constexpr qsizetype MaxDocumentTextMarkupPathElements = 1'000'000;

enum class ShapeKind { Line, Arrow, Rectangle, Ellipse };

struct ShapeStyle {
    QColor strokeColor = QColor("#202833");
    qreal widthPt = 2.5;
    QColor fillColor = Qt::transparent;

    bool operator==(const ShapeStyle&) const = default;
};

struct ShapeAnnotation {
    QUuid id = QUuid::createUuid();
    ShapeKind kind = ShapeKind::Line;
    QPointF start;
    QPointF end;
    qreal rotationDegrees = 0;
    ShapeStyle style;

    bool operator==(const ShapeAnnotation&) const = default;
};

inline constexpr qsizetype MaxDocumentShapes = 10'000;

struct PageAnnotations {
    QVector<InkStroke> strokes;
    QVector<TextBox> textBoxes;
    QVector<TextMarkup> textMarkups;
    QVector<ShapeAnnotation> shapes;
};

inline bool operator==(const InkPoint& a, const InkPoint& b)
{ return a.position == b.position && a.pressure == b.pressure; }
inline bool operator==(const InkStroke& a, const InkStroke& b)
{
    return a.id == b.id && a.color == b.color && a.widthPt == b.widthPt
        && a.points == b.points && a.kind == b.kind;
}
inline bool operator==(const TextBox& a, const TextBox& b)
{
    return a.id == b.id && a.rect == b.rect && a.text == b.text
        && a.fontFamily == b.fontFamily && a.fontSizePt == b.fontSizePt && a.color == b.color;
}
inline bool operator==(const PageAnnotations& a, const PageAnnotations& b)
{ return a.strokes == b.strokes && a.textBoxes == b.textBoxes && a.textMarkups == b.textMarkups && a.shapes == b.shapes; }

inline bool isAreaShape(ShapeKind kind)
{
    return kind == ShapeKind::Rectangle || kind == ShapeKind::Ellipse;
}

inline QPointF shapeCenter(const ShapeAnnotation& shape)
{
    return (shape.start + shape.end) / 2;
}

inline QTransform shapeRotation(const ShapeAnnotation& shape)
{
    const QPointF center = shapeCenter(shape);
    QTransform transform;
    transform.translate(center.x(), center.y());
    transform.rotate(shape.rotationDegrees);
    transform.translate(-center.x(), -center.y());
    return transform;
}

inline QPainterPath shapePath(const ShapeAnnotation& shape)
{
    QPainterPath path;
    const QRectF rect = QRectF(shape.start, shape.end).normalized();
    switch (shape.kind) {
    case ShapeKind::Line:
    case ShapeKind::Arrow:
        path.moveTo(shape.start);
        path.lineTo(shape.end);
        break;
    case ShapeKind::Rectangle:
        path.addRect(rect);
        break;
    case ShapeKind::Ellipse:
        path.addEllipse(rect);
        break;
    }
    return path;
}

inline QPainterPath shapeFill(const ShapeAnnotation& shape)
{
    if (!isAreaShape(shape.kind) || shape.style.fillColor.alpha() == 0) return {};
    return shapeRotation(shape).map(shapePath(shape));
}

inline QPainterPath shapeOutline(const ShapeAnnotation& shape)
{
    if (shape.style.strokeColor.alpha() == 0 || !std::isfinite(shape.style.widthPt) || shape.style.widthPt <= 0)
        return {};
    if (shape.kind == ShapeKind::Arrow) {
        const QPointF delta = shape.end - shape.start;
        const qreal length = std::hypot(delta.x(), delta.y());
        if (!std::isfinite(length) || length < 0.001) return {};
        const QPointF direction = delta / length;
        const QPointF normal(-direction.y(), direction.x());
        const qreal halfShaft = shape.style.widthPt / 2;
        const qreal headLength = std::min(length * 0.65, shape.style.widthPt * 4);
        const qreal halfHead = std::max(halfShaft, headLength * 0.45);
        const QPointF base = shape.end - direction * headLength;
        // A single silhouette applies alpha once to the shaft and head.
        QPainterPath outline;
        outline.moveTo(shape.start + normal * halfShaft);
        outline.lineTo(base + normal * halfShaft);
        outline.lineTo(base + normal * halfHead);
        outline.lineTo(shape.end);
        outline.lineTo(base - normal * halfHead);
        outline.lineTo(base - normal * halfShaft);
        outline.lineTo(shape.start - normal * halfShaft);
        outline.closeSubpath();
        return shapeRotation(shape).map(outline);
    }
    QPainterPathStroker stroker;
    stroker.setWidth(shape.style.widthPt);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return shapeRotation(shape).map(stroker.createStroke(shapePath(shape)));
}

inline QRectF shapeBounds(const ShapeAnnotation& shape)
{
    return shapeOutline(shape).boundingRect().united(shapeFill(shape).boundingRect());
}

inline bool validShape(const ShapeAnnotation& shape)
{
    if (shape.id.isNull() || !shape.style.strokeColor.isValid() || !shape.style.fillColor.isValid()
        || !std::isfinite(shape.style.widthPt) || shape.style.widthPt < MinInkStrokeWidthPt
        || shape.style.widthPt > MaxInkStrokeWidthPt || !std::isfinite(shape.rotationDegrees)
        || std::abs(shape.rotationDegrees) > 180)
        return false;
    const auto pointFits = [](const QPointF& p) {
        return std::abs(p.x()) <= MaxInkCoordinate && std::abs(p.y()) <= MaxInkCoordinate;
    };
    if (!pointFits(shape.start) || !pointFits(shape.end)) return false;
    const QPointF delta = shape.end - shape.start;
    switch (shape.kind) {
    case ShapeKind::Line:
    case ShapeKind::Arrow:
        if (std::hypot(delta.x(), delta.y()) < 0.001 || shape.style.fillColor.alpha() != 0)
            return false;
        break;
    case ShapeKind::Rectangle:
    case ShapeKind::Ellipse:
        if (std::abs(delta.x()) < 0.001 || std::abs(delta.y()) < 0.001)
            return false;
        break;
    default:
        return false;
    }
    const QRectF bounds = shapeBounds(shape);
    return bounds.isValid() && pointFits(bounds.topLeft()) && pointFits(bounds.bottomRight());
}

inline void paintShape(QPainter& painter, const ShapeAnnotation& shape)
{
    painter.fillPath(shapeFill(shape), shape.style.fillColor);
    painter.fillPath(shapeOutline(shape), shape.style.strokeColor);
}

inline qreal textMarkupLength(const QPointF& delta)
{
    return std::hypot(delta.x(), delta.y());
}

inline qreal textMarkupHeight(const TextMarkupQuad& quad)
{
    return (textMarkupLength(quad.topLeft - quad.bottomLeft)
            + textMarkupLength(quad.topRight - quad.bottomRight)) / 2;
}

inline int textMarkupWaveSegments(const TextMarkupQuad& quad)
{
    const qreal length = textMarkupLength(quad.bottomRight - quad.bottomLeft);
    const qreal halfPeriod = qBound(qreal(1.0), textMarkupHeight(quad) * 0.30, qreal(6.0));
    if (!std::isfinite(length) || !std::isfinite(halfPeriod))
        return 2;
    return int(qBound(qreal(2), std::ceil(length / halfPeriod), qreal(128)));
}

inline qsizetype textMarkupPathElements(const TextMarkup& markup)
{
    if (markup.kind != TextMarkupKind::Squiggly)
        return markup.quads.size() * 5;
    qsizetype count = 0;
    for (const auto& quad : markup.quads)
        count += 2 * (textMarkupWaveSegments(quad) + 1) + 1;
    return count;
}

inline bool validTextMarkupQuad(const TextMarkupQuad& quad)
{
    const QPointF vertices[] { quad.topLeft, quad.topRight, quad.bottomRight, quad.bottomLeft };
    for (const QPointF& point : vertices)
        if (!std::isfinite(point.x()) || !std::isfinite(point.y())
            || std::abs(point.x()) > 1e8 || std::abs(point.y()) > 1e8)
            return false;
    // A repeated corner or inverted/self-intersecting quad is not selectable text.
    qreal previousCross = 0;
    for (int i = 0; i < 4; ++i) {
        const QPointF a = vertices[(i + 1) % 4] - vertices[i];
        const QPointF b = vertices[(i + 2) % 4] - vertices[(i + 1) % 4];
        const qreal cross = a.x() * b.y() - a.y() * b.x();
        if (textMarkupLength(a) < 0.001 || std::abs(cross) < 1e-8
            || (i > 0 && (cross > 0) != (previousCross > 0)))
            return false;
        previousCross = cross;
    }
    return true;
}

inline bool validTextMarkup(const TextMarkup& markup)
{
    if (markup.id.isNull() || !markup.color.isValid() || markup.quads.isEmpty()
        || markup.quads.size() > MaxTextMarkupQuads)
        return false;
    switch (markup.kind) {
    case TextMarkupKind::Highlight:
    case TextMarkupKind::Underline:
    case TextMarkupKind::StrikeOut:
    case TextMarkupKind::Squiggly:
        break;
    default:
        return false;
    }
    for (const auto& quad : markup.quads)
        if (!validTextMarkupQuad(quad))
            return false;
    return true;
}

// Both overlay and PDF appearance use the same fill geometry. The small
// decorations stay inside the selected quadrilateral even on rotated text.
inline QPainterPath textMarkupPath(const TextMarkup& markup)
{
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    for (const auto& quad : markup.quads) {
        if (markup.kind == TextMarkupKind::Highlight) {
            path.moveTo(quad.topLeft);
            path.lineTo(quad.topRight);
            path.lineTo(quad.bottomRight);
            path.lineTo(quad.bottomLeft);
            path.closeSubpath();
            continue;
        }
        const qreal height = textMarkupHeight(quad);
        if (!std::isfinite(height) || height <= 0)
            continue;
        // Coordinates inside the quad: t follows the text and v runs from
        // the bottom edge toward the top edge.
        auto point = [&](qreal t, qreal v) {
            const QPointF bottom = quad.bottomLeft + (quad.bottomRight - quad.bottomLeft) * t;
            const QPointF top = quad.topLeft + (quad.topRight - quad.topLeft) * t;
            return bottom + (top - bottom) * v;
        };
        const qreal halfThickness = qMin(qreal(0.15),
            qBound(qreal(0.20), height * 0.036, qreal(1.25)) / height);
        if (markup.kind == TextMarkupKind::Squiggly) {
            const int segments = textMarkupWaveSegments(quad);
            const qreal amplitude = qMin(qreal(0.08), qreal(1.8) / height);
            auto waveCenter = [&](int i) {
                return halfThickness + amplitude * ((i % 2 == 0) ? 1.0 : 3.0);
            };
            path.moveTo(point(0, waveCenter(0) + halfThickness));
            for (int i = 1; i <= segments; ++i)
                path.lineTo(point(qreal(i) / segments, waveCenter(i) + halfThickness));
            for (int i = segments; i >= 0; --i)
                path.lineTo(point(qreal(i) / segments, waveCenter(i) - halfThickness));
        } else {
            const qreal center = markup.kind == TextMarkupKind::StrikeOut ? 0.46 : halfThickness + 0.04;
            path.moveTo(point(0, center + halfThickness));
            path.lineTo(point(1, center + halfThickness));
            path.lineTo(point(1, center - halfThickness));
            path.lineTo(point(0, center - halfThickness));
        }
        path.closeSubpath();
    }
    return path;
}

inline QRectF textMarkupBounds(const TextMarkup& markup)
{
    if (markup.quads.isEmpty())
        return QRectF();
    qreal left = markup.quads.first().topLeft.x();
    qreal right = left;
    qreal bottom = markup.quads.first().topLeft.y();
    qreal top = bottom;
    for (const auto& quad : markup.quads) {
        const QPointF corners[] { quad.topLeft, quad.topRight, quad.bottomLeft, quad.bottomRight };
        for (const auto& point : corners) {
            left = qMin(left, point.x());
            right = qMax(right, point.x());
            bottom = qMin(bottom, point.y());
            top = qMax(top, point.y());
        }
    }
    return QRectF(QPointF(left, bottom), QPointF(right, top));
}

inline void paintTextMarkup(QPainter& painter, const TextMarkup& markup)
{
    painter.save();
    painter.setCompositionMode(markup.kind == TextMarkupKind::Highlight
        ? QPainter::CompositionMode_Multiply : QPainter::CompositionMode_SourceOver);
    painter.fillPath(textMarkupPath(markup), markup.color);
    painter.restore();
}

// Paint with the caller's canonical-PDF-to-device transform. Font dimensions
// are independent of device DPI. Canvas and the PDF raster appearance share
// this exact layout, including Unicode wrapping, clipping and alpha.
inline void paintTextBox(QPainter& painter, const TextBox& box)
{
    if (!box.rect.isValid() || !std::isfinite(box.fontSizePt) || box.fontSizePt <= 0)
        return;
    painter.save();
    painter.setClipRect(box.rect, Qt::IntersectClip);
    painter.translate(box.rect.left(), box.rect.bottom());
    const qreal unit = box.fontSizePt / 1000.0;
    painter.scale(unit, -unit);
    QFont font(box.fontFamily);
    font.setPixelSize(1000);
    font.setHintingPreference(QFont::PreferNoHinting);
    painter.setFont(font);
    painter.setPen(box.color);
    painter.setRenderHint(QPainter::TextAntialiasing);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    const QFontMetricsF metrics(font, painter.device());
    option.setTabStopDistance(metrics.horizontalAdvance(QChar(' ')) * 8);
    const qreal width = box.rect.width() / unit;
    const qreal height = box.rect.height() / unit;
    const qreal emptyHeight = metrics.height();
    qreal y = 0;
    const QString normalized = QString(box.text).replace(QStringLiteral("\r\n"), QStringLiteral("\n"))
        .replace(QChar('\r'), QChar('\n'));
    for (const QString& paragraph : normalized.split(QChar('\n'))) {
        if (y >= height)
            break;
        if (paragraph.isEmpty()) {
            y += emptyHeight;
            continue;
        }
        QTextLayout layout(paragraph, font, painter.device());
        layout.setTextOption(option);
        layout.beginLayout();
        while (y < height) {
            QTextLine line = layout.createLine();
            if (!line.isValid())
                break;
            line.setLineWidth(width);
            line.setPosition(QPointF(0, y));
            y += line.height();
        }
        layout.endLayout();
        layout.draw(&painter, QPointF());
    }
    painter.restore();
}

struct RenderedPage {
    QImage image;
    QTransform pdfToImage;
    QString error;

    explicit operator bool() const { return !image.isNull() && error.isEmpty(); }
};

inline qreal strokeSampleWidth(const InkStroke& stroke, const InkPoint& point)
{
    if (stroke.kind == InkStrokeKind::Highlighter)
        return stroke.widthPt;
    const qreal pressure = std::isfinite(point.pressure)
        ? qBound(qreal(0), point.pressure, qreal(1)) : qreal(0.6);
    return stroke.widthPt * (qreal(0.25) + qreal(1.25) * pressure);
}

inline QRectF inkStrokeBounds(const InkStroke& stroke)
{
    QRectF bounds;
    // A connecting segment stays inside the combined bounds of its round caps.
    for (const auto& point : stroke.points) {
        const qreal width = strokeSampleWidth(stroke, point);
        bounds = bounds.united(QRectF(point.position - QPointF(width / 2, width / 2), QSizeF(width, width)));
    }
    return bounds;
}

// Shared by the live overlay and the PDF appearance writer. One winding fill
// paints the entire stroke once, including overlapping caps and joins.
inline QPainterPath inkStrokeOutline(const InkStroke& stroke)
{
    QPainterPath outline;
    outline.setFillRule(Qt::WindingFill);
    for (qsizetype i = 0; i < stroke.points.size(); ++i) {
        const auto& point = stroke.points[i];
        const qreal radius = strokeSampleWidth(stroke, point) / 2;
        outline.addEllipse(point.position, radius, radius);
        if (i == 0)
            continue;
        const auto& previous = stroke.points[i - 1];
        const QPointF delta = point.position - previous.position;
        const qreal length = std::hypot(delta.x(), delta.y());
        if (length <= 1e-9)
            continue;
        const QPointF normal(-delta.y() / length, delta.x() / length);
        const qreal previousRadius = strokeSampleWidth(stroke, previous) / 2;
        QPolygonF segment;
        segment << previous.position + normal * previousRadius
                << previous.position - normal * previousRadius
                << point.position - normal * radius
                << point.position + normal * radius;
        outline.addPolygon(segment);
        outline.closeSubpath();
    }
    return outline;
}

inline void paintInkStroke(QPainter& painter, const InkStroke& stroke)
{
    painter.save();
    painter.setCompositionMode(stroke.kind == InkStrokeKind::Highlighter
        ? QPainter::CompositionMode_Multiply : QPainter::CompositionMode_SourceOver);
    painter.fillPath(inkStrokeOutline(stroke), stroke.color);
    painter.restore();
}

} // namespace propernotes

Q_DECLARE_METATYPE(propernotes::InkPoint)
Q_DECLARE_METATYPE(propernotes::InkStrokeKind)
Q_DECLARE_METATYPE(propernotes::InkStroke)
Q_DECLARE_METATYPE(propernotes::TextBox)
Q_DECLARE_METATYPE(propernotes::TextMarkupQuad)
Q_DECLARE_METATYPE(propernotes::TextMarkup)
Q_DECLARE_METATYPE(propernotes::ShapeKind)
Q_DECLARE_METATYPE(propernotes::ShapeStyle)
Q_DECLARE_METATYPE(propernotes::ShapeAnnotation)
Q_DECLARE_METATYPE(propernotes::PageAnnotations)
Q_DECLARE_METATYPE(propernotes::RenderedPage)
