// SPDX-License-Identifier: GPL-3.0-only
#include "DocumentView.h"

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
constexpr int MaximumCanvases = 24;
constexpr qreal MaximumPagePixels = 6'000'000;
constexpr int MaximumScroll = 1'000'000'000;
qreal boundedZoom(qreal value) { return std::clamp(value, qreal(0.25), qreal(4)); }
// Fitting a physically large page must not inherit the manual zoom minimum.
qreal boundedFitZoom(qreal value) { return std::clamp(value, qreal(0.000001), qreal(4)); }
bool closeScale(qreal a, qreal b) { return a >= b * 0.90 && a <= b * 1.6; }
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
    invalidateRequest();
    m_refreshTimer.stop();
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
    m_currentPage = -1;
    m_contentSize = {};
    horizontalScrollBar()->setRange(0, 0);
    verticalScrollBar()->setRange(0, 0);
    viewport()->update();
}

void DocumentView::updatePageAnnotations(int page, const PageAnnotations& annotations)
{
    if (page < 0 || page >= m_pages.size())
        return;
    m_annotations[page] = annotations;
    if (auto* canvas = m_canvases.value(page, nullptr)) {
        if (canvas->pageAnnotations() != annotations)
            canvas->setPageAnnotations(annotations);
    }
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
    canvas->setPressureEnabled(m_pressure);
    canvas->setPressureSensitivity(m_sensitivity);
    canvas->setEraserMode(m_eraserMode);
    canvas->setEraserRadius(m_eraserRadius);
    canvas->setTouchAndMouseAnnotate(m_inputFollowsTool);
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
            if (m_pagePositions[page].intersects(viewportRect))
                m_visible.append(page);
            desired.insert(page);
        }
    }
    if (row != m_rows.cend())
        for (int page = row->first; page <= row->last; ++page)
            desired.insert(page);
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
            connect(canvas, &InkCanvas::annotationsCommitted, this,
                [this](int index, const PageAnnotations& before, const PageAnnotations& after, const QString& reason, const QUuid& focus) {
                    m_annotations[index] = after;
                    Q_EMIT annotationsCommitted(index, before, after, reason, focus);
                });
            connect(canvas, &InkCanvas::normalToolCompleted, this, &DocumentView::normalToolCompleted);
            connect(canvas, &InkCanvas::selectionChanged, this, &DocumentView::selectionChanged);
            connect(canvas, &InkCanvas::textEditingChanged, this, &DocumentView::textEditingChanged);
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
        canvas->setEditingEnabled(m_editing && m_cache.contains(page));
        canvas->show();
    }
    const int page = pageAt(viewportRect.center());
    if (page >= 0 && page != m_currentPage) {
        m_currentPage = page;
        Q_EMIT currentPageChanged(page);
    }
    if (m_request.id && (!m_wanted.contains(m_request.page)
        || !closeScale(m_request.scale, rasterScaleFor(m_request.page))))
        invalidateRequest();
    trimCache();
    requestNext();
    viewport()->update();
}

qreal DocumentView::rasterScaleFor(int page) const
{
    const QSizeF points = m_pages[page].displaySize;
    if (points.width() <= 0 || points.height() <= 0)
        return 1;
    // Leave space for image stride and ceil-to-pixel rounding across live pages.
    const qreal pixels = std::min(MaximumPagePixels, qreal(CacheBytes) * 0.90 / (4 * std::max(1, int(m_wanted.size()))));
    const qreal scale = std::min({m_zoom * devicePixelRatioF(),
        std::sqrt(pixels / (points.width() * points.height())),
        16000.0 / points.width(), 16000.0 / points.height()});
    return std::max(qreal(0.00001), std::floor(scale * 65536) / 65536);
}

void DocumentView::requestNext()
{
    if (m_suspended || m_request.id || m_wanted.isEmpty())
        return;
    int next = -1;
    qreal nearest = std::numeric_limits<qreal>::max();
    const QPointF center = offset() + QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0);
    for (int page : m_wanted) {
        const qreal scale = rasterScaleFor(page);
        const auto cached = m_cache.constFind(page);
        if (m_errors.contains(page) || (cached != m_cache.cend() && closeScale(cached->scale, scale)))
            continue;
        const QPointF delta = m_pagePositions[page].center() - center;
        const qreal distance = QPointF::dotProduct(delta, delta);
        if (distance < nearest) {
            nearest = distance;
            next = page;
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
    requestNext();
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
}

void DocumentView::trimCache()
{
    qint64 bytes = 0;
    for (const auto& entry : std::as_const(m_cache))
        bytes += entry.page.image.sizeInBytes();
    while (bytes > CacheBytes) {
        int victim = -1;
        bool victimWanted = true;
        quint64 oldest = std::numeric_limits<quint64>::max();
        for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it) {
            const auto* canvas = m_canvases.value(it.key(), nullptr);
            if (canvas && (canvas->hasPointerContact() || canvas->hasActiveGesture() || canvas->isTextEditing()))
                continue;
            const bool wanted = m_wanted.contains(it.key());
            if (victim < 0 || (victimWanted && !wanted) || (victimWanted == wanted && it->used < oldest)) {
                victim = it.key();
                victimWanted = wanted;
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
    } else {
        QAbstractScrollArea::wheelEvent(event);
    }
}

void DocumentView::finishGesture()
{
    const auto canvases = m_canvases;
    for (auto* canvas : canvases)
        canvas->finishActiveOperation(InkCanvas::FinishReason::ForcedCommit);
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
    for (auto* canvas : std::as_const(m_canvases))
        canvas->clearSelection();
}

#define PROPERNOTES_CANVAS_OPTION(method, member, type, setter) \
void DocumentView::method(type value) { member = value; for (auto* canvas : std::as_const(m_canvases)) canvas->setter(value); }
PROPERNOTES_CANVAS_OPTION(setTool, m_tool, InkCanvas::Tool, setTool)
PROPERNOTES_CANVAS_OPTION(setPenColor, m_penColor, const QColor&, setPenColor)
PROPERNOTES_CANVAS_OPTION(setPenWidth, m_penWidth, qreal, setPenWidth)
PROPERNOTES_CANVAS_OPTION(setPenOpacity, m_penOpacity, qreal, setPenOpacity)
PROPERNOTES_CANVAS_OPTION(setPressureEnabled, m_pressure, bool, setPressureEnabled)
PROPERNOTES_CANVAS_OPTION(setPressureSensitivity, m_sensitivity, qreal, setPressureSensitivity)
PROPERNOTES_CANVAS_OPTION(setEraserMode, m_eraserMode, InkCanvas::EraseMode, setEraserMode)
PROPERNOTES_CANVAS_OPTION(setEraserRadius, m_eraserRadius, qreal, setEraserRadius)
PROPERNOTES_CANVAS_OPTION(setInputFollowsTool, m_inputFollowsTool, bool, setTouchAndMouseAnnotate)
#undef PROPERNOTES_CANVAS_OPTION

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
}

} // namespace propernotes
