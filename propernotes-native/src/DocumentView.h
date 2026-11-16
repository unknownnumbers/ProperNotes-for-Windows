// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "InkCanvas.h"
#include <QAbstractScrollArea>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QTimer>

namespace propernotes {

// Geometry is metadata supplied by the worker. No PdfDocument crosses into this widget.
struct PageGeometry {
    QRectF pdfRect;
    QSizeF displaySize;
    QTransform pdfToDisplay;
};

class DocumentView final : public QAbstractScrollArea {
    Q_OBJECT
public:
    enum class LayoutMode { SinglePage, FacingPages, SingleContinuous, FacingContinuous };
    enum class ZoomMode { Free, FitWidth, FitPage, FitHeight };
    Q_ENUM(LayoutMode)
    Q_ENUM(ZoomMode)
    struct ViewState {
        int page = -1;
        QPointF pdfAnchor;
        QPointF viewportAnchor = QPointF(0.5, 0.5);
        qreal zoom = 1.0;
        LayoutMode layout = LayoutMode::SingleContinuous;
        ZoomMode zoomMode = ZoomMode::FitWidth;
        bool valid = false;
    };

    explicit DocumentView(QWidget* parent = nullptr);
    void setDocument(const QVector<PageGeometry>& pages, const QMap<int, PageAnnotations>& annotations);
    void clearDocument();
    void updatePageAnnotations(int page, const PageAnnotations& annotations);
    void setLayoutMode(LayoutMode mode);
    LayoutMode layoutMode() const { return m_layout; }
    void setZoomMode(ZoomMode mode);
    ZoomMode zoomMode() const { return m_zoomMode; }
    void setZoom(qreal zoom, const QPointF& viewportAnchor = QPointF(-1, -1));
    qreal zoom() const { return m_zoom; }
    int currentPage() const { return m_currentPage; }
    int pageCount() const { return int(m_pages.size()); }
    void goToPage(int page);
    ViewState captureViewState() const;
    void restoreViewState(const ViewState& state);
    void setRenderingSuspended(bool suspended);
    void deliverRenderedPage(int page, qreal scale, quint64 requestId, const RenderedPage& result);
    void deliverRenderFailure(int page, quint64 requestId, const QString& error);

    void setTool(InkCanvas::Tool tool);
    void setPenColor(const QColor& color);
    void setPenWidth(qreal width);
    void setPenOpacity(qreal opacity);
    void setPressureEnabled(bool enabled);
    void setPressureSensitivity(qreal sensitivity);
    void setEraserMode(InkCanvas::EraseMode mode);
    void setEraserRadius(qreal radius);
    void setInputFollowsTool(bool enabled);
    void setTextStyle(const QString& family, qreal size, const QColor& color);
    void setDarkTheme(bool dark);
    void setEditingEnabled(bool enabled);
    void finishGesture();
    void cancelGesture();
    bool hasActiveGesture() const;
    bool isTextEditing() const;
    void clearSelection();

signals:
    // Request IDs are monotonic for this widget, including across documents.
    // The host may skip cancelled work before starting its serial worker render.
    void renderRequested(int page, qreal scale, quint64 requestId);
    void renderCancelled(quint64 requestId);
    void currentPageChanged(int page);
    void viewStateChanged(const propernotes::DocumentView::ViewState& state, const QString& reason);
    void annotationsCommitted(int page, const propernotes::PageAnnotations& before,
                              const propernotes::PageAnnotations& after, const QString& reason, const QUuid& focusId);
    void normalToolCompleted(propernotes::InkCanvas::Tool tool, const QString& reason);
    void selectionChanged();
    void textEditingChanged(bool editing);
    void activity(const QString& message);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    struct CachedPage { RenderedPage page; qreal scale = 1; quint64 used = 0; quint64 revision = 0; };
    struct Request { int page = -1; qreal scale = 1; quint64 id = 0; };
    struct Row { int first = 0; int last = 0; qreal top = 0; qreal bottom = 0; };
    void rebuildLayout();
    void refreshVisible();
    void requestNext();
    void applyImage(int page);
    void configureCanvas(InkCanvas* canvas);
    void updatePenContactGate();
    void trimCache();
    void invalidateRequest();
    void publishState(const QString& reason);
    void restoreAnchor(const ViewState& state);
    ViewState captureAt(const QPointF& viewportPoint) const;
    QPointF offset() const;
    void setOffset(const QPointF& value);
    qreal fittedZoom() const;
    qreal rasterScaleFor(int page) const;
    int pageAt(const QPointF& contentPoint) const;
    bool isFacing() const;
    bool isContinuous() const;

    QVector<PageGeometry> m_pages;
    QVector<QRectF> m_pagePositions;
    QVector<Row> m_rows;
    QVector<int> m_visible;
    QMap<int, PageAnnotations> m_annotations;
    QHash<int, InkCanvas*> m_canvases;
    QHash<int, CachedPage> m_cache;
    QHash<int, QString> m_errors;
    QSet<int> m_wanted;
    Request m_request;
    QSizeF m_contentSize;
    QTimer m_refreshTimer;
    quint64 m_nextRequest = 0;
    quint64 m_clock = 0;
    qreal m_horizontalUnit = 1;
    qreal m_verticalUnit = 1;
    qreal m_zoom = 1;
    int m_currentPage = -1;
    LayoutMode m_layout = LayoutMode::SingleContinuous;
    ZoomMode m_zoomMode = ZoomMode::FitWidth;
    InkCanvas::Tool m_tool = InkCanvas::Tool::Pen;
    InkCanvas::EraseMode m_eraserMode = InkCanvas::EraseMode::WholeStroke;
    QColor m_penColor = QColor("#202833");
    qreal m_penWidth = 2.5;
    qreal m_penOpacity = 1;
    bool m_pressure = true;
    qreal m_sensitivity = 50;
    qreal m_eraserRadius = 8;
    QString m_textFamily = QStringLiteral("Segoe UI");
    qreal m_textSize = 12;
    QColor m_textColor = QColor("#202833");
    bool m_inputFollowsTool = false;
    bool m_dark = false;
    bool m_editing = false;
    bool m_rebuilding = false;
    bool m_refreshing = false;
    bool m_adjusting = false;
    bool m_suspended = false;
};

} // namespace propernotes
Q_DECLARE_METATYPE(propernotes::DocumentView::ViewState)
