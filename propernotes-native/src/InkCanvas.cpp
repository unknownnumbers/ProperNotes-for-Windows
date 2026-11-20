// SPDX-License-Identifier: GPL-3.0-only
#include "InkCanvas.h"

#include <QApplication>
#include <QEvent>
#include <QFocusEvent>
#include <QFont>
#include <QKeyEvent>
#include <QInputMethod>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointingDevice>
#include <QSignalBlocker>
#include <QTabletEvent>
#include <QTextDocument>
#include <QTextCursor>
#include <QTouchEvent>
#include <QVariant>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

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

InkPoint interpolate(const InkPoint& a, const InkPoint& b, qreal t)
{
    return {a.position + (b.position - a.position) * t, a.pressure + (b.pressure - a.pressure) * t};
}

// The center-line distance to a convex eraser capsule minus the interpolated
// half-width is convex. Its removed sublevel set is a single interval.
bool cutInterval(const InkStroke& stroke, const InkPoint& a, const InkPoint& b,
                 const QPointF& from, const QPointF& to, qreal radius, qreal& enter, qreal& leave)
{
    const qreal maxRadius = radius + std::max(strokeSampleWidth(stroke, a), strokeSampleWidth(stroke, b)) / 2;
    if (segmentDistance(a.position, b.position, from, to) > maxRadius)
        return false;
    const auto distance = [&](qreal t) {
        const auto point = interpolate(a, b, t);
        return pointSegmentDistance(point.position, from, to) - radius - strokeSampleWidth(stroke, point) / 2;
    };
    qreal low = 0, high = 1;
    for (int step = 0; step < 22; ++step) {
        const qreal l = low + (high - low) / 3;
        const qreal r = high - (high - low) / 3;
        if (distance(l) < distance(r)) high = r; else low = l;
    }
    const qreal minimum = (low + high) / 2;
    if (std::min({distance(0), distance(1), distance(minimum)}) > 0)
        return false;
    enter = 0;
    if (distance(0) > 0) {
        low = 0; high = minimum;
        for (int step = 0; step < 26; ++step) {
            const qreal middle = (low + high) / 2;
            if (distance(middle) > 0) low = middle; else high = middle;
        }
        enter = high;
    }
    leave = 1;
    if (distance(1) > 0) {
        low = minimum; high = 1;
        for (int step = 0; step < 26; ++step) {
            const qreal middle = (low + high) / 2;
            if (distance(middle) <= 0) low = middle; else high = middle;
        }
        leave = low;
    }
    return true;
}

QVector<InkStroke> erasePartial(const InkStroke& stroke, const QPointF& from, const QPointF& to, qreal radius)
{
    if (!strokeHit(stroke, from, to, radius))
        return {stroke};
    if (stroke.points.size() == 1)
        return {};
    QVector<InkStroke> fragments;
    QVector<InkPoint> current;
    const auto flush = [&] {
        if (current.isEmpty()) return;
        InkStroke fragment = stroke;
        fragment.id = fragments.isEmpty() ? stroke.id : QUuid::createUuid();
        fragment.points = std::exchange(current, {});
        fragments.append(std::move(fragment));
    };
    bool changed = false;
    for (qsizetype index = 1; index < stroke.points.size(); ++index) {
        const auto& a = stroke.points[index - 1];
        const auto& b = stroke.points[index];
        qreal enter = 0, leave = 0;
        if (!cutInterval(stroke, a, b, from, to, radius, enter, leave)) {
            if (current.isEmpty()) current.append(a);
            current.append(b);
            continue;
        }
        changed = true;
        if (enter > 1e-8) {
            if (current.isEmpty()) current.append(a);
            current.append(interpolate(a, b, enter));
        }
        flush();
        if (leave < 1 - 1e-8) {
            current.append(interpolate(a, b, leave));
            current.append(b);
        }
    }
    flush();
    return changed ? fragments : QVector<InkStroke>{stroke};
}

bool rectHit(const QRectF& rect, const QPointF& from, const QPointF& to, qreal radius)
{
    if (rect.adjusted(-radius, -radius, radius, radius).contains(from) ||
        rect.adjusted(-radius, -radius, radius, radius).contains(to)) return true;
    const QRectF grown = rect.adjusted(-radius, -radius, radius, radius);
    for (const QLineF& edge : {QLineF(grown.topLeft(), grown.topRight()), QLineF(grown.topRight(), grown.bottomRight()),
                              QLineF(grown.bottomRight(), grown.bottomLeft()), QLineF(grown.bottomLeft(), grown.topLeft())}) {
        if (QLineF(from, to).intersects(edge, nullptr) == QLineF::BoundedIntersection) return true;
    }
    return false;
}

QPainterPath eraserArea(const QPointF& from, const QPointF& to, qreal radius)
{
    QPainterPath path;
    if (from == to) {
        path.addEllipse(from, radius, radius);
        return path;
    }
    path.moveTo(from);
    path.lineTo(to);
    QPainterPathStroker stroker;
    stroker.setWidth(radius * 2);
    stroker.setCapStyle(Qt::RoundCap);
    return stroker.createStroke(path);
}

void paintTextQuads(QPainter& painter, const QVector<TextMarkupQuad>& quads, const QColor& color)
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    for (const auto& quad : quads) {
        QPolygonF polygon;
        polygon << quad.topLeft << quad.topRight << quad.bottomRight << quad.bottomLeft;
        painter.drawPolygon(polygon);
    }
}
} // namespace

InkCanvas::InkCanvas(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_AcceptTouchEvents);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAccessibleName(QStringLiteral("PDF 页面和批注"));
    qApp->installEventFilter(this);
    updateCursor();
}

InkCanvas::~InkCanvas()
{
    qApp->removeEventFilter(this);
}

void InkCanvas::setPage(int pageIndex, const QImage& background, const QTransform& pdfToImage,
                        const QRectF& pageRect, qreal rasterScale, const PageAnnotations& annotations)
{
    finishActiveOperation();
    if (pageIndex != m_pageIndex) {
        clearSelection();
        m_textPage.reset();
        m_selectedTextQuads.clear();
        m_searchQuads.clear();
    }
    m_pageIndex = pageIndex;
    m_background = background;
    m_background.setDevicePixelRatio(1.0);
    m_pdfToImage = pdfToImage;
    m_pageRect = pageRect.normalized();
    m_rasterScale = rasterScale;
    m_pageUserUnit = std::hypot(pdfToImage.m11(), pdfToImage.m12()) / rasterScale;
    bool invertible = false;
    m_imageToPdf = pdfToImage.inverted(&invertible);
    if (!invertible || m_pageRect.isEmpty() || !std::isfinite(rasterScale) || rasterScale <= 0 ||
        !std::isfinite(m_pageUserUnit) || m_pageUserUnit <= 0) {
        clearPage();
        Q_EMIT activity(QStringLiteral("页面坐标不可用，已停止编辑。"));
        return;
    }
    m_annotations = annotations;
    rebuildInkCache();
    updateExtent();
}

void InkCanvas::setPageAnnotations(const PageAnnotations& annotations)
{
    finishActiveOperation();
    m_annotations = annotations;
    QSet<QUuid> valid;
    for (const auto& stroke : annotations.strokes) valid.insert(stroke.id);
    for (const auto& text : annotations.textBoxes) valid.insert(text.id);
    for (const auto& markup : annotations.textMarkups) valid.insert(markup.id);
    m_selection.intersect(valid);
    rebuildInkCache();
    Q_EMIT selectionChanged();
}

void InkCanvas::clearPage()
{
    finishActiveOperation();
    m_pageIndex = -1;
    m_background = {};
    m_inkCache = {};
    m_pageRect = {};
    m_annotations = {};
    m_textPage.reset();
    m_selectedTextQuads.clear();
    m_searchQuads.clear();
    clearSelection();
    m_editingEnabled = false;
    updateExtent();
}

void InkCanvas::setTool(Tool tool)
{
    if (m_tool == tool) return;
    finishActiveOperation();
    m_tool = tool;
    if (tool == Tool::TextSelect)
        clearSelection();
    updateCursor();
    update();
}

void InkCanvas::setPenColor(const QColor& color) { if (color.isValid()) m_penColor = color; }
void InkCanvas::setPenWidth(qreal width) { if (std::isfinite(width)) m_penWidthPt = std::clamp(width, 0.5, 24.0); }
void InkCanvas::setPenOpacity(qreal opacity) { if (std::isfinite(opacity)) m_penOpacity = std::clamp(opacity, 0.0, 1.0); }
void InkCanvas::setPressureSensitivity(qreal sensitivity) { if (std::isfinite(sensitivity)) m_pressureSensitivity = std::clamp(sensitivity, 0.0, 100.0); }
void InkCanvas::setEraserRadius(qreal radius) { if (std::isfinite(radius)) m_eraserRadiusPt = std::clamp(radius, 1.0, 100.0); }
void InkCanvas::setTouchAndMouseAnnotate(bool enabled) { if (m_touchAndMouseAnnotate != enabled) finishActiveOperation(); m_touchAndMouseAnnotate = enabled; }
void InkCanvas::setTextStyle(const QString& family, qreal size, const QColor& color)
{
    if (!family.isEmpty()) m_textFamily = family;
    if (std::isfinite(size)) m_textSizePt = std::clamp(size, 4.0, 240.0);
    if (color.isValid()) m_textColor = color;
}

void InkCanvas::setEditingEnabled(bool enabled)
{
    if (!enabled && m_editingEnabled) finishActiveOperation();
    m_editingEnabled = enabled;
    updateCursor();
}

bool InkCanvas::hasActiveGesture() const { return m_gesture != Gesture::None || m_editor != nullptr; }

void InkCanvas::setTextPage(const std::shared_ptr<const PdfTextPage>& text)
{
    if (m_gesture != Gesture::TextSelection)
        m_textPage = text;
}

void InkCanvas::setTextSelection(const QVector<TextMarkupQuad>& quads)
{
    m_selectedTextQuads = quads;
    update();
}

void InkCanvas::setSearchHighlight(const QVector<TextMarkupQuad>& quads)
{
    m_searchQuads = quads;
    update();
}

void InkCanvas::setZoom(qreal zoom)
{
    if (!std::isfinite(zoom)) return;
    m_zoom = std::clamp(zoom, 1e-6, 4.0);
    updateExtent();
}

QSizeF InkCanvas::imageSize() const
{
    // PDF geometry gives the page extent. The last row or column of the
    // backing QImage may exist only because allocation rounds up to pixels.
    return m_pdfToImage.mapRect(m_pageRect).size();
}

void InkCanvas::updateExtent()
{
    m_imageScale = m_zoom / m_rasterScale;
    resize(sizeHint());
    positionEditor();
    updateGeometry();
    update();
}

QSize InkCanvas::sizeHint() const
{
    if (m_pageIndex < 0) return {640, 720};
    const QSizeF image = imageSize();
    return {int(std::ceil(image.width() * m_imageScale)) + PageMargin * 2,
            int(std::ceil(image.height() * m_imageScale)) + PageMargin * 2};
}

QTransform InkCanvas::pdfToWidget() const
{
    return {m_pdfToImage.m11() * m_imageScale, m_pdfToImage.m12() * m_imageScale,
            m_pdfToImage.m21() * m_imageScale, m_pdfToImage.m22() * m_imageScale,
            PageMargin + m_pdfToImage.dx() * m_imageScale, PageMargin + m_pdfToImage.dy() * m_imageScale};
}

QPointF InkCanvas::pdfPointAt(const QPointF& position) const { return m_imageToPdf.map((position - QPointF(PageMargin, PageMargin)) / m_imageScale); }
QPointF InkCanvas::widgetPointForPdf(const QPointF& point) const { return pdfToWidget().map(point); }
bool InkCanvas::insidePaper(const QPointF& point) const
{
    return !m_background.isNull() && QRectF(QPointF(PageMargin, PageMargin), imageSize() * m_imageScale).contains(point);
}
QPointF InkCanvas::clampedPdfPoint(const QPointF& position) const
{
    QPointF point = pdfPointAt(position);
    point.setX(std::clamp(point.x(), m_pageRect.left(), m_pageRect.right()));
    point.setY(std::clamp(point.y(), m_pageRect.top(), m_pageRect.bottom()));
    return point;
}

qreal InkCanvas::effectivePressure(qreal reported, bool nativePen) const
{
    const bool enabled = m_gesture == Gesture::Ink ? m_inkPressureEnabled : m_pressureEnabled;
    const qreal sensitivity = m_gesture == Gesture::Ink ? m_inkSensitivity : m_pressureSensitivity;
    if (!nativePen || !enabled) return 0.6;
    const qreal bounded = std::clamp(std::isfinite(reported) ? reported : 0.6, 0.0, 1.0);
    return std::pow(bounded, std::pow(2.0, (50.0 - sensitivity) / 50.0));
}

void InkCanvas::rebuildInkCache()
{
    if (m_background.isNull() || (m_annotations.strokes.isEmpty() && m_annotations.textBoxes.isEmpty()
        && m_annotations.textMarkups.isEmpty())) {
        m_inkCache = {};
        update();
        return;
    }
    // Composite highlights against the page itself so Multiply preserves the
    // original ink of the printed text beneath a translucent review mark.
    m_inkCache = m_background.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&m_inkCache);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setWorldTransform(m_pdfToImage);
    for (const auto& markup : m_annotations.textMarkups) paintTextMarkup(painter, markup);
    for (const auto& stroke : m_annotations.strokes) painter.fillPath(inkStrokeOutline(stroke), stroke.color);
    for (const auto& text : m_annotations.textBoxes) {
        if (!m_editor || text.id != m_editTextId) paintTextBox(painter, text);
    }
    update();
}

void InkCanvas::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(m_darkTheme ? "#1d222b" : "#e7eaf0"));
    painter.setRenderHint(QPainter::Antialiasing);
    if (m_pageIndex < 0) {
        painter.setPen(QColor(m_darkTheme ? "#a8b3c6" : "#657084"));
        painter.drawText(rect().adjusted(32, 32, -32, -32), Qt::AlignCenter | Qt::TextWordWrap,
                         QStringLiteral("打开本地 PDF，开始批注"));
        return;
    }
    const QRectF paper(QPointF(PageMargin, PageMargin), imageSize() * m_imageScale);
    painter.fillRect(paper.translated(3, 4), QColor(0, 0, 0, 24));
    painter.fillRect(paper, Qt::white);
    if (m_background.isNull()) {
        painter.setPen(QColor("#657084"));
        painter.drawText(paper, Qt::AlignCenter, QStringLiteral("正在显示页面…"));
    } else {
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QImage& image = m_inkCache.isNull() ? m_background : m_inkCache;
        painter.drawImage(paper, image, QRectF(QPointF(), imageSize()));
    }
    painter.save();
    painter.setClipRect(paper);
    painter.setWorldTransform(pdfToWidget());
    paintTextQuads(painter, m_searchQuads, QColor(255, 169, 17, 105));
    const auto& selection = m_gesture == Gesture::TextSelection ? m_textPreview.quads : m_selectedTextQuads;
    paintTextQuads(painter, selection, QColor(48, 128, 237, 105));
    if (m_gesture == Gesture::Ink) painter.fillPath(inkStrokeOutline(m_pendingStroke), m_pendingStroke.color);
    painter.restore();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor("#2674d9"), 1.3, Qt::DashLine));
    if (m_gesture == Gesture::Marquee || m_gesture == Gesture::TextFrame)
        painter.drawRect(pdfToWidget().mapRect(QRectF(m_gestureStart, m_gestureEnd).normalized()));
    if (!m_selection.isEmpty()) {
        painter.drawRect(pdfToWidget().mapRect(selectionBounds()));
        if (m_selection.size() == 1) {
            for (const auto& text : m_annotations.textBoxes) {
                if (!m_selection.contains(text.id)) continue;
                painter.setBrush(Qt::white);
                for (const QPointF& point : {text.rect.topLeft(), text.rect.topRight(), text.rect.bottomRight(), text.rect.bottomLeft()})
                    painter.drawRect(QRectF(widgetPointForPdf(point) - QPointF(4, 4), QSizeF(8, 8)));
            }
        }
    }
    if (m_editingEnabled && (m_tool == Tool::ObjectEraser || m_hoverEraser) && insidePaper(m_hoverPosition)) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor("#2674d9"), 1.2));
        painter.drawEllipse(m_hoverPosition, m_eraserRadiusPt * m_zoom, m_eraserRadiusPt * m_zoom);
    }
}

void InkCanvas::clearSelection()
{
    if (m_selection.isEmpty()) return;
    m_selection.clear();
    Q_EMIT selectionChanged();
    update();
}

QRectF InkCanvas::objectBounds(const QUuid& id) const
{
    for (const auto& stroke : m_annotations.strokes)
        if (stroke.id == id) return inkStrokeOutline(stroke).boundingRect();
    for (const auto& text : m_annotations.textBoxes)
        if (text.id == id) return text.rect;
    for (const auto& markup : m_annotations.textMarkups)
        if (markup.id == id) return textMarkupBounds(markup);
    return {};
}

QRectF InkCanvas::selectionBounds() const
{
    QRectF result;
    for (const auto& id : m_selection) result = result.united(objectBounds(id));
    return result;
}

QUuid InkCanvas::textAt(const QPointF& point) const
{
    for (auto it = m_annotations.textBoxes.crbegin(); it != m_annotations.textBoxes.crend(); ++it)
        if (it->rect.contains(point)) return it->id;
    return {};
}

QSet<QUuid> InkCanvas::objectsIn(const QRectF& rect) const
{
    QSet<QUuid> result;
    for (const auto& stroke : m_annotations.strokes) {
        const auto path = inkStrokeOutline(stroke);
        if (path.intersects(rect) || rect.contains(path.boundingRect())) result.insert(stroke.id);
    }
    for (const auto& text : m_annotations.textBoxes)
        if (rect.intersects(text.rect) || rect.contains(text.rect)) result.insert(text.id);
    for (const auto& markup : m_annotations.textMarkups) {
        const auto path = textMarkupPath(markup);
        if (path.intersects(rect) || rect.contains(path.boundingRect())) result.insert(markup.id);
    }
    return result;
}

int InkCanvas::textResizeCornerAt(const QPointF& point) const
{
    if (m_selection.size() != 1) return -1;
    for (const auto& text : m_annotations.textBoxes) {
        if (!m_selection.contains(text.id)) continue;
        const QVector<QPointF> corners{text.rect.topLeft(), text.rect.topRight(), text.rect.bottomRight(), text.rect.bottomLeft()};
        for (int index = 0; index < corners.size(); ++index)
            if (QLineF(point, widgetPointForPdf(corners[index])).length() <= 7) return index;
    }
    return -1;
}

void InkCanvas::pointerPress(const QPointF& position, const QPointF& global, qreal pressure,
                             Source source, bool tailEraser, bool forcePan)
{
    if (m_editor) finishTextEdit(false);
    if (m_gesture != Gesture::None) return;
    m_hoverPosition = position;
    m_source = source;
    m_tailEraser = tailEraser;
    m_previousPanGlobal = global;
    if (!forcePan && !tailEraser && m_tool == Tool::TextSelect) {
        setFocus(Qt::MouseFocusReason);
        beginTextSelection(position);
        return;
    }
    if (forcePan || (!tailEraser && m_tool == Tool::Pan) || !m_editingEnabled) {
        m_gesture = Gesture::Pan;
        updateCursor();
        return;
    }
    if (!insidePaper(position)) { m_source = Source::None; return; }
    setFocus(Qt::MouseFocusReason);
    m_beforeGesture = m_annotations;
    m_selectionBeforeGesture = m_selection;
    m_gestureStart = m_gestureEnd = clampedPdfPoint(position);
    m_focusId = {};
    if (tailEraser || m_tool == Tool::ObjectEraser) {
        m_gesture = Gesture::Erase;
        m_activeEraseMode = m_eraseMode;
        m_activeEraserRadiusPt = m_eraserRadiusPt;
        m_previousErasePoint = m_gestureStart;
        eraseBetween(m_gestureStart, m_gestureStart);
    } else if (m_tool == Tool::Pen) {
        m_gesture = Gesture::Ink;
        m_inkPressureEnabled = m_pressureEnabled;
        m_inkSensitivity = m_pressureSensitivity;
        m_pendingStroke = {};
        m_pendingStroke.color = m_penColor;
        m_pendingStroke.color.setAlphaF(m_penColor.alphaF() * m_penOpacity);
        m_pendingStroke.widthPt = m_penWidthPt / m_pageUserUnit;
        m_focusId = m_pendingStroke.id;
        addPoint(position, pressure);
    } else if (m_tool == Tool::Select) {
        m_resizeCorner = textResizeCornerAt(position);
        if (m_resizeCorner >= 0) {
            m_gesture = Gesture::TextResize;
            m_focusId = *m_selection.cbegin();
        } else if (!m_selection.isEmpty() && selectionBounds().contains(m_gestureStart)) {
            const bool anchored = std::any_of(m_annotations.textMarkups.cbegin(), m_annotations.textMarkups.cend(),
                [this](const TextMarkup& markup) { return m_selection.contains(markup.id); });
            if (anchored) {
                resetGesture();
                Q_EMIT activity(QStringLiteral("审阅标记跟随正文位置，可以删除或撤销。若要移动其他批注，请单独选择它们。"));
                return;
            }
            m_gesture = Gesture::Move;
            m_focusId = *m_selection.cbegin();
        } else {
            m_gesture = Gesture::Marquee;
            clearSelection();
        }
    } else if (m_tool == Tool::TextBox) {
        const QUuid hit = textAt(m_gestureStart);
        if (!hit.isNull()) {
            resetGesture();
            beginTextEdit(hit);
            return;
        }
        m_gesture = Gesture::TextFrame;
    }
    updateCursor();
    update();
}

void InkCanvas::beginTextSelection(const QPointF& position)
{
    Q_EMIT textSelectionStarted();
    clearSelection();
    if (!insidePaper(position)) {
        resetGesture();
        return;
    }
    if (!m_textPage) {
        Q_EMIT textPageRequested(m_pageIndex);
        Q_EMIT activity(QStringLiteral("正在读取本页文字。读取后即可拖动选择。"));
        resetGesture();
        return;
    }
    if (!m_textPage->error.isEmpty()) {
        Q_EMIT activity(m_textPage->error);
        resetGesture();
        return;
    }
    if (m_textPage->glyphs.isEmpty()) {
        Q_EMIT activity(QStringLiteral("本页没有可选中的 PDF 正文。"));
        resetGesture();
        return;
    }
    const qreal tolerance = std::min(10 / m_zoom, qreal(16)) / m_pageUserUnit;
    const int glyph = m_textPage->hitTest(pdfPointAt(position), tolerance);
    if (glyph < 0) {
        resetGesture();
        return;
    }
    m_textDragStart = position;
    m_textFirstGlyph = glyph;
    m_gesture = Gesture::TextSelection;
    update();
}

void InkCanvas::updateTextSelection(const QPointF& position)
{
    m_textDragEnd = position;
    // A click clears the previous selection. Dragging within one glyph still
    // selects it, which also handles PDF ligatures kept as one glyph cluster.
    if (m_textLastGlyph < 0 && QLineF(m_textDragStart, position).length() < 3)
        return;
    const int glyph = m_textPage->hitTest(clampedPdfPoint(position), std::numeric_limits<qreal>::infinity());
    if (glyph < 0 || glyph == m_textLastGlyph)
        return;
    m_textLastGlyph = glyph;
    m_textPreview = m_textPage->select(m_textFirstGlyph, glyph);
}

void InkCanvas::addPoint(const QPointF& position, qreal pressure)
{
    if (m_pendingStroke.points.size() >= MaximumStrokePoints) return;
    const InkPoint point{clampedPdfPoint(position), std::clamp(pressure, 0.0, 1.0)};
    if (!m_pendingStroke.points.isEmpty()) {
        const auto& previous = m_pendingStroke.points.back();
        if (QLineF(previous.position, point.position).length() < 0.20 / m_pageUserUnit &&
            std::abs(previous.pressure - point.pressure) < 0.025) return;
    }
    m_pendingStroke.points.append(point);
}

void InkCanvas::pointerMove(const QPointF& position, const QPointF& global, qreal pressure)
{
    m_hoverPosition = position;
    if (m_gesture == Gesture::Pan) {
        Q_EMIT panRequested(global - m_previousPanGlobal);
        m_previousPanGlobal = global;
    } else if (m_gesture == Gesture::Ink) {
        addPoint(position, pressure);
        if (m_pendingStroke.points.size() >= MaximumStrokePoints) {
            finishActiveOperation();
            Q_EMIT activity(QStringLiteral("单笔采样已到上限，已保留笔迹；请抬笔后继续。"));
        }
    } else if (m_gesture == Gesture::Erase) {
        const QPointF next = clampedPdfPoint(position);
        eraseBetween(m_previousErasePoint, next);
        m_previousErasePoint = next;
    } else if (m_gesture == Gesture::TextSelection) {
        updateTextSelection(position);
    } else if (m_gesture == Gesture::Move) {
        const QRectF bounds = [&] {
            QRectF result;
            for (const auto& stroke : m_beforeGesture.strokes)
                if (m_selection.contains(stroke.id)) result = result.united(inkStrokeOutline(stroke).boundingRect());
            for (const auto& text : m_beforeGesture.textBoxes)
                if (m_selection.contains(text.id)) result = result.united(text.rect);
            return result;
        }();
        QPointF delta = clampedPdfPoint(position) - m_gestureStart;
        const qreal minX = std::min(qreal(0), m_pageRect.left() - bounds.left());
        const qreal maxX = std::max(qreal(0), m_pageRect.right() - bounds.right());
        const qreal minY = std::min(qreal(0), m_pageRect.top() - bounds.top());
        const qreal maxY = std::max(qreal(0), m_pageRect.bottom() - bounds.bottom());
        delta.setX(std::clamp(delta.x(), minX, maxX));
        delta.setY(std::clamp(delta.y(), minY, maxY));
        m_annotations = m_beforeGesture;
        for (auto& stroke : m_annotations.strokes)
            if (m_selection.contains(stroke.id)) for (auto& point : stroke.points) point.position += delta;
        for (auto& text : m_annotations.textBoxes)
            if (m_selection.contains(text.id)) text.rect.translate(delta);
        rebuildInkCache();
    } else if (m_gesture == Gesture::TextResize) {
        m_annotations = m_beforeGesture;
        for (auto& text : m_annotations.textBoxes) {
            if (text.id != m_focusId) continue;
            const QVector<QPointF> opposite{text.rect.bottomRight(), text.rect.bottomLeft(), text.rect.topLeft(), text.rect.topRight()};
            const QRectF candidate(opposite[m_resizeCorner], clampedPdfPoint(position));
            const QRectF rect = candidate.normalized();
            if (rect.width() >= 8 / m_pageUserUnit && rect.height() >= 8 / m_pageUserUnit) text.rect = rect;
        }
        rebuildInkCache();
    } else if (m_gesture == Gesture::Marquee || m_gesture == Gesture::TextFrame) {
        m_gestureEnd = clampedPdfPoint(position);
    }
    update();
}

void InkCanvas::eraseBetween(const QPointF& from, const QPointF& to)
{
    const qreal radius = m_activeEraserRadiusPt / m_pageUserUnit;
    QVector<InkStroke> strokes;
    for (const auto& stroke : m_annotations.strokes) {
        if (m_activeEraseMode == EraseMode::WholeStroke) {
            if (!strokeHit(stroke, from, to, radius)) strokes.append(stroke);
        } else {
            strokes.append(erasePartial(stroke, from, to, radius));
        }
        if (strokes.size() > 100000) {
            Q_EMIT activity(QStringLiteral("笔迹数量达到当前上限，这一步擦除未应用。"));
            return;
        }
    }
    m_annotations.strokes = std::move(strokes);
    for (qsizetype index = m_annotations.textBoxes.size(); index > 0; --index)
        if (rectHit(m_annotations.textBoxes[index - 1].rect, from, to, radius)) m_annotations.textBoxes.removeAt(index - 1);
    if (!m_annotations.textMarkups.isEmpty()) {
        const auto area = eraserArea(from, to, radius);
        for (qsizetype index = m_annotations.textMarkups.size(); index > 0; --index) {
            const auto& markup = m_annotations.textMarkups[index - 1];
            if (area.boundingRect().intersects(textMarkupBounds(markup)) && area.intersects(textMarkupPath(markup)))
                m_annotations.textMarkups.removeAt(index - 1);
        }
    }
    rebuildInkCache();
}

void InkCanvas::resetGesture()
{
    m_gesture = Gesture::None;
    m_source = Source::None;
    m_tailEraser = false;
    m_resizeCorner = -1;
    m_pendingStroke = {};
    m_beforeGesture = {};
    m_selectionBeforeGesture.clear();
    m_textFirstGlyph = -1;
    m_textLastGlyph = -1;
    m_textPreview = {};
    updateCursor();
}

void InkCanvas::commit(const PageAnnotations& before, const QString& reason, const QUuid& focusId,
                        bool normalCompletion, Tool completedTool)
{
    const PageAnnotations after = m_annotations;
    if (before != after) {
        QSet<QUuid> valid;
        for (const auto& stroke : after.strokes) valid.insert(stroke.id);
        for (const auto& text : after.textBoxes) valid.insert(text.id);
        for (const auto& markup : after.textMarkups) valid.insert(markup.id);
        const auto oldSelection = m_selection;
        m_selection.intersect(valid);
        if (oldSelection != m_selection) Q_EMIT selectionChanged();
        rebuildInkCache();
        Q_EMIT annotationsCommitted(m_pageIndex, before, after, reason, focusId);
        if (normalCompletion) Q_EMIT normalToolCompleted(completedTool, reason);
    }
    Q_EMIT operationFinished();
    update();
}

void InkCanvas::pointerRelease(const QPointF& position, qreal pressure)
{
    if (m_gesture == Gesture::Ink) addPoint(position, pressure);
    else if (m_gesture != Gesture::Pan && m_gesture != Gesture::None)
        pointerMove(position, m_previousPanGlobal, pressure);
    finishActiveOperation(FinishReason::NormalRelease);
}

void InkCanvas::finishActiveOperation(FinishReason finishReason)
{
    if (m_editor) { finishTextEdit(false); return; }
    if (m_gesture == Gesture::None) return;
    const Gesture gesture = m_gesture;
    if (gesture == Gesture::TextSelection) {
        auto selected = std::move(m_textPreview);
        const auto releasedAt = m_textDragEnd;
        resetGesture();
        if (finishReason == FinishReason::NormalRelease && !selected.text.isEmpty())
            Q_EMIT textSelectionFinished(m_pageIndex, selected, releasedAt);
        Q_EMIT operationFinished();
        update();
        return;
    }
    const auto before = m_beforeGesture;
    const QUuid focusId = m_focusId;
    const bool completed = finishReason == FinishReason::NormalRelease && !m_tailEraser;
    const QRectF frame = QRectF(m_gestureStart, m_gestureEnd).normalized();
    if (gesture == Gesture::Ink && !m_pendingStroke.points.isEmpty()) m_annotations.strokes.append(m_pendingStroke);
    if (gesture == Gesture::Marquee && finishReason == FinishReason::NormalRelease) {
        const qreal slop = 3 / (m_zoom * m_pageUserUnit);
        m_selection = objectsIn(frame.isEmpty() ? QRectF(m_gestureStart - QPointF(slop, slop), QSizeF(slop * 2, slop * 2)) : frame);
        Q_EMIT selectionChanged();
    }
    resetGesture();
    if (gesture == Gesture::TextFrame && finishReason == FinishReason::NormalRelease &&
        frame.width() >= 8 / m_pageUserUnit && frame.height() >= 8 / m_pageUserUnit) {
        beginTextEdit({}, frame);
        return;
    }
    if (gesture == Gesture::Ink) commit(before, QStringLiteral("ink"), focusId, completed, Tool::Pen);
    else if (gesture == Gesture::Erase) commit(before, QStringLiteral("erase"), {}, completed, Tool::ObjectEraser);
    else if (gesture == Gesture::Move) commit(before, QStringLiteral("move"), focusId, false, Tool::Select);
    else if (gesture == Gesture::TextResize) commit(before, QStringLiteral("text-resize"), focusId, false, Tool::Select);
    else { Q_EMIT operationFinished(); update(); }
}

void InkCanvas::cancelActiveOperation()
{
    if (m_editor) { finishTextEdit(true); return; }
    if (m_gesture == Gesture::None) return;
    if (m_gesture == Gesture::TextSelection) {
        resetGesture();
        update();
        Q_EMIT operationFinished();
        return;
    }
    if (m_gesture != Gesture::Pan) {
        m_annotations = m_beforeGesture;
        m_selection = m_selectionBeforeGesture;
    }
    resetGesture();
    rebuildInkCache();
    Q_EMIT selectionChanged();
    Q_EMIT operationFinished();
}

void InkCanvas::cancelTouchOperation()
{
    if (m_source == Source::Touch) cancelActiveOperation();
    m_touchActive = false;
    m_previousTouchCount = 0;
    m_touchNavigationOnly = false;
    Q_EMIT operationFinished();
}

void InkCanvas::setExternalPenContact(bool down)
{
    m_externalPenContact = down;
    if (down) {
        m_suppressedTouch = true;
        if (m_source == Source::Touch || m_touchActive) cancelTouchOperation();
    }
}

void InkCanvas::beginTextEdit(const QUuid& id, const QRectF& newRect)
{
    if (!m_editingEnabled || m_background.isNull() || m_editor) return;
    TextBox text;
    const bool existing = !id.isNull();
    bool found = false;
    if (existing) {
        for (const auto& item : m_annotations.textBoxes)
            if (item.id == id) { text = item; found = true; break; }
        if (!found) return;
    } else {
        text.rect = newRect;
        text.fontFamily = m_textFamily;
        text.fontSizePt = m_textSizePt / m_pageUserUnit;
        text.color = m_textColor;
    }
    m_beforeTextEdit = m_annotations;
    m_editTextId = existing ? id : QUuid();
    m_editTextRect = text.rect;
    m_editor = new QPlainTextEdit(this);
    m_editor->setAcceptDrops(false);
    m_editor->setTabChangesFocus(false);
    m_editor->setPlainText(text.text);
    m_editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_editor->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_editor->setStyleSheet(QStringLiteral("QPlainTextEdit{background:white;color:%1;border:1px solid #2674d9;padding:0;}").arg(text.color.name()));
    QFont font(text.fontFamily);
    font.setPixelSize(std::max(1, qRound(text.fontSizePt * m_pageUserUnit * m_zoom)));
    m_editor->setFont(font);
    m_editor->document()->setDocumentMargin(0);
    m_editor->installEventFilter(this);
    m_editor->setProperty("newTextId", QVariant::fromValue(text.id));
    m_editor->setProperty("textFamily", text.fontFamily);
    m_editor->setProperty("textSizePt", text.fontSizePt);
    m_editor->setProperty("textColor", QVariant::fromValue(text.color));
    const auto lastValidText = std::make_shared<QString>(text.text);
    connect(m_editor, &QPlainTextEdit::textChanged, m_editor, [this, editor = m_editor, lastValidText] {
        const QString candidate = editor->toPlainText();
        if (candidate.size() <= 100000) {
            *lastValidText = candidate;
            return;
        }
        const QSignalBlocker blocker(editor);
        const int position = std::min(editor->textCursor().position(), int(lastValidText->size()));
        editor->setPlainText(*lastValidText);
        auto cursor = editor->textCursor();
        cursor.setPosition(position);
        editor->setTextCursor(cursor);
        Q_EMIT activity(QStringLiteral("文字框最多 100000 个字符，超出的本次输入未应用。"));
    });
    positionEditor();
    m_editor->show();
    m_editor->setFocus(Qt::OtherFocusReason);
    rebuildInkCache();
    Q_EMIT textEditingChanged(true);
}

void InkCanvas::positionEditor()
{
    if (!m_editor) return;
    const QRectF mapped = pdfToWidget().mapRect(m_editTextRect);
    m_editor->setGeometry(mapped.toAlignedRect());
    QFont font(m_editor->property("textFamily").toString());
    font.setPixelSize(std::max(1, qRound(m_editor->property("textSizePt").toDouble() * m_pageUserUnit * m_zoom)));
    m_editor->setFont(font);
    m_editor->setTabStopDistance(QFontMetricsF(font).horizontalAdvance(QChar(' ')) * 8);
}

void InkCanvas::finishTextEdit(bool cancel)
{
    if (!m_editor || m_textFinishing) return;
    m_textFinishing = true;
    auto* editor = m_editor;
    if (cancel) QGuiApplication::inputMethod()->reset();
    else QGuiApplication::inputMethod()->commit();
    const QString plain = editor->toPlainText();
    const bool existing = !m_editTextId.isNull();
    const auto before = m_beforeTextEdit;
    QUuid focusId = m_editTextId;
    const bool empty = plain.trimmed().isEmpty();
    if (!cancel) {
        if (existing && empty) {
            for (qsizetype index = m_annotations.textBoxes.size(); index > 0; --index)
                if (m_annotations.textBoxes[index - 1].id == m_editTextId) m_annotations.textBoxes.removeAt(index - 1);
        } else if (existing) {
            for (auto& text : m_annotations.textBoxes)
                if (text.id == m_editTextId) { text.text = plain; break; }
        } else if (!empty) {
            TextBox text;
            text.id = editor->property("newTextId").toUuid();
            text.rect = m_editTextRect;
            text.text = plain;
            text.fontFamily = editor->property("textFamily").toString();
            text.fontSizePt = editor->property("textSizePt").toDouble();
            text.color = editor->property("textColor").value<QColor>();
            focusId = text.id;
            m_annotations.textBoxes.append(std::move(text));
        }
    }
    m_editor = nullptr;
    m_editTextId = {};
    m_beforeTextEdit = {};
    editor->removeEventFilter(this);
    editor->hide();
    editor->deleteLater();
    m_textFinishing = false;
    rebuildInkCache();
    Q_EMIT textEditingChanged(false);
    if (!cancel && (existing || !empty)) {
        commit(before, existing ? QStringLiteral("text-edit") : QStringLiteral("text-create"), focusId,
               !existing, Tool::TextBox);
        if (existing && empty) Q_EMIT activity(QStringLiteral("空文字框已移除，可撤销。"));
    } else
        Q_EMIT operationFinished();
}

void InkCanvas::updateCursor()
{
    if (m_gesture == Gesture::Pan)
        setCursor(Qt::ClosedHandCursor);
    else if (m_tool == Tool::TextSelect && !m_hoverEraser)
        setCursor(Qt::IBeamCursor);
    else
        setCursor(m_tool == Tool::Pan || !m_editingEnabled ? Qt::OpenHandCursor : Qt::CrossCursor);
}

void InkCanvas::tabletEvent(QTabletEvent* event)
{
    event->accept();
    const bool eraser = event->pointingDevice()->pointerType() == QPointingDevice::PointerType::Eraser;
    const bool hasPressure = event->pointingDevice()->capabilities().testFlag(QInputDevice::Capability::Pressure);
    const qreal pressure = effectivePressure(event->pressure(), hasPressure);
    m_hoverPosition = event->position();
    m_hoverEraser = eraser;
    if (event->type() == QEvent::TabletPress) {
        if (m_source == Source::Touch) cancelActiveOperation();
        m_tabletActive = true;
        Q_EMIT penContactChanged(true);
        m_touchActive = false;
        m_previousTouchCount = 0;
        m_touchNavigationOnly = false;
        if (event->button() == Qt::LeftButton || eraser)
            pointerPress(event->position(), event->globalPosition(), pressure, Source::Pen, eraser);
    } else if (event->type() == QEvent::TabletMove) {
        if (m_source == Source::Pen) pointerMove(event->position(), event->globalPosition(), pressure);
    } else if (event->type() == QEvent::TabletRelease) {
        if (m_source == Source::Pen) {
            const qreal last = m_gesture == Gesture::Ink && !m_pendingStroke.points.isEmpty()
                ? m_pendingStroke.points.back().pressure : pressure;
            pointerRelease(event->position(), last);
        }
        m_tabletActive = false;
        Q_EMIT penContactChanged(false);
        Q_EMIT operationFinished();
    }
    update();
}

void InkCanvas::mousePressEvent(QMouseEvent* event)
{
    event->accept();
    if (m_tabletActive || m_touchActive || event->source() != Qt::MouseEventNotSynthesized) return;
    m_hoverEraser = false;
    if (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)
        pointerPress(event->position(), event->globalPosition(), 0.6, Source::Mouse, false,
                     event->button() == Qt::MiddleButton || (!m_touchAndMouseAnnotate && m_tool != Tool::TextSelect));
}

void InkCanvas::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();
    if (m_tabletActive || m_touchActive || event->source() != Qt::MouseEventNotSynthesized) return;
    m_hoverEraser = false;
    if (m_source == Source::Mouse) pointerMove(event->position(), event->globalPosition(), 0.6);
    else { m_hoverPosition = event->position(); update(); }
}

void InkCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept();
    if (!m_tabletActive && !m_touchActive && event->source() == Qt::MouseEventNotSynthesized &&
        m_source == Source::Mouse && (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton))
        pointerRelease(event->position(), 0.6);
}

void InkCanvas::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_touchAndMouseAnnotate && m_editingEnabled &&
        !m_tabletActive && !m_touchActive && event->source() == Qt::MouseEventNotSynthesized &&
        (m_tool == Tool::Select || m_tool == Tool::TextBox)) {
        finishActiveOperation();
        const auto id = textAt(pdfPointAt(event->position()));
        if (!id.isNull()) { beginTextEdit(id); event->accept(); return; }
    }
    mousePressEvent(event);
}

void InkCanvas::wheelEvent(QWheelEvent* event)
{
    if (hasActiveGesture()) { event->accept(); return; }
    if (event->modifiers().testFlag(Qt::ControlModifier) && event->angleDelta().y() != 0) {
        Q_EMIT zoomRequested(std::pow(1.15, qreal(event->angleDelta().y()) / 120.0), event->position());
        event->accept();
    } else event->ignore();
}

bool InkCanvas::event(QEvent* event)
{
    if (event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchUpdate ||
        event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel) {
        auto* touch = static_cast<QTouchEvent*>(event);
        event->accept();
        if (event->type() == QEvent::TouchBegin && !m_externalPenContact && !m_tabletActive)
            m_suppressedTouch = false;
        if (m_externalPenContact || m_tabletActive || m_suppressedTouch) {
            m_suppressedTouch = event->type() != QEvent::TouchEnd && event->type() != QEvent::TouchCancel;
            if (!m_suppressedTouch) {
                m_touchActive = false;
                m_previousTouchCount = 0;
                Q_EMIT operationFinished();
            }
            return true;
        }
        if (event->type() == QEvent::TouchCancel) {
            if (m_source == Source::Touch) cancelActiveOperation();
            m_touchActive = false;
            m_previousTouchCount = 0;
            m_touchNavigationOnly = false;
            Q_EMIT operationFinished();
            return true;
        }
        if (m_tabletActive || m_source == Source::Pen) return true;
        QVector<QPointF> globals;
        QPointF released;
        for (const auto& point : touch->points()) {
            released = point.position();
            if (point.state() != QEventPoint::State::Released) globals.append(point.globalPosition());
            if (globals.size() == 2) break;
        }
        if (event->type() == QEvent::TouchEnd || globals.isEmpty()) {
            if (m_source == Source::Touch) pointerRelease(released, 0.6);
            m_touchActive = false;
            m_previousTouchCount = 0;
            m_touchNavigationOnly = false;
            Q_EMIT operationFinished();
            return true;
        }
        const QPointF center = globals.size() == 2 ? (globals[0] + globals[1]) / 2 : globals[0];
        const qreal distance = globals.size() == 2 ? QLineF(globals[0], globals[1]).length() : 0;
        if (globals.size() >= 2) {
            if (m_source == Source::Touch) cancelActiveOperation();
            m_touchNavigationOnly = true;
            if (m_touchActive && m_previousTouchCount == 2) {
                Q_EMIT panRequested(center - m_previousTouchGlobal);
                if (m_previousTouchDistance > 10 && distance > 10)
                    Q_EMIT zoomRequested(distance / m_previousTouchDistance, mapFromGlobal(center));
            }
        } else if (!m_touchActive || m_previousTouchCount != 1) {
            pointerPress(mapFromGlobal(center), center, 0.6, Source::Touch, false,
                         !m_touchAndMouseAnnotate || m_touchNavigationOnly);
        } else if (m_source == Source::Touch) {
            pointerMove(mapFromGlobal(center), center, 0.6);
        }
        m_touchActive = true;
        m_previousTouchGlobal = center;
        m_previousTouchCount = int(globals.size());
        m_previousTouchDistance = distance;
        return true;
    }
    if (event->type() == QEvent::WindowDeactivate) {
        if (!m_editor) finishActiveOperation();
        m_tabletActive = false;
        m_touchActive = false;
        Q_EMIT penContactChanged(false);
        Q_EMIT operationFinished();
    } else if (event->type() == QEvent::UngrabMouse) {
        if (m_gesture != Gesture::None) cancelActiveOperation();
        m_tabletActive = false;
        m_touchActive = false;
        Q_EMIT penContactChanged(false);
        Q_EMIT operationFinished();
    }
    return QWidget::event(event);
}

bool InkCanvas::eventFilter(QObject* watched, QEvent* event)
{
    if (!m_editor || m_textFinishing) return QWidget::eventFilter(watched, event);
    auto* widget = qobject_cast<QWidget*>(watched);
    const bool inEditor = widget && (widget == m_editor || m_editor->isAncestorOf(widget));
    if (inEditor && (event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchUpdate ||
                     event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel)) {
        if (event->type() == QEvent::TouchBegin && !m_externalPenContact && !m_tabletActive)
            m_suppressedTouch = false;
        if (m_externalPenContact || m_tabletActive || m_suppressedTouch) {
            m_suppressedTouch = event->type() != QEvent::TouchEnd && event->type() != QEvent::TouchCancel;
            event->accept();
            return true;
        }
    }
    if (inEditor && event->type() == QEvent::Wheel) {
        auto* wheel = static_cast<QWheelEvent*>(event);
        if (wheel->modifiers().testFlag(Qt::ControlModifier)) {
            const QPointF anchor = mapFromGlobal(wheel->globalPosition());
            const qreal delta = wheel->angleDelta().y() != 0 ? wheel->angleDelta().y() : wheel->pixelDelta().y();
            finishTextEdit(false);
            Q_EMIT zoomRequested(std::pow(1.0015, delta), anchor);
            event->accept();
            return true;
        }
    }
    if (inEditor && (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride)) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape ||
            ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && key->modifiers().testFlag(Qt::ControlModifier))) {
            event->accept();
            if (event->type() == QEvent::KeyPress) finishTextEdit(key->key() == Qt::Key_Escape);
            return true;
        }
    }
    if (widget && !inEditor && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::TabletPress ||
        (event->type() == QEvent::TouchBegin && !m_externalPenContact && !m_tabletActive))) finishTextEdit(false);
    return QWidget::eventFilter(watched, event);
}

void InkCanvas::focusOutEvent(QFocusEvent* event)
{
    // Transferring focus into our own text editor is part of the same operation.
    if (!m_editor) finishActiveOperation();
    QWidget::focusOutEvent(event);
}

void InkCanvas::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        if (hasActiveGesture()) cancelActiveOperation();
        else if (m_tool == Tool::TextSelect) Q_EMIT textSelectionStarted();
        else clearSelection();
        event->accept();
        return;
    }
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) &&
        m_tool != Tool::TextSelect && m_editingEnabled && !m_selection.isEmpty() && !hasActiveGesture()) {
        const auto before = m_annotations;
        for (qsizetype i = m_annotations.strokes.size(); i > 0; --i)
            if (m_selection.contains(m_annotations.strokes[i - 1].id)) m_annotations.strokes.removeAt(i - 1);
        for (qsizetype i = m_annotations.textBoxes.size(); i > 0; --i)
            if (m_selection.contains(m_annotations.textBoxes[i - 1].id)) m_annotations.textBoxes.removeAt(i - 1);
        for (qsizetype i = m_annotations.textMarkups.size(); i > 0; --i)
            if (m_selection.contains(m_annotations.textMarkups[i - 1].id)) m_annotations.textMarkups.removeAt(i - 1);
        clearSelection();
        commit(before, QStringLiteral("delete"), {}, false, Tool::Select);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace propernotes
