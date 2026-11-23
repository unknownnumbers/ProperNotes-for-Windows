// SPDX-License-Identifier: GPL-3.0-only
#include "MainWindow.h"
#include "AnnotationListPane.h"
#include "MainWindowState.h"
#include "FavoritesBar.h"
#include "DocumentSession.h"
#include "SettingsDialog.h"
#include "ThumbnailBrowser.h"
#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QIcon>
#include <QLabel>
#include <QLineF>
#include <QListWidget>
#include <QMenu>
#include <QMetaObject>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace propernotes {
namespace {
QString workerError()
{
    try { throw; }
    catch (const std::exception& error) { return QString::fromUtf8(error.what()); }
    catch (...) { return QStringLiteral("页面处理失败，文档数据仍保留。"); }
}
qsizetype modelBytes(const PageAnnotations& page)
{
    qsizetype bytes = qsizetype(page.strokes.size()) * sizeof(InkStroke) + qsizetype(page.textBoxes.size()) * sizeof(TextBox);
    bytes += page.textMarkups.size() * qsizetype(sizeof(TextMarkup));
    bytes += page.shapes.size() * qsizetype(sizeof(ShapeAnnotation));
    for (const auto& stroke : page.strokes) bytes += stroke.points.size() * qsizetype(sizeof(InkPoint));
    for (const auto& text : page.textBoxes) bytes += (text.text.size() + text.fontFamily.size()) * 2;
    for (const auto& markup : page.textMarkups) bytes += markup.quads.size() * qsizetype(sizeof(TextMarkupQuad));
    return bytes;
}
bool modelFits(const QMap<int, PageAnnotations>& pages, QString& error)
{
    qsizetype strokes = 0, points = 0, texts = 0, characters = 0, shapes = 0;
    qsizetype markupCount = 0, markupQuads = 0, markupElements = 0;
    QSet<QUuid> ids;
    for (const auto& page : pages) {
        strokes += page.strokes.size(); texts += page.textBoxes.size();
        for (const auto& stroke : page.strokes) {
            if (stroke.id.isNull() || ids.contains(stroke.id) || stroke.points.isEmpty() || stroke.points.size() > 200000) {
                error = QStringLiteral("笔迹标识或采样数量无效，本次编辑未应用。"); return false;
            }
            ids.insert(stroke.id); points += stroke.points.size();
        }
        for (const auto& text : page.textBoxes) {
            if (text.id.isNull() || ids.contains(text.id) || text.text.size() > 100000 || text.text.contains(QChar::Null) ||
                !text.rect.isValid() || !std::isfinite(text.rect.x()) || !std::isfinite(text.rect.y()) ||
                !std::isfinite(text.rect.width()) || !std::isfinite(text.rect.height())) {
                error = QStringLiteral("文字框数据超出当前支持范围，本次编辑未应用。"); return false;
            }
            ids.insert(text.id); characters += text.text.size();
        }
        for (const auto& markup : page.textMarkups) {
            if (ids.contains(markup.id) || !validTextMarkup(markup)) {
                error = QStringLiteral("审阅标记的范围或样式无效，本次编辑未应用。");
                return false;
            }
            ids.insert(markup.id);
            ++markupCount;
            markupQuads += markup.quads.size();
            markupElements += textMarkupPathElements(markup);
            if (markupCount > MaxDocumentTextMarkups || markupQuads > MaxDocumentTextMarkupQuads ||
                markupElements > MaxDocumentTextMarkupPathElements) {
                error = QStringLiteral("文档已达到审阅标记上限，本次编辑未应用。");
                return false;
            }
        }
        for (const auto& shape : page.shapes) {
            if (ids.contains(shape.id) || !validShape(shape)) {
                error = QStringLiteral("形状的范围或样式无效，本次编辑未应用。");
                return false;
            }
            ids.insert(shape.id);
        }
        shapes += page.shapes.size();
        if (strokes > 100000 || points > 2000000 || texts > 10000 || characters > 2000000 || shapes > MaxDocumentShapes) {
            error = QStringLiteral("文档已达到当前批注数量或内容上限，本次编辑未应用；原数据保留。"); return false;
        }
    }
    return true;
}
QRectF focusBounds(const PageAnnotations& page, const QUuid& id)
{
    for (const auto& stroke : page.strokes) if (stroke.id == id) return inkStrokeOutline(stroke).boundingRect();
    for (const auto& text : page.textBoxes) if (text.id == id) return text.rect;
    for (const auto& markup : page.textMarkups) if (markup.id == id) return textMarkupBounds(markup);
    for (const auto& shape : page.shapes) if (shape.id == id) return shapeBounds(shape);
    return {};
}
QString layoutName(DocumentView::LayoutMode mode)
{
    switch (mode) {
    case DocumentView::LayoutMode::SinglePage: return QStringLiteral("single");
    case DocumentView::LayoutMode::FacingPages: return QStringLiteral("facing");
    case DocumentView::LayoutMode::FacingContinuous: return QStringLiteral("facing-continuous");
    default: return QStringLiteral("single-continuous");
    }
}
bool sameView(const DocumentView::ViewState& a, const DocumentView::ViewState& b)
{
    return a.valid && b.valid && a.page == b.page && a.layout == b.layout && a.zoomMode == b.zoomMode &&
        std::abs(a.zoom - b.zoom) < 1e-6 && QLineF(a.pdfAnchor, b.pdfAnchor).length() < 0.5;
}
} // namespace

void MainWindow::commitAnnotations(int page, const PageAnnotations& before, const PageAnnotations& after,
                                   const QString& reason, const QUuid& focus)
{
    m_lastCommitAccepted = false;
    if (page < 0 || page >= m_pageCount || m_readOnly || m_fileBusy) {
        if (page >= 0 && page < m_pageCount) m_view->updatePageAnnotations(page, m_pageAnnotations.value(page));
        return;
    }
    const auto authoritative = m_pageAnnotations.value(page);
    if (after == authoritative) return;
    QString error;
    auto candidate = m_pageAnnotations;
    candidate.insert(page, after);
    if (before != authoritative) error = QStringLiteral("页面编辑状态已改变，本次过时操作未应用。");
    if (!error.isEmpty() || !modelFits(candidate, error)) {
        m_view->updatePageAnnotations(page, authoritative);
        QTimer::singleShot(0, this, [this, error] { showError(error); });
        return;
    }
    m_pageAnnotations = std::move(candidate);
    m_annotationList->updatePageAnnotations(page, after);
    m_lastCommitAccepted = true;
    for (const auto& record : std::as_const(m_editRedo)) m_historyBytes -= record.bytes;
    m_editRedo.clear();
    const qsizetype bytes = modelBytes(before) + modelBytes(after);
    m_editUndo.append({page, before, after, reason, focus, bytes});
    m_historyBytes += bytes;
    constexpr qsizetype HistoryBudget = 256 * 1024 * 1024;
    while (m_editUndo.size() > 256 || (m_historyBytes > HistoryBudget && m_editUndo.size() > 1)) {
        m_historyBytes -= m_editUndo.front().bytes;
        m_editUndo.removeFirst();
    }
    m_dirty = m_pageAnnotations != m_savedAnnotations;
    ++m_contentRevision;
    m_sidebarReady.remove(page);
    if (auto* item = m_pages->item(page)) item->setIcon(m_sidebarPlaceholder);
    m_sidebarPreviewTimer->start();
    updateUi();
    scheduleWorkspaceSave();
}

void MainWindow::editHistory(bool redo)
{
    if (m_fileBusy || m_readOnly || m_view->isTextEditing()) return;
    m_view->finishGesture();
    auto& source = redo ? m_editRedo : m_editUndo;
    auto& destination = redo ? m_editUndo : m_editRedo;
    if (source.isEmpty()) return;
    const auto record = source.takeLast();
    const auto value = redo ? record.after : record.before;
    m_pageAnnotations.insert(record.page, value);
    m_annotationList->updatePageAnnotations(record.page, value);
    destination.append(record);
    m_view->updatePageAnnotations(record.page, value);
    ++m_contentRevision;
    m_dirty = m_pageAnnotations != m_savedAnnotations;
    m_sidebarReady.remove(record.page);
    if (auto* item = m_pages->item(record.page)) item->setIcon(m_sidebarPlaceholder);
    m_sidebarPreviewTimer->start();
    auto state = m_view->captureViewState();
    state.page = record.page;
    QRectF focus = focusBounds(value, record.focus);
    if (focus.isEmpty()) focus = focusBounds(redo ? record.before : record.after, record.focus);
    if (focus.isEmpty()) {
        const auto& removed = redo ? record.before : record.after;
        QHash<QUuid, const InkStroke*> currentStrokes;
        for (const auto& stroke : value.strokes) currentStrokes.insert(stroke.id, &stroke);
        for (const auto& stroke : removed.strokes) {
            const auto current = currentStrokes.constFind(stroke.id);
            if (current == currentStrokes.cend() || *current.value() != stroke) {
                focus = inkStrokeOutline(stroke).boundingRect(); break;
            }
        }
        if (focus.isEmpty()) {
            QSet<QUuid> currentText;
            for (const auto& text : value.textBoxes) currentText.insert(text.id);
            for (const auto& text : removed.textBoxes) {
                if (!currentText.contains(text.id)) { focus = text.rect; break; }
            }
        }
        if (focus.isEmpty()) {
            QSet<QUuid> currentShapes;
            for (const auto& shape : value.shapes) currentShapes.insert(shape.id);
            for (const auto& shape : removed.shapes) {
                if (!currentShapes.contains(shape.id)) { focus = shapeBounds(shape); break; }
            }
        }
        if (focus.isEmpty()) {
            QSet<QUuid> currentMarkups;
            for (const auto& markup : value.textMarkups) currentMarkups.insert(markup.id);
            for (const auto& markup : removed.textMarkups) {
                if (!currentMarkups.contains(markup.id)) { focus = textMarkupBounds(markup); break; }
            }
        }
    }
    if (focus.isEmpty()) focus = m_geometry.value(record.page).pdfRect;
    state.pdfAnchor = focus.center(); state.viewportAnchor = QPointF(0.5, 0.5); state.valid = true;
    // This locates the result but never turns an edit into a navigation command.
    m_restoringView = true;
    m_view->restoreViewState(state);
    m_restoringView = false;
    updateUi();
    scheduleWorkspaceSave();
}

bool MainWindow::canUseObjectClipboard() const
{
    return m_tool == InkCanvas::Tool::Select && m_pageCount > 0 && !m_readOnly && !m_fileBusy &&
        !m_closingAll && !m_thumbnailBrowser && !m_settingsDialog && !m_view->isTextEditing();
}

bool MainWindow::objectClipboardAvailable() const
{
    return (!m_objectClipboard.strokes.isEmpty() || !m_objectClipboard.textBoxes.isEmpty() || !m_objectClipboard.shapes.isEmpty()) &&
        m_activeTab >= 0 && m_activeTab < m_tabs.size() &&
        m_tabs[m_activeTab]->id == m_objectClipboard.tabId &&
        m_objectClipboard.sourcePage >= 0 && m_objectClipboard.sourcePage < m_geometry.size();
}

void MainWindow::updateObjectClipboardUi()
{
    const bool usable = canUseObjectClipboard();
    const bool selected = usable && m_view->hasSelectedObjects();
    m_cutObjectsAction->setEnabled(selected);
    m_copyObjectsAction->setEnabled(selected);
    m_pasteObjectsAction->setVisible(m_tool == InkCanvas::Tool::Select);
    m_pasteObjectsAction->setEnabled(usable && objectClipboardAvailable());
}

void MainWindow::copySelectedObjects(bool cut)
{
    if (!canUseObjectClipboard() || m_view->hasActiveGesture() || m_activeTab < 0 || m_activeTab >= m_tabs.size()) return;
    auto selection = m_view->selectedObjects();
    const qsizetype count = selection.strokes.size() + selection.textBoxes.size() + selection.shapes.size();
    if (!count || selection.page < 0 || selection.page >= m_geometry.size()) return;

    if (cut) {
        const auto before = m_pageAnnotations.value(selection.page);
        QSet<QUuid> ids;
        for (const auto& stroke : std::as_const(selection.strokes)) ids.insert(stroke.id);
        for (const auto& box : std::as_const(selection.textBoxes)) ids.insert(box.id);
        for (const auto& shape : std::as_const(selection.shapes)) ids.insert(shape.id);
        auto after = before;
        const auto isSelected = [&ids](const auto& annotation) { return ids.contains(annotation.id); };
        if (after.strokes.removeIf(isSelected) != selection.strokes.size() ||
            after.textBoxes.removeIf(isSelected) != selection.textBoxes.size() ||
            after.shapes.removeIf(isSelected) != selection.shapes.size()) return;
        QUuid focus;
        if (!selection.shapes.isEmpty()) focus = selection.shapes.front().id;
        else if (!selection.textBoxes.isEmpty()) focus = selection.textBoxes.front().id;
        else focus = selection.strokes.front().id;
        commitAnnotations(selection.page, before, after, QStringLiteral("annotation-cut"), focus);
        if (!std::exchange(m_lastCommitAccepted, false)) return;
        m_view->updatePageAnnotations(selection.page, after);
    }

    m_objectClipboard = {m_tabs[m_activeTab]->id, selection.page, std::move(selection.strokes),
        std::move(selection.textBoxes), std::move(selection.shapes)};
    updateObjectClipboardUi();
    statusBar()->showMessage((cut ? QStringLiteral("已剪切 %1 个批注，可粘贴到本 PDF 的其他页。")
        : QStringLiteral("已复制 %1 个批注，可粘贴到本 PDF 的其他页。")).arg(count), 4500);
}

void MainWindow::pasteObjects()
{
    if (!canUseObjectClipboard() || m_view->hasActiveGesture() || !objectClipboardAvailable()) return;
    const int page = m_view->currentPage();
    if (page < 0 || page >= m_geometry.size()) return;
    const auto& target = m_geometry[page];
    const auto& source = m_geometry[m_objectClipboard.sourcePage];
    bool invertible = false;
    const QTransform displayToPdf = target.pdfToDisplay.inverted(&invertible);
    const QRectF paper = target.pdfRect.normalized();
    if (!invertible || !paper.isValid()) return;
    const QTransform sourceToTarget = page == m_objectClipboard.sourcePage
        ? QTransform() : source.pdfToDisplay * displayToPdf;
    // Coordinates, pen widths and font sizes use PDF page user-space units.
    const qreal sizeScale = std::hypot(sourceToTarget.m11(), sourceToTarget.m12());
    const qreal pageRotation = qRadiansToDegrees(std::atan2(sourceToTarget.m12(), sourceToTarget.m11()));

    auto strokes = m_objectClipboard.strokes;
    auto boxes = m_objectClipboard.textBoxes;
    auto shapes = m_objectClipboard.shapes;
    QRectF bounds;
    for (auto& stroke : strokes) {
        stroke.widthPt *= sizeScale;
        if (!(stroke.widthPt >= MinInkStrokeWidthPt && stroke.widthPt <= MaxInkStrokeWidthPt)) {
            statusBar()->showMessage(QStringLiteral("换算后的线宽超出当前页可编辑范围。剪贴板仍保留原批注。"), 6500);
            return;
        }
        for (auto& point : stroke.points) {
            point.position = sourceToTarget.map(point.position);
        }
        bounds = bounds.united(inkStrokeBounds(stroke));
    }
    for (auto& box : boxes) {
        // Keep the text layout dimensions while positioning the center in the destination page.
        const QPointF center = sourceToTarget.map(box.rect.center());
        const QSizeF size = box.rect.size() * sizeScale;
        box.rect = QRectF(center - QPointF(size.width() / 2, size.height() / 2), size);
        box.fontSizePt *= sizeScale;
        if (!(size.width() >= 0.1 && size.width() <= 100'000 && size.height() >= 0.1 && size.height() <= 100'000 &&
              box.fontSizePt >= 1 && box.fontSizePt <= 1000)) {
            statusBar()->showMessage(QStringLiteral("换算后的文字框尺寸或字号超出当前页可编辑范围。剪贴板仍保留原批注。"), 6500);
            return;
        }
        bounds = bounds.united(box.rect);
    }
    for (auto& shape : shapes) {
        const QPointF center = shapeCenter(shape);
        const QPointF targetCenter = sourceToTarget.map(center);
        shape.start = targetCenter + (shape.start - center) * sizeScale;
        shape.end = targetCenter + (shape.end - center) * sizeScale;
        shape.rotationDegrees = std::remainder(shape.rotationDegrees + pageRotation, qreal(360));
        shape.style.widthPt *= sizeScale;
        if (!(shape.style.widthPt >= MinInkStrokeWidthPt && shape.style.widthPt <= MaxInkStrokeWidthPt)) {
            statusBar()->showMessage(QStringLiteral("换算后的形状线宽超出当前页可编辑范围。剪贴板仍保留原批注。"), 6500);
            return;
        }
        bounds = bounds.united(shapeBounds(shape));
    }
    if (!bounds.isValid() || bounds.width() > paper.width() || bounds.height() > paper.height()) {
        statusBar()->showMessage(QStringLiteral("剪贴板中的批注放不进当前页，仍可粘贴到其他页。"), 6500);
        return;
    }
    const QPointF center = m_view->visiblePageCenterPdf(page);
    const qreal right = std::max(paper.left(), paper.right() - bounds.width());
    const qreal bottom = std::max(paper.top(), paper.bottom() - bounds.height());
    const QPointF targetOrigin(
        std::clamp(center.x() - bounds.width() / 2, paper.left(), right),
        std::clamp(center.y() - bounds.height() / 2, paper.top(), bottom));
    const QPointF offset = targetOrigin - bounds.topLeft();
    const auto positionFits = [](const QPointF& point) {
        return std::abs(point.x()) <= MaxInkCoordinate && std::abs(point.y()) <= MaxInkCoordinate;
    };
    QSet<QUuid> ids;
    for (auto& stroke : strokes) {
        stroke.id = QUuid::createUuid();
        ids.insert(stroke.id);
        for (auto& point : stroke.points) {
            point.position += offset;
            if (!positionFits(point.position)) {
                statusBar()->showMessage(QStringLiteral("粘贴位置超出 PDF 坐标范围。"), 6500);
                return;
            }
        }
    }
    for (auto& box : boxes) {
        box.rect.translate(offset);
        if (!positionFits(box.rect.topLeft()) || !positionFits(box.rect.bottomRight())) {
            statusBar()->showMessage(QStringLiteral("粘贴位置超出 PDF 坐标范围。"), 6500);
            return;
        }
        box.id = QUuid::createUuid();
        ids.insert(box.id);
    }
    for (auto& shape : shapes) {
        shape.start += offset;
        shape.end += offset;
        if (!validShape(shape)) {
            statusBar()->showMessage(QStringLiteral("换算后的形状超出当前页可编辑范围，剪贴板仍保留原批注。"), 6500);
            return;
        }
        shape.id = QUuid::createUuid();
        ids.insert(shape.id);
    }

    const auto before = m_pageAnnotations.value(page);
    auto after = before;
    QUuid focus;
    if (!shapes.isEmpty()) focus = shapes.front().id;
    else if (!boxes.isEmpty()) focus = boxes.front().id;
    else focus = strokes.front().id;
    after.strokes.append(strokes);
    after.textBoxes.append(boxes);
    after.shapes.append(shapes);
    commitAnnotations(page, before, after, QStringLiteral("annotation-paste"), focus);
    if (!std::exchange(m_lastCommitAccepted, false)) return;
    m_view->updatePageAnnotations(page, after);
    m_view->selectPageObjects(page, ids);
    statusBar()->showMessage(QStringLiteral("已粘贴 %1 个批注。可拖动新选区。").arg(ids.size()), 4500);
}

bool MainWindow::isSingleUse(InkCanvas::Tool tool) const
{
    if (tool == InkCanvas::Tool::TextSelect) {
        const auto* review = activeReviewTool();
        return review && review->reviewSingleUse;
    }
    const auto preferences=effectiveToolPreferences();
    if (tool == InkCanvas::Tool::Pen) return preferences.penSingleUse;
    if (tool == InkCanvas::Tool::Highlighter) return preferences.highlighterSingleUse;
    if (tool == InkCanvas::Tool::ObjectEraser) return preferences.eraserSingleUse;
    if (tool == InkCanvas::Tool::TextBox) return preferences.textSingleUse;
    if (const auto shape = InkCanvas::shapeKind(tool)) return preferences.shapeToolPreference(*shape).singleUse;
    return false;
}

qreal MainWindow::effectivePenWidth() const
{
    auto tools=m_preferences.tools;auto color=m_preferences.penColor;qreal width=m_preferences.penWidthPt;
    if(m_liveFavorite&&m_liveFavorite->active)propernotes::applyFavorite(*m_liveFavorite->active,tools,color,width);
    return width;
}

void MainWindow::beginPendingDefaultChange()
{
    m_liveFavorite->pendingDefaultChange=true;
}

bool MainWindow::finishPendingDefaultChange(bool success)
{
    const bool pending=std::exchange(m_liveFavorite->pendingDefaultChange,false);
    if(pending&&success&&!activeReviewTool()){clearFavoriteSelection();return true;}
    return false;
}

void MainWindow::rememberPersistentTool()
{
    const auto* review = activeReviewTool();
    if (!m_liveFavorite || (m_tool == InkCanvas::Tool::TextSelect && !review) || isSingleUse(m_tool)) return;
    FavoriteState::RuntimeTool state;
    state.tool=m_tool;
    state.snapshot=review ? *review : captureFavorite(m_tool,effectiveToolPreferences(),m_color,effectivePenWidth(),favoriteToolName(m_tool));
    state.snapshot.id=QUuid(); // Runtime default snapshots are not stored favorites.
    if(m_liveFavorite->active&&m_liveFavorite->active->tool==m_tool&&!m_liveFavorite->active->id.isNull())state.favorite=m_liveFavorite->active;
    auto& history=m_liveFavorite->persistent;
    for(qsizetype index=history.size();index>0;--index){
        const auto& old=history[index-1];
        const bool samePreset=(!old.favorite&&!state.favorite)||(old.favorite&&state.favorite&&old.favorite->id==state.favorite->id);
        if(old.tool==state.tool&&samePreset&&old.snapshot.markupKind==state.snapshot.markupKind)history.removeAt(index-1);
    }
    history.append(std::move(state));
    if(history.size()>64)history.removeFirst();
}

void MainWindow::chooseTool(InkCanvas::Tool tool, bool userChoice)
{
    ++m_toolSerial;
    m_view->finishGesture();
    if (m_review && (m_tool != tool || (userChoice && activeReviewTool()))) m_review->reviewFeedback.clear();
    if(userChoice&&m_liveFavorite->active){clearFavoriteSelection();applyPreferences(m_preferences);}
    m_tool = tool;
    m_view->setTool(tool);
    syncStrokeStyleControls(effectiveToolPreferences(), effectivePenWidth());
    if(userChoice)rememberPersistentTool();
    updateToolUi();
    updateTextReviewUi();
}

void MainWindow::toolCompleted(InkCanvas::Tool tool, const QString&)
{
    const bool accepted = std::exchange(m_lastCommitAccepted, false);
    if (!accepted || m_tool != tool || !isSingleUse(tool)) return;
    const quint64 serial = m_toolSerial;
    QTimer::singleShot(0, this, [this, tool, serial] {
        if (m_toolSerial != serial || m_tool != tool || !isSingleUse(tool)) return;
        const auto* review = activeReviewTool();
        auto current=review ? *review : captureFavorite(m_tool,effectiveToolPreferences(),m_color,effectivePenWidth(),favoriteToolName(m_tool));
        current.id=QUuid();
        const auto activeId=m_liveFavorite->active?m_liveFavorite->active->id:QUuid();
        std::optional<FavoriteState::RuntimeTool> previous;
        for(auto it=m_liveFavorite->persistent.crbegin();it!=m_liveFavorite->persistent.crend();++it){
            const auto presetId=it->favorite?it->favorite->id:QUuid();
            if (it->tool == tool && presetId == activeId) {
                const bool sameTool = review ? it->snapshot.markupKind == review->markupKind
                    : favoriteToolsJson({it->snapshot}) == favoriteToolsJson({current});
                if (sameTool) continue;
            }
            previous=*it;break;
        }
        ++m_toolSerial;m_view->finishGesture();
        if(previous){m_liveFavorite->active=previous->favorite?previous->favorite:std::optional<FavoriteTool>(previous->snapshot);m_tool=previous->tool;}
        else{m_liveFavorite->active.reset();m_tool=InkCanvas::Tool::Pan;}
        if (const auto* restored = activeReviewTool()) m_review->kind = *restored->markupKind;
        applyPreferences(m_preferences);m_view->setTool(m_tool);updateToolUi();updateTextReviewUi();
    });
}

void MainWindow::updateToolUi()
{
    if (!m_lockAction) return;
    const bool wasUpdating = m_updatingUi; m_updatingUi = true;
    m_penAction->setChecked(m_tool == InkCanvas::Tool::Pen);
    m_highlighterAction->setChecked(m_tool == InkCanvas::Tool::Highlighter);
    m_eraserAction->setChecked(m_tool == InkCanvas::Tool::ObjectEraser);
    m_selectAction->setChecked(m_tool == InkCanvas::Tool::Select);
    m_textAction->setChecked(m_tool == InkCanvas::Tool::TextBox);
    m_panAction->setChecked(m_tool == InkCanvas::Tool::Pan);
    const auto shape = InkCanvas::shapeKind(m_tool);
    m_shapeToolButton->setText(shape ? favoriteToolName(m_tool) : QStringLiteral("形状"));
    for (auto* action : m_shapeToolButton->menu()->actions()) action->setChecked(action->data().toInt() == int(m_tool));
    m_followAction->setChecked(m_preferences.tools.inputFollowsTool);
    m_editTextMarkupsAction->setVisible(m_tool == InkCanvas::Tool::Select || m_tool == InkCanvas::Tool::ObjectEraser);
    m_editTextMarkupsAction->setChecked(m_preferences.editTextMarkups);
    m_lockAction->setChecked(!isSingleUse(m_tool));
    const auto* review = activeReviewTool();
    m_view->setTextReviewActive(review != nullptr);
    updateObjectClipboardUi();
    const bool lockableReview = review && m_pageCount > 0 && !m_readOnly && !m_closingAll && !m_thumbnailBrowser && !m_settingsDialog;
    const bool lockableDrawingTool = m_preferencesWritable && (m_tool == InkCanvas::Tool::Pen ||
        m_tool == InkCanvas::Tool::Highlighter || m_tool == InkCanvas::Tool::ObjectEraser || m_tool == InkCanvas::Tool::TextBox || shape);
    m_lockAction->setEnabled(!m_preferencesBusy && !m_backupBusy && !m_fileBusy && (lockableReview || lockableDrawingTool));
    m_updatingUi = wasUpdating;
    if(m_favoritesBar){
        if (review)
            m_favoritesBar->setCurrentTool(*review);
        else
            m_favoritesBar->setCurrentTool(m_tool,effectiveToolPreferences(),m_color,effectivePenWidth());
        m_favoritesBar->setActiveFavoriteId(m_liveFavorite->active&&m_liveFavorite->active->tool==m_tool?m_liveFavorite->active->id:QUuid());
    }
}

void MainWindow::viewChanged(const DocumentView::ViewState& state, const QString& reason)
{
    if (m_restoringView || !state.valid || m_pageCount < 1) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_viewHistoryIndex >= 0 && sameView(m_viewHistory[m_viewHistoryIndex], state)) return;
    const bool coalesce = m_viewHistoryIndex > 0 && m_viewHistoryIndex == m_viewHistory.size() - 1 &&
        reason == m_lastViewReason && (reason == QStringLiteral("scroll") || reason == QStringLiteral("zoom") ||
                                     reason == QStringLiteral("resize")) && now - m_lastViewChangeMs < 650;
    if (coalesce) m_viewHistory[m_viewHistoryIndex] = state;
    else {
        m_viewHistory.resize(m_viewHistoryIndex + 1);
        m_viewHistory.append(state);
        if (m_viewHistory.size() > 256) m_viewHistory.removeFirst();
        m_viewHistoryIndex = int(m_viewHistory.size()) - 1;
    }
    m_lastViewReason = reason; m_lastViewChangeMs = now;
    scheduleWorkspaceSave();
    updateUi();
}

void MainWindow::navigateHistory(int direction)
{
    if (m_fileBusy || m_thumbnailBrowser) return;
    const int next = m_viewHistoryIndex + direction;
    if (next < 0 || next >= m_viewHistory.size()) return;
    m_view->finishGesture();
    m_viewHistoryIndex = next;
    m_restoringView = true;
    m_view->restoreViewState(m_viewHistory[next]);
    m_restoringView = false;
    m_lastViewReason.clear(); updateUi();scheduleWorkspaceSave();
}

void MainWindow::goToPage(int page)
{
    if (!m_fileBusy && page >= 0 && page < m_pageCount) m_view->goToPage(page);
}
void MainWindow::requestRender() { m_view->setRenderingSuspended(false); m_sidebarPreviewTimer->start(); }
void MainWindow::changeZoom(qreal zoom, const QPointF& anchor) { if (!m_fileBusy) m_view->setZoom(zoom, anchor); }
void MainWindow::fitWidth() { if (!m_fileBusy) m_view->setZoomMode(DocumentView::ZoomMode::FitWidth); }

void MainWindow::requestPageRender(int page, qreal scale, quint64 id, int route)
{
    if (m_fileBusy || page < 0 || page >= m_pageCount) return;
    RenderedPage cachedRaster;
    QSize previewSize;
    if (route == 2) {
        const QSize maximumSize = m_pages->iconSize();
        const QSizeF display = m_geometry.at(page).displaySize;
        previewSize = QSize(qCeil(display.width() * scale), qCeil(display.height() * scale)).boundedTo(maximumSize);
        cachedRaster = m_view->cachedPageRaster(page);
        if (cachedRaster.image.width() < previewSize.width() || cachedRaster.image.height() < previewSize.height())
            cachedRaster = {};
    }
    const QString key = QString::number(route) + QLatin1Char(':') + QString::number(id);
    auto ticket = std::make_shared<std::atomic_bool>(false);
    m_renderTickets.insert(key, ticket);
    const auto epoch = m_renderEpoch; const quint64 generation = epoch->load();
    const auto session = m_session; const QPointer<MainWindow> window(this);
    QMetaObject::invokeMethod(m_workerContext, [session, window, ticket, epoch, generation, page, scale, id, route, key,
                                              cachedRaster = std::move(cachedRaster), previewSize] {
        if (ticket->load() || epoch->load() != generation) return;
        RenderedPage result;
        try {
            if (cachedRaster && previewSize.isValid()) {
                result.image = cachedRaster.image.scaled(previewSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                if (result) {
                    result.pdfToImage = cachedRaster.pdfToImage * QTransform::fromScale(
                        qreal(previewSize.width()) / cachedRaster.image.width(),
                        qreal(previewSize.height()) / cachedRaster.image.height());
                }
            }
            if (!result) {
                if (session->document) result = session->document->renderPage(page, scale);
                else result.error = QStringLiteral("文档不可用。");
            }
        } catch (...) { result.error = workerError(); }
        if (!window || ticket->load() || epoch->load() != generation) return;
        QMetaObject::invokeMethod(window.data(), [window, ticket, epoch, generation, page, scale, id, route, key, result = std::move(result)] {
            if (!window || ticket->load() || epoch->load() != generation) return;
            window->m_renderTickets.remove(key);
            if (route == 0) {
                if (result) window->m_view->deliverRenderedPage(page, scale, id, result);
                else window->m_view->deliverRenderFailure(page, id, result.error);
            } else if (route == 1) {
                if (!window->m_thumbnailBrowser) return;
                if (result) window->m_thumbnailBrowser->deliverRenderedPage(page, scale, id, result);
                else window->m_thumbnailBrowser->deliverRenderFailure(page, id, result.error);
            } else if (id == window->m_sidebarRequest && page == window->m_sidebarPendingPage) {
                window->m_sidebarRequestBusy = false;
                window->m_sidebarPendingPage = -1;
                window->m_sidebarReady.insert(page);
                if (result) {
                    QImage image = result.image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
                    QPainter painter(&image); painter.setWorldTransform(result.pdfToImage);
                    const auto annotations = window->m_pageAnnotations.value(page);
                    for (const auto& markup : annotations.textMarkups) paintTextMarkup(painter, markup);
                    for (const auto& stroke : annotations.strokes) paintInkStroke(painter, stroke);
                    for (const auto& shape : annotations.shapes) paintShape(painter, shape);
                    for (const auto& text : annotations.textBoxes) paintTextBox(painter, text);
                    painter.end();
                    if (auto* item = window->m_pages->item(page)) {
                        QPixmap icon(window->m_pages->iconSize());
                        icon.fill(Qt::transparent);
                        QPainter iconPainter(&icon);
                        iconPainter.drawImage(QPoint((icon.width() - image.width()) / 2, (icon.height() - image.height()) / 2), image);
                        iconPainter.end();
                        item->setToolTip({});
                        item->setIcon(QIcon(icon));
                    }
                    window->m_sidebarIcons.removeAll(page);
                    window->m_sidebarIcons.append(page);
                    while (window->m_sidebarIcons.size() > 128) {
                        const int victim = window->m_sidebarIcons.takeFirst();
                        if (auto* item = window->m_pages->item(victim)) item->setIcon(window->m_sidebarPlaceholder);
                        window->m_sidebarReady.remove(victim);
                    }
                } else if (auto* item = window->m_pages->item(page)) item->setToolTip(result.error);
                QTimer::singleShot(0, window.data(), [window] {
                    if (window && !window->m_sidebarPreviewTimer->isActive()) window->refreshSidebarPreview();
                });
            }
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void MainWindow::cancelPageRender(quint64 id, int route)
{
    const QString key = QString::number(route) + QLatin1Char(':') + QString::number(id);
    const auto ticket = m_renderTickets.take(key);
    if (ticket) ticket->store(true);
}

void MainWindow::cancelSidebarPreviewRequest()
{
    if (m_sidebarRequestBusy) cancelPageRender(m_sidebarRequest, 2);
    m_sidebarRequestBusy = false;
    m_sidebarPendingPage = -1;
}

void MainWindow::refreshSidebarPreview()
{
    if (m_fileBusy || !m_pages->isVisible() || m_pageCount < 1 || m_thumbnailBrowser) return;
    const QRect viewport = m_pages->viewport()->rect();
    if (viewport.isEmpty()) return;
    const auto rowRect = [this](int page) { return m_pages->visualRect(m_pages->model()->index(page, 0)); };
    if (m_sidebarRequestBusy) {
        if (rowRect(m_sidebarPendingPage).intersects(viewport)) return;
        cancelSidebarPreviewRequest();
    }

    // Row geometry is ordered because this is a flat list with a fixed icon size.
    int first = 0;
    int end = m_pages->count();
    while (first < end) {
        const int middle = first + (end - first) / 2;
        if (rowRect(middle).bottom() < viewport.top()) first = middle + 1;
        else end = middle;
    }

    const QSize maximumSize = m_pages->iconSize();
    for (int page = first; page < m_pages->count(); ++page) {
        const QRect row = rowRect(page);
        if (row.top() > viewport.bottom()) break;
        if (m_sidebarReady.contains(page)) continue;
        if (!row.intersects(viewport)) continue;
        const QSizeF display = m_geometry.at(page).displaySize;
        if (display.width() <= 0 || display.height() <= 0) continue;
        m_sidebarPendingPage = page;
        m_sidebarRequestBusy = true;
        const qreal scale = std::min(maximumSize.width() / display.width(), maximumSize.height() / display.height());
        requestPageRender(page, std::nextafter(scale, qreal(0)), ++m_sidebarRequest, 2);
        return;
    }
}

void MainWindow::openPageOverview()
{
    if (m_fileBusy || m_backupBusy || m_preferencesBusy || m_thumbnailBrowser || m_pageCount < 1) return;
    m_view->finishGesture();
    const auto reading = m_view->captureViewState();
    m_view->setRenderingSuspended(true);
    m_backupTimer->stop(); m_sidebarPreviewTimer->stop();
    cancelSidebarPreviewRequest();
    ThumbnailBrowser browser(this); m_thumbnailBrowser = &browser;
    browser.setDocument(m_geometry, m_pageAnnotations, m_currentPage);
    browser.setThumbnailWidth(m_preferences.tools.thumbnailSize);
    int target = -1;
    connect(&browser, &ThumbnailBrowser::pageActivated, &browser, [&target](int page) { target = page; });
    connect(&browser, &ThumbnailBrowser::renderRequested, this, [this](int page, qreal scale, quint64 id) { requestPageRender(page, scale, id, 1); });
    connect(&browser, &ThumbnailBrowser::renderCancelled, this, [this](quint64 id) { cancelPageRender(id, 1); });
    browser.openAtPage(m_currentPage);
    browser.exec();
    const int thumbnailSize = browser.thumbnailWidth();
    m_thumbnailBrowser.clear();
    m_view->setRenderingSuspended(false);
    if (target >= 0) goToPage(target);
    else { m_restoringView = true; m_view->restoreViewState(reading); m_restoringView = false; }
    if (thumbnailSize != m_preferences.tools.thumbnailSize && m_preferencesWritable) {
        auto values = m_preferences; values.tools.thumbnailSize = thumbnailSize; persistPreferences(values);
    }
    m_sidebarPreviewTimer->start(); scheduleBackup(); updateUi();
}
} // namespace propernotes
