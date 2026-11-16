// SPDX-License-Identifier: GPL-3.0-only
#include "MainWindow.h"
#include "InkCanvas.h"
#include "PdfDocument.h"
#include "SettingsDialog.h"
#include "DocumentSession.h"
#include "ThumbnailBrowser.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QColorDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QLabel>
#include <QKeySequence>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QPair>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QPalette>
#include <QStyleHints>
#include <QSpinBox>
#include <QSplitter>
#include <QSplitterHandle>
#include <QStatusBar>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace propernotes {

namespace {
QString exceptionText()
{
    try { throw; }
    catch (const std::exception& error) { return QString::fromUtf8(error.what()); }
    catch (...) { return QStringLiteral("PDF 处理失败，当前界面数据仍保留。"); }
}

struct OpenResult {
    bool ok = false;
    QString error;
    QString path;
    QString readOnlyReason;
    bool readOnly = false;
    int pageCount = 0;
    QVector<PageGeometry> geometry;
    QMap<int, PageAnnotations> annotations;
};

struct PreferenceResult {
    bool ok = false;
    bool writable = false;
    AppPreferenceValues values;
    QString error;
    QString migrationWarning;
};

QString backupResultText(const BackupResult& result, const QString& success)
{
    QStringList lines;
    if (!result.ok)
        lines.append(result.error.isEmpty() ? QStringLiteral("备份操作未获确认，原 PDF 未被覆盖。") : result.error);
    else
        lines.append(success);
    lines.append(result.warnings);
    return lines.join(QLatin1Char('\n'));
}
} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), m_session(std::make_shared<DocumentSession>()),
      m_renderEpoch(std::make_shared<std::atomic<quint64>>(0))
{
    resize(1240, 860);
    setMinimumSize(800, 560);
    buildUi();

    m_workerThread = new QThread(this);
    m_workerContext = new QObject;
    m_workerContext->moveToThread(m_workerThread);
    const auto session = m_session;
    connect(m_workerThread, &QThread::finished, m_workerContext, [session] {
        session->document.reset();
        session->preferences.reset();
    }, Qt::DirectConnection);
    connect(m_workerThread, &QThread::finished, m_workerContext, &QObject::deleteLater);
    m_workerThread->start();
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (m_preferences.theme == QStringLiteral("system"))
            applyTheme();
    });
    loadPreferences();
    updateUi();
    statusBar()->showMessage(QStringLiteral("原生最小原型 · 笔 / 鼠标批注，触摸浏览 · 设备及性能待验证"));
}

MainWindow::~MainWindow()
{
    m_renderEpoch->fetch_add(1);
    for (const auto& ticket : std::as_const(m_renderTickets)) ticket->store(true);
    m_workerThread->quit();
    m_workerThread->wait();
}

void MainWindow::loadPreferences(bool reopenSettings)
{
    if (m_preferencesBusy || m_backupBusy || m_fileBusy || m_rendering)
        return;
    m_preferencesBusy = true;
    m_backupTimer->stop();
    m_sidebarTimer->stop();
    m_resumeRenderAfterPreferences |= m_renderTimer->isActive();
    m_renderTimer->stop();
    updateUi();
    const auto session = m_session;
    const QPointer<MainWindow> window(this);
    const QString applicationDirectory = QCoreApplication::applicationDirPath();
    QMetaObject::invokeMethod(m_workerContext, [session, window, applicationDirectory, reopenSettings] {
        PreferenceResult result;
        try {
            if (!session->preferences)
                session->preferences = std::make_unique<AppPreferences>(applicationDirectory);
            result.ok = session->preferences->load();
            result.values = session->preferences->values();
            result.error = session->preferences->lastError();
            result.writable = result.ok && session->preferences->canWrite();
        } catch (...) { result.error = exceptionText(); }
        if (!window)
            return;
        QMetaObject::invokeMethod(window.data(), [window, result, reopenSettings] {
            if (!window)
                return;
            const bool firstLoad = !window->m_preferencesLoaded;
            window->m_preferencesLoaded = true;
            window->m_preferencesBusy = false;
            window->m_preferencesValid = result.ok;
            window->m_preferencesWritable = result.writable;
            window->m_preferencesWarning = result.ok ? QString() : result.error;
            if (!result.ok && window->m_preferencesWarning.isEmpty())
                window->m_preferencesWarning = QStringLiteral("无法读取设置，原设置文件保留。");
            if (result.ok || firstLoad)
                window->applyPreferences(result.values);
            window->updateUi();
            window->scheduleBackup();
            if (std::exchange(window->m_resumeRenderAfterPreferences, false) && window->m_pageCount > 0)
                window->m_renderTimer->start();
            const QString pending = std::exchange(window->m_pendingOpen, QString());
            if (!pending.isEmpty())
                window->openFile(pending);
            else if (reopenSettings)
                window->openSettings();
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void MainWindow::applyPreferences(const AppPreferenceValues& values)
{
    const bool layoutChanged = values.tools.layout != m_preferences.tools.layout;
    const bool previouslySingle = isSingleUse(m_tool);
    m_preferences = values;
    if (previouslySingle && !isSingleUse(m_tool)) {
        m_persistentTools.removeAll(m_tool);
        m_persistentTools.append(m_tool);
    }
    m_color = values.penColor;
    const bool wasUpdating = m_updatingUi;
    m_updatingUi = true;
    m_width->setValue(values.penWidthPt);
    m_sidebarAction->setChecked(values.sidebarVisible);
    m_navigation->setVisible(values.sidebarVisible);
    m_splitter->setSizes({values.sidebarWidth, std::max(1, m_splitter->width() - values.sidebarWidth)});
    m_view->setPenColor(values.penColor);
    m_view->setPenWidth(values.penWidthPt);
    m_view->setPenOpacity(values.tools.opacity);
    m_view->setPressureEnabled(values.tools.pressureEnabled);
    m_view->setPressureSensitivity(values.tools.pressureSensitivity);
    m_view->setEraserMode(values.tools.eraserMode == QStringLiteral("partial")
        ? InkCanvas::EraseMode::PartialStroke : InkCanvas::EraseMode::WholeStroke);
    m_view->setEraserRadius(values.tools.eraserRadiusPt);
    m_view->setInputFollowsTool(values.tools.inputFollowsTool);
    QColor textColor = values.penColor;
    textColor.setAlphaF(values.tools.opacity);
    m_view->setTextStyle(values.tools.textFamily, values.tools.textSizePt, textColor);
    m_opacity->setValue(values.tools.opacity * 100);
    m_pressure->setChecked(values.tools.pressureEnabled);
    m_sensitivity->setValue(values.tools.pressureSensitivity);
    m_eraseMode->setCurrentIndex(m_eraseMode->findData(values.tools.eraserMode));
    m_eraserSize->setValue(values.tools.eraserRadiusPt);
    m_textFamily->setCurrentFont(QFont(values.tools.textFamily));
    m_textSize->setValue(values.tools.textSizePt);
    m_layout->setCurrentIndex(m_layout->findData(values.tools.layout));
    if (layoutChanged) {
        const auto layout = values.tools.layout == QStringLiteral("single") ? DocumentView::LayoutMode::SinglePage
            : values.tools.layout == QStringLiteral("facing") ? DocumentView::LayoutMode::FacingPages
            : values.tools.layout == QStringLiteral("facing-continuous") ? DocumentView::LayoutMode::FacingContinuous
            : DocumentView::LayoutMode::SingleContinuous;
        m_view->setLayoutMode(layout);
    }
    m_updatingUi = wasUpdating;
    applyTheme();
    updateToolUi();
}

void MainWindow::applyTheme()
{
    const bool dark = m_preferences.theme == QStringLiteral("dark") ||
        (m_preferences.theme == QStringLiteral("system") &&
         QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
    const QColor window = QColor(dark ? "#252c37" : "#eef1f6");
    const QColor base = QColor(dark ? "#303947" : "#ffffff");
    const QColor text = QColor(dark ? "#e5ebf5" : "#202833");
    const QColor border = QColor(dark ? "#465267" : "#dce2ec");
    const QColor selected = QColor(dark ? "#244b7a" : "#e0ebff");
    QPalette palette;
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, window);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, base);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::ToolTipBase, base);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::Highlight, selected);
    palette.setColor(QPalette::HighlightedText, text);
    palette.setColor(QPalette::PlaceholderText, QColor(dark ? "#acb7c9" : "#657084"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(dark ? "#7c8798" : "#8992a0"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(dark ? "#7c8798" : "#8992a0"));
    QApplication::setPalette(palette);
    setStyleSheet(QStringLiteral(
        "QMainWindow,QDialog{background:%1;color:%2;}"
        "QToolBar{background:%3;border:0;border-bottom:1px solid %4;spacing:7px;padding:7px;}"
        "QToolButton{border:1px solid transparent;border-radius:5px;padding:7px 10px;}"
        "QToolButton:hover{background:%5;}"
        "QToolButton:checked{background:%5;border-color:#709acc;}"
        "QListWidget,QTreeWidget{background:%3;border:0;outline:0;padding:8px;}"
        "QListWidget::item{padding:12px;border-radius:5px;margin:3px;}"
        "QListWidget::item:selected{background:%5;}"
        "QScrollArea{background:%6;border:0;}"
        "QSpinBox,QDoubleSpinBox,QLineEdit,QComboBox{padding:5px;border:1px solid %4;border-radius:4px;background:%3;}"
        "QStatusBar{background:%3;border-top:1px solid %4;}"
        "QSplitter::handle{background:%4;}"
        "QGroupBox{border:1px solid %4;border-radius:4px;margin-top:10px;padding-top:10px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:10px;}"
    ).arg(window.name(), text.name(), base.name(), border.name(), selected.name(),
          QString(dark ? "#1d222b" : "#e7eaf0")));
    m_view->setDarkTheme(dark);
}

void MainWindow::persistPreferences(AppPreferenceValues values, bool migrate)
{
    if (m_preferencesBusy || m_backupBusy || !m_preferencesWritable)
        return;
    m_view->finishGesture();
    if (m_sidebarTimer->isActive() && m_navigation->isVisible())
        values.sidebarWidth = std::clamp(m_splitter->sizes().value(0, values.sidebarWidth), 112, 400);
    m_sidebarTimer->stop();
    m_backupTimer->stop();
    m_resumeRenderAfterPreferences |= m_renderTimer->isActive();
    m_renderTimer->stop();
    m_preferencesBusy = true;
    updateUi();
    const auto session = m_session;
    const QPointer<MainWindow> window(this);
    QMetaObject::invokeMethod(m_workerContext, [session, window, values = std::move(values), migrate]() mutable {
        PreferenceResult result;
        try {
            if (!session->preferences || !session->preferences->canWrite())
                result.error = QStringLiteral("当前设置只读，未覆盖原配置。");
            else {
                auto& preferences = *session->preferences;
                const QString oldRoot = preferences.resolveDirectory(preferences.values().backupDirectory);
                values.backupDirectory = preferences.normalizeDirectory(values.backupDirectory);
                QStringList roots = preferences.values().backupDirectories;
                roots.append(values.backupDirectories);
                roots.append(values.backupDirectory);
                values.backupDirectories.clear();
                for (const QString& root : roots) {
                    const QString normalized = preferences.normalizeDirectory(root);
                    bool found = false;
                    for (const QString& known : std::as_const(values.backupDirectories))
                        found |= known.compare(normalized, Qt::CaseInsensitive) == 0;
                    if (!found)
                        values.backupDirectories.append(normalized);
                }
                // Commit the registry before migration. Even a partial migration
                // must leave the destination discoverable in future sessions.
                result.ok = preferences.save(values);
                result.error = preferences.lastError();
                result.values = preferences.values();
                result.writable = result.ok && preferences.canWrite();
                const QString newRoot = preferences.resolveDirectory(result.values.backupDirectory);
                if (result.ok && migrate && oldRoot.compare(newRoot, Qt::CaseInsensitive) != 0) {
                    const auto migration = BackupStore().migrateRoot(oldRoot, newRoot);
                    if (!migration.ok || !migration.warnings.isEmpty())
                        result.migrationWarning = backupResultText(migration, QStringLiteral("迁移已完成。"));
                }
            }
        } catch (...) {
            result.error = exceptionText();
            // A migration can fail after preferences have committed. Preserve
            // the committed root registry rather than hiding copied backups.
            if (result.ok)
                result.migrationWarning = result.error;
        }
        if (!window)
            return;
        QMetaObject::invokeMethod(window.data(), [window, result] {
            if (!window)
                return;
            window->m_preferencesBusy = false;
            window->m_preferencesWritable = result.writable;
            if (result.ok) {
                window->m_preferencesValid = true;
                window->m_preferencesWarning.clear();
                window->applyPreferences(result.values);
                window->m_backupWarning = result.migrationWarning;
                window->statusBar()->showMessage(QStringLiteral("设置已保存。"), 5000);
            } else {
                window->m_preferencesWarning = result.error.isEmpty() ? QStringLiteral("设置保存失败，原状态保留。") : result.error;
                window->applyPreferences(window->m_preferences);
            }
            window->updateUi();
            window->scheduleBackup();
            if (std::exchange(window->m_resumeRenderAfterPreferences, false) && window->m_pageCount > 0)
                window->m_renderTimer->start();
            if (!result.ok)
                window->showError(window->m_preferencesWarning);
            else if (!result.migrationWarning.isEmpty())
                window->showError(QStringLiteral("设置已保存，新旧目录均保留在管理列表。\n") + result.migrationWarning);
            if (std::exchange(window->m_closeAfterPreferences, false))
                window->close();
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void MainWindow::openSettings(bool backupManager)
{
    if (!m_preferencesLoaded || m_preferencesBusy || m_backupBusy || m_fileBusy || m_rendering || m_settingsDialog)
        return;
    m_view->finishGesture();
    const bool renderPending = m_renderTimer->isActive();
    m_renderTimer->stop();
    m_backupTimer->stop();
    auto draft = m_preferences;
    if (m_sidebarTimer->isActive() && m_navigation->isVisible())
        draft.sidebarWidth = std::clamp(m_splitter->sizes().value(0, draft.sidebarWidth), 112, 400);
    m_sidebarTimer->stop();
    SettingsDialog dialog(draft, QCoreApplication::applicationDirPath(), m_preferencesWarning,
                          m_preferencesWritable, this);
    m_settingsDialog = &dialog;
    if (backupManager)
        dialog.showBackupManager();
    dialog.setCanBackupNow(m_preferencesValid && m_preferencesWritable && !m_readOnly &&
                          m_pageCount > 0 && m_preferences.backupRetainCount > 0);
    QString recoveredPath;
    connect(&dialog, &SettingsDialog::refreshRequested, this, &MainWindow::refreshBackupCatalog);
    connect(&dialog, &SettingsDialog::createBackupRequested, this, [this] { createBackup(true); });
    connect(&dialog, &SettingsDialog::cleanupRequested, this, &MainWindow::cleanupBackups);
    connect(&dialog, &SettingsDialog::restoreRequested, this, &MainWindow::restoreBackup);
    connect(&dialog, &SettingsDialog::reloadPreferencesRequested, &dialog, [&dialog] { dialog.done(3); });
    dialog.setProperty("recoveredPath", QString());
    updateUi();
    refreshBackupCatalog();
    const int result = dialog.exec();
    recoveredPath = dialog.property("recoveredPath").toString();
    m_settingsDialog.clear();
    if (result == QDialog::Accepted)
        persistPreferences(dialog.values(), dialog.migrateRequested());
    else if (result == 3)
        loadPreferences(true);
    else {
        applyPreferences(m_preferences);
        updateUi();
        scheduleBackup();
    }
    if (renderPending && m_preferencesBusy)
        m_resumeRenderAfterPreferences = true;
    else if (renderPending && !m_rendering && m_pageCount > 0)
        m_renderTimer->start();
    if (result == SettingsDialog::OpenRecoveredCopy && !recoveredPath.isEmpty())
        openFile(recoveredPath);
}

void MainWindow::scheduleBackup(int delayMs)
{
    m_backupTimer->stop();
    if (!m_preferencesLoaded || !m_preferencesValid || !m_preferencesWritable ||
        m_preferencesBusy || m_settingsDialog || m_preferences.backupRetainCount == 0)
        return;
    const int delay = delayMs >= 0 ? delayMs : m_preferences.backupIntervalMinutes * 60 * 1000;
    m_backupTimer->start(delay);
}

void MainWindow::createBackup(bool manual)
{
    if (!m_preferencesValid || !m_preferencesWritable || m_preferences.backupRetainCount == 0 ||
        m_readOnly || m_pageCount < 1) {
        if (manual && m_settingsDialog)
            m_settingsDialog->setStatus(QStringLiteral("当前 PDF 无法备份，或保留数量为 0。已有备份未清理。"));
        scheduleBackup();
        return;
    }
    if (m_backupBusy || m_preferencesBusy || m_fileBusy || m_rendering ||
        m_view->hasActiveGesture() || m_sidebarTimer->isActive() ||
        (!manual && (m_settingsDialog || QApplication::activeModalWidget()))) {
        if (!manual)
            scheduleBackup(5000);
        return;
    }
    if (!manual && m_contentRevision <= m_backedUpRevision) {
        scheduleBackup();
        return;
    }
    const auto snapshot = m_pageAnnotations;
    const quint64 generation = m_documentGeneration;
    const quint64 revision = m_contentRevision;
    const auto session = m_session;
    const QPointer<MainWindow> window(this);
    m_backupBusy = true;
    m_backupTimer->stop();
    if (m_settingsDialog)
        m_settingsDialog->setBusy(true, QStringLiteral("正在创建并校验本地备份…"));
    updateUi();
    QMetaObject::invokeMethod(m_workerContext, [session, window, snapshot, generation, revision, manual] {
        BackupResult result;
        try {
            if (!session->document || !session->preferences)
                result.error = QStringLiteral("文档或设置不可用，未创建备份。");
            else {
                bool valid = true;
                for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it) {
                    if (!session->document->setAnnotations(it.key(), it.value())) {
                        result.error = session->document->lastError();
                        valid = false;
                        break;
                    }
                }
                if (valid) {
                    const auto& preferences = *session->preferences;
                    result = BackupStore().create(preferences.resolveDirectory(preferences.values().backupDirectory),
                                                  *session->document, preferences.values().backupRetainCount);
                }
            }
        } catch (...) { result.error = exceptionText(); }
        if (!window)
            return;
        QMetaObject::invokeMethod(window.data(), [window, result, generation, revision, manual] {
            if (!window)
                return;
            // A save never resets contentRevision. Only the captured state is
            // acknowledged, so ink added while the worker writes is still due.
            if (result.ok && generation == window->m_documentGeneration)
                window->m_backedUpRevision = std::max(window->m_backedUpRevision, revision);
            const QString text = backupResultText(result, !result.skipped && result.affectedSnapshots > 0
                ? QStringLiteral("本地备份已创建并校验。") : QStringLiteral("已有相同内容的备份，无需新增。"));
            window->m_backupWarning = (!result.ok || !result.warnings.isEmpty()) ? text : QString();
            window->finishBackupOperation();
            window->statusBar()->showMessage(text, 12000);
            if (manual && window->m_settingsDialog) {
                window->m_settingsDialog->setStatus(text);
                window->refreshBackupCatalog();
            }
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void MainWindow::finishBackupOperation()
{
    m_backupBusy = false;
    if (m_settingsDialog) {
        m_settingsDialog->setBusy(false);
        m_settingsDialog->setCanBackupNow(m_preferencesValid && m_preferencesWritable && !m_readOnly &&
                                          m_pageCount > 0 && m_preferences.backupRetainCount > 0);
    }
    updateUi();
    scheduleBackup();
}

void MainWindow::refreshBackupCatalog()
{
    if (!m_settingsDialog || m_backupBusy || m_preferencesBusy)
        return;
    if (!m_preferencesValid) {
        m_settingsDialog->setCatalog({}, {});
        m_settingsDialog->setStatus(QStringLiteral("设置未能有效读取，未访问临时默认备份目录。请先重新读取有效配置。"));
        return;
    }
    m_backupBusy = true;
    m_settingsDialog->setBusy(true, QStringLiteral("正在读取已登记的备份目录…"));
    updateUi();
    const auto session = m_session;
    const QPointer<MainWindow> window(this);
    const QString currentPath = m_path;
    QMetaObject::invokeMethod(m_workerContext, [session, window, currentPath] {
        BackupCatalog catalog;
        QString documentId;
        try {
            if (session->preferences) {
                QStringList roots;
                for (const QString& root : session->preferences->values().backupDirectories)
                    roots.append(session->preferences->resolveDirectory(root));
                catalog = BackupStore().list(roots);
            }
            if (!currentPath.isEmpty())
                documentId = BackupStore::documentIdForPath(currentPath);
        } catch (...) { catalog.warnings.append(exceptionText()); }
        if (!window)
            return;
        QMetaObject::invokeMethod(window.data(), [window, catalog = std::move(catalog), documentId] {
            if (!window)
                return;
            window->finishBackupOperation();
            if (window->m_settingsDialog)
                window->m_settingsDialog->setCatalog(catalog, documentId);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void MainWindow::cleanupBackups(const QVector<BackupGroupKey>& groups)
{
    if (!m_settingsDialog || m_backupBusy || m_preferencesBusy || groups.isEmpty())
        return;
    QMessageBox box(QMessageBox::Question, QStringLiteral("清理本地备份"),
                    QStringLiteral("删除所选 %1 组 PDF 的全部备份？原 PDF 保留。此操作不能撤销。").arg(groups.size()),
                    QMessageBox::Yes | QMessageBox::No, m_settingsDialog.data());
    box.setDefaultButton(QMessageBox::No);
    if (box.exec() != QMessageBox::Yes)
        return;
    m_backupBusy = true;
    m_settingsDialog->setBusy(true, QStringLiteral("正在清理选定的备份…"));
    updateUi();
    const QPointer<MainWindow> window(this);
    QMetaObject::invokeMethod(m_workerContext, [window, groups] {
        BackupResult result;
        try { result = BackupStore().removeGroups(groups); }
        catch (...) { result.error = exceptionText(); }
        if (!window)
            return;
        QMetaObject::invokeMethod(window.data(), [window, result] {
            if (!window)
                return;
            window->finishBackupOperation();
            const QString text = backupResultText(result, QStringLiteral("已清理 %1 份本地备份。").arg(result.affectedSnapshots));
            if (window->m_settingsDialog)
                window->m_settingsDialog->setStatus(text);
            if (!result.ok || !result.warnings.isEmpty())
                window->showError(text);
            window->refreshBackupCatalog();
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void MainWindow::restoreBackup(const BackupSnapshotKey& snapshot, const QString& suggestedName)
{
    if (!m_settingsDialog || m_backupBusy || m_preferencesBusy)
        return;
    QString target = QFileDialog::getSaveFileName(m_settingsDialog.data(), QStringLiteral("恢复为新 PDF"), suggestedName,
                                                 QStringLiteral("PDF 文件 (*.pdf)"), nullptr, QFileDialog::DontConfirmOverwrite);
    if (target.isEmpty())
        return;
    if (!target.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
        target += QStringLiteral(".pdf");
    m_backupBusy = true;
    m_settingsDialog->setBusy(true, QStringLiteral("正在校验并另存恢复副本…"));
    updateUi();
    const QPointer<MainWindow> window(this);
    QMetaObject::invokeMethod(m_workerContext, [window, snapshot, target] {
        BackupResult result;
        try {
            const QFileInfo destination(target);
            if (destination.exists() || destination.isSymLink() || destination.isJunction())
                result.error = QStringLiteral("恢复目标已存在。请选择一个新文件名；未覆盖它。");
            else
                result = BackupStore().restoreCopy(snapshot, target);
        } catch (...) { result.error = exceptionText(); }
        if (!window)
            return;
        QMetaObject::invokeMethod(window.data(), [window, result, target] {
            if (!window)
                return;
            window->finishBackupOperation();
            if (!window->m_settingsDialog)
                return;
            if (!result.ok) {
                const QString text = backupResultText(result, {});
                window->m_settingsDialog->setStatus(text);
                window->showError(text);
                return;
            }
            window->m_settingsDialog->setProperty("recoveredPath", target);
            window->m_settingsDialog->done(SettingsDialog::OpenRecoveredCopy);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void MainWindow::updateUi()
{
    if (m_updatingUi) return;
    m_updatingUi = true;
    const bool document = m_pageCount > 0;
    const bool ready = document && !m_fileBusy;
    const bool editable = ready && !m_readOnly;
    const bool textEditing = m_view->isTextEditing();
    const bool filesReady = !m_fileBusy && !m_backupBusy && !m_preferencesBusy && !m_thumbnailBrowser;
    const bool preferencesReady = !m_preferencesBusy && !m_backupBusy && !m_fileBusy && m_preferencesWritable;
    m_openAction->setEnabled(filesReady);
    m_saveAction->setEnabled(editable && filesReady && (m_dirty || textEditing));
    m_saveAsAction->setEnabled(editable && filesReady);
    m_settingsAction->setEnabled(filesReady && m_preferencesLoaded);
    m_backupsAction->setEnabled(filesReady && m_preferencesLoaded);
    m_overviewAction->setEnabled(filesReady && document);
    m_sidebarAction->setEnabled(preferencesReady);
    m_splitter->handle(1)->setEnabled(preferencesReady && !m_settingsDialog);
    m_colorAction->setEnabled(preferencesReady);
    for (auto* action : {m_penAction,m_eraserAction,m_selectAction,m_textAction}) action->setEnabled(editable);
    m_panAction->setEnabled(ready);
    for (auto* widget : QVector<QWidget*>{m_width,m_opacity,m_eraserSize,m_textSize,
                                        m_pressure,m_sensitivity,m_eraseMode,m_textFamily})
        widget->setEnabled(preferencesReady);
    m_followAction->setEnabled(preferencesReady);
    m_layout->setEnabled(preferencesReady && ready);
    m_undoAction->setEnabled(editable && !textEditing && !m_editUndo.isEmpty());
    m_redoAction->setEnabled(editable && !textEditing && !m_editRedo.isEmpty());
    m_backAction->setEnabled(ready && m_viewHistoryIndex > 0);
    m_forwardAction->setEnabled(ready && m_viewHistoryIndex + 1 < m_viewHistory.size());
    m_previousAction->setEnabled(ready && m_currentPage > 0);
    m_nextAction->setEnabled(ready && m_currentPage + 1 < m_pageCount);
    m_pageNumber->setEnabled(ready); m_pages->setEnabled(ready);
    for (auto* action : {m_zoomInAction,m_zoomOutAction,m_fitAction,m_fitPageAction,m_fitHeightAction}) action->setEnabled(ready);
    m_view->setEnabled(!m_fileBusy);
    m_view->setEditingEnabled(editable);
    m_pageNumber->setMaximum(std::max(1,m_pageCount));
    if (document) {
        m_pageNumber->setValue(std::max(1,m_currentPage+1));
        m_pages->setCurrentRow(m_currentPage);
    }
    const auto mode=m_view->layoutMode();
    const QString nameMode=mode==DocumentView::LayoutMode::SinglePage?QStringLiteral("single"):
        mode==DocumentView::LayoutMode::FacingPages?QStringLiteral("facing"):
        mode==DocumentView::LayoutMode::FacingContinuous?QStringLiteral("facing-continuous"):QStringLiteral("single-continuous");
    m_layout->setCurrentIndex(m_layout->findData(nameMode));
    m_pageLabel->setText(document?QStringLiteral("%1 / %2 页").arg(m_currentPage+1).arg(m_pageCount):QStringLiteral("未打开文件"));
    m_zoomLabel->setText(QString::number(m_view->zoom()*100,'f',m_view->zoom()<0.1?2:0)+QLatin1Char('%'));
    const QString name=m_path.isEmpty()?QStringLiteral("尚未打开 PDF"):QFileInfo(m_path).fileName();
    m_fileLabel->setText((m_dirty?QStringLiteral("● "):QString())+name+(m_readOnly?QStringLiteral(" · 只读"):QString()));
    m_fileLabel->setToolTip(m_path);
    setWindowTitle(QStringLiteral("ProperNotes for Windows — 原生预览")+(document?QStringLiteral(" · ")+name:QString())+(m_dirty?QStringLiteral(" *"):QString()));
    m_documentBanner->setVisible(document&&m_readOnly);
    m_documentBanner->setText(QStringLiteral("只读：")+m_readOnlyReason+QStringLiteral(" 本版本不会覆盖此文件。"));
    QStringList warnings;
    if(!m_preferencesWarning.isEmpty())warnings.append(m_preferencesWarning+QStringLiteral(" 自动备份已暂停，请在设置中重新读取有效配置。"));
    if(!m_backupWarning.isEmpty())warnings.append(m_backupWarning);
    m_preferencesBanner->setText(warnings.join(QLatin1Char('\n')));m_preferencesBanner->setVisible(!warnings.isEmpty());
    m_updatingUi=false;
    updateToolUi();
}
void MainWindow::showError(const QString& message)
{
    QMessageBox box(QMessageBox::Warning, QStringLiteral("ProperNotes for Windows"), {}, QMessageBox::Ok, this);
    box.setTextFormat(Qt::PlainText);
    box.setText(message);
    box.exec();
}

void MainWindow::chooseOpenFile()
{
    if (m_fileBusy || m_rendering || m_backupBusy || m_preferencesBusy)
        return;
    m_view->finishGesture();
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开 PDF"), {}, QStringLiteral("PDF 文件 (*.pdf)"));
    if (!path.isEmpty())
        openFile(path);
}

void MainWindow::openFile(const QString& path)
{
    if (path.isEmpty())
        return;
    if (!m_preferencesLoaded) {
        m_pendingOpen = path;
        return;
    }
    if (m_fileBusy || m_rendering || m_backupBusy || m_preferencesBusy)
        return;
    m_view->finishGesture();
    resolveUnsaved([this, path] { beginOpen(path); });
}

void MainWindow::resolveUnsaved(const std::function<void()>& continuation)
{
    if (!m_dirty) {
        continuation();
        return;
    }
    // A modal message box runs an event loop. Do not let a delayed zoom
    // render make saveFile() busy after the user has chosen Save.
    const bool renderPending = m_renderTimer->isActive();
    m_renderTimer->stop();
    QMessageBox box(QMessageBox::Question, QStringLiteral("保存修改"),
                    QStringLiteral("当前 PDF 有未保存的笔迹。是否先保存？"),
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    box.setDefaultButton(QMessageBox::Save);
    const auto result = box.exec();
    if (result == QMessageBox::Save) {
        m_resumeRenderAfterSaveFailure = renderPending;
        saveFile(false, continuation);
        // A cancelled file picker or an early refusal starts no async save.
        if (!m_fileBusy && std::exchange(m_resumeRenderAfterSaveFailure, false))
            m_renderTimer->start();
    } else if (result == QMessageBox::Discard) {
        continuation();
    } else if (renderPending) {
        m_renderTimer->start();
    }
}

void MainWindow::beginOpen(const QString& path)
{
    m_view->finishGesture();
    m_renderTimer->stop();m_sidebarPreviewTimer->stop();
    m_renderEpoch->fetch_add(1);
    for(const auto& ticket:std::as_const(m_renderTickets))ticket->store(true);
    m_renderTickets.clear();m_sidebarRequestBusy=false;
    m_view->setRenderingSuspended(true);
    m_fileBusy=true;updateUi();
    statusBar()->showMessage(QStringLiteral("正在打开 PDF…"));
    const auto session=m_session;const QPointer<MainWindow> window(this);
    QMetaObject::invokeMethod(m_workerContext,[session,window,path]{
        OpenResult result;
        try {
            auto candidate=std::make_unique<PdfDocument>();
            if(!candidate->open(path))result.error=candidate->lastError();
            else if(candidate->pageCount()<1)result.error=QStringLiteral("此 PDF 没有可显示的页面。");
            else {
                bool valid=true;
                for(int page=0;page<candidate->pageCount();++page){
                    PageGeometry geometry{candidate->pageRect(page),candidate->pageSize(page),candidate->pageToDisplayTransform(page)};
                    if(!geometry.pdfRect.isValid()||!geometry.pdfToDisplay.isInvertible()||
                       !std::isfinite(geometry.displaySize.width())||!std::isfinite(geometry.displaySize.height())||
                       geometry.displaySize.width()<=0||geometry.displaySize.height()<=0){
                        result.error=QStringLiteral("PDF 页面坐标无法安全显示。");valid=false;break;
                    }
                    result.geometry.append(geometry);result.annotations.insert(page,candidate->annotations(page));
                }
                if(valid){
                    const auto size=result.geometry.front().displaySize;
                    const qreal density=std::max(qreal(1e-6),std::min({qreal(1),16000/size.width(),16000/size.height(),std::sqrt(6000000/(size.width()*size.height()))}));
                    const auto first=candidate->renderPage(0,density);
                    if(!first||!first.pdfToImage.isInvertible())result.error=first.error.isEmpty()?QStringLiteral("PDF 首页无法完整显示。"):first.error;
                    else {
                        result.path=candidate->currentPath();result.pageCount=candidate->pageCount();
                        result.readOnly=candidate->isReadOnly();result.readOnlyReason=candidate->readOnlyReason();
                        result.ok=true;session->document=std::move(candidate);
                    }
                }
            }
        }catch(...){result.error=exceptionText();}
        if(!window)return;
        QMetaObject::invokeMethod(window.data(),[window,result=std::move(result)]{
            if(!window)return;
            if(!result.ok){window->finishFileOperation();window->showError(result.error.isEmpty()?QStringLiteral("打开失败，原文档仍保留。"):result.error);return;}
            window->m_restoringView=true;
            window->m_path=result.path;window->m_readOnly=result.readOnly;window->m_readOnlyReason=result.readOnlyReason;
            window->m_pageCount=result.pageCount;window->m_currentPage=0;window->m_targetPage=0;
            window->m_pageAnnotations=result.annotations;window->m_savedAnnotations=result.annotations;window->m_geometry=result.geometry;
            window->m_dirty=false;++window->m_documentGeneration;window->m_contentRevision=0;window->m_backedUpRevision=0;window->m_backupWarning.clear();
            window->m_editUndo.clear();window->m_editRedo.clear();window->m_historyBytes=0;
            window->m_viewHistory.clear();window->m_viewHistoryIndex=-1;window->m_lastViewReason.clear();
            window->m_sidebarReady.clear();window->m_sidebarIcons.clear();window->m_sidebarPendingPage=-1;
            window->m_updatingUi=true;window->m_pages->clear();
            for(int page=0;page<result.pageCount;++page)window->m_pages->addItem(QStringLiteral("第 %1 页").arg(page+1));
            window->m_updatingUi=false;
            const QString layout=window->m_preferences.tools.layout;
            window->m_view->setLayoutMode(layout==QStringLiteral("single")?DocumentView::LayoutMode::SinglePage:
                layout==QStringLiteral("facing")?DocumentView::LayoutMode::FacingPages:
                layout==QStringLiteral("facing-continuous")?DocumentView::LayoutMode::FacingContinuous:DocumentView::LayoutMode::SingleContinuous);
            window->m_view->setDocument(result.geometry,result.annotations);
            window->m_restoringView=false;
            window->viewChanged(window->m_view->captureViewState(),QStringLiteral("document"));
            window->finishFileOperation();window->scheduleBackup();window->m_sidebarPreviewTimer->start();
            window->statusBar()->showMessage(result.readOnly?QStringLiteral("PDF 以只读方式打开。"):QStringLiteral("PDF 已打开。"));
        },Qt::QueuedConnection);
    },Qt::QueuedConnection);
}
void MainWindow::finishFileOperation()
{
    m_fileBusy = false;
    m_view->setRenderingSuspended(false);
    const bool resumeRender = std::exchange(m_resumeRenderAfterSaveFailure, false);
    updateUi();
    m_sidebarPreviewTimer->start();
    if (resumeRender && m_pageCount > 0 && !m_rendering && !m_allowClose)
        m_renderTimer->start();
}

void MainWindow::saveFile(bool saveAs, std::function<void()> afterSuccess)
{
    if (m_fileBusy || m_rendering || m_backupBusy || m_preferencesBusy || m_pageCount < 1 || m_readOnly)
        return;
    m_view->finishGesture();
    QString target = m_path;
    if (saveAs || target.isEmpty()) {
        target = QFileDialog::getSaveFileName(this, QStringLiteral("另存 PDF"), m_path,
                                            QStringLiteral("PDF 文件 (*.pdf)"), nullptr,
                                            QFileDialog::DontConfirmOverwrite);
        if (target.isEmpty())
            return;
        if (!target.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
            target += QStringLiteral(".pdf");
        saveAs = true;
    }
    m_afterSave = std::move(afterSuccess);
    m_fileBusy = true;
    m_renderTimer->stop();
    m_sidebarPreviewTimer->stop();
    m_view->setRenderingSuspended(true);
    for (const auto& ticket : std::as_const(m_renderTickets)) ticket->store(true);
    m_renderTickets.clear();
    m_sidebarRequestBusy = false;
    updateUi();
    const auto snapshot = m_pageAnnotations;
    if (!saveAs) {
        performSave(target, {}, snapshot);
        return;
    }
    statusBar()->showMessage(QStringLiteral("正在核对另存目标…"));
    const QPointer<MainWindow> window(this);
    QMetaObject::invokeMethod(m_workerContext, [window, target, snapshot] {
        QString error;
        QByteArray hash;
        bool exists = false;
        try {
            exists = QFileInfo::exists(target);
            if (exists)
                hash = PdfDocument::fileSha256(target, &error);
            if (exists && hash.isEmpty() && error.isEmpty())
                error = QStringLiteral("无法核对已有目标文件，未覆盖。");
        } catch (...) { error = exceptionText(); }
        if (!window)
            return;
        QMetaObject::invokeMethod(window.data(), [window, target, snapshot, exists, hash, error] {
            if (!window)
                return;
            if (!error.isEmpty()) {
                window->m_afterSave = {};
                window->finishFileOperation();
                window->showError(error);
                return;
            }
            if (exists) {
                QMessageBox box(QMessageBox::Question, QStringLiteral("覆盖已有 PDF"), {}, QMessageBox::Yes | QMessageBox::No, window.data());
                box.setTextFormat(Qt::PlainText);
                box.setText(QStringLiteral("目标文件已存在：\n%1\n\n是否用当前 PDF 覆盖？若确认后目标改变，将停止保存。").arg(target));
                box.setDefaultButton(QMessageBox::No);
                if (box.exec() != QMessageBox::Yes) {
                    window->m_afterSave = {};
                    window->finishFileOperation();
                    return;
                }
            }
            window->performSave(target, hash, snapshot);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void MainWindow::performSave(const QString& path, const QByteArray& expectedTargetSha256,
                              const QMap<int, PageAnnotations>& snapshot)
{
    statusBar()->showMessage(QStringLiteral("正在保存 PDF…"));
    const auto session = m_session;
    const QPointer<MainWindow> window(this);
    QMetaObject::invokeMethod(m_workerContext, [session, window, path, expectedTargetSha256, snapshot] {
        bool ok = false;
        QString error;
        QString savedPath;
        try {
            if (!session->document)
                error = QStringLiteral("文档已不可用，当前笔迹仍保留。");
            else {
                bool valid = true;
                for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it) {
                    if (!session->document->setAnnotations(it.key(), it.value())) {
                        valid = false;
                        error = session->document->lastError();
                        break;
                    }
                }
                if (valid) {
                    ok = session->document->save(path, expectedTargetSha256);
                    error = ok ? QString() : session->document->lastError();
                    savedPath = session->document->currentPath();
                }
            }
        } catch (...) { error = exceptionText(); }
        if (!window)
            return;
        QMetaObject::invokeMethod(window.data(), [window, ok, savedPath, error, snapshot] {
            if (!window)
                return;
            auto continuation = std::move(window->m_afterSave);
            window->m_afterSave = {};
            if (ok) {
                if (window->m_path.compare(savedPath, Qt::CaseInsensitive) != 0)
                    window->m_backedUpRevision = 0;
                window->m_path = savedPath;
                window->m_savedAnnotations = snapshot;
                window->m_dirty = window->m_pageAnnotations != snapshot;
                window->m_resumeRenderAfterSaveFailure = false;
            }
            window->finishFileOperation();
            if (!ok) {
                window->showError(error.isEmpty() ? QStringLiteral("保存未获确认，当前笔迹仍保留。请核对目标文件。") : error);
                return;
            }
            window->statusBar()->showMessage(QStringLiteral("PDF 已保存 · 重新打开可继续擦除和修改本原型笔迹"));
            if (continuation)
                continuation();
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_allowClose) {
        event->accept();
        return;
    }
    if (m_fileBusy || m_rendering || m_backupBusy || m_preferencesBusy || m_settingsDialog || m_thumbnailBrowser) {
        event->ignore();
        statusBar()->showMessage(QStringLiteral("PDF 正在处理，请完成后再关闭。"));
        return;
    }
    if (m_sidebarTimer->isActive() && m_preferencesWritable) {
        event->ignore();
        auto values = m_preferences;
        values.sidebarWidth = std::clamp(m_splitter->sizes().value(0, values.sidebarWidth), 112, 400);
        m_closeAfterPreferences = true;
        persistPreferences(values);
        return;
    }
    m_view->finishGesture();
    if (!m_dirty) {
        event->accept();
        return;
    }
    event->ignore();
    resolveUnsaved([this] {
        m_allowClose = true;
        close();
    });
}

} // namespace propernotes
