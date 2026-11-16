// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "InkData.h"
#include "AppPreferences.h"
#include "BackupStore.h"
#include "DocumentView.h"

#include <QMainWindow>
#include <QMap>
#include <QByteArray>
#include <QPointer>
#include <atomic>
#include <functional>
#include <memory>

class QAction;
class QCloseEvent;
class QDoubleSpinBox;
class QComboBox;
class QCheckBox;
class QFontComboBox;
class QLabel;
class QListWidget;
class QObject;
class QScrollArea;
class QSpinBox;
class QSplitter;
class QThread;
class QTimer;
class QTabBar;
class QMenu;

namespace propernotes {
class InkCanvas;
class SettingsDialog;
class ThumbnailBrowser;
struct DocumentSession;
class WorkspaceStore;
class FavoritesBar;
struct FavoriteTool;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    void openFile(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    void updateUi();
    void showError(const QString& message);
    void chooseOpenFile();
    void beginOpen(const QString& path);
    void resolveUnsaved(const std::function<void()>& continuation);
    void saveFile(bool saveAs, std::function<void()> afterSuccess = {});
    void performSave(const QString& path, const QByteArray& expectedTargetSha256,
                     const QMap<int, PageAnnotations>& snapshot);
    void goToPage(int pageIndex);
    void requestRender();
    void changeZoom(qreal zoom, const QPointF& viewportAnchor);
    void fitWidth();
    void commitAnnotations(int page, const PageAnnotations& before, const PageAnnotations& after, const QString& reason, const QUuid& focus);
    void editHistory(bool redo);
    void chooseTool(InkCanvas::Tool tool, bool userChoice = true);
    void toolCompleted(InkCanvas::Tool tool, const QString& reason);
    void updateToolUi();
    bool isSingleUse(InkCanvas::Tool tool) const;
    void viewChanged(const DocumentView::ViewState& state, const QString& reason);
    void navigateHistory(int direction);
    void openPageOverview();
    void requestPageRender(int page, qreal scale, quint64 id, int route);
    void cancelPageRender(quint64 id, int route);
    void refreshSidebarPreview();
    void finishFileOperation();
    void loadPreferences(bool reopenSettings = false);
    void applyPreferences(const AppPreferenceValues& values);
    void applyTheme();
    void persistPreferences(AppPreferenceValues values, bool migrate = false);
    void openSettings(bool backupManager = false);
    void scheduleBackup(int delayMs = -1);
    void createBackup(bool manual);
    void refreshBackupCatalog();
    void cleanupBackups(const QVector<BackupGroupKey>& groups);
    void restoreBackup(const BackupSnapshotKey& snapshot, const QString& suggestedName);
    void finishBackupOperation();
    void buildWorkspaceUi();
    void stashActiveTab();
    void activateTab(int index, bool load = true);
    void closeTab(int index);
    void closeAllTabs();
    void refreshTabs();
    void clearActiveDocument();
    void cancelActiveRenders();
    void loadWorkspace();
    void scheduleWorkspaceSave();
    void saveWorkspace(bool exiting = false);
    void refreshRecentMenu();
    void rememberOpenedFile(bool savedAs = false, const QString& oldPath = {});
    bool targetUsedByOtherTab(const QString& path) const;
    void applyFavorite(const FavoriteTool& favorite);
    void addCurrentFavorite();
    void clearFavoriteSelection();
    ToolPreferences effectiveToolPreferences() const;
    qreal effectivePenWidth() const;
    void rememberPersistentTool();
    void beginPendingDefaultChange();
    bool finishPendingDefaultChange(bool success);

    DocumentView* m_view = nullptr;
    QListWidget* m_pages = nullptr;
    QSpinBox* m_pageNumber = nullptr;
    QDoubleSpinBox* m_width = nullptr;
    QDoubleSpinBox* m_opacity = nullptr;
    QDoubleSpinBox* m_eraserSize = nullptr;
    QDoubleSpinBox* m_textSize = nullptr;
    QSpinBox* m_sensitivity = nullptr;
    QCheckBox* m_pressure = nullptr;
    QComboBox* m_layout = nullptr;
    QComboBox* m_eraseMode = nullptr;
    QFontComboBox* m_textFamily = nullptr;
    QLabel* m_fileLabel = nullptr;
    QLabel* m_zoomLabel = nullptr;
    QLabel* m_pageLabel = nullptr;
    QLabel* m_documentBanner = nullptr;
    QLabel* m_preferencesBanner = nullptr;
    QSplitter* m_splitter = nullptr;
    QWidget* m_navigation = nullptr;
    QAction* m_openAction = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_saveAsAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_backAction = nullptr;
    QAction* m_forwardAction = nullptr;
    QAction* m_overviewAction = nullptr;
    QAction* m_selectAction = nullptr;
    QAction* m_textAction = nullptr;
    QAction* m_followAction = nullptr;
    QAction* m_lockAction = nullptr;
    QAction* m_fitPageAction = nullptr;
    QAction* m_fitHeightAction = nullptr;
    QAction* m_penAction = nullptr;
    QAction* m_eraserAction = nullptr;
    QAction* m_panAction = nullptr;
    QAction* m_previousAction = nullptr;
    QAction* m_nextAction = nullptr;
    QAction* m_zoomInAction = nullptr;
    QAction* m_zoomOutAction = nullptr;
    QAction* m_fitAction = nullptr;
    QAction* m_colorAction = nullptr;
    QAction* m_settingsAction = nullptr;
    QAction* m_backupsAction = nullptr;
    QAction* m_sidebarAction = nullptr;
    QTimer* m_renderTimer = nullptr;
    QTimer* m_sidebarTimer = nullptr;
    QTimer* m_backupTimer = nullptr;
    QTimer* m_sidebarPreviewTimer = nullptr;
    QTabBar* m_tabsBar = nullptr;
    QMenu* m_recentMenu = nullptr;
    QAction* m_recentAction = nullptr;
    QAction* m_closeTabAction = nullptr;
    QAction* m_restoreSessionAction = nullptr;
    FavoritesBar* m_favoritesBar = nullptr;
    QTimer* m_workspaceTimer = nullptr;
    QThread* m_workerThread = nullptr;
    QObject* m_workerContext = nullptr;
    std::shared_ptr<DocumentSession> m_session;
    std::shared_ptr<DocumentSession> m_services;
    std::shared_ptr<QVector<std::shared_ptr<DocumentSession>>> m_allSessions;
    std::shared_ptr<WorkspaceStore> m_workspaceStore;
    std::shared_ptr<std::atomic<quint64>> m_renderEpoch;
    QMap<int, PageAnnotations> m_pageAnnotations;
    QMap<int, PageAnnotations> m_savedAnnotations;
    QVector<PageGeometry> m_geometry;
    struct EditRecord { int page; PageAnnotations before; PageAnnotations after; QString reason; QUuid focus; qsizetype bytes; };
    QVector<EditRecord> m_editUndo;
    QVector<EditRecord> m_editRedo;
    struct TabState {
        quint64 id = 0;
        std::shared_ptr<DocumentSession> session;
        QString path;
        QString readOnlyReason;
        QString backupWarning;
        QString loadError;
        QMap<int, PageAnnotations> annotations;
        QMap<int, PageAnnotations> savedAnnotations;
        QVector<PageGeometry> geometry;
        QVector<EditRecord> undo, redo;
        qsizetype historyBytes = 0;
        QVector<DocumentView::ViewState> viewHistory;
        int viewHistoryIndex = -1;
        DocumentView::ViewState viewState;
        quint64 contentRevision = 0, backedUpRevision = 0;
        qint64 nextBackupMs = 0;
        bool dirty = false, readOnly = false, loaded = false;
        bool hadUnsavedChanges = false;
    };
    QVector<std::shared_ptr<TabState>> m_tabs;
    int m_activeTab = -1;
    int m_openingTab = -1;
    quint64 m_nextTabId = 0;
    bool m_switchingTab = false;
    bool m_closingAll = false;
    quint64 m_closeOriginalTabId = 0;
    int m_closeCursor = 0;
    bool m_workspaceLoaded = false;
    bool m_workspaceWritable = false;
    bool m_workspaceBusy = false;
    bool m_workspaceLoading = false;
    bool m_workspacePending = false;
    bool m_workspaceExitPending = false;
    QString m_workspaceWarning;
    struct WorkspaceState;
    std::unique_ptr<WorkspaceState> m_workspace;
    struct FavoriteState;
    std::unique_ptr<FavoriteState> m_liveFavorite;
    qsizetype m_historyBytes = 0;
    QVector<DocumentView::ViewState> m_viewHistory;
    int m_viewHistoryIndex = -1;
    bool m_restoringView = false;
    QString m_lastViewReason;
    qint64 m_lastViewChangeMs = 0;
    QHash<QString, std::shared_ptr<std::atomic_bool>> m_renderTickets;
    QPointer<ThumbnailBrowser> m_thumbnailBrowser;
    quint64 m_sidebarRequest = 0;
    bool m_sidebarRequestBusy = false;
    QSet<int> m_sidebarReady;
    QVector<int> m_sidebarIcons;
    int m_sidebarPendingPage = -1;
    InkCanvas::Tool m_tool = InkCanvas::Tool::Pen;
    quint64 m_toolSerial = 0;
    bool m_lastCommitAccepted = false;
    QString m_path;
    QString m_readOnlyReason;
    QString m_preferencesWarning;
    QString m_backupWarning;
    QString m_pendingOpen;
    AppPreferenceValues m_preferences;
    QPointer<SettingsDialog> m_settingsDialog;
    quint64 m_documentGeneration = 0;
    quint64 m_contentRevision = 0;
    quint64 m_backedUpRevision = 0;
    QColor m_color = QColor("#202833");
    int m_pageCount = 0;
    int m_currentPage = -1;
    int m_targetPage = -1;
    bool m_dirty = false;
    bool m_readOnly = false;
    bool m_fileBusy = false;
    bool m_rendering = false;
    bool m_updatingUi = false;
    bool m_allowClose = false;
    bool m_resumeRenderAfterSaveFailure = false;
    bool m_preferencesLoaded = false;
    bool m_preferencesValid = false;
    bool m_preferencesWritable = false;
    bool m_preferencesBusy = false;
    bool m_backupBusy = false;
    bool m_resumeRenderAfterPreferences = false;
    bool m_closeAfterPreferences = false;
    std::function<void()> m_afterSave;
};

} // namespace propernotes
