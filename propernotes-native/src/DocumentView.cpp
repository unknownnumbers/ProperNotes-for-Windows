// SPDX-License-Identifier: GPL-3.0-only
#include "DocumentView.h"

#include <QApplication>
#include <QEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QVariant>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace propernotes {
namespace {
constexpr qreal Gap = 8;
constexpr qint64 CacheBytes = 96LL * 1024 * 1024;
constexpr qint64 TextCacheBytes = 24LL * 1024 * 1024;
constexpr int MaximumCanvases = 24;
constexpr int MaximumCachedTextPages = 8;
constexpr int MaximumPendingTextPages = 2;
constexpr qreal MaximumVisiblePagePixels = 16'000'000;
constexpr qreal MaximumNeighborPagePixels = 1'000'000;
constexpr qreal MaximumRasterEdge = 16'000;
constexpr int MaximumScroll = 1'000'000'000;
qreal boundedZoom(qreal value) { return std::clamp(value, qreal(0.25), qreal(4)); }
// Fitting a physically large page must not inherit the manual zoom minimum.
qreal boundedFitZoom(qreal value) { return std::clamp(value, qreal(0.000001), qreal(4)); }
bool rasterMeetsTarget(qreal available, qreal target) { return available >= target * 0.999; }

qreal rasterScale(const QSizeF& points, qreal target, qreal pixelBudget)
{
    return std::min({target, std::sqrt(pixelBudget / (points.width() * points.height())),
                     MaximumRasterEdge / points.width(), MaximumRasterEdge / points.height()});
}

qint64 rasterBytes(const QSizeF& points, qreal scale)
{
    return qint64(std::ceil(points.width() * scale)) * qint64(std::ceil(points.height() * scale)) * 4;
}
}

DocumentView::DocumentView(QWidget* parent) : QAbstractScrollArea(parent)
{
    setAccessibleName(QStringLiteral("PDF document pages"));
    setFocusPolicy(Qt::StrongFocus);
    setFrameShape(QFrame::NoFrame);
    horizontalScrollBar()->setSingleStep(40);
    verticalScrollBar()->setSingleStep(60);
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(30);
    connect(&m_refreshTimer, &QTimer::timeout, this, &DocumentView::refreshVisible);
    m_rasterRefreshTimer.setSingleShot(true);
    m_rasterRefreshTimer.setInterval(85);
    connect(&m_rasterRefreshTimer, &QTimer::timeout, this, &DocumentView::refreshVisible);
}

bool DocumentView::isFacing() const
{ return m_layout == LayoutMode::FacingPages || m_layout == LayoutMode::FacingContinuous; }
bool DocumentView::isContinuous() const
{ return m_layout == LayoutMode::SingleContinuous || m_layout == LayoutMode::FacingContinuous; }

void DocumentView::setDocument(const QVector<PageGeometry>& pages, const QMap<int, PageAnnotations>& annotations)
{
    QScopedValueRollback<bool> adjusting(m_adjusting, true);
    clearDocument();
    m_pages = pages;
    m_annotations = annotations;
    m_currentPage = pages.isEmpty() ? -1 : 0;
    if (m_zoomMode != ZoomMode::Free)
        m_zoom = fittedZoom();
    rebuildLayout();
    setOffset({0, 0});
    refreshVisible();
    Q_EMIT currentPageChanged(m_currentPage);
    publishState(QStringLiteral("document"));
}

void DocumentView::clearDocument()
{
    finishGesture();
    clearPendingObjectSelection();
    invalidateRequest();
    m_refreshTimer.stop();
    m_rasterRefreshTimer.stop();
    const auto canvases = m_canvases;
    m_canvases.clear();
    for (auto* canvas : canvases) {
        canvas->hide();
        canvas->deleteLater();
    }
    m_pages.clear();
    m_pagePositions.clear();
    m_rows.clear();
    m_visible.clear();
    m_annotations.clear();
    m_cache.clear();
    m_errors.clear();
    m_wanted.clear();
    m_rasterScales.clear();
    m_pendingTextPages.clear();
    m_textPages.clear();
    m_currentPage = -1;
    m_contentSize = {};
    m_shiftWheelRemainder = 0;
    clearTextSelection();
    clearSearchResult();
    horizontalScrollBar()->setRange(0, 0);
    verticalScrollBar()->setRange(0, 0);
    viewport()->update();
}

void DocumentView::updatePageAnnotations(int page, const PageAnnotations& annotations)
{
    if (page < 0 || page >= m_pages.size())
        return;
    m_annotations[page] = annotations;
    if (m_pendingObjectPage == page) {
        QSet<QUuid> surviving;
        for (const auto& stroke : annotations.strokes)
            if (m_pendingObjectIds.contains(stroke.id)) surviving.insert(stroke.id);
        for (const auto& box : annotations.textBoxes)
            if (m_pendingObjectIds.contains(box.id)) surviving.insert(box.id);
        for (const auto& shape : annotations.shapes)
            if (m_pendingObjectIds.contains(shape.id)) surviving.insert(shape.id);
        m_pendingObjectIds = std::move(surviving);
        if (m_pendingObjectIds.isEmpty()) clearPendingObjectSelection();
    }
    if (auto* canvas = m_canvases.value(page, nullptr)) {
        if (canvas->pageAnnotations() != annotations)
            canvas->setPageAnnotations(annotations);
    }
}

void DocumentView::requestTextPage(int page)
{
    if (m_suspended || m_tool != InkCanvas::Tool::TextSelect || page < 0 || page >= m_pages.size())
        return;
    auto cached = m_textPages.find(page);
    if (cached != m_textPages.end()) {
        cached->used = ++m_clock;
        return;
    }
    if (m_pendingTextPages.contains(page) || m_pendingTextPages.size() >= MaximumPendingTextPages)
        return;
    m_pendingTextPages.insert(page);
    Q_EMIT textPageRequested(page);
}

void DocumentView::deliverTextPage(int page, const std::shared_ptr<const PdfTextPage>& text)
{
    if (!m_pendingTextPages.remove(page) || page < 0 || page >= m_pages.size())
        return;
    if (text) {
        m_textPages.insert(page, {text, ++m_clock});
        if (auto* canvas = m_canvases.value(page, nullptr)) canvas->setTextPage(text);
        trimTextCache();
    }
    // If rapid page navigation filled the two slots, request the page that
    // ended up active when a slot frees. An absent result is retried on demand.
    if (page != m_currentPage)
        requestTextPage(m_currentPage);
}

void DocumentView::trimTextCache()
{
    const auto pageBytes = [](const PdfTextPage& text) -> qint64 {
        return text.text.size() * qint64(sizeof(QChar)) + text.glyphs.size() * qint64(sizeof(PdfTextGlyph));
    };
    qint64 bytes = 0;
    for (const auto& entry : std::as_const(m_textPages)) bytes += pageBytes(*entry.text);
    while (m_textPages.size() > MaximumCachedTextPages || bytes > TextCacheBytes) {
        int victim = -1;
        quint64 oldest = std::numeric_limits<quint64>::max();
        for (auto it = m_textPages.cbegin(); it != m_textPages.cend(); ++it) {
            const auto* canvas = m_canvases.value(it.key(), nullptr);
            if (it.key() == m_currentPage || (canvas && canvas->hasActiveGesture()))
                continue;
            if (it->used < oldest) {
                victim = it.key();
                oldest = it->used;
            }
        }
        if (victim < 0)
            break;
        bytes -= pageBytes(*m_textPages.take(victim).text);
        if (auto* canvas = m_canvases.value(victim, nullptr)) canvas->setTextPage({});
    }
}

void DocumentView::clearTextSelection()
{
    if (m_textSelectionPage < 0)
        return;
    if (auto* canvas = m_canvases.value(m_textSelectionPage, nullptr)) canvas->setTextSelection({});
    m_textSelection = {};
    m_textSelectionPage = -1;
    Q_EMIT textSelectionChanged();
}

void DocumentView::clearSearchResult()
{
    if (auto* canvas = m_canvases.value(m_searchPage, nullptr)) canvas->setSearchHighlight({});
    m_searchPage = -1;
    m_searchQuads.clear();
}

void DocumentView::showSearchResult(int page, const QVector<TextMarkupQuad>& quads)
{
    if (page < 0 || page >= m_pages.size())
        return;
    clearSearchResult();
    if (quads.isEmpty()) {
        goToPage(page);
        return;
    }

    QRectF bounds;
    for (const auto& quad : quads) {
        QPolygonF polygon;
        polygon << quad.topLeft << quad.topRight << quad.bottomRight << quad.bottomLeft;
        bounds = bounds.united(polygon.boundingRect());
    }
    m_searchPage = page;
    m_searchQuads = quads;
    if (auto* canvas = m_canvases.value(page, nullptr)) canvas->setSearchHighlight(quads);

    QScopedValueRollback<bool> adjusting(m_adjusting, true);
    finishGesture();
    m_currentPage = page;
    if (m_zoomMode != ZoomMode::Free)
        m_zoom = fittedZoom();
    rebuildLayout();
    const QRectF pageRect = m_pages[page].pdfRect.normalized();
    const auto center = bounds.center();
    const QPointF anchor(std::clamp(center.x(), pageRect.left(), pageRect.right()),
                         std::clamp(center.y(), pageRect.top(), pageRect.bottom()));
    const ViewState state{page, anchor, {0.5, 0.5}, m_zoom, m_layout, m_zoomMode, true};
    restoreAnchor(state);
    refreshVisible();
    Q_EMIT currentPageChanged(m_currentPage);
    Q_EMIT viewStateChanged(state, QStringLiteral("search"));
}

QPointF DocumentView::offset() const
{
    return {horizontalScrollBar()->value() * m_horizontalUnit,
            verticalScrollBar()->value() * m_verticalUnit};
}

void DocumentView::setOffset(const QPointF& value)
{
    horizontalScrollBar()->setValue(int(std::clamp(value.x() / m_horizontalUnit, qreal(0), qreal(horizontalScrollBar()->maximum()))));
    verticalScrollBar()->setValue(int(std::clamp(value.y() / m_verticalUnit, qreal(0), qreal(verticalScrollBar()->maximum()))));
}

qreal DocumentView::fittedZoom() const
{
    if (m_currentPage < 0 || m_currentPage >= m_pages.size())
        return 1;
    const int first = isFacing() ? (m_currentPage / 2) * 2 : m_currentPage;
    const int last = isFacing() ? std::min(first + 1, int(m_pages.size()) - 1) : first;
    qreal width = 0;
    qreal height = 0;
    for (int page = first; page <= last; ++page) {
        width += m_pages[page].displaySize.width();
        height = std::max(height, m_pages[page].displaySize.height());
    }
    if (width <= 0 || height <= 0)
        return 1;
    const qreal overhead = (last - first + 1) * InkCanvas::PageMargin * 2 + (last - first) * Gap + Gap * 2;
    const qreal fitWidth = std::max(qreal(1), viewport()->width() - overhead) / width;
    const qreal fitHeight = std::max(qreal(1), viewport()->height() - InkCanvas::PageMargin * 2 - Gap * 2) / height;
    return boundedFitZoom(m_zoomMode == ZoomMode::FitHeight ? fitHeight
        : m_zoomMode == ZoomMode::FitPage ? std::min(fitWidth, fitHeight) : fitWidth);
}

void DocumentView::rebuildLayout()
{
    QScopedValueRollback<bool> rebuilding(m_rebuilding, true);
    m_pagePositions = QVector<QRectF>(m_pages.size());
    m_rows.clear();
    const int columns = isFacing() ? 2 : 1;
    const int firstPage = isContinuous() ? 0 : std::max(0, (m_currentPage / columns) * columns);
    const int endPage = isContinuous() ? int(m_pages.size()) : std::min(firstPage + columns, int(m_pages.size()));
    qreal widest = 0;
    for (int first = firstPage; first < endPage; first += columns) {
        qreal width = 0;
        for (int page = first; page < std::min(first + columns, endPage); ++page)
            width += std::max(qreal(1), m_pages[page].displaySize.width() * m_zoom) + InkCanvas::PageMargin * 2;
        if (first + 1 < endPage && columns == 2)
            width += Gap;
        widest = std::max(widest, width);
    }
    const qreal contentWidth = std::max(qreal(viewport()->width()), widest + Gap * 2);
    qreal y = Gap;
    for (int first = firstPage; first < endPage; first += columns) {
        const int last = std::min(first + columns, endPage) - 1;
        qreal rowWidth = 0;
        qreal rowHeight = 0;
        for (int page = first; page <= last; ++page) {
            const QSizeF size(std::max(qreal(1), m_pages[page].displaySize.width() * m_zoom) + InkCanvas::PageMargin * 2,
                              std::max(qreal(1), m_pages[page].displaySize.height() * m_zoom) + InkCanvas::PageMargin * 2);
            rowWidth += size.width() + (page == first ? 0 : Gap);
            rowHeight = std::max(rowHeight, size.height());
        }
        qreal x = (contentWidth - rowWidth) / 2;
        for (int page = first; page <= last; ++page) {
            const QSizeF size(std::max(qreal(1), m_pages[page].displaySize.width() * m_zoom) + InkCanvas::PageMargin * 2,
                              std::max(qreal(1), m_pages[page].displaySize.height() * m_zoom) + InkCanvas::PageMargin * 2);
            m_pagePositions[page] = QRectF(QPointF(x, y), size);
            x += size.width() + Gap;
        }
        m_rows.append({first, last, y, y + rowHeight});
        y += rowHeight + Gap;
    }
    m_contentSize = {contentWidth, std::max(qreal(viewport()->height()), y)};
    const qreal horizontalRange = std::max(qreal(0), m_contentSize.width() - viewport()->width());
    const qreal verticalRange = std::max(qreal(0), m_contentSize.height() - viewport()->height());
    m_horizontalUnit = std::max(qreal(1), horizontalRange / MaximumScroll);
    m_verticalUnit = std::max(qreal(1), verticalRange / MaximumScroll);
    horizontalScrollBar()->setRange(0, int(std::ceil(horizontalRange / m_horizontalUnit)));
    verticalScrollBar()->setRange(0, int(std::ceil(verticalRange / m_verticalUnit)));
    horizontalScrollBar()->setPageStep(std::max(1, int(viewport()->width() / m_horizontalUnit)));
    verticalScrollBar()->setPageStep(std::max(1, int(viewport()->height() / m_verticalUnit)));
}

int DocumentView::pageAt(const QPointF& point) const
{
    if (m_rows.isEmpty())
        return -1;
    auto row = std::lower_bound(m_rows.cbegin(), m_rows.cend(), point.y(),
        [](const Row& value, qreal y) { return value.bottom < y; });
    if (row == m_rows.cend())
        row = m_rows.cend() - 1;
    int best = row->first;
    qreal distance = std::numeric_limits<qreal>::max();
    for (int page = row->first; page <= row->last; ++page) {
        const QRectF rect = m_pagePositions[page];
        const qreal dx = std::max({rect.left() - point.x(), qreal(0), point.x() - rect.right()});
        const qreal dy = std::max({rect.top() - point.y(), qreal(0), point.y() - rect.bottom()});
        const qreal candidate = dx * dx + dy * dy;
        if (candidate < distance || (qFuzzyCompare(candidate + 1, distance + 1) && page == m_currentPage)) {
            distance = candidate;
            best = page;
        }
    }
    return best;
}

DocumentView::ViewState DocumentView::captureAt(const QPointF& viewportPoint) const
{
    ViewState state;
    const QPointF contentPoint = viewportPoint + offset();
    state.page = pageAt(contentPoint);
    if (state.page < 0)
        return state;
    bool invertible = false;
    const QTransform inverse = m_pages[state.page].pdfToDisplay.inverted(&invertible);
    if (!invertible)
        return state;
    const QPointF displayPoint = (contentPoint - m_pagePositions[state.page].topLeft()
        - QPointF(InkCanvas::PageMargin, InkCanvas::PageMargin)) / m_zoom;
    state.pdfAnchor = inverse.map(displayPoint);
    const QRectF rect = m_pages[state.page].pdfRect;
    state.pdfAnchor.setX(std::clamp(state.pdfAnchor.x(), rect.left(), rect.right()));
    state.pdfAnchor.setY(std::clamp(state.pdfAnchor.y(), rect.top(), rect.bottom()));
    state.viewportAnchor = {viewportPoint.x() / std::max(1, viewport()->width()),
                            viewportPoint.y() / std::max(1, viewport()->height())};
    state.zoom = m_zoom;
    state.layout = m_layout;
    state.zoomMode = m_zoomMode;
    state.valid = true;
    return state;
}

DocumentView::ViewState DocumentView::captureViewState() const
{ return captureAt(QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0)); }

QPointF DocumentView::visiblePageCenterPdf(int page) const
{
    if (page < 0 || page >= m_pages.size() || page >= m_pagePositions.size()) return {};
    const auto& geometry = m_pages[page];
    const QRectF crop = geometry.pdfRect.normalized();
    constexpr int margin = InkCanvas::PageMargin;
    const QRectF paper = m_pagePositions[page].adjusted(margin, margin, -margin, -margin);
    const QRectF visible = paper.intersected(QRectF(offset(), QSizeF(viewport()->size())));
    bool invertible = false;
    const QTransform inverse = geometry.pdfToDisplay.inverted(&invertible);
    if (visible.isEmpty() || !invertible) return crop.center();
    const QPointF center = inverse.map((visible.center() - paper.topLeft()) / m_zoom);
    return {std::clamp(center.x(), crop.left(), crop.right()),
            std::clamp(center.y(), crop.top(), crop.bottom())};
}

void DocumentView::restoreAnchor(const ViewState& state)
{
    if (!state.valid || state.page < 0 || state.page >= m_pagePositions.size())
        return;
    const QPointF point = m_pagePositions[state.page].topLeft() + QPointF(InkCanvas::PageMargin, InkCanvas::PageMargin)
        + m_pages[state.page].pdfToDisplay.map(state.pdfAnchor) * m_zoom;
    setOffset(point - QPointF(state.viewportAnchor.x() * viewport()->width(), state.viewportAnchor.y() * viewport()->height()));
}

void DocumentView::restoreViewState(const ViewState& state)
{
    if (!state.valid || state.page < 0 || state.page >= m_pages.size() || !std::isfinite(state.zoom)
        || !std::isfinite(state.pdfAnchor.x()) || !std::isfinite(state.pdfAnchor.y())
        || !std::isfinite(state.viewportAnchor.x()) || !std::isfinite(state.viewportAnchor.y()))
        return;
    QScopedValueRollback<bool> adjusting(m_adjusting, true);
    finishGesture();
    m_currentPage = state.page;
    m_layout = state.layout;
    m_zoomMode = state.zoomMode;
    m_zoom = state.zoomMode == ZoomMode::Free ? boundedZoom(state.zoom) : boundedFitZoom(state.zoom);
    rebuildLayout();
    restoreAnchor(state);
    refreshVisible();
    Q_EMIT currentPageChanged(m_currentPage);
    publishState(QStringLiteral("restore"));
}

void DocumentView::setLayoutMode(LayoutMode mode)
{
    if (m_layout == mode)
        return;
    QScopedValueRollback<bool> adjusting(m_adjusting, true);
    finishGesture();
    const ViewState anchor = captureViewState();
    m_layout = mode;
    if (m_zoomMode != ZoomMode::Free)
        m_zoom = fittedZoom();
    rebuildLayout();
    restoreAnchor(anchor);
    refreshVisible();
    publishState(QStringLiteral("layout"));
}

void DocumentView::setZoomMode(ZoomMode mode)
{
    QScopedValueRollback<bool> adjusting(m_adjusting, true);
    finishGesture();
    const ViewState anchor = captureViewState();
    m_zoomMode = mode;
    if (mode != ZoomMode::Free)
        m_zoom = fittedZoom();
    m_errors.clear();
    rebuildLayout();
    restoreAnchor(anchor);
    refreshVisible();
    publishState(QStringLiteral("zoom"));
}

void DocumentView::setZoom(qreal zoom, const QPointF& viewportAnchor)
{
    if (!std::isfinite(zoom))
        return;
    // A fit below the manual minimum must not turn a zoom-out wheel gesture
    // into a jump in the opposite direction to 25 percent.
    if (m_zoom < 0.25 && zoom < m_zoom)
        return;
    QScopedValueRollback<bool> adjusting(m_adjusting, true);
    finishGesture();
    const QPointF point = viewportAnchor.x() < 0 || viewportAnchor.y() < 0
        ? QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0) : viewportAnchor;
    const ViewState anchor = captureAt(point);
    m_zoom = boundedZoom(zoom);
    m_zoomMode = ZoomMode::Free;
    m_errors.clear();
    // Continue displaying the last raster during a run of wheel/pinch events.
    // Once the scale settles, refine it to the window's physical pixel density.
    m_rasterRefreshTimer.start();
    rebuildLayout();
    restoreAnchor(anchor);
    refreshVisible();
    publishState(QStringLiteral("zoom"));
}

void DocumentView::goToPage(int page)
{
    if (page < 0 || page >= m_pages.size())
        return;
    QScopedValueRollback<bool> adjusting(m_adjusting, true);
    finishGesture();
    m_currentPage = page;
    if (m_zoomMode != ZoomMode::Free)
        m_zoom = fittedZoom();
    rebuildLayout();
    ViewState state;
    state.valid = true;
    state.page = page;
    state.pdfAnchor = m_pages[page].pdfToDisplay.inverted().map(QPointF(m_pages[page].displaySize.width() / 2, 0));
    state.viewportAnchor = QPointF(0.5, 0.03);
    restoreAnchor(state);
    refreshVisible();
    Q_EMIT currentPageChanged(m_currentPage);
    publishState(QStringLiteral("navigate"));
}

void DocumentView::configureCanvas(InkCanvas* canvas)
{
    canvas->setTool(m_tool);
    canvas->setPenColor(m_penColor);
    canvas->setPenWidth(m_penWidth);
    canvas->setPenOpacity(m_penOpacity);
    canvas->setHighlighterStyle(m_highlighterColor, m_highlighterWidthPt);
    canvas->setShapeStyle(m_shapeStyle);
    canvas->setPressureEnabled(m_pressure);
    canvas->setPressureSensitivity(m_sensitivity);
    canvas->setEraserMode(m_eraserMode);
    canvas->setEraserRadius(m_eraserRadius);
    canvas->setEditTextMarkups(m_editTextMarkups);
    canvas->setTouchAndMouseAnnotate(m_inputFollowsTool);
    canvas->setTextReviewActive(m_textReviewActive);
    canvas->setTextStyle(m_textFamily, m_textSize, m_textColor);
    canvas->setDarkTheme(m_dark);
    canvas->setZoom(m_zoom);
    bool penContact = false;
    for (const auto* other : std::as_const(m_canvases))
        penContact = penContact || other->hasPenContact();
    canvas->setExternalPenContact(penContact);
}

void DocumentView::updatePenContactGate()
{
    bool penContact = false;
    for (const auto* canvas : std::as_const(m_canvases))
        penContact = penContact || canvas->hasPenContact();
    // Include the pen's own page: an older palm-contact stream must stay
    // suppressed there as well after the pen is lifted.
    for (auto* canvas : std::as_const(m_canvases))
        canvas->setExternalPenContact(penContact);
}

void DocumentView::refreshVisible()
{
    if (m_rebuilding || m_refreshing)
        return;
    QScopedValueRollback<bool> refreshing(m_refreshing, true);
    m_visible.clear();
    const QRectF viewportRect(offset(), QSizeF(viewport()->size()));
    auto row = std::lower_bound(m_rows.cbegin(), m_rows.cend(), viewportRect.top(),
        [](const Row& value, qreal top) { return value.bottom < top; });
    QSet<int> desired;
    if (row != m_rows.cbegin() && row != m_rows.cend()) {
        const auto previous = row - 1;
        for (int page = previous->first; page <= previous->last; ++page)
            desired.insert(page);
    }
    for (; row != m_rows.cend() && row->top <= viewportRect.bottom(); ++row) {
        for (int page = row->first; page <= row->last; ++page) {
            constexpr int margin = InkCanvas::PageMargin;
            if (m_pagePositions[page].adjusted(margin, margin, -margin, -margin).intersects(viewportRect))
                m_visible.append(page);
            desired.insert(page);
        }
    }
    if (row != m_rows.cend())
        for (int page = row->first; page <= row->last; ++page)
            desired.insert(page);
    if (m_pendingObjectPage >= 0 && !m_visible.contains(m_pendingObjectPage)) clearPendingObjectSelection();
    QVector<int> ordered(desired.cbegin(), desired.cend());
    const QPointF center = viewportRect.center();
    std::sort(ordered.begin(), ordered.end(), [this, center](int a, int b) {
        const QPointF da = m_pagePositions[a].center() - center;
        const QPointF db = m_pagePositions[b].center() - center;
        return QPointF::dotProduct(da, da) < QPointF::dotProduct(db, db);
    });
    m_wanted.clear();
    for (int page : ordered) {
        if (m_wanted.size() >= MaximumCanvases)
            break;
        m_wanted.insert(page);
    }
    // Never destroy a page while it owns a pen contact or a text draft.
    for (auto it = m_canvases.cbegin(); it != m_canvases.cend(); ++it)
        if (it.value()->hasPointerContact() || it.value()->hasActiveGesture() || it.value()->isTextEditing())
            m_wanted.insert(it.key());
    const auto oldCanvases = m_canvases;
    for (auto it = oldCanvases.cbegin(); it != oldCanvases.cend(); ++it) {
        if (m_wanted.contains(it.key()))
            continue;
        m_canvases.remove(it.key());
        it.value()->finishActiveOperation();
        it.value()->hide();
        it.value()->deleteLater();
    }
    for (int page : m_wanted) {
        InkCanvas* canvas = m_canvases.value(page, nullptr);
        if (!canvas) {
            canvas = new InkCanvas(viewport());
            m_canvases.insert(page, canvas);
            configureCanvas(canvas);
            const auto& geometry = m_pages[page];
            canvas->setPage(page, {}, geometry.pdfToDisplay, geometry.pdfRect, 1, m_annotations.value(page));
            const auto cachedText = m_textPages.constFind(page);
            if (cachedText != m_textPages.cend()) canvas->setTextPage(cachedText->text);
            if (m_textSelectionPage == page) canvas->setTextSelection(m_textSelection.quads);
            if (m_searchPage == page) canvas->setSearchHighlight(m_searchQuads);
            connect(canvas, &InkCanvas::annotationsCommitted, this,
                [this](int index, const PageAnnotations& before, const PageAnnotations& after, const QString& reason, const QUuid& focus) {
                    m_annotations[index] = after;
                    Q_EMIT annotationsCommitted(index, before, after, reason, focus);
                });
            connect(canvas, &InkCanvas::normalToolCompleted, this, &DocumentView::normalToolCompleted);
            connect(canvas, &InkCanvas::selectionChanged, this, [this, canvas, page] {
                if (m_canvases.value(page) != canvas) return;
                if (m_pendingObjectPage >= 0 && canvas->hasFocus() && !canvas->selectionIds().isEmpty())
                    clearPendingObjectSelection();
                Q_EMIT selectionChanged();
            });
            connect(canvas, &InkCanvas::objectCutRequested, this, [this, canvas, page] {
                if (m_canvases.value(page) != canvas) return;
                const QScopedValueRollback<int> sourcePage(m_objectActionPage, page);
                Q_EMIT objectCutRequested();
            });
            connect(canvas, &InkCanvas::objectCopyRequested, this, [this, canvas, page] {
                if (m_canvases.value(page) != canvas) return;
                const QScopedValueRollback<int> sourcePage(m_objectActionPage, page);
                Q_EMIT objectCopyRequested();
            });
            connect(canvas, &InkCanvas::textEditingChanged, this, &DocumentView::textEditingChanged);
            connect(canvas, &InkCanvas::textPageRequested, this, &DocumentView::requestTextPage);
            connect(canvas, &InkCanvas::textSelectionStarted, this, &DocumentView::clearTextSelection);
            connect(canvas, &InkCanvas::textSelectionFinished, this,
                [this, canvas](int index, const PdfTextSelection& selection, const QPointF& releasedAt) {
                if (m_canvases.value(index, nullptr) != canvas || m_tool != InkCanvas::Tool::TextSelect)
                    return;
                const QPointF released = QPointF(canvas->mapTo(viewport(), releasedAt.toPoint())) + offset();
                const int releasePage = pageAt(released);
                if (releasePage >= 0 && releasePage != index) {
                    constexpr int margin = InkCanvas::PageMargin;
                    const QRectF paper = m_pagePositions[releasePage].adjusted(margin, margin, -margin, -margin);
                    if (paper.contains(released)) {
                        Q_EMIT activity(QStringLiteral("请在同一页内选择正文。"));
                        return;
                    }
                }
                clearTextSelection();
                m_textSelection = selection;
                m_textSelectionPage = index;
                canvas->setTextSelection(selection.quads);
                Q_EMIT textSelectionChanged();
                Q_EMIT textSelectionCompleted();
            });
            connect(canvas, &InkCanvas::activity, this, &DocumentView::activity);
            connect(canvas, &InkCanvas::operationFinished, this, [this] { m_refreshTimer.start(0); });
            connect(canvas, &InkCanvas::penContactChanged, this, [this](bool) { updatePenContactGate(); });
            connect(canvas, &InkCanvas::panRequested, this, [this](const QPointF& delta) { setOffset(offset() - delta); });
            connect(canvas, &InkCanvas::zoomRequested, this, [this, canvas](qreal factor, const QPointF& anchor) {
                setZoom(m_zoom * factor, QPointF(canvas->mapTo(viewport(), anchor.toPoint())));
            });
        }
        canvas->setZoom(m_zoom);
        const QRectF rect = m_pagePositions[page].translated(-offset());
        canvas->move(qRound(rect.left()), qRound(rect.top()));
        applyImage(page);
        canvas->setVisibleCanvasRect(viewport()->rect().translated(-canvas->pos()));
        canvas->setEditingEnabled(m_editing && m_cache.contains(page));
        canvas->show();
    }
    const int page = pageAt(viewportRect.center());
    if (page >= 0 && page != m_currentPage) {
        m_currentPage = page;
        Q_EMIT currentPageChanged(page);
    }
    planRasterScales();
    if (m_request.id && (!m_wanted.contains(m_request.page)
        || !rasterMeetsTarget(m_request.scale, rasterScaleFor(m_request.page))))
        invalidateRequest();
    trimCache();
    requestNext();
    requestTextPage(m_currentPage);
    viewport()->update();
}

void DocumentView::planRasterScales()
{
    m_rasterScales.clear();
    const qreal target = m_zoom * viewport()->devicePixelRatioF();
    // Reserve space for pixel rounding. A few nearby preview images may use a
    // small share, while pages with paper in view get the remaining capacity.
    const qreal plannedPixels = qreal(CacheBytes) * 0.90 / 4;
    qsizetype neighborCount = 0;
    qreal visibleBytes = 0;
    for (int page : std::as_const(m_wanted)) {
        if (!m_visible.contains(page)) {
            ++neighborCount;
            continue;
        }
        const auto points = m_pages[page].displaySize;
        const qreal scale = rasterScale(points, target, MaximumVisiblePagePixels);
        m_rasterScales.insert(page, scale);
        visibleBytes += qreal(rasterBytes(points, scale));
    }
    const qreal neighborPixels = std::min(plannedPixels * 0.12, MaximumNeighborPagePixels * qreal(neighborCount));
    const qreal visibleBudget = (plannedPixels - neighborPixels) * 4;
    if (visibleBytes > visibleBudget) {
        const qreal reduction = std::sqrt(visibleBudget / visibleBytes);
        for (auto it = m_rasterScales.begin(); it != m_rasterScales.end(); ++it) it.value() *= reduction;
    }
    if (neighborCount > 0) {
        const qreal pixelsPerNeighbor = neighborPixels / qreal(neighborCount);
        for (int page : std::as_const(m_wanted)) {
            if (!m_rasterScales.contains(page))
                m_rasterScales.insert(page, rasterScale(m_pages[page].displaySize, target * 0.65, pixelsPerNeighbor));
        }
    }
}

qreal DocumentView::rasterScaleFor(int page) const
{
    return m_rasterScales.value(page);
}

void DocumentView::requestNext()
{
    if (m_suspended || m_request.id || m_wanted.isEmpty() || m_rasterRefreshTimer.isActive())
        return;
    int next = -1;
    bool nextVisible = false;
    qreal nearest = std::numeric_limits<qreal>::max();
    const QPointF center = offset() + QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0);
    qint64 wantedBytes = 0;
    for (int page : std::as_const(m_wanted)) {
        const auto existing = m_cache.constFind(page);
        if (existing != m_cache.cend()) wantedBytes += existing->page.image.sizeInBytes();
    }
    for (int page : m_wanted) {
        const qreal scale = rasterScaleFor(page);
        const auto cached = m_cache.constFind(page);
        if (scale <= 0 || m_errors.contains(page) || (cached != m_cache.cend() && rasterMeetsTarget(cached->scale, scale)))
            continue;
        const bool visible = m_visible.contains(page);
        const qint64 replacement = cached == m_cache.cend() ? 0 : cached->page.image.sizeInBytes();
        // Skip prefetch when keeping sharper live images takes its free space.
        // This avoids repeatedly building and immediately evicting a neighbor.
        if (!visible && wantedBytes - replacement + rasterBytes(m_pages[page].displaySize, scale) > CacheBytes)
            continue;
        const QPointF delta = m_pagePositions[page].center() - center;
        const qreal distance = QPointF::dotProduct(delta, delta);
        if (next < 0 || (visible && !nextVisible) || (visible == nextVisible && distance < nearest)) {
            nearest = distance;
            next = page;
            nextVisible = visible;
        }
    }
    if (next >= 0) {
        m_request = {next, rasterScaleFor(next), ++m_nextRequest};
        Q_EMIT renderRequested(next, m_request.scale, m_request.id);
    }
}

void DocumentView::invalidateRequest()
{
    if (m_request.id) {
        const quint64 id = m_request.id;
        m_request = {};
        Q_EMIT renderCancelled(id);
    }
}

void DocumentView::deliverRenderedPage(int page, qreal scale, quint64 requestId, const RenderedPage& result)
{
    if (!requestId || m_request.id != requestId || m_request.page != page || page < 0 || page >= m_pages.size())
        return;
    if (!result || result.image.sizeInBytes() > CacheBytes || !std::isfinite(scale) || scale <= 0
        || !qFuzzyCompare(scale, m_request.scale)) {
        deliverRenderFailure(page, requestId, result.error.isEmpty() ? QStringLiteral("Page image exceeds the view cache budget.") : result.error);
        return;
    }
    m_request = {};
    m_cache[page] = {result, scale, ++m_clock, requestId};
    m_errors.remove(page);
    applyImage(page);
    trimCache();
    // An in-use older page can temporarily pin the available cache. Its
    // operation-finished signal will reschedule a raster that did not fit.
    if (m_cache.contains(page) || !hasActiveGesture()) requestNext();
    viewport()->update();
}

void DocumentView::deliverRenderFailure(int page, quint64 requestId, const QString& error)
{
    if (!requestId || m_request.id != requestId || m_request.page != page)
        return;
    m_request = {};
    m_errors[page] = error;
    Q_EMIT activity(QStringLiteral("Page %1: %2").arg(page + 1).arg(error));
    requestNext();
    viewport()->update();
}

void DocumentView::applyImage(int page)
{
    auto* canvas = m_canvases.value(page, nullptr);
    auto cached = m_cache.find(page);
    if (!canvas || cached == m_cache.end() || canvas->hasPointerContact() || canvas->hasActiveGesture() || canvas->isTextEditing())
        return;
    cached->used = ++m_clock;
    if (canvas->property("propernotesRenderId").toULongLong() != cached->revision) {
        canvas->setPage(page, cached->page.image, cached->page.pdfToImage, m_pages[page].pdfRect,
                        cached->scale, m_annotations.value(page));
        canvas->setProperty("propernotesRenderId", QVariant::fromValue(cached->revision));
        canvas->setZoom(m_zoom);
    }
    canvas->setEditingEnabled(m_editing);
    applyPendingObjectSelection(page);
}

void DocumentView::trimCache()
{
    qint64 bytes = 0;
    for (const auto& entry : std::as_const(m_cache))
        bytes += entry.page.image.sizeInBytes();
    while (bytes > CacheBytes) {
        int victim = -1;
        int victimPriority = std::numeric_limits<int>::max();
        quint64 oldest = std::numeric_limits<quint64>::max();
        for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it) {
            const auto* canvas = m_canvases.value(it.key(), nullptr);
            if (canvas && (canvas->hasPointerContact() || canvas->hasActiveGesture() || canvas->isTextEditing()))
                continue;
            int priority = 0;
            if (m_wanted.contains(it.key())) {
                priority = 1;
                if (m_visible.contains(it.key())) {
                    const qreal target = rasterScaleFor(it.key());
                    // When sharper cached images would crowd out the current
                    // plan, downsize those before evicting other visible images.
                    priority = target > 0 && it->scale > target * 1.01 ? 2 : 3;
                }
            }
            if (priority < victimPriority || (priority == victimPriority && it->used < oldest)) {
                victim = it.key();
                victimPriority = priority;
                oldest = it->used;
            }
        }
        if (victim < 0)
            break;
        bytes -= m_cache[victim].page.image.sizeInBytes();
        m_cache.remove(victim);
        if (auto* canvas = m_canvases.value(victim, nullptr)) {
            canvas->setPage(victim, {}, m_pages[victim].pdfToDisplay, m_pages[victim].pdfRect, 1, m_annotations.value(victim));
            canvas->setProperty("propernotesRenderId", QVariant());
            canvas->setZoom(m_zoom);
            canvas->setEditingEnabled(false);
        }
    }
}

void DocumentView::setRenderingSuspended(bool suspended)
{
    m_suspended = suspended;
    if (suspended)
        invalidateRequest();
    else
        refreshVisible();
}

void DocumentView::publishState(const QString& reason)
{
    if (!m_rebuilding && !m_pages.isEmpty() && !(m_adjusting && reason == QStringLiteral("scroll")))
        Q_EMIT viewStateChanged(captureViewState(), reason);
}

bool DocumentView::event(QEvent* event)
{
    const auto type = event->type();
    const bool handled = QAbstractScrollArea::event(event);
    if (type == QEvent::DevicePixelRatioChange) {
        m_errors.clear();
        m_rasterRefreshTimer.start();
    }
    return handled;
}

void DocumentView::resizeEvent(QResizeEvent* event)
{
    QScopedValueRollback<bool> adjusting(m_adjusting, true);
    if (!m_rebuilding && hasActiveGesture())
        finishGesture();
    const ViewState anchor = captureViewState();
    QAbstractScrollArea::resizeEvent(event);
    if (m_rebuilding)
        return;
    if (m_zoomMode != ZoomMode::Free)
        m_zoom = fittedZoom();
    rebuildLayout();
    restoreAnchor(anchor);
    refreshVisible();
    publishState(QStringLiteral("resize"));
}

void DocumentView::scrollContentsBy(int, int)
{
    if (m_rebuilding)
        return;
    refreshVisible();
    publishState(QStringLiteral("scroll"));
}

void DocumentView::paintEvent(QPaintEvent*)
{
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), m_dark ? QColor("#20242b") : QColor("#e6e9ee"));
    for (int page : std::as_const(m_visible)) {
        if (m_canvases.contains(page))
            continue;
        const QRectF paper = m_pagePositions[page].translated(-offset()).adjusted(InkCanvas::PageMargin,
            InkCanvas::PageMargin, -InkCanvas::PageMargin, -InkCanvas::PageMargin);
        painter.fillRect(paper, Qt::white);
        painter.setPen(QColor("#606976"));
        painter.drawText(paper, Qt::AlignCenter, QStringLiteral("Page %1").arg(page + 1));
    }
}

void DocumentView::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        const qreal delta = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->pixelDelta().y();
        setZoom(m_zoom * std::pow(1.0015, delta), event->position());
        event->accept();
        return;
    }

    const auto modifiers = event->modifiers();
    const auto angle = event->angleDelta();
    const auto pixels = event->pixelDelta();
    const bool shiftOnly = modifiers == Qt::ShiftModifier;
    // Preserve genuine horizontal wheel or touchpad input. Only turn the
    // vertical component of a Shift + wheel gesture into horizontal motion.
    const bool vertical = (pixels.y() != 0 || angle.y() != 0) &&
        (pixels.isNull() ? std::abs(angle.y()) >= std::abs(angle.x()) : std::abs(pixels.y()) >= std::abs(pixels.x()));
    if (shiftOnly && vertical) {
        auto* bar = horizontalScrollBar();
        if (!hasActiveGesture() && bar->maximum() > bar->minimum()) {
            const qreal delta = pixels.y() != 0 ? pixels.y() / m_horizontalUnit :
                angle.y() / 120.0 * QApplication::wheelScrollLines() * bar->singleStep();
            const qreal step = std::trunc(m_shiftWheelRemainder + delta);
            m_shiftWheelRemainder += delta - step;
            const qreal target = bar->value() - step;
            const qreal clamped = std::clamp(target, qreal(bar->minimum()), qreal(bar->maximum()));
            if (target != clamped) m_shiftWheelRemainder = 0;
            bar->setValue(int(clamped));
        }
        event->accept();
        return;
    }
    m_shiftWheelRemainder = 0;
    QAbstractScrollArea::wheelEvent(event);
}

void DocumentView::finishGesture()
{
    const auto canvases = m_canvases;
    for (auto* canvas : canvases)
        canvas->finishGesture();
}
void DocumentView::cancelGesture()
{
    const auto canvases = m_canvases;
    for (auto* canvas : canvases)
        canvas->cancelActiveOperation();
}
bool DocumentView::hasActiveGesture() const
{
    for (const auto* canvas : m_canvases)
        if (canvas->hasActiveGesture())
            return true;
    return false;
}
bool DocumentView::isTextEditing() const
{
    for (const auto* canvas : m_canvases)
        if (canvas->isTextEditing())
            return true;
    return false;
}
void DocumentView::clearSelection()
{
    clearPendingObjectSelection();
    for (auto* canvas : std::as_const(m_canvases))
        canvas->clearSelection();
    clearTextSelection();
}

int DocumentView::objectSelectionPage() const
{
    if (!m_editing || m_tool != InkCanvas::Tool::Select) return -1;
    if (m_objectActionPage >= 0) {
        const auto* canvas = m_canvases.value(m_objectActionPage, nullptr);
        return canvas && m_visible.contains(m_objectActionPage) && canvas->hasObjectSelection() ? m_objectActionPage : -1;
    }

    const auto* focus = QApplication::focusWidget();
    int selectedPage = -1;
    bool ambiguous = false;
    for (int page : m_visible) {
        const auto* canvas = m_canvases.value(page, nullptr);
        if (!canvas) continue;
        if (canvas == focus || (focus && canvas->isAncestorOf(focus)))
            return canvas->hasObjectSelection() ? page : -1;
        if (!canvas->selectionIds().isEmpty()) {
            ambiguous = ambiguous || selectedPage >= 0;
            selectedPage = page;
        }
    }
    return !ambiguous && selectedPage >= 0 && m_canvases.value(selectedPage)->hasObjectSelection() ? selectedPage : -1;
}

bool DocumentView::hasSelectedObjects() const { return objectSelectionPage() >= 0; }

DocumentView::ObjectSelection DocumentView::selectedObjects() const
{
    const int page = objectSelectionPage();
    if (page < 0) return {};
    auto selected = m_canvases.value(page)->selectedObjects();
    return {page, std::move(selected.strokes), std::move(selected.textBoxes), std::move(selected.shapes)};
}

void DocumentView::clearPendingObjectSelection()
{
    m_pendingObjectPage = -1;
    m_pendingObjectIds.clear();
}

void DocumentView::selectPageObjects(int page, const QSet<QUuid>& ids)
{
    if (!m_editing || m_tool != InkCanvas::Tool::Select || page < 0 || page >= m_pages.size()) return;
    clearSelection();
    if (ids.isEmpty() || !m_visible.contains(page)) return;
    m_pendingObjectPage = page;
    m_pendingObjectIds = ids;
    applyPendingObjectSelection(page);
}

void DocumentView::applyPendingObjectSelection(int page)
{
    if (page != m_pendingObjectPage || !m_editing || m_tool != InkCanvas::Tool::Select
        || !m_cache.contains(page) || !m_visible.contains(page) || hasActiveGesture()) return;
    auto* canvas = m_canvases.value(page, nullptr);
    if (!canvas || canvas->pageIndex() != page || !canvas->property("propernotesRenderId").toULongLong()
        || canvas->hasPointerContact()) return;
    const auto ids = std::exchange(m_pendingObjectIds, QSet<QUuid>{});
    m_pendingObjectPage = -1;
    const QWidget* focus = QApplication::focusWidget();
    if (focus && (focus == this || isAncestorOf(focus))) canvas->setFocus(Qt::OtherFocusReason);
    canvas->selectObjects(ids);
}

void DocumentView::setTool(InkCanvas::Tool tool)
{
    if (m_tool == tool)
        return;
    clearPendingObjectSelection();
    m_tool = tool;
    for (auto* canvas : std::as_const(m_canvases))
        canvas->setTool(tool);
    if (tool == InkCanvas::Tool::TextSelect)
        requestTextPage(m_currentPage);
    else
        clearTextSelection();
}

#define PROPERNOTES_CANVAS_OPTION(method, member, type, setter) \
void DocumentView::method(type value) { member = value; for (auto* canvas : std::as_const(m_canvases)) canvas->setter(value); }
PROPERNOTES_CANVAS_OPTION(setPenColor, m_penColor, const QColor&, setPenColor)
PROPERNOTES_CANVAS_OPTION(setPenWidth, m_penWidth, qreal, setPenWidth)
PROPERNOTES_CANVAS_OPTION(setPenOpacity, m_penOpacity, qreal, setPenOpacity)
PROPERNOTES_CANVAS_OPTION(setPressureEnabled, m_pressure, bool, setPressureEnabled)
PROPERNOTES_CANVAS_OPTION(setPressureSensitivity, m_sensitivity, qreal, setPressureSensitivity)
PROPERNOTES_CANVAS_OPTION(setEraserMode, m_eraserMode, InkCanvas::EraseMode, setEraserMode)
PROPERNOTES_CANVAS_OPTION(setEraserRadius, m_eraserRadius, qreal, setEraserRadius)
PROPERNOTES_CANVAS_OPTION(setEditTextMarkups, m_editTextMarkups, bool, setEditTextMarkups)
PROPERNOTES_CANVAS_OPTION(setShapeStyle, m_shapeStyle, const ShapeStyle&, setShapeStyle)
PROPERNOTES_CANVAS_OPTION(setInputFollowsTool, m_inputFollowsTool, bool, setTouchAndMouseAnnotate)
PROPERNOTES_CANVAS_OPTION(setTextReviewActive, m_textReviewActive, bool, setTextReviewActive)
#undef PROPERNOTES_CANVAS_OPTION

void DocumentView::setHighlighterStyle(const QColor& color, qreal widthPt)
{
    if (!color.isValid() || color.alpha() == 0 || !std::isfinite(widthPt))
        return;
    m_highlighterColor = color;
    m_highlighterWidthPt = widthPt;
    for (auto* canvas : std::as_const(m_canvases))
        canvas->setHighlighterStyle(color, widthPt);
}

void DocumentView::setDarkTheme(bool dark)
{
    m_dark = dark;
    for (auto* canvas : std::as_const(m_canvases))
        canvas->setDarkTheme(dark);
    viewport()->update();
}

void DocumentView::setTextStyle(const QString& family, qreal size, const QColor& color)
{
    m_textFamily = family; m_textSize = size; m_textColor = color;
    for (auto* canvas : std::as_const(m_canvases))
        canvas->setTextStyle(family, size, color);
}
void DocumentView::setEditingEnabled(bool enabled)
{
    m_editing = enabled;
    for (auto it = m_canvases.cbegin(); it != m_canvases.cend(); ++it)
        it.value()->setEditingEnabled(enabled && m_cache.contains(it.key()));
    if (enabled && m_pendingObjectPage >= 0) applyPendingObjectSelection(m_pendingObjectPage);
}

} // namespace propernotes
