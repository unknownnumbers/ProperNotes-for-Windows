// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "DocumentView.h"
#include <QDialog>
#include <QHash>
#include <QSet>
#include <QTimer>

class QListView;
class QSlider;
class QLabel;

namespace propernotes {
class ThumbnailPageModel;
class ThumbnailDelegate;

// A separate full-screen browser. It never changes DocumentView's reading zoom.
class ThumbnailBrowser final : public QDialog {
    Q_OBJECT
public:
    explicit ThumbnailBrowser(QWidget* parent = nullptr);
    void setDocument(const QVector<PageGeometry>& pages, const QMap<int, PageAnnotations>& annotations, int currentPage);
    void openAtPage(int page);
    void setThumbnailWidth(int width);
    int thumbnailWidth() const { return m_thumbnailWidth; }
    void deliverRenderedPage(int page, qreal scale, quint64 requestId, const RenderedPage& result);
    void deliverRenderFailure(int page, quint64 requestId, const QString& error);

signals:
    void pageActivated(int page);
    void renderRequested(int page, qreal scale, quint64 requestId);
    void renderCancelled(quint64 requestId);
    void thumbnailWidthChanged(int width);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    friend class ThumbnailPageModel;
    friend class ThumbnailDelegate;
    struct CachedPage { RenderedPage page; qreal scale = 1; quint64 used = 0; };
    void refreshVisible();
    void requestNext();
    void cancelRequest();
    void trimCache();
    QSize cellSize() const;
    qreal rasterScaleFor(int page) const;
    QListView* m_list = nullptr;
    QSlider* m_slider = nullptr;
    QLabel* m_sizeLabel = nullptr;
    ThumbnailPageModel* m_model = nullptr;
    QVector<PageGeometry> m_pages;
    QMap<int, PageAnnotations> m_annotations;
    QHash<int, CachedPage> m_cache;
    QSet<int> m_wanted;
    QSet<int> m_failed;
    QTimer m_refreshTimer;
    quint64 m_nextRequest = 0;
    quint64 m_clock = 0;
    quint64 m_requestId = 0;
    int m_requestPage = -1;
    qreal m_requestScale = 1;
    int m_thumbnailWidth = 180;
};
} // namespace propernotes
