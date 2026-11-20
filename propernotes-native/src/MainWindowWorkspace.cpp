// SPDX-License-Identifier: GPL-3.0-only
#include "MainWindowState.h"
#include "DocumentSession.h"
#include "FavoritesBar.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabBar>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace propernotes {
namespace {
WorkspaceViewState storedView(const DocumentView::ViewState& view) {
    WorkspaceViewState result;
    result.page=view.page; result.pdfAnchor=view.pdfAnchor; result.viewportAnchor=view.viewportAnchor;
    result.zoom=view.zoom; result.valid=view.valid;
    result.layout=view.layout==DocumentView::LayoutMode::SinglePage?QStringLiteral("single"):
        view.layout==DocumentView::LayoutMode::FacingPages?QStringLiteral("facing"):
        view.layout==DocumentView::LayoutMode::FacingContinuous?QStringLiteral("facing-continuous"):QStringLiteral("single-continuous");
    result.zoomMode=view.zoomMode==DocumentView::ZoomMode::Free?QStringLiteral("free"):
        view.zoomMode==DocumentView::ZoomMode::FitPage?QStringLiteral("fit-page"):
        view.zoomMode==DocumentView::ZoomMode::FitHeight?QStringLiteral("fit-height"):QStringLiteral("fit-width");
    return result;
}
DocumentView::ViewState liveView(const WorkspaceViewState& view) {
    DocumentView::ViewState result;
    result.page=view.page; result.pdfAnchor=view.pdfAnchor; result.viewportAnchor=view.viewportAnchor;
    result.zoom=view.zoom; result.valid=view.valid;
    result.layout=view.layout==QStringLiteral("single")?DocumentView::LayoutMode::SinglePage:
        view.layout==QStringLiteral("facing")?DocumentView::LayoutMode::FacingPages:
        view.layout==QStringLiteral("facing-continuous")?DocumentView::LayoutMode::FacingContinuous:DocumentView::LayoutMode::SingleContinuous;
    result.zoomMode=view.zoomMode==QStringLiteral("free")?DocumentView::ZoomMode::Free:
        view.zoomMode==QStringLiteral("fit-page")?DocumentView::ZoomMode::FitPage:
        view.zoomMode==QStringLiteral("fit-height")?DocumentView::ZoomMode::FitHeight:DocumentView::ZoomMode::FitWidth;
    return result;
}
}

void MainWindow::buildWorkspaceUi() {
    m_tabsBar=new QTabBar;
    m_tabsBar->setExpanding(false); m_tabsBar->setMovable(true); m_tabsBar->setTabsClosable(true);
    m_tabsBar->setDocumentMode(true); m_tabsBar->setElideMode(Qt::ElideMiddle);
    static_cast<QVBoxLayout*>(centralWidget()->layout())->insertWidget(0,m_tabsBar);
    connect(m_tabsBar,&QTabBar::currentChanged,this,[this](int index){
        if(m_switchingTab||index<0)return;
        const quint64 id=m_tabsBar->tabData(index).toULongLong();
        QTimer::singleShot(0,this,[this,id]{for(int i=0;i<m_tabs.size();++i)if(m_tabs[i]->id==id){activateTab(i);break;}});
    });
    connect(m_tabsBar,&QTabBar::tabCloseRequested,this,&MainWindow::closeTab);
    connect(m_tabsBar,&QTabBar::tabBarDoubleClicked,this,[this](int index){if(index>=0)activateTab(index);});
    connect(m_tabsBar,&QTabBar::tabMoved,this,[this](int from,int to){
        if(m_switchingTab||from<0||to<0||from>=m_tabs.size()||to>=m_tabs.size())return;
        const auto active=m_activeTab>=0?m_tabs[m_activeTab]:nullptr;
        m_tabs.move(from,to); m_activeTab=int(m_tabs.indexOf(active));scheduleWorkspaceSave();
    });
    auto* file=menuBar()->addMenu(QStringLiteral("文件"));
    file->addAction(m_openAction); m_recentMenu=file->addMenu(QStringLiteral("最近文件"));
    m_recentAction=m_recentMenu->menuAction(); connect(m_recentMenu,&QMenu::aboutToShow,this,&MainWindow::refreshRecentMenu);
    file->addSeparator();file->addAction(m_saveAction);file->addAction(m_saveAsAction);
    m_closeTabAction=file->addAction(QStringLiteral("关闭当前标签"));m_closeTabAction->setShortcut(QKeySequence::Close);
    connect(m_closeTabAction,&QAction::triggered,this,[this]{closeTab(m_activeTab);});
    auto* retry=file->addAction(QStringLiteral("重试加载当前标签"));
    connect(retry,&QAction::triggered,this,[this]{if(m_activeTab>=0&&!m_tabs[m_activeTab]->loaded)activateTab(m_activeTab);});
    m_restoreSessionAction=file->addAction(QStringLiteral("启动时恢复上次标签"));m_restoreSessionAction->setCheckable(true);
    connect(m_restoreSessionAction,&QAction::triggered,this,[this](bool on){
        m_workspace->values.restoreEnabled=on;scheduleWorkspaceSave();
    });
    auto* reload=file->addAction(QStringLiteral("重新读取阅读与会话记录"));
    connect(reload,&QAction::triggered,this,[this]{
        if(!m_workspaceBusy&&!m_fileBusy&&!m_backupBusy&&!m_preferencesBusy&&!m_closingAll)loadWorkspace();
    });
    auto addCycle=[this](const QKeySequence& shortcut,int direction){
        auto* action=new QAction(this);action->setShortcut(shortcut);addAction(action);
        connect(action,&QAction::triggered,this,[this,direction]{if(!m_tabs.isEmpty())activateTab((m_activeTab+direction+int(m_tabs.size()))%int(m_tabs.size()));});
    };
    addCycle(QKeySequence(Qt::CTRL|Qt::Key_Tab),1);addCycle(QKeySequence(Qt::CTRL|Qt::SHIFT|Qt::Key_Tab),-1);
    m_workspaceTimer=new QTimer(this);m_workspaceTimer->setSingleShot(true);m_workspaceTimer->setInterval(650);
    connect(m_workspaceTimer,&QTimer::timeout,this,[this]{saveWorkspace();});
    m_favoritesBar=new FavoritesBar(this);
    addDockWidget(Qt::LeftDockWidgetArea,m_favoritesBar);
    auto* view=menuBar()->addMenu(QStringLiteral("视图"));
    view->addAction(m_favoritesBar->visibilityAction());view->addAction(m_sidebarAction);view->addAction(m_overviewAction);
    connect(m_favoritesBar,&FavoritesBar::activated,this,&MainWindow::applyFavorite);
    connect(m_favoritesBar,&FavoritesBar::addCurrentRequested,this,&MainWindow::addCurrentFavorite);
    connect(m_favoritesBar,&FavoritesBar::favoritesEdited,this,[this](const QVector<FavoriteTool>& favorites){auto values=m_preferences;values.favorites=favorites;persistPreferences(values);});
    connect(m_favoritesBar,&FavoritesBar::barPreferencesEdited,this,[this](const FavoritesBarPreferences& bar){
        if(!m_preferencesWritable||m_closingAll)return;
        if(m_preferencesBusy||m_backupBusy||m_fileBusy){m_liveFavorite->queuedBar=bar;return;}
        auto values=m_preferences;values.favoritesBar=bar;persistPreferences(values);
    });
    applyPreferences(m_preferences);
}

void MainWindow::cancelActiveRenders() {
    m_renderTimer->stop();m_sidebarPreviewTimer->stop();m_renderEpoch->fetch_add(1);
    for(const auto& ticket:std::as_const(m_renderTickets))ticket->store(true);
    m_renderTickets.clear();m_sidebarRequestBusy=false;m_sidebarPendingPage=-1;
    m_view->setRenderingSuspended(true);
}
void MainWindow::stashActiveTab() {
    if(m_activeTab<0||m_activeTab>=m_tabs.size()||!m_tabs[m_activeTab]->loaded)return;
    auto& tab=*m_tabs[m_activeTab];
    tab.path=m_path;tab.readOnly=m_readOnly;tab.readOnlyReason=m_readOnlyReason;tab.backupWarning=m_backupWarning;
    tab.annotations=m_pageAnnotations;tab.savedAnnotations=m_savedAnnotations;tab.geometry=m_geometry;
    tab.undo=m_editUndo;tab.redo=m_editRedo;tab.historyBytes=m_historyBytes;
    tab.viewHistory=m_viewHistory;tab.viewHistoryIndex=m_viewHistoryIndex;tab.viewState=m_view->captureViewState();
    tab.dirty=m_dirty;tab.contentRevision=m_contentRevision;tab.backedUpRevision=m_backedUpRevision;
    if(m_workspaceLoaded){
        ReadingEntry entry;entry.path=tab.path;entry.view=storedView(tab.viewState);entry.hadUnsavedChanges=tab.dirty;
        for(const auto& old:m_workspace->values.readingStates)if(WorkspaceStore::pathKey(old.path)==WorkspaceStore::pathKey(tab.path)){
            entry.fileSize=old.fileSize;entry.modifiedUtc=old.modifiedUtc;break;
        }
        QString error;if(!WorkspaceStore::rememberReading(m_workspace->values,entry,&error))m_workspaceWarning=error;
    }
}
void MainWindow::clearActiveDocument() {
    resetTextSearch(true);
    m_restoringView=true;m_view->clearDocument();m_restoringView=false;
    m_path.clear();m_readOnlyReason.clear();m_backupWarning.clear();m_geometry.clear();
    m_pageAnnotations.clear();m_savedAnnotations.clear();m_editUndo.clear();m_editRedo.clear();m_historyBytes=0;
    m_viewHistory.clear();m_viewHistoryIndex=-1;m_lastViewReason.clear();
    m_pageCount=0;m_currentPage=-1;m_targetPage=-1;m_dirty=false;m_readOnly=false;
    m_contentRevision=0;m_backedUpRevision=0;m_sidebarReady.clear();m_sidebarIcons.clear();
    {const QSignalBlocker block(m_pages);m_pages->clear();}
}
void MainWindow::activateTab(int index,bool load) {
    if(index<0||index>=m_tabs.size())return;
    if(m_fileBusy||m_backupBusy||m_preferencesBusy||m_workspaceLoading||m_settingsDialog||m_thumbnailBrowser||
       (m_closingAll&&load)) {refreshTabs();return;}
    if(index==m_activeTab&&m_tabs[index]->loaded){refreshTabs();return;}
    m_view->finishGesture();stashActiveTab();cancelActiveRenders();++m_documentGeneration;++m_toolSerial;
    clearActiveDocument();m_activeTab=index;
    const auto tab=m_tabs[index];m_session=tab->session?tab->session:m_services;
    m_path=tab->path;m_readOnlyReason=tab->readOnlyReason;
    if(tab->loaded) {
        m_pageAnnotations=tab->annotations;m_savedAnnotations=tab->savedAnnotations;m_geometry=tab->geometry;
        m_editUndo=tab->undo;m_editRedo=tab->redo;m_historyBytes=tab->historyBytes;
        m_viewHistory=tab->viewHistory;m_viewHistoryIndex=tab->viewHistoryIndex;
        m_dirty=tab->dirty;m_readOnly=tab->readOnly;m_backupWarning=tab->backupWarning;
        m_contentRevision=tab->contentRevision;m_backedUpRevision=tab->backedUpRevision;
        m_pageCount=int(m_geometry.size());m_restoringView=true;
        {const QSignalBlocker block(m_pages);for(int page=0;page<m_pageCount;++page)m_pages->addItem(QStringLiteral("第 %1 页").arg(page+1));}
        m_view->setDocument(m_geometry,m_pageAnnotations);
        if(tab->viewState.valid)m_view->restoreViewState(tab->viewState);
        m_currentPage=m_view->currentPage();m_targetPage=m_currentPage;m_restoringView=false;
        m_view->setRenderingSuspended(false);m_sidebarPreviewTimer->start();
    }
    refreshTabs();updateUi();scheduleBackup();scheduleWorkspaceSave();
    if(!tab->loaded&&load){m_openingTab=index;beginOpen(tab->path);}
}
void MainWindow::refreshTabs() {
    if(!m_tabsBar)return;
    m_switchingTab=true;const QSignalBlocker block(m_tabsBar);
    while(m_tabsBar->count()>m_tabs.size())m_tabsBar->removeTab(m_tabsBar->count()-1);
    for(int i=0;i<m_tabs.size();++i){
        const auto& tab=*m_tabs[i];const bool dirty=i==m_activeTab?m_dirty:tab.dirty;
        QString name=QFileInfo(tab.path).fileName();name.replace(QLatin1Char('&'),QStringLiteral("&&"));
        if(dirty)name.prepend(QStringLiteral("● "));
        if(!tab.loaded)name.append(tab.loadError.isEmpty()?QStringLiteral(" · 待加载"):QStringLiteral(" · 重试"));
        if(i>=m_tabsBar->count())m_tabsBar->addTab(name);else m_tabsBar->setTabText(i,name);
        m_tabsBar->setTabData(i,QVariant::fromValue(tab.id));
        m_tabsBar->setTabToolTip(i,tab.path+(tab.loadError.isEmpty()?QString():QLatin1Char('\n')+tab.loadError));
    }
    m_tabsBar->setCurrentIndex(m_activeTab);m_switchingTab=false;
    m_tabsBar->setEnabled(!m_fileBusy&&!m_backupBusy&&!m_preferencesBusy&&!m_workspaceLoading&&!m_closingAll&&!m_thumbnailBrowser);
    m_closeTabAction->setEnabled(m_activeTab>=0&&!m_fileBusy&&!m_backupBusy&&!m_preferencesBusy&&!m_closingAll&&!m_thumbnailBrowser);
    m_recentAction->setEnabled(!m_fileBusy&&!m_backupBusy&&!m_preferencesBusy&&!m_closingAll&&!m_thumbnailBrowser);
    m_restoreSessionAction->setEnabled(m_workspaceLoaded&&m_workspaceWritable&&!m_workspaceLoading&&!m_closingAll);
    m_restoreSessionAction->setChecked(m_workspace->values.restoreEnabled);
}
void MainWindow::closeTab(int index) {
    if(index<0||index>=m_tabs.size()||m_closingAll||m_fileBusy||m_backupBusy||m_preferencesBusy||m_workspaceLoading||m_thumbnailBrowser||m_settingsDialog)return;
    activateTab(index,false);m_view->finishGesture();
    const auto tab=m_tabs[index];
    resolveUnsaved([this,tab]{
        const int position=int(m_tabs.indexOf(tab));if(position<0)return;
        stashActiveTab();scheduleWorkspaceSave();cancelActiveRenders();
        m_tabs.removeAt(position);m_activeTab=-1;clearActiveDocument();m_session=m_services;++m_documentGeneration;
        if(tab->session){const auto session=tab->session;QMetaObject::invokeMethod(m_workerContext,[session]{session->document.reset();},Qt::QueuedConnection);}
        if(!m_tabs.isEmpty())activateTab(std::min(position,int(m_tabs.size())-1));
        else {refreshTabs();updateUi();}
        scheduleWorkspaceSave();
    });
}
void MainWindow::closeAllTabs() {
    if(!m_closingAll){
        m_view->finishGesture();stashActiveTab();m_closingAll=true;m_closeCursor=0;
        m_closeOriginalTabId=m_activeTab>=0?m_tabs[m_activeTab]->id:0;updateUi();
    }
    while(m_closeCursor<m_tabs.size()){
        const int index=m_closeCursor;
        activateTab(index,false);
        if(!m_dirty){++m_closeCursor;continue;}
        QMessageBox box(QMessageBox::Question,QStringLiteral("退出前保存"),{},QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,this);
        box.setTextFormat(Qt::PlainText);box.setText(QStringLiteral("%1 有未保存修改，是否保存？").arg(m_path));box.setDefaultButton(QMessageBox::Save);
        const auto result=box.exec();
        if(result==QMessageBox::Discard){++m_closeCursor;continue;}
        if(result==QMessageBox::Save){
            saveFile(false,[this]{++m_closeCursor;closeAllTabs();});
            if(m_fileBusy)return;
        }
        m_closingAll=false;updateUi();return;
    }
    for(int i=0;i<m_tabs.size();++i)if(m_tabs[i]->id==m_closeOriginalTabId){activateTab(i,false);break;}
    stashActiveTab();
    if (m_review->cancelSearch) m_review->cancelSearch->store(true);
    cancelActiveRenders();
    saveWorkspace(true);
}

void MainWindow::finishWindowClose()
{
    if (m_allowClose) return;
    m_allowClose = true;
    // A read-only or unavailable workspace can finish inside the first
    // closeEvent. Retry after that ignored event has unwound.
    QTimer::singleShot(0, this, [this] { close(); });
}

void MainWindow::loadWorkspace() {
    if(m_workspaceBusy)return;
    m_workspaceBusy=true;m_workspaceLoading=true;updateUi();const bool initial=!m_workspaceLoaded;
    const QString root=QCoreApplication::applicationDirPath();
    if(!m_workspaceStore)m_workspaceStore=std::make_shared<WorkspaceStore>(root);
    const auto store=m_workspaceStore;const QPointer<MainWindow> window(this);
    QMetaObject::invokeMethod(m_workerContext,[store,window,initial]{
        const bool ok=store->load();const auto values=store->values();const auto error=store->lastError();const bool writable=store->canWrite();
        if(!window)return;
        QMetaObject::invokeMethod(window.data(),[window,ok,values,error,writable,initial]{
            if(!window)return;
            window->m_workspaceBusy=false;window->m_workspaceLoading=false;window->m_workspaceLoaded=true;window->m_workspaceWritable=writable;
            window->m_workspaceWarning=ok?QString():error;
            if(ok||initial)window->m_workspace->values=values;
            if(initial&&ok&&values.restoreEnabled){
                for(const auto& path:values.sessionPaths){
                    auto tab=std::make_shared<TabState>();tab->id=++window->m_nextTabId;tab->path=path;
                    for(const auto& entry:values.readingStates)if(WorkspaceStore::pathKey(entry.path)==WorkspaceStore::pathKey(path)){
                        tab->viewState=liveView(entry.view);tab->hadUnsavedChanges=entry.hadUnsavedChanges;break;
                    }
                    window->m_tabs.append(tab);
                }
            }
            const QString pending=std::exchange(window->m_pendingOpen,QString());
            if(!pending.isEmpty())window->openFile(pending);
            else if(initial&&!window->m_tabs.isEmpty()){
                int active=0;for(int i=0;i<window->m_tabs.size();++i)if(WorkspaceStore::pathKey(window->m_tabs[i]->path)==WorkspaceStore::pathKey(values.activePath)){active=i;break;}
                window->activateTab(active);
            }
            window->refreshTabs();window->updateUi();
            if(window->m_workspacePending)window->m_workspaceTimer->start();
        },Qt::QueuedConnection);
    },Qt::QueuedConnection);
}
void MainWindow::scheduleWorkspaceSave() {
    if(!m_workspaceLoaded)return;
    m_workspacePending=true;m_workspaceTimer->start();
}
void MainWindow::saveWorkspace(bool exiting) {
    m_workspaceExitPending|=exiting;m_workspaceTimer->stop();
    if(m_workspaceBusy){m_workspacePending=true;return;}
    if(!m_workspaceWritable){
        if(m_workspaceExitPending) finishWindowClose();
        return;
    }
    if(!exiting&&(m_fileBusy||m_preferencesBusy||m_backupBusy||m_view->hasActiveGesture()||m_view->isTextEditing()||m_settingsDialog||m_thumbnailBrowser)){
        m_workspaceTimer->start();return;
    }
    stashActiveTab();auto values=m_workspace->values;QString error;
    QStringList paths;
    for(const auto& tab:m_tabs)paths.append(tab->path);
    WorkspaceStore::setSession(values,paths,m_activeTab>=0?m_tabs[m_activeTab]->path:QString(),&error);
    for(const auto& tab:m_tabs){
        if(!error.isEmpty())break;
        if(tab->loaded){
            ReadingEntry entry;entry.path=tab->path;entry.view=storedView(tab->viewState);entry.hadUnsavedChanges=tab->dirty;
            // Hints from successful open/save are not file identity.
            for(const auto& old:values.readingStates)if(WorkspaceStore::pathKey(old.path)==WorkspaceStore::pathKey(entry.path)){
                entry.fileSize=old.fileSize;entry.modifiedUtc=old.modifiedUtc;break;
            }
            if(!WorkspaceStore::rememberReading(values,entry,&error))break;
        }
    }
    if(error.isEmpty())WorkspaceStore::setSession(values,paths,m_activeTab>=0?m_tabs[m_activeTab]->path:QString(),&error);
    if(!error.isEmpty()){
        m_workspaceWarning=error;updateUi();
        if(m_workspaceExitPending){showError(error+QStringLiteral("\n阅读记录未保存；PDF 的保存结果不受影响。"));finishWindowClose();}
        return;
    }
    m_workspace->values=values;m_workspacePending=false;m_workspaceBusy=true;
    const auto store=m_workspaceStore;const QPointer<MainWindow> window(this);
    QMetaObject::invokeMethod(m_workerContext,[store,values,window]{
        const bool ok=store->save(values);const QString error=store->lastError();const bool writable=store->canWrite();
        if(!window)return;
        QMetaObject::invokeMethod(window.data(),[window,ok,error,writable]{
            if(!window)return;
            window->m_workspaceBusy=false;window->m_workspaceWritable=writable;window->m_workspaceWarning=ok?QString():error;window->updateUi();
            if(window->m_workspaceExitPending){
                if(window->m_workspacePending&&ok)window->saveWorkspace(true);
                else {if(!ok)window->showError(error+QStringLiteral("\n阅读记录未保存；PDF 的保存结果不受影响。"));window->finishWindowClose();}
            }else if(window->m_workspacePending)window->m_workspaceTimer->start();
        },Qt::QueuedConnection);
    },Qt::QueuedConnection);
}
void MainWindow::rememberOpenedFile(bool savedAs,const QString& oldPath) {
    if(!m_workspaceLoaded||m_path.isEmpty())return;
    auto& values=m_workspace->values;QString error;
    if(savedAs&&!oldPath.isEmpty()&&WorkspaceStore::pathKey(oldPath)!=WorkspaceStore::pathKey(m_path)){
        // Keep the source's independent last reading record when Save As changes identity.
        ReadingEntry old;old.path=oldPath;old.view=storedView(m_view->captureViewState());
        for(const auto& previous:values.readingStates)if(WorkspaceStore::pathKey(previous.path)==WorkspaceStore::pathKey(oldPath)){old.fileSize=previous.fileSize;old.modifiedUtc=previous.modifiedUtc;break;}
        WorkspaceStore::rememberReading(values,old,&error);
    }
    WorkspaceStore::recordOpened(values,m_path,QDateTime::currentDateTimeUtc(),&error);
        if(!error.isEmpty())m_workspaceWarning=error;
    if(m_activeTab>=0)m_tabs[m_activeTab]->path=m_path;
    scheduleWorkspaceSave();refreshTabs();
}
void MainWindow::refreshRecentMenu() {
    m_recentMenu->clear();
    if(m_workspace->values.recent.isEmpty())m_recentMenu->addAction(QStringLiteral("没有最近文件"))->setEnabled(false);
    for(const auto& entry:m_workspace->values.recent){
        int page=-1;for(const auto& reading:m_workspace->values.readingStates)if(WorkspaceStore::pathKey(reading.path)==WorkspaceStore::pathKey(entry.path)){page=reading.view.page;break;}
        QString name=QFileInfo(entry.path).fileName();name.replace(QLatin1Char('&'),QStringLiteral("&&"));
        auto* item=m_recentMenu->addMenu(name+(page>=0?QStringLiteral(" · 第 %1 页").arg(page+1):QString()));
        item->setToolTip(entry.path+QLatin1Char('\n')+entry.openedUtc.toLocalTime().toString(Qt::ISODate));
        auto* path=item->addAction(entry.path);path->setEnabled(false);
        auto* time=item->addAction(entry.openedUtc.toLocalTime().toString(Qt::ISODate));time->setEnabled(false);
        connect(item->addAction(QStringLiteral("打开 / 激活")),&QAction::triggered,this,[this,path=entry.path]{openFile(path);});
        auto* remove=item->addAction(QStringLiteral("从最近列表移除"));remove->setEnabled(m_workspaceWritable&&!m_workspaceLoading);
        connect(remove,&QAction::triggered,this,[this,path=entry.path]{if(m_workspaceLoading)return;WorkspaceStore::removeRecent(m_workspace->values,path);scheduleWorkspaceSave();});
    }
    m_recentMenu->addSeparator();auto* clear=m_recentMenu->addAction(QStringLiteral("清空最近列表"));clear->setEnabled(m_workspaceWritable&&!m_workspace->values.recent.isEmpty());
    connect(clear,&QAction::triggered,this,[this]{if(m_workspaceLoading)return;WorkspaceStore::clearRecent(m_workspace->values);scheduleWorkspaceSave();});
}
bool MainWindow::targetUsedByOtherTab(const QString& path) const {
    const QString normalized=WorkspaceStore::normalizedPath(path);
    const QString canonical=QFileInfo(path).canonicalFilePath();
    const auto key=WorkspaceStore::pathKey(canonical.isEmpty()?normalized:canonical);
    for(int i=0;i<m_tabs.size();++i){
        if(i==m_activeTab)continue;
        const auto other=WorkspaceStore::pathKey(m_tabs[i]->path);
        if(other==key||other==WorkspaceStore::pathKey(normalized))return true;
        const QString physical=QFileInfo(m_tabs[i]->path).canonicalFilePath();
        if(!physical.isEmpty()&&WorkspaceStore::pathKey(physical)==key)return true;
    }
    return false;
}

ToolPreferences MainWindow::effectiveToolPreferences() const {
    auto tools=m_preferences.tools;auto color=m_preferences.penColor;qreal width=m_preferences.penWidthPt;
    if(m_liveFavorite&&m_liveFavorite->active)propernotes::applyFavorite(*m_liveFavorite->active,tools,color,width);
    return tools;
}
void MainWindow::clearFavoriteSelection() {if(m_liveFavorite)m_liveFavorite->active.reset();}
void MainWindow::applyFavorite(const FavoriteTool& favorite) {
    if(m_fileBusy||m_backupBusy||m_preferencesBusy||m_thumbnailBrowser||m_settingsDialog||m_closingAll)return;
    if (favorite.markupKind) {
        if (m_pageCount < 1 || m_readOnly) return;
        m_view->finishGesture();
        rememberPersistentTool();
        m_liveFavorite->active = favorite;
        m_review->kind = *favorite.markupKind;
        applyPreferences(m_preferences);
        chooseTool(InkCanvas::Tool::TextSelect, false);
        updateTextReviewUi();
        statusBar()->showMessage(QStringLiteral("先选正文，再点“%1”。").arg(textMarkupKindName(m_review->kind)), 4500);
        m_view->setFocus();
        return;
    }
    m_view->finishGesture();rememberPersistentTool();m_liveFavorite->active=favorite;applyPreferences(m_preferences);
    chooseTool(favorite.tool,false);
    rememberPersistentTool();
    updateToolUi();
}
void MainWindow::addCurrentFavorite() {
    if(!m_preferencesWritable||m_preferencesBusy||m_fileBusy||m_backupBusy||m_closingAll)return;
    if (m_tool == InkCanvas::Tool::TextSelect) { favoriteTextReview(); return; }
    m_view->finishGesture();
    bool ok=false;const QString name=QInputDialog::getText(this,QStringLiteral("收藏当前工具"),QStringLiteral("预设名称"),QLineEdit::Normal,favoriteToolName(m_tool),&ok);
    if(!ok||name.trimmed().isEmpty())return;
    auto values=m_preferences;values.favorites.append(captureFavorite(m_tool,effectiveToolPreferences(),m_color,m_width->value(),name.trimmed()));
    persistPreferences(values);
}
} // namespace propernotes
