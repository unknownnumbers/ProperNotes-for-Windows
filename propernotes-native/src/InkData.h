// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QColor>
#include <QImage>
#include <QMetaType>
#include <QPointF>
#include <QPainterPath>
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
#include <cmath>

namespace propernotes {

// Canonical PDF user-space coordinates, in PDF points. The render transform
// supplied by PdfDocument handles the crop origin, page rotation and Y axis.
struct InkPoint {
    QPointF position;
    qreal pressure = 0.6;
};

struct InkStroke {
    QUuid id = QUuid::createUuid();
    QColor color = QColor(25, 55, 95);
    qreal widthPt = 2.0;
    QVector<InkPoint> points;
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

struct PageAnnotations {
    QVector<InkStroke> strokes;
    QVector<TextBox> textBoxes;
};

inline bool operator==(const InkPoint& a, const InkPoint& b)
{ return a.position == b.position && a.pressure == b.pressure; }
inline bool operator==(const InkStroke& a, const InkStroke& b)
{ return a.id == b.id && a.color == b.color && a.widthPt == b.widthPt && a.points == b.points; }
inline bool operator==(const TextBox& a, const TextBox& b)
{
    return a.id == b.id && a.rect == b.rect && a.text == b.text
        && a.fontFamily == b.fontFamily && a.fontSizePt == b.fontSizePt && a.color == b.color;
}
inline bool operator==(const PageAnnotations& a, const PageAnnotations& b)
{ return a.strokes == b.strokes && a.textBoxes == b.textBoxes; }

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
    const qreal pressure = std::isfinite(point.pressure)
        ? qBound(qreal(0), point.pressure, qreal(1)) : qreal(0.6);
    return stroke.widthPt * (qreal(0.25) + qreal(1.25) * pressure);
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

} // namespace propernotes

Q_DECLARE_METATYPE(propernotes::InkPoint)
Q_DECLARE_METATYPE(propernotes::InkStroke)
Q_DECLARE_METATYPE(propernotes::TextBox)
Q_DECLARE_METATYPE(propernotes::PageAnnotations)
Q_DECLARE_METATYPE(propernotes::RenderedPage)
