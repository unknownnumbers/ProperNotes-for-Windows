// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "InkData.h"
#include "PdfTextLayer.h"
#include <QImage>
#include <QSet>
#include <QWidget>
#include <memory>

class QFocusEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QPlainTextEdit;
class QTabletEvent;
class QWheelEvent;

namespace propernotes {

// Document history belongs to the host, not to this disposable page view.
class InkCanvas final : public QWidget {
    Q_OBJECT
public:
    enum class Tool { Pen, ObjectEraser, Pan, Select, TextBox, TextSelect };
    enum class EraseMode { WholeStroke, PartialStroke };
    enum class FinishReason { ForcedCommit, NormalRelease };
    Q_ENUM(Tool)
    Q_ENUM(EraseMode)
    static constexpr int PageMargin = 24;
    explicit InkCanvas(QWidget* parent = nullptr);
    ~InkCanvas() override;
    void setPage(int pageIndex, const QImage& background, const QTransform& pdfToImage,
                 const QRectF& pageRect, qreal rasterScale, const PageAnnotations& annotations);
    void setPageAnnotations(const PageAnnotations& annotations);
    PageAnnotations pageAnnotations() const { return m_annotations; }
    void clearPage();
    void setTool(Tool tool);
    Tool tool() const { return m_tool; }
    void setPenColor(const QColor& color);
    void setPenWidth(qreal widthPt);
    void setPenOpacity(qreal opacity);
    void setPressureEnabled(bool enabled) { m_pressureEnabled = enabled; }
    void setPressureSensitivity(qreal sensitivity);
    void setEraserMode(EraseMode mode) { m_eraseMode = mode; }
    void setEraserRadius(qreal radiusPt);
    void setTouchAndMouseAnnotate(bool enabled);
    void setTextStyle(const QString& family, qreal sizePt, const QColor& color);
    void setEditingEnabled(bool enabled);
    void setDarkTheme(bool dark) { m_darkTheme = dark; update(); }
    void setZoom(qreal zoom);
    qreal zoom() const { return m_zoom; }
    int pageIndex() const { return m_pageIndex; }
    bool hasActiveGesture() const;
    bool hasPointerContact() const { return m_tabletActive || m_touchActive || m_gesture != Gesture::None; }
    bool hasPenContact() const { return m_tabletActive; }
    void cancelTouchOperation();
    void setExternalPenContact(bool down);
    bool isTextEditing() const { return m_editor != nullptr; }
    QSet<QUuid> selectionIds() const { return m_selection; }
    void clearSelection();
    void setTextPage(const std::shared_ptr<const PdfTextPage>& text);
    void setTextSelection(const QVector<TextMarkupQuad>& quads);
    void setSearchHighlight(const QVector<TextMarkupQuad>& quads);
    QRectF objectBounds(const QUuid& id) const;
    QPointF pdfPointAt(const QPointF& widgetPoint) const;
    QPointF widgetPointForPdf(const QPointF& pdfPoint) const;
    QSize sizeHint() const override;
    void finishActiveOperation(FinishReason reason = FinishReason::ForcedCommit);
    void finishGesture() { finishActiveOperation(FinishReason::ForcedCommit); }
    void cancelActiveOperation();

signals:
    void annotationsCommitted(int pageIndex, const propernotes::PageAnnotations& before,
                              const propernotes::PageAnnotations& after, const QString& reason, const QUuid& focusId);
    void normalToolCompleted(propernotes::InkCanvas::Tool tool, const QString& reason);
    void selectionChanged();
    void textEditingChanged(bool editing);
    void textPageRequested(int pageIndex);
    void textSelectionStarted();
    void textSelectionFinished(int pageIndex, const propernotes::PdfTextSelection& selection,
                               const QPointF& releasedAt);
    void operationFinished();
    void penContactChanged(bool down);
    void panRequested(const QPointF& delta);
    void zoomRequested(qreal factor, const QPointF& widgetAnchor);
    void activity(const QString& message);

protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    enum class Gesture { None, Ink, Erase, Pan, Marquee, Move, TextFrame, TextResize, TextSelection };
    enum class Source { None, Pen, Mouse, Touch };
    void pointerPress(const QPointF& position, const QPointF& global, qreal pressure, Source source,
                      bool tailEraser = false, bool forcePan = false);
    void pointerMove(const QPointF& position, const QPointF& global, qreal pressure);
    void pointerRelease(const QPointF& position, qreal pressure);
    void addPoint(const QPointF& position, qreal pressure);
    void eraseBetween(const QPointF& from, const QPointF& to);
    void rebuildInkCache();
    void updateExtent();
    void updateCursor();
    void resetGesture();
    void commit(const PageAnnotations& before, const QString& reason, const QUuid& focusId,
                bool normalCompletion, Tool completedTool);
    void beginTextEdit(const QUuid& id, const QRectF& newRect = {});
    void finishTextEdit(bool cancel);
    void beginTextSelection(const QPointF& position);
    void updateTextSelection(const QPointF& position);
    void positionEditor();
    QUuid textAt(const QPointF& pdfPoint) const;
    QSet<QUuid> objectsIn(const QRectF& pdfRect) const;
    QRectF selectionBounds() const;
    int textResizeCornerAt(const QPointF& widgetPoint) const;
    QTransform pdfToWidget() const;
    QSizeF imageSize() const;
    bool insidePaper(const QPointF& position) const;
    QPointF clampedPdfPoint(const QPointF& position) const;
    qreal effectivePressure(qreal reported, bool nativePen) const;

    static constexpr int MaximumStrokePoints = 100000;
    int m_pageIndex = -1;
    Tool m_tool = Tool::Pen;
    Gesture m_gesture = Gesture::None;
    Source m_source = Source::None;
    EraseMode m_eraseMode = EraseMode::WholeStroke;
    QColor m_penColor = QColor("#202833");
    qreal m_penWidthPt = 2.5;
    qreal m_penOpacity = 1.0;
    qreal m_eraserRadiusPt = 8.0;
    bool m_pressureEnabled = true;
    qreal m_pressureSensitivity = 50;
    bool m_inkPressureEnabled = true;
    qreal m_inkSensitivity = 50;
    EraseMode m_activeEraseMode = EraseMode::WholeStroke;
    qreal m_activeEraserRadiusPt = 8;
    QString m_textFamily = QStringLiteral("Segoe UI");
    qreal m_textSizePt = 12;
    QColor m_textColor = QColor("#202833");
    qreal m_zoom = 1.0;
    qreal m_imageScale = 1.0;
    qreal m_rasterScale = 1.0;
    qreal m_pageUserUnit = 1.0;
    bool m_editingEnabled = false;
    bool m_darkTheme = false;
    bool m_touchAndMouseAnnotate = false;
    bool m_tabletActive = false;
    bool m_touchActive = false;
    bool m_tailEraser = false;
    bool m_hoverEraser = false;
    bool m_textFinishing = false;
    bool m_touchNavigationOnly = false;
    bool m_externalPenContact = false;
    bool m_suppressedTouch = false;
    QPointF m_previousErasePoint;
    QPointF m_previousPanGlobal;
    QPointF m_hoverPosition;
    QPointF m_gestureStart;
    QPointF m_gestureEnd;
    QPointF m_previousTouchGlobal;
    qreal m_previousTouchDistance = 0;
    int m_previousTouchCount = 0;
    int m_resizeCorner = -1;
    QUuid m_focusId;
    QImage m_background;
    QImage m_inkCache;
    QTransform m_pdfToImage;
    QTransform m_imageToPdf;
    QRectF m_pageRect;
    PageAnnotations m_annotations;
    PageAnnotations m_beforeGesture;
    QSet<QUuid> m_selection;
    QSet<QUuid> m_selectionBeforeGesture;
    InkStroke m_pendingStroke;
    QPlainTextEdit* m_editor = nullptr;
    QUuid m_editTextId;
    QRectF m_editTextRect;
    PageAnnotations m_beforeTextEdit;
    std::shared_ptr<const PdfTextPage> m_textPage;
    QVector<TextMarkupQuad> m_selectedTextQuads;
    QVector<TextMarkupQuad> m_searchQuads;
    PdfTextSelection m_textPreview;
    QPointF m_textDragStart;
    QPointF m_textDragEnd;
    int m_textFirstGlyph = -1;
    int m_textLastGlyph = -1;
};
} // namespace propernotes
