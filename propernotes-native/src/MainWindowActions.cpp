// SPDX-License-Identifier: GPL-3.0-only
#include "MainWindow.h"
#include "DocumentSession.h"
#include "ThumbnailBrowser.h"
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
#include <QMetaObject>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
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
    for (const auto& stroke : page.strokes) bytes += stroke.points.size() * qsizetype(sizeof(InkPoint));
    for (const auto& text : page.textBoxes) bytes += (text.text.size() + text.fontFamily.size()) * 2;
    return bytes;
}
bool modelFits(const QMap<int, PageAnnotations>& pages, QString& error)
{
    qsizetype strokes = 0, points = 0, texts = 0, characters = 0;
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
        if (strokes > 100000 || points > 2000000 || texts > 10000 || characters > 2000000) {
            error = QStringLiteral("文档已达到当前批注数量或内容上限，本次编辑未应用；原数据保留。"); return false;
        }
    }
    return true;
}
QRectF focusBounds(const PageAnnotations& page, const QUuid& id)
{
    for (const auto& stroke : page.strokes) if (stroke.id == id) return inkStrokeOutline(stroke).boundingRect();
    for (const auto& text : page.textBoxes) if (text.id == id) return text.rect;
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
    if (auto* item = m_pages->item(page)) item->setIcon(QIcon());
    m_sidebarPreviewTimer->start();
    updateUi();
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
    destination.append(record);
    m_view->updatePageAnnotations(record.page, value);
    ++m_contentRevision;
    m_dirty = m_pageAnnotations != m_savedAnnotations;
    m_sidebarReady.remove(record.page);
    if (auto* item = m_pages->item(record.page)) item->setIcon(QIcon());
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
    }
    if (focus.isEmpty()) focus = m_geometry.value(record.page).pdfRect;
    state.pdfAnchor = focus.center(); state.viewportAnchor = QPointF(0.5, 0.5); state.valid = true;
    // This locates the result but never turns an edit into a navigation command.
    m_restoringView = true;
    m_view->restoreViewState(state);
    m_restoringView = false;
    updateUi();
}

bool MainWindow::isSingleUse(InkCanvas::Tool tool) const
{
    if (tool == InkCanvas::Tool::Pen) return m_preferences.tools.penSingleUse;
    if (tool == InkCanvas::Tool::ObjectEraser) return m_preferences.tools.eraserSingleUse;
    if (tool == InkCanvas::Tool::TextBox) return m_preferences.tools.textSingleUse;
    return false;
}

void MainWindow::chooseTool(InkCanvas::Tool tool, bool userChoice)
{
    ++m_toolSerial;
    m_view->finishGesture();
    m_tool = tool;
    m_view->setTool(tool);
    if (userChoice && !isSingleUse(tool)) {
        m_persistentTools.removeAll(tool);
        m_persistentTools.append(tool);
    }
    updateToolUi();
}

void MainWindow::toolCompleted(InkCanvas::Tool tool, const QString&)
{
    const bool accepted = std::exchange(m_lastCommitAccepted, false);
    if (!accepted || m_tool != tool || !isSingleUse(tool)) return;
    const quint64 serial = m_toolSerial;
    QTimer::singleShot(0, this, [this, tool, serial] {
        if (m_toolSerial != serial || m_tool != tool || !isSingleUse(tool)) return;
        auto previous = InkCanvas::Tool::Pan;
        for (auto it = m_persistentTools.crbegin(); it != m_persistentTools.crend(); ++it) {
            if (*it != tool && !isSingleUse(*it)) { previous = *it; break; }
        }
        chooseTool(previous, false);
    });
}

void MainWindow::updateToolUi()
{
    if (!m_lockAction) return;
    const bool wasUpdating = m_updatingUi; m_updatingUi = true;
    m_penAction->setChecked(m_tool == InkCanvas::Tool::Pen);
    m_eraserAction->setChecked(m_tool == InkCanvas::Tool::ObjectEraser);
    m_selectAction->setChecked(m_tool == InkCanvas::Tool::Select);
    m_textAction->setChecked(m_tool == InkCanvas::Tool::TextBox);
    m_panAction->setChecked(m_tool == InkCanvas::Tool::Pan);
    m_followAction->setChecked(m_preferences.tools.inputFollowsTool);
    m_lockAction->setChecked(!isSingleUse(m_tool));
    m_lockAction->setEnabled(!m_preferencesBusy && !m_backupBusy && !m_fileBusy && m_preferencesWritable &&
        (m_tool == InkCanvas::Tool::Pen || m_tool == InkCanvas::Tool::ObjectEraser || m_tool == InkCanvas::Tool::TextBox));
    m_updatingUi = wasUpdating;
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
    m_lastViewReason.clear(); updateUi();
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
    const QString key = QString::number(route) + QLatin1Char(':') + QString::number(id);
    auto ticket = std::make_shared<std::atomic_bool>(false);
    m_renderTickets.insert(key, ticket);
    const auto epoch = m_renderEpoch; const quint64 generation = epoch->load();
    const auto session = m_session; const QPointer<MainWindow> window(this);
    QMetaObject::invokeMethod(m_workerContext, [session, window, ticket, epoch, generation, page, scale, id, route, key] {
        if (ticket->load() || epoch->load() != generation) return;
        RenderedPage result;
        try {
            if (session->document) result = session->document->renderPage(page, scale);
            else result.error = QStringLiteral("文档不可用。");
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
                window->m_sidebarReady.insert(page);
                if (result) {
                    QImage image = result.image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
                    QPainter painter(&image); painter.setWorldTransform(result.pdfToImage);
                    const auto annotations = window->m_pageAnnotations.value(page);
                    for (const auto& stroke : annotations.strokes) painter.fillPath(inkStrokeOutline(stroke), stroke.color);
                    for (const auto& text : annotations.textBoxes) paintTextBox(painter, text);
                    painter.end();
                    if (auto* item = window->m_pages->item(page)) item->setIcon(QIcon(QPixmap::fromImage(image)));
                    window->m_sidebarIcons.removeAll(page);
                    window->m_sidebarIcons.append(page);
                    while (window->m_sidebarIcons.size() > 128) {
                        const int victim = window->m_sidebarIcons.takeFirst();
                        if (auto* item = window->m_pages->item(victim)) item->setIcon(QIcon());
                        window->m_sidebarReady.remove(victim);
                    }
                } else if (auto* item = window->m_pages->item(page)) item->setToolTip(result.error);
                window->m_sidebarPreviewTimer->start();
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

void MainWindow::refreshSidebarPreview()
{
    if (m_fileBusy || m_sidebarRequestBusy || !m_navigation->isVisible() || m_pageCount < 1 || m_thumbnailBrowser) return;
    for (int page = 0; page < m_pages->count(); ++page) {
        if (m_sidebarReady.contains(page)) continue;
        const auto* item = m_pages->item(page);
        if (!m_pages->visualItemRect(item).intersects(m_pages->viewport()->rect())) continue;
        const QSizeF display = m_geometry[page].displaySize;
        if (display.width() <= 0 || display.height() <= 0) continue;
        m_sidebarPendingPage = page; m_sidebarRequestBusy = true; ++m_sidebarRequest;
        requestPageRender(page, std::min(120 / display.width(), 170 / display.height()), m_sidebarRequest, 2);
        break;
    }
}

void MainWindow::openPageOverview()
{
    if (m_fileBusy || m_backupBusy || m_preferencesBusy || m_thumbnailBrowser || m_pageCount < 1) return;
    m_view->finishGesture();
    const auto reading = m_view->captureViewState();
    m_view->setRenderingSuspended(true);
    m_backupTimer->stop(); m_sidebarPreviewTimer->stop();
    if (m_sidebarRequestBusy) cancelPageRender(m_sidebarRequest, 2);
    m_sidebarRequestBusy = false;
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
