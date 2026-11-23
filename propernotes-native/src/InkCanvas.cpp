// SPDX-License-Identifier: GPL-3.0-only
#include "InkCanvas.h"
#include "ToolPreferences.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFocusEvent>
#include <QFont>
#include <QFormLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QInputMethod>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointingDevice>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QTabletEvent>
#include <QTextDocument>
#include <QTextCursor>
#include <QTouchEvent>
#include <QToolButton>
#include <QVariant>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
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

std::array<QPointF, 4> rectCorners(const QRectF& rect)
{
    return {rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()};
}

class SelectionActionButton final : public QToolButton {
public:
    SelectionActionButton(const QString& label, const QString& description, QWidget* parent) : QToolButton(parent)
    {
        setCursor(Qt::ArrowCursor);
        setText(label);
        setToolTip(description);
        setAccessibleName(description);
        setMinimumSize(44, 32);
        setFocusPolicy(Qt::TabFocus);
        hide();
    }

protected:
    void tabletEvent(QTabletEvent* event) override
    {
        event->accept();
        if (event->type() == QEvent::TabletPress)
            m_penPressed = event->button() == Qt::LeftButton
                && event->pointingDevice()->pointerType() != QPointingDevice::PointerType::Eraser;
        if (event->type() == QEvent::TabletRelease) {
            const bool activate = std::exchange(m_penPressed, false) && rect().contains(event->position().toPoint());
            if (activate) click();
        }
    }

    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::Hide || event->type() == QEvent::WindowDeactivate) m_penPressed = false;
        return QToolButton::event(event);
    }

private:
    bool m_penPressed = false;
};
} // namespace

InkCanvas::InkCanvas(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_AcceptTouchEvents);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAccessibleName(QStringLiteral("PDF 页面和批注"));
    m_objectStyleButton = new SelectionActionButton(QStringLiteral("样式"), QStringLiteral("编辑所选批注的样式"), this);
    m_objectCutButton = new SelectionActionButton(QStringLiteral("剪切"), QStringLiteral("剪切所选的批注"), this);
    m_objectCopyButton = new SelectionActionButton(QStringLiteral("复制"), QStringLiteral("复制所选的批注"), this);
    m_deleteSelectionButton = new SelectionActionButton(QStringLiteral("删除"), QStringLiteral("删除所选的批注，可撤销"), this);
    connect(m_objectStyleButton, &QToolButton::clicked, this, &InkCanvas::beginObjectStyleEdit);
    connect(m_objectCutButton, &QToolButton::clicked, this, &InkCanvas::objectCutRequested);
    connect(m_objectCopyButton, &QToolButton::clicked, this, &InkCanvas::objectCopyRequested);
    connect(m_deleteSelectionButton, &QToolButton::clicked, this, &InkCanvas::deleteSelection);
    qApp->installEventFilter(this);
    updateCursor();
    updateSelectionActions();
}

InkCanvas::~InkCanvas()
{
    qApp->removeEventFilter(this);
}

void InkCanvas::setPage(int pageIndex, const QImage& background, const QTransform& pdfToImage,
                        const QRectF& pageRect, qreal rasterScale, const PageAnnotations& annotations)
{
    finishActiveOperation();
    if (pageIndex != m_pageIndex || annotations != m_annotations) {
        m_touchSelectionAction = nullptr;
        cancelObjectStyleEdit();
    }
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
    m_touchSelectionAction = nullptr;
    cancelObjectStyleEdit();
    m_annotations = annotations;
    QSet<QUuid> valid;
    for (const auto& stroke : annotations.strokes) valid.insert(stroke.id);
    for (const auto& shape : annotations.shapes) valid.insert(shape.id);
    for (const auto& text : annotations.textBoxes) valid.insert(text.id);
    if (m_editTextMarkups)
        for (const auto& markup : annotations.textMarkups) valid.insert(markup.id);
    m_selection.intersect(valid);
    rebuildInkCache();
    updateSelectionActions();
    Q_EMIT selectionChanged();
}

void InkCanvas::clearPage()
{
    finishActiveOperation();
    m_touchSelectionAction = nullptr;
    cancelObjectStyleEdit();
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
    m_touchSelectionAction = nullptr;
    cancelObjectStyleEdit();
    m_tool = tool;
    if (tool == Tool::TextSelect)
        clearSelection();
    updateCursor();
    updateSelectionActions();
    update();
}

std::optional<ShapeKind> InkCanvas::shapeKind(Tool tool)
{
    switch (tool) {
    case Tool::Line: return ShapeKind::Line;
    case Tool::Arrow: return ShapeKind::Arrow;
    case Tool::Rectangle: return ShapeKind::Rectangle;
    case Tool::Ellipse: return ShapeKind::Ellipse;
    default: return std::nullopt;
    }
}

void InkCanvas::setPenColor(const QColor& color) { if (color.isValid()) m_penColor = color; }
void InkCanvas::setPenWidth(qreal width) { if (std::isfinite(width)) m_penWidthPt = std::clamp(width, 0.5, 24.0); }
void InkCanvas::setPenOpacity(qreal opacity) { if (std::isfinite(opacity)) m_penOpacity = std::clamp(opacity, 0.0, 1.0); }
void InkCanvas::setHighlighterStyle(const QColor& color, qreal widthPt)
{
    if (!color.isValid() || color.alpha() == 0 || !std::isfinite(widthPt))
        return;
    m_highlighterColor = color;
    m_highlighterWidthPt = std::clamp(widthPt, qreal(2), qreal(48));
}
void InkCanvas::setPressureSensitivity(qreal sensitivity) { if (std::isfinite(sensitivity)) m_pressureSensitivity = std::clamp(sensitivity, 0.0, 100.0); }
void InkCanvas::setEraserRadius(qreal radius) { if (std::isfinite(radius)) m_eraserRadiusPt = std::clamp(radius, 1.0, 100.0); }
void InkCanvas::setEditTextMarkups(bool enabled)
{
    if (m_editTextMarkups == enabled) return;
    if (m_gesture == Gesture::Erase || m_gesture == Gesture::Marquee) cancelActiveOperation();
    m_editTextMarkups = enabled;
    if (enabled) return;

    const auto before = m_selection;
    for (const auto& markup : m_annotations.textMarkups) {
        m_selection.remove(markup.id);
        m_selectionBeforeGesture.remove(markup.id);
    }
    if (before != m_selection) {
        m_touchSelectionAction = nullptr;
        updateSelectionActions();
        Q_EMIT selectionChanged();
        update();
    }
}
void InkCanvas::setTouchAndMouseAnnotate(bool enabled) { if (m_touchAndMouseAnnotate != enabled) finishActiveOperation(); m_touchAndMouseAnnotate = enabled; }
void InkCanvas::setTextReviewActive(bool active)
{
    if (m_textReviewActive == active) return;
    m_textReviewActive = active;
    if (m_gesture == Gesture::TextSelection) finishActiveOperation();
}

void InkCanvas::setTextStyle(const QString& family, qreal size, const QColor& color)
{
    if (!family.isEmpty()) m_textFamily = family;
    if (std::isfinite(size)) m_textSizePt = std::clamp(size, 4.0, 240.0);
    if (color.isValid()) m_textColor = color;
}

void InkCanvas::setEditingEnabled(bool enabled)
{
    if (enabled == m_editingEnabled) return;
    if (!enabled && m_editingEnabled) finishActiveOperation();
    m_editingEnabled = enabled;
    if (!enabled) {
        m_touchSelectionAction = nullptr;
        cancelObjectStyleEdit();
    }
    updateCursor();
    updateSelectionActions();
}

bool InkCanvas::hasActiveGesture() const { return m_gesture != Gesture::None || m_editor || m_objectStyleDialog || m_touchSelectionAction; }

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
    const qreal bounded = std::clamp(zoom, 1e-6, 4.0);
    if (bounded == m_zoom) return;
    m_zoom = bounded;
    updateExtent();
}

void InkCanvas::setVisibleCanvasRect(const QRect& visibleRect)
{
    if (visibleRect == m_visibleCanvasRect) return;
    m_visibleCanvasRect = visibleRect;
    if (!m_visibleCanvasRect.intersects(rect())) cancelObjectStyleEdit();
    updateSelectionActions();
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
    updateSelectionActions();
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
    if (m_background.isNull() || (m_annotations.strokes.isEmpty() && m_annotations.shapes.isEmpty() && m_annotations.textBoxes.isEmpty()
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
    qsizetype strokeIndex = 0;
    for (const auto& stroke : m_annotations.strokes) {
        const bool preview = !m_transformPreview.strokes.isEmpty() && m_selection.contains(stroke.id);
        paintInkStroke(painter, preview ? m_transformPreview.strokes[strokeIndex++] : stroke);
    }
    qsizetype shapeIndex = 0;
    for (const auto& shape : m_annotations.shapes) {
        const bool preview = !m_transformPreview.shapes.isEmpty() && m_selection.contains(shape.id);
        paintShape(painter, preview ? m_transformPreview.shapes[shapeIndex++] : shape);
    }
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
    // Paint one fresh preview over the page on each repaint. Never stamp
    // individual translucent segments into a persistent stroke buffer.
    const bool drawing = m_gesture == Gesture::Ink;
    const bool highlighting = drawing && m_pendingStroke.kind == InkStrokeKind::Highlighter;
    if (highlighting) paintInkStroke(painter, m_pendingStroke);
    paintTextQuads(painter, m_searchQuads, QColor(255, 169, 17, 105));
    const auto& selection = m_gesture == Gesture::TextSelection ? m_textPreview.quads : m_selectedTextQuads;
    paintTextQuads(painter, selection, QColor(48, 128, 237, 105));
    if (drawing && !highlighting) paintInkStroke(painter, m_pendingStroke);
    if (m_pendingShape && validShape(*m_pendingShape)) paintShape(painter, *m_pendingShape);
    painter.restore();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor("#2674d9"), 1.3, Qt::DashLine));
    if (m_gesture == Gesture::Marquee || m_gesture == Gesture::TextFrame)
        painter.drawRect(pdfToWidget().mapRect(QRectF(m_gestureStart, m_gestureEnd).normalized()));
    if (!m_selection.isEmpty()) {
        const bool transformHandles = hasTransformSelection() && (m_gesture == Gesture::None ||
            m_gesture == Gesture::ScaleSelection || m_gesture == Gesture::RotateSelection);
        const QTransform transform = pdfToWidget();
        const QRectF frame = transformHandles ? selectionTransformFrame() : transform.mapRect(movableSelectionBounds(m_annotations));
        if (m_transformRejected) painter.setPen(QPen(QColor("#b14838"), 1.3, Qt::DashLine));
        if (!frame.isEmpty()) painter.drawRect(frame);
        if (transformHandles) {
            painter.setPen(QPen(QColor("#2674d9"), 1.3));
            painter.setBrush(Qt::white);
            for (const auto& corner : rectCorners(frame))
                painter.drawRect(QRectF(corner - QPointF(4.5, 4.5), QSizeF(9, 9)));
            const QPointF handle = selectionRotationHandle(frame);
            const qreal edge = handle.y() < frame.top() ? frame.top() : frame.bottom();
            painter.drawLine(QPointF(handle.x(), edge), handle);
            painter.drawEllipse(handle, 6, 6);
        } else if (m_selection.size() == 1) {
            for (const auto& text : m_annotations.textBoxes) {
                if (!m_selection.contains(text.id)) continue;
                painter.setBrush(Qt::white);
                for (const QPointF& point : {text.rect.topLeft(), text.rect.topRight(), text.rect.bottomRight(), text.rect.bottomLeft()})
                    painter.drawRect(QRectF(widgetPointForPdf(point) - QPointF(4, 4), QSizeF(8, 8)));
            }
        }
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor("#2674d9"), 1.3, Qt::DotLine));
        for (const auto& markup : m_annotations.textMarkups) {
            if (m_selection.contains(markup.id)) painter.drawPath(transform.map(textMarkupPath(markup)));
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
    m_touchSelectionAction = nullptr;
    if (m_gesture == Gesture::ScaleSelection || m_gesture == Gesture::RotateSelection)
        finishSelectionTransform(FinishReason::ForcedCommit);
    m_selection.clear();
    cancelObjectStyleEdit();
    updateSelectionActions();
    Q_EMIT selectionChanged();
    update();
}

bool InkCanvas::hasSelectedTextMarkup() const
{
    return m_editTextMarkups && !m_selection.isEmpty() && std::any_of(m_annotations.textMarkups.cbegin(),
        m_annotations.textMarkups.cend(), [this](const TextMarkup& markup) { return m_selection.contains(markup.id); });
}

bool InkCanvas::hasTransformSelection() const
{
    if (!m_editingEnabled || m_pageIndex < 0 || m_background.isNull() || m_tool != Tool::Select || m_selection.isEmpty())
        return false;
    const bool selectedTextBox = std::any_of(m_annotations.textBoxes.cbegin(), m_annotations.textBoxes.cend(), [this](const TextBox& text) {
        return m_selection.contains(text.id);
    });
    const auto selected = [this](const auto& object) { return m_selection.contains(object.id); };
    return !selectedTextBox && (std::any_of(m_annotations.strokes.cbegin(), m_annotations.strokes.cend(), selected)
        || std::any_of(m_annotations.shapes.cbegin(), m_annotations.shapes.cend(), selected));
}

bool InkCanvas::hasObjectSelection() const
{
    return m_editingEnabled && m_pageIndex >= 0 && !m_background.isNull() && m_tool == Tool::Select
        && !m_selection.isEmpty() && !hasSelectedTextMarkup();
}

PageAnnotations InkCanvas::selectedObjects() const
{
    PageAnnotations result;
    if (!hasObjectSelection()) return result;
    for (const auto& stroke : m_annotations.strokes) {
        if (m_selection.contains(stroke.id)) result.strokes.append(stroke);
    }
    for (const auto& shape : m_annotations.shapes) {
        if (m_selection.contains(shape.id)) result.shapes.append(shape);
    }
    for (const auto& text : m_annotations.textBoxes) {
        if (m_selection.contains(text.id)) result.textBoxes.append(text);
    }
    return result;
}

void InkCanvas::selectObjects(const QSet<QUuid>& ids)
{
    if (!m_editingEnabled || m_pageIndex < 0 || m_background.isNull() || m_tool != Tool::Select || hasActiveGesture()) return;
    QSet<QUuid> valid;
    for (const auto& stroke : m_annotations.strokes) {
        if (ids.contains(stroke.id)) valid.insert(stroke.id);
    }
    for (const auto& shape : m_annotations.shapes) {
        if (ids.contains(shape.id)) valid.insert(shape.id);
    }
    for (const auto& text : m_annotations.textBoxes) {
        if (ids.contains(text.id)) valid.insert(text.id);
    }
    if (valid == m_selection) return;
    m_selection = std::move(valid);
    updateSelectionActions();
    Q_EMIT selectionChanged();
    update();
}

const InkStroke* InkCanvas::selectedInkStroke() const
{
    if (!m_editingEnabled || m_pageIndex < 0 || m_background.isNull() || m_tool != Tool::Select || m_selection.size() != 1)
        return nullptr;
    const auto id = *m_selection.cbegin();
    const auto stroke = std::find_if(m_annotations.strokes.cbegin(), m_annotations.strokes.cend(),
        [&id](const InkStroke& value) { return value.id == id; });
    return stroke == m_annotations.strokes.cend() ? nullptr : &*stroke;
}

const ShapeAnnotation* InkCanvas::selectedShape() const
{
    if (!hasObjectSelection() || m_selection.size() != 1) return nullptr;
    const auto id = *m_selection.cbegin();
    const auto shape = std::find_if(m_annotations.shapes.cbegin(), m_annotations.shapes.cend(),
        [&id](const ShapeAnnotation& value) { return value.id == id; });
    return shape == m_annotations.shapes.cend() ? nullptr : &*shape;
}

void InkCanvas::updateSelectionActions()
{
    const auto hideActions = [this] {
        m_objectStyleButton->hide();
        m_objectCutButton->hide();
        m_objectCopyButton->hide();
        m_deleteSelectionButton->hide();
    };
    if (m_gesture != Gesture::None || m_editor || m_objectStyleDialog || m_externalPenContact || !m_editingEnabled
        || m_pageIndex < 0 || m_background.isNull() || m_tool != Tool::Select || m_selection.isEmpty()) {
        hideActions();
        return;
    }

    const bool anchored = hasSelectedTextMarkup();
    bool hasRotateHandle = hasTransformSelection();
    const QRect visible = m_visibleCanvasRect.intersected(rect());
    const QRect available = visible.adjusted(6, 6, -6, -6);
    QRectF frame = hasRotateHandle ? selectionTransformFrame() : pdfToWidget().mapRect(movableSelectionBounds(m_annotations));
    if (anchored && !frame.intersects(QRectF(visible))) {
        frame = {};
        hasRotateHandle = false;
        const QTransform transform = pdfToWidget();
        for (const auto& markup : m_annotations.textMarkups) {
            if (m_selection.contains(markup.id)) frame = frame.united(transform.mapRect(textMarkupBounds(markup)));
        }
    }
    const QRectF visibleFrame = frame.intersected(QRectF(visible));
    QVector<QToolButton*> actions;
    if (anchored) {
        m_objectCutButton->hide();
        m_objectCopyButton->hide();
        actions.append(m_deleteSelectionButton);
    } else {
        m_deleteSelectionButton->hide();
        actions = {m_objectCutButton, m_objectCopyButton};
    }
    if (selectedInkStroke() || selectedShape()) actions.prepend(m_objectStyleButton);
    else m_objectStyleButton->hide();

    constexpr int gap = 3;
    QSize buttonSize;
    for (const auto* button : std::as_const(actions))
        buttonSize = buttonSize.expandedTo(button->sizeHint().expandedTo(button->minimumSize()));
    const int count = int(actions.size());
    const int rowWidth = count * (buttonSize.width() + gap) - gap;
    const bool horizontal = rowWidth <= available.width();
    const QSize extent = horizontal ? QSize(rowWidth, buttonSize.height()) : QSize(buttonSize.width(), count * (buttonSize.height() + gap) - gap);
    if (extent.width() > available.width() || extent.height() > available.height() || visibleFrame.isEmpty()) {
        hideActions();
        return;
    }

    const int maxX = available.x() + available.width() - extent.width();
    const int maxY = available.y() + available.height() - extent.height();
    const qreal rotateY = hasRotateHandle ? selectionRotationHandle(frame).y() : visibleFrame.center().y();
    const int below = qRound(std::max(visibleFrame.bottom(), rotateY)) + 14;
    const int above = qRound(std::min(visibleFrame.top(), rotateY)) - extent.height() - 14;
    const int x = std::clamp(qRound(visibleFrame.right()) - extent.width(), available.x(), maxX);
    const int y = below <= maxY ? std::max(below, available.y()) : std::clamp(above, available.y(), maxY);
    QPoint position(x, y);
    const QPoint step = horizontal ? QPoint(buttonSize.width() + gap, 0) : QPoint(0, buttonSize.height() + gap);
    for (auto* button : std::as_const(actions)) {
        button->setGeometry(QRect(position, buttonSize));
        button->show();
        position += step;
    }
}

QToolButton* InkCanvas::selectionActionAt(const QPointF& position) const
{
    for (auto* button : {m_objectStyleButton, m_objectCutButton, m_objectCopyButton, m_deleteSelectionButton}) {
        if (button->isVisible() && button->isEnabled() && button->geometry().contains(position.toPoint())) return button;
    }
    return nullptr;
}

void InkCanvas::beginObjectStyleEdit()
{
    const InkStroke* stroke = selectedInkStroke();
    const ShapeAnnotation* shape = selectedShape();
    if ((!stroke && !shape) || hasActiveGesture() || hasPointerContact() || m_externalPenContact) return;

    const auto originalStroke = stroke ? std::optional<InkStroke>(*stroke) : std::nullopt;
    const auto originalShape = shape ? std::optional<ShapeAnnotation>(*shape) : std::nullopt;
    const QColor originalColor = shape ? shape->style.strokeColor : stroke->color;
    const QRectF objectRect = shape ? shapeBounds(*shape) : inkStrokeBounds(*stroke);
    const QUuid id = shape ? shape->id : stroke->id;
    const int page = m_pageIndex;
    const qreal userUnit = m_pageUserUnit;
    const bool highlighter = stroke && stroke->kind == InkStrokeKind::Highlighter;
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    if (shape) dialog->setWindowTitle(QStringLiteral("编辑形状样式"));
    else dialog->setWindowTitle(highlighter ? QStringLiteral("编辑自由荧光笔样式") : QStringLiteral("编辑笔迹样式"));
    auto* form = new QFormLayout(dialog);

    const auto colorControl = [this, dialog, form](const QString& label, const QColor& initial) {
        auto* button = new QPushButton(dialog);
        auto* picker = new QColorDialog(initial.toRgb(), dialog);
        auto color = std::make_shared<QColor>(initial.toRgb());
        const auto showColor = [button](const QColor& value) {
            QPixmap swatch(18, 18);
            swatch.fill(QColor(value.rgb()));
            button->setIcon(QIcon(swatch));
            button->setText(value.name(QColor::HexRgb));
        };
        showColor(*color);
        connect(button, &QPushButton::clicked, picker, [picker, color] {
            picker->setCurrentColor(QColor(color->rgb()));
            picker->open();
        });
        connect(picker, &QColorDialog::colorSelected, dialog, [color, showColor](const QColor& value) {
            *color = value.toRgb();
            showColor(*color);
        });
        form->addRow(label, button);
        return std::pair{button, color};
    };
    const auto draftColor = colorControl(shape ? QStringLiteral("线条颜色") : QStringLiteral("颜色"), originalColor).second;

    auto* width = new QDoubleSpinBox(dialog);
    width->setDecimals(3);
    width->setSingleStep(0.5);
    width->setSuffix(QStringLiteral(" pt"));
    const qreal originalWidth = (shape ? shape->style.widthPt : stroke->widthPt) * userUnit;
    const qreal normalMinWidth = highlighter ? MinHighlighterWidthPt : 0.5;
    const qreal normalMaxWidth = highlighter ? MaxHighlighterWidthPt : 24;
    width->setRange(std::min(originalWidth, normalMinWidth), std::max(originalWidth, normalMaxWidth));
    width->setValue(originalWidth);
    const qreal initialWidth = width->value();
    form->addRow(QStringLiteral("粗细"), width);

    auto* opacity = new QDoubleSpinBox(dialog);
    opacity->setDecimals(2);
    opacity->setSuffix(QStringLiteral("%"));
    const qreal originalOpacity = originalColor.alphaF() * 100;
    const qreal minimumOpacity = shape ? 1 : highlighter ? MinHighlighterOpacity : 0;
    opacity->setRange(std::min(minimumOpacity, originalOpacity), 100);
    opacity->setValue(originalOpacity);
    const qreal initialOpacity = opacity->value();
    form->addRow(shape ? QStringLiteral("线条不透明度") : QStringLiteral("不透明度"), opacity);

    QCheckBox* fillEnabled = nullptr;
    QDoubleSpinBox* fillOpacity = nullptr;
    std::shared_ptr<QColor> draftFill;
    qreal initialFillOpacity = 0;
    if (shape && isAreaShape(shape->kind)) {
        const QColor originalFill = shape->style.fillColor;
        fillEnabled = new QCheckBox(QStringLiteral("填充"), dialog);
        fillEnabled->setChecked(originalFill.alpha() > 0);
        form->addRow(fillEnabled);
        const auto fill = colorControl(QStringLiteral("填充颜色"), originalFill);
        draftFill = fill.second;
        fillOpacity = new QDoubleSpinBox(dialog);
        fillOpacity->setDecimals(2);
        fillOpacity->setSuffix(QStringLiteral("%"));
        const qreal originalFillOpacity = originalFill.alphaF() * 100;
        fillOpacity->setRange(originalFillOpacity > 0 ? std::min(qreal(1), originalFillOpacity) : 1, 100);
        fillOpacity->setValue(originalFillOpacity > 0 ? originalFillOpacity : 25);
        initialFillOpacity = fillOpacity->value();
        form->addRow(QStringLiteral("填充不透明度"), fillOpacity);
        fill.first->setEnabled(fillEnabled->isChecked());
        fillOpacity->setEnabled(fillEnabled->isChecked());
        connect(fillEnabled, &QCheckBox::toggled, fill.first, &QWidget::setEnabled);
        connect(fillEnabled, &QCheckBox::toggled, fillOpacity, &QWidget::setEnabled);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Apply)->setText(QStringLiteral("应用"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    form->addRow(buttons);

    connect(dialog, &QDialog::finished, this, [this, dialog, page, userUnit, id, originalStroke, originalShape,
            originalColor, draftColor, width, opacity, initialWidth, initialOpacity, fillEnabled, fillOpacity,
            initialFillOpacity, draftFill](int result) {
        if (m_objectStyleDialog.data() != dialog) return;
        m_objectStyleDialog.clear();
        if (result == QDialog::Accepted && page == m_pageIndex && qFuzzyCompare(userUnit, m_pageUserUnit)) {
            QColor color = originalColor;
            if (draftColor->rgb() != originalColor.rgb()) {
                color = *draftColor;
                color.setAlphaF(originalColor.alphaF());
            }
            if (opacity->value() != initialOpacity) color.setAlphaF(opacity->value() / 100);
            const qreal chosenWidth = std::clamp(width->value() / userUnit, MinInkStrokeWidthPt, MaxInkStrokeWidthPt);
            if (originalStroke) {
                const InkStroke* selected = selectedInkStroke();
                if (selected && *selected == *originalStroke) {
                    auto edited = *originalStroke;
                    edited.color = color;
                    if (width->value() != initialWidth) edited.widthPt = chosenWidth;
                    if (edited != *selected) {
                        const qsizetype index = selected - m_annotations.strokes.constData();
                        const auto before = m_annotations;
                        m_annotations.strokes[index] = std::move(edited);
                        commit(before, QStringLiteral("ink-style"), id, false, Tool::Select);
                        return;
                    }
                }
            } else {
                const ShapeAnnotation* selected = selectedShape();
                if (selected && *selected == *originalShape) {
                    auto edited = *originalShape;
                    edited.style.strokeColor = color;
                    if (width->value() != initialWidth) edited.style.widthPt = chosenWidth;
                    if (fillEnabled) {
                        const QColor& originalFill = originalShape->style.fillColor;
                        QColor& fill = edited.style.fillColor;
                        if (draftFill->rgb() != originalFill.rgb()) {
                            fill = *draftFill;
                            fill.setAlphaF(originalFill.alphaF());
                        }
                        if (!fillEnabled->isChecked()) fill.setAlpha(0);
                        else if (originalFill.alpha() == 0 || fillOpacity->value() != initialFillOpacity)
                            fill.setAlphaF(fillOpacity->value() / 100);
                    }
                    if (edited != *selected) {
                        if (validShape(edited)) {
                            const qsizetype index = selected - m_annotations.shapes.constData();
                            const auto before = m_annotations;
                            m_annotations.shapes[index] = std::move(edited);
                            commit(before, QStringLiteral("shape-style"), id, false, Tool::Select);
                            return;
                        }
                        Q_EMIT activity(QStringLiteral("形状样式无效，请保留可见的线条或填充。"));
                    }
                }
            }
        }
        updateSelectionActions();
        Q_EMIT operationFinished();
    });

    const QRectF frame = pdfToWidget().mapRect(objectRect);
    const QRectF visible = frame.intersected(QRectF(m_visibleCanvasRect));
    const QPoint anchor = m_objectStyleButton->isVisible() ? m_objectStyleButton->mapToGlobal(QPoint(0, m_objectStyleButton->height()))
        : mapToGlobal((visible.isEmpty() ? frame.center() : visible.center()).toPoint());
    QScreen* display = QGuiApplication::screenAt(anchor);
    if (!display) display = screen();
    dialog->adjustSize();
    if (display) {
        const QRect area = display->availableGeometry().adjusted(12, 34, -12, -12);
        const int maxX = std::max(area.x(), area.x() + area.width() - dialog->width());
        const int maxY = std::max(area.y(), area.y() + area.height() - dialog->height());
        dialog->move(std::clamp(anchor.x(), area.x(), maxX), std::clamp(anchor.y() + 8, area.y(), maxY));
    }
    m_objectStyleDialog = dialog;
    updateSelectionActions();
    dialog->open();
}

void InkCanvas::cancelObjectStyleEdit()
{
    if (m_objectStyleDialog) m_objectStyleDialog->reject();
}

QRectF InkCanvas::objectBounds(const QUuid& id) const
{
    for (const auto& stroke : m_annotations.strokes)
        if (stroke.id == id) return inkStrokeBounds(stroke);
    for (const auto& shape : m_annotations.shapes)
        if (shape.id == id) return shapeBounds(shape);
    for (const auto& text : m_annotations.textBoxes)
        if (text.id == id) return text.rect;
    for (const auto& markup : m_annotations.textMarkups)
        if (markup.id == id) return textMarkupBounds(markup);
    return {};
}

QRectF InkCanvas::movableSelectionBounds(const PageAnnotations& annotations) const
{
    QRectF result;
    for (const auto& stroke : annotations.strokes) {
        if (m_selection.contains(stroke.id)) result = result.united(inkStrokeBounds(stroke));
    }
    for (const auto& shape : annotations.shapes) {
        if (m_selection.contains(shape.id)) result = result.united(shapeBounds(shape));
    }
    for (const auto& text : annotations.textBoxes) {
        if (m_selection.contains(text.id)) result = result.united(text.rect);
    }
    return result;
}

QUuid InkCanvas::firstMovableId() const
{
    for (const auto& stroke : m_annotations.strokes)
        if (m_selection.contains(stroke.id)) return stroke.id;
    for (const auto& shape : m_annotations.shapes)
        if (m_selection.contains(shape.id)) return shape.id;
    for (const auto& text : m_annotations.textBoxes)
        if (m_selection.contains(text.id)) return text.id;
    return {};
}

QRectF InkCanvas::selectionTransformFrame() const
{
    const bool hasPreview = !m_transformPreview.strokes.isEmpty() || !m_transformPreview.shapes.isEmpty();
    const QRectF bounds = movableSelectionBounds(hasPreview ? m_transformPreview : m_annotations);
    const QRectF frame = pdfToWidget().mapRect(bounds);
    const qreal padX = std::max(qreal(3), (40 - frame.width()) / 2);
    const qreal padY = std::max(qreal(3), (40 - frame.height()) / 2);
    return frame.adjusted(-padX, -padY, padX, padY);
}

QPointF InkCanvas::selectionRotationHandle(const QRectF& frame) const
{
    constexpr qreal offset = 23;
    const QRect visible = m_visibleCanvasRect.intersected(rect());
    const qreal above = frame.top() - offset;
    return {frame.center().x(), above >= visible.top() + 8 ? above : frame.bottom() + offset};
}

bool InkCanvas::beginSelectionTransform(const QPointF& position, Source source)
{
    if (!hasTransformSelection()) return false;
    const QRectF frame = selectionTransformFrame();
    const qreal targetRadius = source == Source::Touch ? 15 : 9;
    const QRectF bounds = movableSelectionBounds(m_annotations);
    if (QLineF(position, selectionRotationHandle(frame)).length() <= targetRadius) {
        m_gesture = Gesture::RotateSelection;
        m_transformPivot = bounds.center();
    } else {
        const auto handles = rectCorners(frame);
        const auto hit = std::find_if(handles.begin(), handles.end(), [&position, targetRadius](const QPointF& point) {
            return QLineF(position, point).length() <= targetRadius;
        });
        if (hit == handles.end()) return false;
        const auto corners = rectCorners(pdfToWidget().mapRect(bounds));
        const size_t opposite = (size_t(hit - handles.begin()) + 2) % corners.size();
        m_gesture = Gesture::ScaleSelection;
        m_transformPivot = pdfPointAt(corners[opposite]);
    }

    m_transformStart = pdfPointAt(position) - m_transformPivot;
    m_beforeGesture = m_annotations;
    m_selectionBeforeGesture = m_selection;
    m_focusId = firstMovableId();
    return true;
}

void InkCanvas::updateSelectionTransform(const QPointF& position, Qt::KeyboardModifiers modifiers)
{
    const QPointF direction = pdfPointAt(position) - m_transformPivot;
    const qreal dot = QPointF::dotProduct(m_transformStart, direction);
    qreal scale = 1;
    qreal angle = 0;
    if (m_gesture == Gesture::ScaleSelection) {
        const qreal distanceSquared = QPointF::dotProduct(m_transformStart, m_transformStart);
        scale = distanceSquared > 0 ? dot / distanceSquared : 0;
    } else {
        const qreal cross = m_transformStart.x() * direction.y() - m_transformStart.y() * direction.x();
        angle = std::atan2(cross, dot);
        if (modifiers.testFlag(Qt::ShiftModifier)) {
            constexpr qreal step = std::numbers::pi_v<qreal> / 12;
            angle = std::round(angle / step) * step;
        }
    }

    m_transformRejected = !std::isfinite(scale) || scale <= 0 || !std::isfinite(angle);
    PageAnnotations candidate;
    const bool changed = std::abs(scale - 1) > 1e-9 || std::abs(angle) > 1e-9;
    if (changed && !m_transformRejected) {
        const qreal sine = std::sin(angle);
        const qreal cosine = std::cos(angle);
        const QRectF allowed = m_pageRect.adjusted(-1e-7, -1e-7, 1e-7, 1e-7);
        const auto mapPoint = [this, scale, sine, cosine](const QPointF& point) {
            const QPointF delta = (point - m_transformPivot) * scale;
            return m_transformPivot + QPointF(cosine * delta.x() - sine * delta.y(), sine * delta.x() + cosine * delta.y());
        };
        for (const auto& original : m_beforeGesture.strokes) {
            if (!m_selection.contains(original.id)) continue;
            InkStroke stroke = original;
            stroke.widthPt *= scale;
            if (stroke.widthPt < MinInkStrokeWidthPt || stroke.widthPt > MaxInkStrokeWidthPt) {
                m_transformRejected = true;
                break;
            }
            for (auto& point : stroke.points) {
                point.position = mapPoint(point.position);
                if (!std::isfinite(point.position.x()) || !std::isfinite(point.position.y()) ||
                    std::abs(point.position.x()) > MaxInkCoordinate || std::abs(point.position.y()) > MaxInkCoordinate) {
                    m_transformRejected = true;
                    break;
                }
            }
            if (m_transformRejected || !allowed.contains(inkStrokeBounds(stroke))) {
                m_transformRejected = true;
                break;
            }
            candidate.strokes.append(std::move(stroke));
        }
        for (const auto& original : m_beforeGesture.shapes) {
            if (m_transformRejected) break;
            if (!m_selection.contains(original.id)) continue;
            ShapeAnnotation shape = original;
            const QPointF center = shapeCenter(original);
            const QPointF newCenter = mapPoint(center);
            shape.start = newCenter + (original.start - center) * scale;
            shape.end = newCenter + (original.end - center) * scale;
            shape.rotationDegrees = std::remainder(original.rotationDegrees + angle * 180 / std::numbers::pi_v<qreal>, 360.0);
            shape.style.widthPt *= scale;
            if (!validShape(shape) || !allowed.contains(shapeBounds(shape))) {
                m_transformRejected = true;
                break;
            }
            candidate.shapes.append(std::move(shape));
        }
    }

    if (m_transformRejected) candidate = {};
    if (candidate == m_transformPreview) return;
    m_transformPreview = std::move(candidate);
    rebuildInkCache();
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
    for (const auto& shape : m_annotations.shapes) {
        if (rect.intersects(shapeBounds(shape)) && (shapeOutline(shape).intersects(rect) || shapeFill(shape).intersects(rect)))
            result.insert(shape.id);
    }
    for (const auto& text : m_annotations.textBoxes)
        if (rect.intersects(text.rect) || rect.contains(text.rect)) result.insert(text.id);
    if (m_editTextMarkups) {
        for (const auto& markup : m_annotations.textMarkups) {
            const auto path = textMarkupPath(markup);
            if (path.intersects(rect) || rect.contains(path.boundingRect())) result.insert(markup.id);
        }
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
    const bool followsTool = source == Source::Pen || m_touchAndMouseAnnotate;
    if (m_tool == Tool::TextSelect && !forcePan && !tailEraser && (followsTool || !m_textReviewActive)) {
        setFocus(Qt::MouseFocusReason);
        beginTextSelection(position);
        return;
    }
    if (forcePan || (!followsTool && m_tool != Tool::Select) || (!tailEraser && m_tool == Tool::Pan) || !m_editingEnabled) {
        m_gesture = Gesture::Pan;
        updateCursor();
        updateSelectionActions();
        return;
    }
    if (followsTool && !tailEraser && beginSelectionTransform(position, source)) {
        setFocus(Qt::MouseFocusReason);
        updateCursor();
        updateSelectionActions();
        update();
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
    } else if (m_tool == Tool::Pen || m_tool == Tool::Highlighter) {
        m_gesture = Gesture::Ink;
        const bool highlighting = m_tool == Tool::Highlighter;
        m_inkPressureEnabled = !highlighting && m_pressureEnabled;
        m_inkSensitivity = m_pressureSensitivity;
        m_pendingStroke = {};
        if (highlighting) {
            m_pendingStroke.kind = InkStrokeKind::Highlighter;
            m_pendingStroke.color = m_highlighterColor;
            m_pendingStroke.widthPt = m_highlighterWidthPt / m_pageUserUnit;
        } else {
            m_pendingStroke.color = m_penColor;
            m_pendingStroke.color.setAlphaF(m_penColor.alphaF() * m_penOpacity);
            m_pendingStroke.widthPt = m_penWidthPt / m_pageUserUnit;
        }
        m_focusId = m_pendingStroke.id;
        addPoint(position, pressure);
    } else if (const auto kind = shapeKind(m_tool)) {
        m_gesture = Gesture::Shape;
        auto& shape = m_pendingShape.emplace();
        shape.kind = *kind;
        shape.start = shape.end = m_gestureStart;
        shape.style = m_shapeStyle;
        shape.style.widthPt /= m_pageUserUnit;
        if (!isAreaShape(*kind)) shape.style.fillColor = Qt::transparent;
        m_focusId = shape.id;
    } else if (m_tool == Tool::Select) {
        m_resizeCorner = followsTool ? textResizeCornerAt(position) : -1;
        if (m_resizeCorner >= 0) {
            m_gesture = Gesture::TextResize;
            m_focusId = *m_selection.cbegin();
        } else if (!m_selection.isEmpty() && movableSelectionBounds(m_annotations).contains(m_gestureStart)) {
            m_gesture = Gesture::Move;
            m_focusId = firstMovableId();
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
    updateSelectionActions();
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
    const qreal samplePressure = m_pendingStroke.kind == InkStrokeKind::Highlighter
        ? qreal(0.6) : std::clamp(pressure, qreal(0), qreal(1));
    const InkPoint point{clampedPdfPoint(position), samplePressure};
    if (!m_pendingStroke.points.isEmpty()) {
        const auto& previous = m_pendingStroke.points.back();
        if (QLineF(previous.position, point.position).length() < 0.20 / m_pageUserUnit &&
            std::abs(previous.pressure - point.pressure) < 0.025) return;
    }
    m_pendingStroke.points.append(point);
}

void InkCanvas::updateShapeDraft(const QPointF& position, Qt::KeyboardModifiers modifiers)
{
    auto& shape = *m_pendingShape;
    QPointF delta = clampedPdfPoint(position) - shape.start;
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        if (isAreaShape(shape.kind)) {
            const qreal side = std::max(std::abs(delta.x()), std::abs(delta.y()));
            const qreal xSign = delta.x() == 0 ? m_pageRect.center().x() - shape.start.x() : delta.x();
            const qreal ySign = delta.y() == 0 ? m_pageRect.center().y() - shape.start.y() : delta.y();
            delta = QPointF(std::copysign(side, xSign), std::copysign(side, ySign));
        } else {
            constexpr qreal step = std::numbers::pi_v<qreal> / 4;
            const qreal angle = std::round(std::atan2(delta.y(), delta.x()) / step) * step;
            const qreal length = std::hypot(delta.x(), delta.y());
            // Snap exact axes, avoiding a rounding residue that can trim a
            // horizontal or vertical gesture that begins on the crop edge.
            const qreal x = std::cos(angle);
            const qreal y = std::sin(angle);
            delta = length * QPointF(std::abs(x) < 1e-9 ? 0 : x, std::abs(y) < 1e-9 ? 0 : y);
        }
        const auto fraction = [](qreal start, qreal distance, qreal minimum, qreal maximum) {
            if (distance > 0) return (maximum - start) / distance;
            if (distance < 0) return (minimum - start) / distance;
            return qreal(1);
        };
        const qreal horizontal = fraction(shape.start.x(), delta.x(), m_pageRect.left(), m_pageRect.right());
        const qreal vertical = fraction(shape.start.y(), delta.y(), m_pageRect.top(), m_pageRect.bottom());
        delta *= std::clamp(std::min(horizontal, vertical), qreal(0), qreal(1));
    }
    shape.end = shape.start + delta;
}

void InkCanvas::pointerMove(const QPointF& position, const QPointF& global, qreal pressure, Qt::KeyboardModifiers modifiers)
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
    } else if (m_gesture == Gesture::Shape) {
        updateShapeDraft(position, modifiers);
    } else if (m_gesture == Gesture::Erase) {
        const QPointF next = clampedPdfPoint(position);
        eraseBetween(m_previousErasePoint, next);
        m_previousErasePoint = next;
    } else if (m_gesture == Gesture::TextSelection) {
        updateTextSelection(position);
    } else if (m_gesture == Gesture::ScaleSelection || m_gesture == Gesture::RotateSelection) {
        updateSelectionTransform(position, modifiers);
    } else if (m_gesture == Gesture::Move) {
        const QRectF bounds = movableSelectionBounds(m_beforeGesture);
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
        for (auto& shape : m_annotations.shapes) {
            if (!m_selection.contains(shape.id)) continue;
            shape.start += delta;
            shape.end += delta;
            if (!validShape(shape)) {
                m_annotations = m_beforeGesture;
                rebuildInkCache();
                return;
            }
        }
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
    const bool whole = m_activeEraseMode == EraseMode::WholeStroke;
    QVector<InkStroke> strokes;
    for (const auto& stroke : m_annotations.strokes) {
        if (whole) {
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
    if (whole) {
        for (qsizetype index = m_annotations.textBoxes.size(); index > 0; --index)
            if (rectHit(m_annotations.textBoxes[index - 1].rect, from, to, radius)) m_annotations.textBoxes.removeAt(index - 1);
    }
    const bool eraseShapes = whole && !m_annotations.shapes.isEmpty();
    const bool eraseMarkups = m_editTextMarkups && !m_annotations.textMarkups.isEmpty();
    if (eraseShapes || eraseMarkups) {
        const auto area = eraserArea(from, to, radius);
        const auto bounds = area.boundingRect();
        if (eraseShapes) {
            m_annotations.shapes.removeIf([&](const ShapeAnnotation& shape) {
                return bounds.intersects(shapeBounds(shape)) && (area.intersects(shapeOutline(shape)) || area.intersects(shapeFill(shape)));
            });
        }
        if (eraseMarkups) {
            m_annotations.textMarkups.removeIf([&](const TextMarkup& markup) {
                return bounds.intersects(textMarkupBounds(markup)) && area.intersects(textMarkupPath(markup));
            });
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
    m_pendingShape.reset();
    m_transformPreview = {};
    m_transformRejected = false;
    m_beforeGesture = {};
    m_selectionBeforeGesture.clear();
    m_textFirstGlyph = -1;
    m_textLastGlyph = -1;
    m_textPreview = {};
    updateCursor();
    updateSelectionActions();
}

void InkCanvas::commit(const PageAnnotations& before, const QString& reason, const QUuid& focusId,
                        bool normalCompletion, Tool completedTool)
{
    const PageAnnotations after = m_annotations;
    if (before != after) {
        QSet<QUuid> valid;
        for (const auto& stroke : after.strokes) valid.insert(stroke.id);
        for (const auto& shape : after.shapes) valid.insert(shape.id);
        for (const auto& text : after.textBoxes) valid.insert(text.id);
        if (m_editTextMarkups)
            for (const auto& markup : after.textMarkups) valid.insert(markup.id);
        const auto oldSelection = m_selection;
        m_selection.intersect(valid);
        if (oldSelection != m_selection) Q_EMIT selectionChanged();
        rebuildInkCache();
        Q_EMIT annotationsCommitted(m_pageIndex, before, after, reason, focusId);
        if (normalCompletion) Q_EMIT normalToolCompleted(completedTool, reason);
    }
    updateSelectionActions();
    Q_EMIT operationFinished();
    update();
}

void InkCanvas::pointerRelease(const QPointF& position, qreal pressure, Qt::KeyboardModifiers modifiers)
{
    if (m_gesture == Gesture::Ink) addPoint(position, pressure);
    else if (m_gesture != Gesture::Pan && m_gesture != Gesture::None)
        pointerMove(position, m_previousPanGlobal, pressure, modifiers);
    finishActiveOperation(FinishReason::NormalRelease);
}

void InkCanvas::finishGesture()
{
    m_touchSelectionAction = nullptr;
    cancelObjectStyleEdit();
    finishActiveOperation(FinishReason::ForcedCommit);
}

void InkCanvas::finishSelectionTransform(FinishReason reason)
{
    const auto before = m_beforeGesture;
    const auto focus = m_focusId;
    const bool scaling = m_gesture == Gesture::ScaleSelection;
    const bool hasShapes = !m_transformPreview.shapes.isEmpty();
    const bool hadPreview = hasShapes || !m_transformPreview.strokes.isEmpty();
    const bool normalRelease = reason == FinishReason::NormalRelease;
    const bool rejected = m_transformRejected;
    const bool changed = normalRelease && !rejected && hadPreview;
    if (changed) {
        qsizetype index = 0;
        for (auto& stroke : m_annotations.strokes) {
            if (m_selection.contains(stroke.id)) stroke = std::move(m_transformPreview.strokes[index++]);
        }
        index = 0;
        for (auto& shape : m_annotations.shapes) {
            if (m_selection.contains(shape.id)) shape = std::move(m_transformPreview.shapes[index++]);
        }
    }
    resetGesture();
    if (changed) {
        const QString action = hasShapes ? (scaling ? QStringLiteral("shape-scale") : QStringLiteral("shape-rotate"))
            : (scaling ? QStringLiteral("ink-scale") : QStringLiteral("ink-rotate"));
        commit(before, action, focus, false, Tool::Select);
        return;
    }
    if (hadPreview) rebuildInkCache();
    if (normalRelease && rejected) Q_EMIT activity(QStringLiteral("变换超出页面边界或可编辑范围，未应用。"));
    Q_EMIT operationFinished();
    update();
}

void InkCanvas::finishActiveOperation(FinishReason finishReason)
{
    if (m_editor) { finishTextEdit(false); return; }
    if (m_gesture == Gesture::None) return;
    const Gesture gesture = m_gesture;
    if (gesture == Gesture::ScaleSelection || gesture == Gesture::RotateSelection) {
        finishSelectionTransform(finishReason);
        return;
    }
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
    const Tool completedTool = m_tool;
    const bool completed = finishReason == FinishReason::NormalRelease && !m_tailEraser;
    const bool highlighting = gesture == Gesture::Ink && m_pendingStroke.kind == InkStrokeKind::Highlighter;
    const QRectF frame = QRectF(m_gestureStart, m_gestureEnd).normalized();
    if (gesture == Gesture::Ink && !m_pendingStroke.points.isEmpty()) m_annotations.strokes.append(m_pendingStroke);
    bool shapeCreated = false;
    if (gesture == Gesture::Shape && completed && m_pendingShape) {
        const QLineF drag(widgetPointForPdf(m_pendingShape->start), widgetPointForPdf(m_pendingShape->end));
        if (drag.length() >= 3) {
            shapeCreated = validShape(*m_pendingShape);
            if (shapeCreated) m_annotations.shapes.append(std::move(*m_pendingShape));
            else Q_EMIT activity(QStringLiteral("形状尺寸或线宽超出当前页可编辑范围。"));
        }
    }
    if (gesture == Gesture::Marquee && finishReason == FinishReason::NormalRelease) {
        const qreal slop = 3 / (m_zoom * m_pageUserUnit);
        m_selection = objectsIn(frame.isEmpty() ? QRectF(m_gestureStart - QPointF(slop, slop), QSizeF(slop * 2, slop * 2)) : frame);
        Q_EMIT selectionChanged();
        if (hasSelectedTextMarkup()) {
            const bool mixed = !movableSelectionBounds(m_annotations).isEmpty();
            Q_EMIT activity(mixed ? QStringLiteral("拖动只移动笔迹、形状和文字框。正文标记留在原文字上，删除会移除所选批注。")
                : QStringLiteral("正文标记固定在原文字上。可以删除所选标记。"));
        }
    }
    resetGesture();
    if (gesture == Gesture::TextFrame && finishReason == FinishReason::NormalRelease &&
        frame.width() >= 8 / m_pageUserUnit && frame.height() >= 8 / m_pageUserUnit) {
        beginTextEdit({}, frame);
        return;
    }
    if (gesture == Gesture::Ink) {
        commit(before, highlighting ? QStringLiteral("highlighter") : QStringLiteral("ink"),
            focusId, completed, highlighting ? Tool::Highlighter : Tool::Pen);
    } else if (shapeCreated) commit(before, QStringLiteral("shape"), focusId, true, completedTool);
    else if (gesture == Gesture::Erase) commit(before, QStringLiteral("erase"), {}, completed, Tool::ObjectEraser);
    else if (gesture == Gesture::Move) commit(before, QStringLiteral("move"), focusId, false, Tool::Select);
    else if (gesture == Gesture::TextResize) commit(before, QStringLiteral("text-resize"), focusId, false, Tool::Select);
    else { Q_EMIT operationFinished(); update(); }
}

void InkCanvas::cancelActiveOperation()
{
    m_touchSelectionAction = nullptr;
    if (m_objectStyleDialog) { cancelObjectStyleEdit(); return; }
    if (m_editor) { finishTextEdit(true); return; }
    if (m_gesture == Gesture::None) return;
    if (m_gesture == Gesture::ScaleSelection || m_gesture == Gesture::RotateSelection) {
        finishSelectionTransform(FinishReason::ForcedCommit);
        return;
    }
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
    m_touchSelectionAction = nullptr;
    m_touchActive = false;
    m_previousTouchCount = 0;
    m_touchNavigationOnly = false;
    Q_EMIT operationFinished();
}

void InkCanvas::setExternalPenContact(bool down)
{
    if (m_externalPenContact == down) return;
    m_externalPenContact = down;
    if (down) {
        m_suppressedTouch = true;
        if (m_source == Source::Touch || m_touchActive) cancelTouchOperation();
    }
    updateSelectionActions();
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
    else if (m_gesture == Gesture::ScaleSelection || m_gesture == Gesture::RotateSelection)
        setCursor(Qt::SizeAllCursor);
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
        if (m_source == Source::Pen) pointerMove(event->position(), event->globalPosition(), pressure, event->modifiers());
    } else if (event->type() == QEvent::TabletRelease) {
        if (m_source == Source::Pen) {
            const qreal last = m_gesture == Gesture::Ink && !m_pendingStroke.points.isEmpty()
                ? m_pendingStroke.points.back().pressure : pressure;
            pointerRelease(event->position(), last, event->modifiers());
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
                     event->button() == Qt::MiddleButton);
}

void InkCanvas::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();
    if (m_tabletActive || m_touchActive || event->source() != Qt::MouseEventNotSynthesized) return;
    m_hoverEraser = false;
    if (m_source == Source::Mouse) pointerMove(event->position(), event->globalPosition(), 0.6, event->modifiers());
    else { m_hoverPosition = event->position(); update(); }
}

void InkCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept();
    if (!m_tabletActive && !m_touchActive && event->source() == Qt::MouseEventNotSynthesized &&
        m_source == Source::Mouse && (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton))
        pointerRelease(event->position(), 0.6, event->modifiers());
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

void InkCanvas::contextMenuEvent(QContextMenuEvent* event)
{
    const bool keyboard = event->reason() == QContextMenuEvent::Keyboard;
    if (!keyboard && !m_touchAndMouseAnnotate) {
        event->ignore();
        return;
    }
    const auto* stroke = selectedInkStroke();
    const auto* shape = selectedShape();
    const QRectF bounds = shape ? shapeBounds(*shape) : stroke ? inkStrokeBounds(*stroke) : QRectF();
    if ((!stroke && !shape) || hasActiveGesture() || hasPointerContact() || m_externalPenContact
        || (!keyboard &&
            !pdfToWidget().mapRect(bounds).adjusted(-4, -4, 4, 4).contains(event->pos()))) {
        event->ignore();
        return;
    }
    event->accept();
    beginObjectStyleEdit();
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
            m_touchSelectionAction = nullptr;
            m_suppressedTouch = event->type() != QEvent::TouchEnd && event->type() != QEvent::TouchCancel;
            if (!m_suppressedTouch) {
                m_touchActive = false;
                m_previousTouchCount = 0;
                Q_EMIT operationFinished();
            }
            return true;
        }
        if (event->type() == QEvent::TouchCancel) {
            cancelTouchOperation();
            return true;
        }
        if (m_tabletActive || m_source == Source::Pen) return true;
        QVector<QPointF> globals;
        QPointF released;
        QPointF releasedGlobal;
        for (const auto& point : touch->points()) {
            released = point.position();
            releasedGlobal = point.globalPosition();
            if (point.state() != QEventPoint::State::Released) globals.append(point.globalPosition());
            if (globals.size() == 2) break;
        }
        if (event->type() == QEvent::TouchEnd || globals.isEmpty()) {
            auto* action = std::exchange(m_touchSelectionAction, nullptr);
            const bool tap = action && touch->points().size() == 1 && action == selectionActionAt(released)
                && QLineF(m_touchSelectionStart, releasedGlobal).length() <= QApplication::startDragDistance();
            if (m_source == Source::Touch) pointerRelease(released, 0.6, touch->modifiers());
            m_touchActive = false;
            m_previousTouchCount = 0;
            m_touchNavigationOnly = false;
            if (tap) action->click();
            Q_EMIT operationFinished();
            return true;
        }
        const QPointF center = globals.size() == 2 ? (globals[0] + globals[1]) / 2 : globals[0];
        const qreal distance = globals.size() == 2 ? QLineF(globals[0], globals[1]).length() : 0;
        if (globals.size() >= 2) {
            m_touchSelectionAction = nullptr;
            if (m_source == Source::Touch) cancelActiveOperation();
            m_touchNavigationOnly = true;
            if (m_touchActive && m_previousTouchCount == 2) {
                Q_EMIT panRequested(center - m_previousTouchGlobal);
                if (m_previousTouchDistance > 10 && distance > 10)
                    Q_EMIT zoomRequested(distance / m_previousTouchDistance, mapFromGlobal(center));
            }
        } else if (!m_touchActive || m_previousTouchCount != 1) {
            const QPointF local = mapFromGlobal(center);
            if (event->type() == QEvent::TouchBegin && !hasActiveGesture()) {
                // Let the canvas own the touch stream so adding a second finger still pinches.
                m_touchSelectionAction = selectionActionAt(local);
                m_touchSelectionStart = center;
            }
            if (!m_touchSelectionAction) pointerPress(local, center, 0.6, Source::Touch, false, m_touchNavigationOnly);
        } else if (m_touchSelectionAction) {
            if (QLineF(m_touchSelectionStart, center).length() > QApplication::startDragDistance()) {
                m_touchSelectionAction = nullptr;
                m_touchNavigationOnly = true;
                pointerPress(mapFromGlobal(center), center, 0.6, Source::Touch, false, true);
            }
        } else if (m_source == Source::Touch) {
            pointerMove(mapFromGlobal(center), center, 0.6, touch->modifiers());
        }
        m_touchActive = true;
        m_previousTouchGlobal = center;
        m_previousTouchCount = int(globals.size());
        m_previousTouchDistance = distance;
        return true;
    }
    if (event->type() == QEvent::WindowDeactivate) {
        m_touchSelectionAction = nullptr;
        if (!m_editor) finishActiveOperation();
        m_tabletActive = false;
        m_touchActive = false;
        Q_EMIT penContactChanged(false);
        Q_EMIT operationFinished();
    } else if (event->type() == QEvent::UngrabMouse) {
        m_touchSelectionAction = nullptr;
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

void InkCanvas::deleteSelection()
{
    if (m_tool != Tool::Select || !m_editingEnabled || m_selection.isEmpty() || hasActiveGesture()) return;
    const auto before = m_annotations;
    const auto selected = [this](const auto& annotation) { return m_selection.contains(annotation.id); };
    m_annotations.strokes.removeIf(selected);
    m_annotations.shapes.removeIf(selected);
    m_annotations.textBoxes.removeIf(selected);
    if (m_editTextMarkups) m_annotations.textMarkups.removeIf(selected);
    clearSelection();
    commit(before, QStringLiteral("delete"), {}, false, Tool::Select);
}

void InkCanvas::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Shift && m_gesture == Gesture::Shape) {
        updateShapeDraft(m_hoverPosition, event->modifiers() | Qt::ShiftModifier);
        update();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        if (hasActiveGesture()) cancelActiveOperation();
        else if (m_tool == Tool::TextSelect) Q_EMIT textSelectionStarted();
        else clearSelection();
        event->accept();
        return;
    }
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) &&
        m_tool == Tool::Select && m_editingEnabled && !m_selection.isEmpty() && !hasActiveGesture()) {
        deleteSelection();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void InkCanvas::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Shift && m_gesture == Gesture::Shape) {
        auto modifiers = event->modifiers();
        modifiers.setFlag(Qt::ShiftModifier, false);
        updateShapeDraft(m_hoverPosition, modifiers);
        update();
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

} // namespace propernotes
