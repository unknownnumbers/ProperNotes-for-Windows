// SPDX-License-Identifier: GPL-3.0-only
#include "ThumbnailBrowser.h"

#include <QAbstractListModel>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QScroller>
#include <QShowEvent>
#include <QSlider>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace propernotes {
namespace {
constexpr qint64 ThumbnailCacheBytes = 24LL * 1024 * 1024;
constexpr int MaximumThumbnails = 1024;
}

class ThumbnailPageModel final : public QAbstractListModel {
public:
    explicit ThumbnailPageModel(ThumbnailBrowser* browser) : QAbstractListModel(browser), m_browser(browser) {}
    int rowCount(const QModelIndex& parent = {}) const override
    { return parent.isValid() ? 0 : int(m_browser->m_pages.size()); }
    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() >= rowCount())
            return {};
        if (role == Qt::DisplayRole || role == Qt::AccessibleTextRole)
            return QStringLiteral("Page %1").arg(index.row() + 1);
        if (role == Qt::SizeHintRole)
            return m_browser->cellSize();
        return {};
    }
    void replacePages(const QVector<PageGeometry>& pages, const QMap<int, PageAnnotations>& annotations)
    {
        beginResetModel();
        m_browser->m_pages = pages;
        m_browser->m_annotations = annotations;
        endResetModel();
    }
    void imageChanged(int page)
    {
        if (page >= 0 && page < rowCount())
            Q_EMIT dataChanged(index(page, 0), index(page, 0));
    }
private:
    ThumbnailBrowser* m_browser;
};

class ThumbnailDelegate final : public QStyledItemDelegate {
public:
    explicit ThumbnailDelegate(ThumbnailBrowser* browser) : QStyledItemDelegate(browser), m_browser(browser) {}
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override
    { return m_browser->cellSize(); }
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const int page = index.row();
        if (page < 0 || page >= m_browser->m_pages.size())
            return;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRectF cell = QRectF(option.rect).adjusted(8, 8, -8, -8);
        if (option.state.testFlag(QStyle::State_Selected)) {
            painter->setPen(QPen(option.palette.highlight().color(), 2));
            painter->setBrush(option.palette.highlight().color().lighter(180));
            painter->drawRoundedRect(cell, 6, 6);
        }
        const QRectF available = cell.adjusted(8, 8, -8, -32);
        const QSizeF points = m_browser->m_pages[page].displaySize;
        const qreal ratio = std::min(available.width() / std::max(qreal(1), points.width()),
                                    available.height() / std::max(qreal(1), points.height()));
        const QSizeF paperSize = points * ratio;
        const QRectF paper(QPointF(available.center().x() - paperSize.width() / 2,
                                  available.center().y() - paperSize.height() / 2), paperSize);
        painter->setPen(QColor("#b6bdc7"));
        painter->setBrush(Qt::white);
        painter->drawRect(paper);
        const auto cached = m_browser->m_cache.constFind(page);
        if (cached != m_browser->m_cache.cend() && cached->page) {
            painter->setRenderHint(QPainter::SmoothPixmapTransform);
            painter->drawImage(paper, cached->page.image);
            painter->save();
            painter->setClipRect(paper);
            painter->translate(paper.topLeft());
            painter->scale(paper.width() / cached->page.image.width(), paper.height() / cached->page.image.height());
            painter->setWorldTransform(cached->page.pdfToImage, true);
            const auto annotations = m_browser->m_annotations.constFind(page);
            if (annotations != m_browser->m_annotations.cend()) {
                for (const auto& markup : annotations->textMarkups)
                    paintTextMarkup(*painter, markup);
                for (const auto& stroke : annotations->strokes)
                    paintInkStroke(*painter, stroke);
                for (const auto& shape : annotations->shapes)
                    paintShape(*painter, shape);
                for (const auto& text : annotations->textBoxes)
                    paintTextBox(*painter, text);
            }
            painter->restore();
        } else {
            painter->setPen(QColor("#737d8a"));
            painter->drawText(paper, Qt::AlignCenter,
                m_browser->m_failed.contains(page) ? QStringLiteral("Unavailable") : QStringLiteral("..."));
        }
        painter->setPen(option.palette.text().color());
        painter->drawText(QRectF(cell.left(), cell.bottom() - 28, cell.width(), 24), Qt::AlignCenter,
                          QStringLiteral("%1").arg(page + 1));
        painter->restore();
    }
private:
    ThumbnailBrowser* m_browser;
};

ThumbnailBrowser::ThumbnailBrowser(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("ProperNotes - All pages"));
    setModal(true);
    auto* layout = new QVBoxLayout(this);
    auto* controls = new QHBoxLayout;
    auto* title = new QLabel(QStringLiteral("All pages"), this);
    controls->addWidget(title);
    controls->addStretch();
    controls->addWidget(new QLabel(QStringLiteral("Thumbnail size"), this));
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(80, 280);
    m_slider->setValue(m_thumbnailWidth);
    m_slider->setFixedWidth(220);
    m_slider->setAccessibleName(QStringLiteral("Thumbnail size"));
    controls->addWidget(m_slider);
    m_sizeLabel = new QLabel(QStringLiteral("%1 px").arg(m_thumbnailWidth), this);
    m_sizeLabel->setMinimumWidth(55);
    controls->addWidget(m_sizeLabel);
    auto* close = new QPushButton(QStringLiteral("Return (Esc)"), this);
    controls->addWidget(close);
    layout->addLayout(controls);
    m_list = new QListView(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setMovement(QListView::Static);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setFlow(QListView::LeftToRight);
    m_list->setWrapping(true);
    m_list->setSpacing(0);
    m_list->setUniformItemSizes(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setGridSize(cellSize());
    m_model = new ThumbnailPageModel(this);
    m_list->setModel(m_model);
    m_list->setItemDelegate(new ThumbnailDelegate(this));
    m_list->viewport()->installEventFilter(this);
    QScroller::grabGesture(m_list->viewport(), QScroller::TouchGesture);
    layout->addWidget(m_list, 1);
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(25);
    connect(&m_refreshTimer, &QTimer::timeout, this, &ThumbnailBrowser::refreshVisible);
    connect(m_slider, &QSlider::valueChanged, this, &ThumbnailBrowser::setThumbnailWidth);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_list->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { m_refreshTimer.start(); });
    connect(m_list, &QListView::clicked, this, [this](const QModelIndex& index) {
        if (index.isValid()) {
            Q_EMIT pageActivated(index.row());
            accept();
        }
    });
    connect(m_list, &QListView::activated, this, [this](const QModelIndex& index) {
        if (isVisible() && index.isValid()) {
            Q_EMIT pageActivated(index.row());
            accept();
        }
    });
}

QSize ThumbnailBrowser::cellSize() const
{ return {m_thumbnailWidth + 32, int(std::ceil(m_thumbnailWidth * 1.42)) + 60}; }

void ThumbnailBrowser::setDocument(const QVector<PageGeometry>& pages, const QMap<int, PageAnnotations>& annotations, int currentPage)
{
    cancelRequest();
    m_cache.clear();
    m_failed.clear();
    m_wanted.clear();
    m_model->replacePages(pages, annotations);
    if (currentPage >= 0 && currentPage < pages.size())
        m_list->setCurrentIndex(m_model->index(currentPage, 0));
    m_refreshTimer.start(0);
}

void ThumbnailBrowser::openAtPage(int page)
{
    if (page >= 0 && page < m_pages.size())
        m_list->setCurrentIndex(m_model->index(page, 0));
    showFullScreen();
    QTimer::singleShot(0, this, [this] {
        m_list->scrollTo(m_list->currentIndex(), QAbstractItemView::PositionAtCenter);
        refreshVisible();
        m_list->setFocus();
    });
}

void ThumbnailBrowser::setThumbnailWidth(int width)
{
    width = std::clamp(width, 80, 280);
    if (width == m_thumbnailWidth)
        return;
    const QModelIndex anchor = m_list->currentIndex();
    m_thumbnailWidth = width;
    m_slider->setValue(width);
    m_sizeLabel->setText(QStringLiteral("%1 px").arg(width));
    cancelRequest();
    m_failed.clear();
    m_list->setGridSize(cellSize());
    m_list->doItemsLayout();
    if (anchor.isValid())
        m_list->scrollTo(anchor, QAbstractItemView::EnsureVisible);
    m_refreshTimer.start(0);
    Q_EMIT thumbnailWidthChanged(width);
}

qreal ThumbnailBrowser::rasterScaleFor(int page) const
{
    const QSizeF points = m_pages[page].displaySize;
    return std::max(qreal(0.00001), std::min({
        m_thumbnailWidth * devicePixelRatioF() / std::max(qreal(1), points.width()),
        m_thumbnailWidth * 1.42 * devicePixelRatioF() / std::max(qreal(1), points.height()),
        std::sqrt(std::min(350000.0, qreal(ThumbnailCacheBytes) * 0.85
            / (4 * std::max(1, int(m_wanted.size())))) / std::max(qreal(1), points.width() * points.height()))}));
}

void ThumbnailBrowser::refreshVisible()
{
    if (!isVisible() || m_pages.isEmpty())
        return;
    const QSize grid = cellSize();
    const int columns = std::max(1, m_list->viewport()->width() / grid.width());
    const int firstRow = std::max(0, m_list->verticalScrollBar()->value() / grid.height() - 1);
    const int rowCount = m_list->viewport()->height() / grid.height() + 4;
    const int first = std::min(int(m_pages.size()), firstRow * columns);
    const int last = std::min(int(m_pages.size()), (firstRow + rowCount) * columns);
    m_wanted.clear();
    // Model rows are lightweight; only visible and adjacent images are requested.
    for (int page = first; page < last; ++page)
        m_wanted.insert(page);
    if (m_wanted.size() > MaximumThumbnails) {
        QVector<int> candidates(m_wanted.cbegin(), m_wanted.cend());
        const int center = (first + last) / 2;
        std::sort(candidates.begin(), candidates.end(), [center](int a, int b) { return std::abs(a - center) < std::abs(b - center); });
        m_wanted.clear();
        for (int i = 0; i < MaximumThumbnails; ++i)
            m_wanted.insert(candidates[i]);
    }
    for (int page : std::as_const(m_wanted))
        if (auto cached = m_cache.find(page); cached != m_cache.end())
            cached->used = ++m_clock;
    if (m_requestId && !m_wanted.contains(m_requestPage))
        cancelRequest();
    trimCache();
    requestNext();
}

void ThumbnailBrowser::requestNext()
{
    if (!isVisible() || m_requestId || m_wanted.isEmpty())
        return;
    int next = -1;
    int nearest = std::numeric_limits<int>::max();
    const int columns = std::max(1, m_list->viewport()->width() / cellSize().width());
    const int middle = (m_list->verticalScrollBar()->value() + m_list->viewport()->height() / 2)
        / cellSize().height() * columns + columns / 2;
    for (int page : m_wanted) {
        const auto cached = m_cache.constFind(page);
        const qreal scale = rasterScaleFor(page);
        if (m_failed.contains(page) || (cached != m_cache.cend() && cached->scale >= scale * 0.90 && cached->scale <= scale * 1.6))
            continue;
        if (std::abs(page - middle) < nearest) {
            nearest = std::abs(page - middle);
            next = page;
        }
    }
    if (next >= 0) {
        m_requestPage = next;
        m_requestScale = rasterScaleFor(next);
        m_requestId = ++m_nextRequest;
        Q_EMIT renderRequested(next, m_requestScale, m_requestId);
    }
}

void ThumbnailBrowser::cancelRequest()
{
    if (m_requestId) {
        const quint64 id = m_requestId;
        m_requestId = 0;
        m_requestPage = -1;
        Q_EMIT renderCancelled(id);
    }
}

void ThumbnailBrowser::deliverRenderedPage(int page, qreal scale, quint64 requestId, const RenderedPage& result)
{
    if (!requestId || requestId != m_requestId || page != m_requestPage || page < 0 || page >= m_pages.size())
        return;
    if (!result || result.image.sizeInBytes() > ThumbnailCacheBytes || !std::isfinite(scale) || scale <= 0
        || !qFuzzyCompare(scale, m_requestScale)) {
        deliverRenderFailure(page, requestId, result.error);
        return;
    }
    m_requestId = 0;
    m_requestPage = -1;
    m_cache[page] = {result, scale, ++m_clock};
    m_failed.remove(page);
    m_model->imageChanged(page);
    trimCache();
    requestNext();
}

void ThumbnailBrowser::deliverRenderFailure(int page, quint64 requestId, const QString&)
{
    if (!requestId || requestId != m_requestId || page != m_requestPage)
        return;
    m_requestId = 0;
    m_requestPage = -1;
    m_failed.insert(page);
    m_model->imageChanged(page);
    requestNext();
}

void ThumbnailBrowser::trimCache()
{
    qint64 bytes = 0;
    for (const auto& cached : std::as_const(m_cache))
        bytes += cached.page.image.sizeInBytes();
    while (bytes > ThumbnailCacheBytes || m_cache.size() > MaximumThumbnails) {
        int victim = -1;
        bool victimWanted = true;
        quint64 oldest = std::numeric_limits<quint64>::max();
        for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it) {
            const bool wanted = m_wanted.contains(it.key());
            if (victim < 0 || (victimWanted && !wanted) || (victimWanted == wanted && it->used < oldest)) {
                victim = it.key(); victimWanted = wanted; oldest = it->used;
            }
        }
        if (victim < 0)
            break;
        bytes -= m_cache[victim].page.image.sizeInBytes();
        m_cache.remove(victim);
        m_model->imageChanged(victim);
    }
}

bool ThumbnailBrowser::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_list->viewport() && event->type() == QEvent::Wheel) {
        auto* wheel = static_cast<QWheelEvent*>(event);
        if (wheel->modifiers().testFlag(Qt::ControlModifier)) {
            const int delta = wheel->angleDelta().y() != 0 ? wheel->angleDelta().y() : wheel->pixelDelta().y();
            setThumbnailWidth(m_thumbnailWidth + (delta > 0 ? 12 : delta < 0 ? -12 : 0));
            wheel->accept();
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void ThumbnailBrowser::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    m_refreshTimer.start(0);
}
void ThumbnailBrowser::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    m_refreshTimer.start(0);
}
void ThumbnailBrowser::hideEvent(QHideEvent* event)
{
    cancelRequest();
    m_refreshTimer.stop();
    QDialog::hideEvent(event);
}
} // namespace propernotes
