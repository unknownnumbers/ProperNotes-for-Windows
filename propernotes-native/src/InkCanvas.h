// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "InkData.h"

#include <QImage>
#include <QPainterPath>
#include <QRectF>
#include <QTransform>
#include <QVector>
#include <QWidget>

class QFocusEvent;
class QMouseEvent;
class QPaintEvent;
class QTabletEvent;
class QWheelEvent;

namespace propernotes {

// Prototype policy: pen/mouse use the selected tool; touch only navigates.
// This is not the unverified Drawboard device-mode or Tool Lock behavior.
class InkCanvas final : public QWidget
{
    Q_OBJECT
public:
    enum class Tool { Pen, ObjectEraser, Pan };

    explicit InkCanvas(QWidget* parent = nullptr);
    void setPage(int pageIndex, const QImage& background, const QTransform& pdfToImage, const QRectF& pageRect, qreal rasterScale,
                 const QVector<InkStroke>& strokes);
    void clearPage();
    void setTool(Tool tool);
    void setPenColor(const QColor& color);
    void setPenWidth(qreal widthPt);
    void setEditingEnabled(bool enabled);
    void setDarkTheme(bool dark) { m_darkTheme = dark; update(); }
    void setZoom(qreal zoom);
    qreal zoom() const { return m_zoom; }
    int pageIndex() const { return m_pageIndex; }
    QVector<InkStroke> strokes() const { return m_strokes; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool hasActiveGesture() const { return m_drawing || m_erasing || m_panning; }
    QPointF pdfPointAt(const QPointF& widgetPoint) const;
    QPointF widgetPointForPdf(const QPointF& pdfPoint) const;
    QSize sizeHint() const override;
    void finishGesture();
    void undo();

signals:
    void strokesChanged();
    void undoAvailabilityChanged(bool available);
    void panRequested(const QPointF& delta);
    void zoomRequested(qreal factor, const QPointF& widgetAnchor);
    void activity(const QString& message);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void beginInk(const QPointF& position, qreal pressure, bool eraser);
    void moveInk(const QPointF& position, qreal pressure);
    void addPoint(const QPointF& position, qreal pressure);
    void eraseBetween(const QPointF& from, const QPointF& to);
    void rebuildInkCache();
    void updateExtent();
    void updateCursor();
    void pushUndo(const QVector<InkStroke>& before);
    QTransform pdfToWidget() const;
    bool insidePaper(const QPointF& position) const;
    QPointF clampedPdfPoint(const QPointF& position) const;

    static constexpr int Margin = 24;
    static constexpr int MaximumStrokePoints = 100000;
    static constexpr int MaximumUndoEntries = 24;
    static constexpr qreal EraserRadiusPt = 8.0;
    int m_pageIndex = -1;
    Tool m_tool = Tool::Pen;
    QColor m_penColor = QColor("#202833");
    qreal m_penWidthPt = 2.5;
    qreal m_zoom = 1.0;
    qreal m_imageScale = 1.0;
    qreal m_rasterScale = 1.0;
    qreal m_pageUserUnit = 1.0;
    bool m_editingEnabled = false;
    bool m_darkTheme = false;
    bool m_drawing = false;
    bool m_erasing = false;
    bool m_eraseChanged = false;
    bool m_panning = false;
    bool m_tabletActive = false;
    bool m_touchActive = false;
    bool m_hoverEraser = false;
    QPointF m_previousErasePoint;
    QPointF m_previousPanGlobal;
    QPointF m_hoverPosition;
    QPointF m_previousTouchGlobal;
    qreal m_previousTouchDistance = 0;
    int m_previousTouchCount = 0;
    QImage m_background;
    QImage m_inkCache;
    QTransform m_pdfToImage;
    QTransform m_imageToPdf;
    QRectF m_pageRect;
    QVector<InkStroke> m_strokes;
    QVector<InkStroke> m_beforeGesture;
    QVector<QVector<InkStroke>> m_undo;
    InkStroke m_pendingStroke;
};

} // namespace propernotes
