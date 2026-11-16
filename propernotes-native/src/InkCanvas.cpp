// SPDX-License-Identifier: GPL-3.0-only
#include "InkCanvas.h"

#include <QEvent>
#include <QFocusEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace propernotes {
namespace {
qreal pointSegmentDistance(const QPointF& point, const QPointF& a, const QPointF& b)
{
    const QPointF edge = b - a;
    const qreal length2 = QPointF::dotProduct(edge, edge);
    const qreal t = length2 > 0 ? std::clamp(QPointF::dotProduct(point - a, edge) / length2, 0.0, 1.0) : 0;
    return QLineF(point, a + edge * t).length();
}

qreal segmentDistance(const QPointF& a, const QPointF& b, const QPointF& c, const QPointF& d)
{
    if (QLineF(a, b).intersects(QLineF(c, d), nullptr) == QLineF::BoundedIntersection)
        return 0;
    return std::min({pointSegmentDistance(a, c, d), pointSegmentDistance(b, c, d),
                     pointSegmentDistance(c, a, b), pointSegmentDistance(d, a, b)});
}

bool strokeHit(const InkStroke& stroke, const QPointF& from, const QPointF& to, qreal radius)
{
    if (stroke.points.isEmpty())
        return false;
    if (stroke.points.size() == 1)
        return pointSegmentDistance(stroke.points.front().position, from, to) <= radius + strokeSampleWidth(stroke, stroke.points.front()) / 2;
    for (qsizetype i = 1; i < stroke.points.size(); ++i) {
        const auto& a = stroke.points[i - 1];
        const auto& b = stroke.points[i];
        const qreal halfWidth = std::max(strokeSampleWidth(stroke, a), strokeSampleWidth(stroke, b)) / 2;
        if (segmentDistance(from, to, a.position, b.position) <= radius + halfWidth)
            return true;
    }
    return false;
}
} // namespace

InkCanvas::InkCanvas(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_AcceptTouchEvents);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAccessibleName(QStringLiteral("PDF 页面和原生墨迹"));
    updateCursor();
}

void InkCanvas::setPage(int pageIndex, const QImage& background, const QTransform& pdfToImage, const QRectF& pageRect, qreal rasterScale,
                        const QVector<InkStroke>& strokes)
{
    finishGesture();
    if (pageIndex != m_pageIndex) {
        m_undo.clear();
        Q_EMIT undoAvailabilityChanged(false);
    }
    m_pageIndex = pageIndex;
    m_background = background;
    m_background.setDevicePixelRatio(1.0);
    m_pdfToImage = pdfToImage;
    m_pageRect = pageRect.normalized();
    m_rasterScale = rasterScale;
    const qreal matrixScale = std::hypot(pdfToImage.m11(), pdfToImage.m12());
    m_pageUserUnit = matrixScale / rasterScale;
    bool invertible = false;
    m_imageToPdf = pdfToImage.inverted(&invertible);
    if (!invertible || background.isNull() || m_pageRect.isEmpty() || !std::isfinite(rasterScale) || rasterScale <= 0 ||
        !std::isfinite(m_pageUserUnit) || m_pageUserUnit <= 0) {
        clearPage();
        Q_EMIT activity(QStringLiteral("页面坐标变换不可用，已停止编辑。"));
        return;
    }
    m_strokes = strokes;
    rebuildInkCache();
    updateExtent();
}

void InkCanvas::clearPage()
{
    finishGesture();
    m_pageIndex = -1;
    m_background = {};
    m_inkCache = {};
    m_strokes.clear();
    m_undo.clear();
    m_editingEnabled = false;
    Q_EMIT undoAvailabilityChanged(false);
    updateExtent();
}

void InkCanvas::setTool(Tool tool)
{
    finishGesture();
    m_tool = tool;
    updateCursor();
    update();
}

void InkCanvas::setPenColor(const QColor& color)
{
    if (color.isValid())
        m_penColor = color;
}

void InkCanvas::setPenWidth(qreal widthPt)
{
    if (std::isfinite(widthPt))
        m_penWidthPt = std::clamp(widthPt, 0.5, 24.0);
}

void InkCanvas::setEditingEnabled(bool enabled)
{
    if (!enabled)
        finishGesture();
    m_editingEnabled = enabled;
    updateCursor();
}

void InkCanvas::setZoom(qreal zoom)
{
    if (!std::isfinite(zoom))
        return;
    m_zoom = std::clamp(zoom, 0.25, 4.0);
    updateExtent();
}

void InkCanvas::updateExtent()
{
    // The PDF transform includes UserUnit; the requested raster scale does not.
    // Keep the page's physical size while changing only screen zoom.
    m_imageScale = m_zoom / m_rasterScale;
    resize(sizeHint());
    updateGeometry();
    update();
}

QSize InkCanvas::sizeHint() const
{
    if (m_background.isNull())
        return {640, 720};
    return {static_cast<int>(std::ceil(m_background.width() * m_imageScale)) + Margin * 2,
            static_cast<int>(std::ceil(m_background.height() * m_imageScale)) + Margin * 2};
}

QTransform InkCanvas::pdfToWidget() const
{
    return {m_pdfToImage.m11() * m_imageScale, m_pdfToImage.m12() * m_imageScale,
            m_pdfToImage.m21() * m_imageScale, m_pdfToImage.m22() * m_imageScale,
            Margin + m_pdfToImage.dx() * m_imageScale, Margin + m_pdfToImage.dy() * m_imageScale};
}

QPointF InkCanvas::pdfPointAt(const QPointF& position) const
{
    return m_imageToPdf.map((position - QPointF(Margin, Margin)) / m_imageScale);
}

QPointF InkCanvas::widgetPointForPdf(const QPointF& position) const
{
    return pdfToWidget().map(position);
}

bool InkCanvas::insidePaper(const QPointF& position) const
{
    return !m_background.isNull() && QRectF(Margin, Margin, m_background.width() * m_imageScale,
                                           m_background.height() * m_imageScale).contains(position);
}

QPointF InkCanvas::clampedPdfPoint(const QPointF& position) const
{
    QPointF imagePoint = (position - QPointF(Margin, Margin)) / m_imageScale;
    imagePoint.setX(std::clamp(imagePoint.x(), 0.0, qreal(m_background.width())));
    imagePoint.setY(std::clamp(imagePoint.y(), 0.0, qreal(m_background.height())));
    QPointF pdfPoint = m_imageToPdf.map(imagePoint);
    pdfPoint.setX(std::clamp(pdfPoint.x(), m_pageRect.left(), m_pageRect.right()));
    pdfPoint.setY(std::clamp(pdfPoint.y(), m_pageRect.top(), m_pageRect.bottom()));
    return pdfPoint;
}

void InkCanvas::rebuildInkCache()
{
    if (m_background.isNull()) {
        m_inkCache = {};
        return;
    }
    m_inkCache = QImage(m_background.size(), QImage::Format_ARGB32_Premultiplied);
    m_inkCache.fill(Qt::transparent);
    QPainter painter(&m_inkCache);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setWorldTransform(m_pdfToImage);
    for (const auto& stroke : m_strokes)
        painter.fillPath(inkStrokeOutline(stroke), stroke.color);
    update();
}

void InkCanvas::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(m_darkTheme ? "#1d222b" : "#e7eaf0"));
    painter.setRenderHint(QPainter::Antialiasing);
    if (m_background.isNull()) {
        painter.setPen(QColor(m_darkTheme ? "#a8b3c6" : "#657084"));
        painter.drawText(rect().adjusted(32, 32, -32, -32), Qt::AlignCenter | Qt::TextWordWrap,
                         QStringLiteral("打开本地 PDF，开始原生手写批注\n\n笔 / 鼠标用于批注，触摸用于浏览"));
        return;
    }
    const QRectF paper(Margin, Margin, m_background.width() * m_imageScale, m_background.height() * m_imageScale);
    painter.fillRect(paper.translated(3, 4), QColor(0, 0, 0, 24));
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(paper, m_background);
    painter.drawImage(paper, m_inkCache);
    painter.save();
    painter.setClipRect(paper);
    painter.setWorldTransform(pdfToWidget());
    if (m_drawing)
        painter.fillPath(inkStrokeOutline(m_pendingStroke), m_pendingStroke.color);
    painter.restore();
    if (m_editingEnabled && (m_tool == Tool::ObjectEraser || m_hoverEraser) && insidePaper(m_hoverPosition)) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor("#2674d9"), 1.2));
        painter.drawEllipse(m_hoverPosition, EraserRadiusPt * m_zoom, EraserRadiusPt * m_zoom);
    }
}

void InkCanvas::pushUndo(const QVector<InkStroke>& before)
{
    m_undo.append(before);
    if (m_undo.size() > MaximumUndoEntries)
        m_undo.removeFirst();
}

void InkCanvas::beginInk(const QPointF& position, qreal pressure, bool eraser)
{
    if (!m_editingEnabled || hasActiveGesture() || !insidePaper(position))
        return;
    setFocus(Qt::MouseFocusReason);
    m_beforeGesture = m_strokes;
    m_previousErasePoint = clampedPdfPoint(position);
    if (eraser) {
        m_erasing = true;
        m_eraseChanged = false;
        eraseBetween(m_previousErasePoint, m_previousErasePoint);
    } else {
        m_drawing = true;
        m_pendingStroke = {};
        m_pendingStroke.id = QUuid::createUuid();
        m_pendingStroke.color = m_penColor;
        m_pendingStroke.widthPt = m_penWidthPt / m_pageUserUnit;
        addPoint(position, pressure);
    }
    update();
}

void InkCanvas::addPoint(const QPointF& position, qreal pressure)
{
    if (m_pendingStroke.points.size() >= MaximumStrokePoints)
        return;
    const InkPoint point{clampedPdfPoint(position), std::clamp(pressure, 0.0, 1.0)};
    if (!m_pendingStroke.points.isEmpty()) {
        const auto& previous = m_pendingStroke.points.back();
        if (QLineF(previous.position, point.position).length() < 0.20 / m_pageUserUnit && std::abs(previous.pressure - point.pressure) < 0.025)
            return;
    }
    m_pendingStroke.points.append(point);
}

void InkCanvas::moveInk(const QPointF& position, qreal pressure)
{
    m_hoverPosition = position;
    if (m_drawing) {
        addPoint(position, pressure);
        if (m_pendingStroke.points.size() >= MaximumStrokePoints) {
            finishGesture();
            Q_EMIT activity(QStringLiteral("单笔采样已到原型上限，已保留这一笔；请抬笔后继续。"));
        }
    } else if (m_erasing) {
        const QPointF next = clampedPdfPoint(position);
        eraseBetween(m_previousErasePoint, next);
        m_previousErasePoint = next;
    }
    update();
}

void InkCanvas::eraseBetween(const QPointF& from, const QPointF& to)
{
    bool removed = false;
    for (qsizetype i = m_strokes.size(); i > 0; --i) {
        if (strokeHit(m_strokes[i - 1], from, to, EraserRadiusPt / m_pageUserUnit)) {
            m_strokes.removeAt(i - 1);
            removed = true;
        }
    }
    if (removed) {
        m_eraseChanged = true;
        rebuildInkCache();
    }
}

void InkCanvas::finishGesture()
{
    bool changed = false;
    if (m_drawing && !m_pendingStroke.points.isEmpty()) {
        pushUndo(m_beforeGesture);
        m_strokes.append(m_pendingStroke);
        changed = true;
    } else if (m_erasing && m_eraseChanged) {
        pushUndo(m_beforeGesture);
        changed = true;
    }
    m_drawing = false;
    m_erasing = false;
    m_eraseChanged = false;
    m_panning = false;
    m_pendingStroke = {};
    m_beforeGesture.clear();
    if (changed) {
        rebuildInkCache();
        Q_EMIT strokesChanged();
        Q_EMIT undoAvailabilityChanged(!m_undo.isEmpty());
    }
    updateCursor();
    update();
}

void InkCanvas::undo()
{
    finishGesture();
    if (!m_editingEnabled || m_undo.isEmpty())
        return;
    m_strokes = m_undo.takeLast();
    rebuildInkCache();
    Q_EMIT strokesChanged();
    Q_EMIT undoAvailabilityChanged(!m_undo.isEmpty());
}

void InkCanvas::updateCursor()
{
    setCursor(m_panning ? Qt::ClosedHandCursor : m_tool == Tool::Pan || !m_editingEnabled ? Qt::OpenHandCursor : Qt::CrossCursor);
}

void InkCanvas::tabletEvent(QTabletEvent* event)
{
    event->accept(); // Do not turn this handled tablet stream into mouse ink.
    const bool eraser = event->pointingDevice()->pointerType() == QPointingDevice::PointerType::Eraser;
    const bool hasPressure = event->pointingDevice()->capabilities().testFlag(QInputDevice::Capability::Pressure);
    const qreal pressure = hasPressure ? event->pressure() : 0.6;
    m_hoverPosition = event->position();
    m_hoverEraser = eraser;
    if (event->type() == QEvent::TabletPress) {
        m_tabletActive = true;
        // A real pen contact takes priority over an earlier touch navigation.
        m_touchActive = false;
        m_previousTouchCount = 0;
        m_previousTouchDistance = 0;
        if (m_tool == Tool::Pan && !eraser) {
            m_panning = true;
            m_previousPanGlobal = event->globalPosition();
            updateCursor();
        } else if (event->button() == Qt::LeftButton || eraser) {
            beginInk(event->position(), pressure, eraser || m_tool == Tool::ObjectEraser);
        }
    } else if (event->type() == QEvent::TabletMove) {
        if (m_panning) {
            Q_EMIT panRequested(event->globalPosition() - m_previousPanGlobal);
            m_previousPanGlobal = event->globalPosition();
        } else {
            moveInk(event->position(), pressure);
        }
    } else if (event->type() == QEvent::TabletRelease) {
        if (m_drawing) {
            const qreal lastPressure = m_pendingStroke.points.isEmpty() ? 0.6 : m_pendingStroke.points.back().pressure;
            addPoint(event->position(), lastPressure);
        }
        finishGesture();
        m_tabletActive = false;
    }
    update();
}

void InkCanvas::mousePressEvent(QMouseEvent* event)
{
    if (m_tabletActive || m_touchActive || event->source() != Qt::MouseEventNotSynthesized) {
        event->accept();
        return;
    }
    m_hoverEraser = false;
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && m_tool == Tool::Pan)) {
        m_panning = true;
        m_previousPanGlobal = event->globalPosition();
        updateCursor();
    } else if (event->button() == Qt::LeftButton) {
        beginInk(event->position(), 0.6, m_tool == Tool::ObjectEraser);
    }
    event->accept();
}

void InkCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (m_tabletActive || m_touchActive || event->source() != Qt::MouseEventNotSynthesized) {
        event->accept();
        return;
    }
    m_hoverEraser = false;
    if (m_panning) {
        Q_EMIT panRequested(event->globalPosition() - m_previousPanGlobal);
        m_previousPanGlobal = event->globalPosition();
    } else {
        moveInk(event->position(), 0.6);
    }
    event->accept();
}

void InkCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_tabletActive && event->source() == Qt::MouseEventNotSynthesized) {
        if (m_drawing)
            addPoint(event->position(), 0.6);
        finishGesture();
    }
    event->accept();
}

void InkCanvas::wheelEvent(QWheelEvent* event)
{
    if (hasActiveGesture()) {
        event->accept();
        return;
    }
    if (event->modifiers().testFlag(Qt::ControlModifier) && event->angleDelta().y() != 0) {
        Q_EMIT zoomRequested(std::pow(1.15, qreal(event->angleDelta().y()) / 120.0), event->position());
        event->accept();
    } else {
        event->ignore();
    }
}

bool InkCanvas::event(QEvent* event)
{
    if (event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchUpdate ||
        event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel) {
        auto* touch = static_cast<QTouchEvent*>(event);
        event->accept();
        if (event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel) {
            m_touchActive = false;
            m_previousTouchCount = 0;
            m_previousTouchDistance = 0;
            return true;
        }
        if (m_tabletActive || hasActiveGesture())
            return true;
        QVector<QPointF> globalPoints;
        for (const auto& point : touch->points()) {
            if (point.state() != QEventPoint::State::Released)
                globalPoints.append(point.globalPosition());
            if (globalPoints.size() == 2)
                break;
        }
        if (globalPoints.isEmpty())
            return true;
        const QPointF center = globalPoints.size() == 2 ? (globalPoints[0] + globalPoints[1]) / 2 : globalPoints[0];
        const qreal distance = globalPoints.size() == 2 ? QLineF(globalPoints[0], globalPoints[1]).length() : 0;
        if (m_touchActive && m_previousTouchCount == globalPoints.size()) {
            if (globalPoints.size() == 2 && m_previousTouchDistance > 10 && distance > 10) {
                Q_EMIT zoomRequested(distance / m_previousTouchDistance, mapFromGlobal(center));
            } else if (globalPoints.size() == 1) {
                Q_EMIT panRequested(center - m_previousTouchGlobal);
            }
        }
        m_touchActive = true;
        m_previousTouchGlobal = center;
        m_previousTouchDistance = distance;
        m_previousTouchCount = static_cast<int>(globalPoints.size());
        return true;
    }
    return QWidget::event(event);
}

void InkCanvas::focusOutEvent(QFocusEvent* event)
{
    // The prototype keeps a captured partial stroke when focus is lost.
    finishGesture();
    m_tabletActive = false;
    m_touchActive = false;
    m_previousTouchCount = 0;
    QWidget::focusOutEvent(event);
}

} // namespace propernotes
