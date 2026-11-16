// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QColor>
#include <QImage>
#include <QMetaType>
#include <QPointF>
#include <QPainterPath>
#include <QPolygonF>
#include <QString>
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
Q_DECLARE_METATYPE(propernotes::RenderedPage)
