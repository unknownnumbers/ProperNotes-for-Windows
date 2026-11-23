// SPDX-License-Identifier: GPL-3.0-only
#include "MainWindowState.h"
#include "FavoritesBar.h"
#include "ReadingControls.h"
#include "ToolIcon.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <algorithm>

namespace propernotes {
void MainWindow::buildReaderChrome()
{
    // Keep the existing actions and transaction paths. Tool style editing now
    // lives in the unified palette instead of four permanent toolbar rows.
    const auto legacyBars = findChildren<QToolBar*>(QString(), Qt::FindDirectChildrenOnly);
    for (auto* toolbar : legacyBars) {
        for (auto* action : toolbar->actions()) if (!qobject_cast<QWidgetAction*>(action)) addAction(action);
        toolbar->hide(); removeToolBar(toolbar);
    }
    auto* readingHost=new QWidget;
    auto* readingRow=new QHBoxLayout(readingHost);readingRow->setContentsMargins(0,0,0,0);readingRow->setSpacing(0);
    const auto dockHost=[readingHost](const QString& name){
        auto* host=new QFrame(readingHost);host->setObjectName(name);host->setFixedWidth(80);host->setAutoFillBackground(true);
        auto* column=new QVBoxLayout(host);column->setContentsMargins(0,0,0,0);column->setSpacing(0);host->hide();return host;
    };
    auto* leftDock=dockHost(QStringLiteral("readerDockLeft"));
    auto* rightDock=dockHost(QStringLiteral("readerDockRight"));
    m_splitter->replaceWidget(1,readingHost);
    readingRow->addWidget(leftDock);readingRow->addWidget(m_view,1);readingRow->addWidget(rightDock);
    m_splitter->setStretchFactor(1,1);
    m_favoritesBar->setDockHosts(m_view->viewport(),leftDock,rightDock);
    auto* edit = menuBar()->addMenu(QStringLiteral("编辑"));
    edit->addAction(m_undoAction); edit->addAction(m_redoAction); edit->addSeparator();
    edit->addAction(m_cutObjectsAction); edit->addAction(m_copyObjectsAction); edit->addAction(m_pasteObjectsAction);
    edit->addSeparator(); edit->addAction(m_review->select); edit->addAction(m_review->copy); edit->addAction(m_review->find);
    addAction(m_review->find);
    auto* view = menuBar()->addMenu(QStringLiteral("阅读"));
    view->addAction(m_backAction); view->addAction(m_forwardAction); view->addSeparator();
    view->addAction(m_previousAction); view->addAction(m_nextAction); view->addAction(m_overviewAction);
    auto* viewMenu = new QMenu(this);
    for (auto* menu : {view, viewMenu}) {
        menu->addSeparator(); menu->addAction(m_fitAction); menu->addAction(m_fitPageAction); menu->addAction(m_fitHeightAction);
    }
    auto* layouts = new QActionGroup(this);
    for (int i = 0; i < m_layout->count(); ++i) {
        auto* action = new QAction(m_layout->itemText(i), layouts);
        action->setCheckable(true); action->setData(m_layout->itemData(i));
        viewMenu->addAction(action); view->addAction(action);
        connect(action, &QAction::triggered, this, [this, action] {
            const QString layout = action->data().toString();
            m_view->finishGesture();
            m_view->setLayoutMode(layout == QStringLiteral("single") ? DocumentView::LayoutMode::SinglePage
                : layout == QStringLiteral("facing") ? DocumentView::LayoutMode::FacingPages
                : layout == QStringLiteral("facing-continuous") ? DocumentView::LayoutMode::FacingContinuous
                : DocumentView::LayoutMode::SingleContinuous);
            auto values = m_preferences; values.tools.layout = layout; persistPreferences(values);
        });
    }
    connect(viewMenu, &QMenu::aboutToShow, this, [this, layouts] {
        for (auto* action : layouts->actions()) {
            action->setChecked(action->data() == m_layout->currentData()); action->setEnabled(m_layout->isEnabled());
        }
    });
    connect(view, &QMenu::aboutToShow, this, [this, layouts] {
        for (auto* action : layouts->actions()) {
            action->setChecked(action->data() == m_layout->currentData()); action->setEnabled(m_layout->isEnabled());
        }
    });
    m_readingControls = new ReadingControls(m_view->viewport(), {m_backAction,m_forwardAction,m_undoAction,m_redoAction},
                                           m_zoomOutAction, m_zoomInAction, viewMenu, this);
    for (auto* action : {m_backAction,m_forwardAction,m_undoAction,m_redoAction})
        connect(action, &QAction::triggered, m_readingControls, &ReadingControls::revealHistory);
    for (auto* action : {m_zoomInAction,m_zoomOutAction,m_fitAction,m_fitPageAction,m_fitHeightAction})
        connect(action, &QAction::triggered, m_readingControls, &ReadingControls::revealZoom);
    connect(m_view, &DocumentView::viewStateChanged, this, [this](const DocumentView::ViewState&, const QString& reason) {
        if (m_restoringView) return;
        if (reason == QStringLiteral("zoom") || reason.startsWith(QStringLiteral("fit"))) m_readingControls->revealZoom();
        else if (reason != QStringLiteral("resize")) m_readingControls->revealHistory();
    });
    connect(m_view, &DocumentView::annotationsCommitted, m_readingControls, &ReadingControls::revealHistory);

    auto* rail = new QFrame;
    rail->setObjectName(QStringLiteral("readerRail")); rail->setFixedWidth(56);
    auto* railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(6,8,6,8); railLayout->setSpacing(6);
    const auto railButton = [rail, railLayout](QAction* action) {
        auto* button = new QToolButton(rail); button->setDefaultAction(action);
        configureReaderIconButton(button);
        button->setAutoRaise(true);
        button->setAccessibleName(action->text()); railLayout->addWidget(button); return button;
    };
    railButton(m_openAction); railButton(m_saveAction);
    auto addPanel = [this, railButton](const QString& label, int index) {
        auto* action = new QAction(label, this); action->setCheckable(true);
        auto* button = railButton(action);
        connect(action, &QAction::triggered, this, [this,index] {
            const bool show = !m_preferences.sidebarVisible || m_navigationTabs->currentIndex() != index;
            m_navigationTabs->setCurrentIndex(index);
            if (show != m_preferences.sidebarVisible) {auto values=m_preferences;values.sidebarVisible=show;persistPreferences(values);}
            else refreshReaderChrome();
        });
        return button;
    };
    m_pagesRail = addPanel(QStringLiteral("页面预览"),0);
    m_annotationsRail = addPanel(QStringLiteral("批注列表"),1);
    railButton(m_review->find);
    railLayout->addStretch(1);
    railButton(m_followAction);
    m_palettePinAction = new QAction(QStringLiteral("固定收藏工具栏位置与高度"),this);
    m_palettePinAction->setCheckable(true); railButton(m_palettePinAction);
    connect(m_palettePinAction,&QAction::triggered,this,[this](bool on){m_favoritesBar->setPinned(on);});
    m_fullScreenAction = new QAction(QStringLiteral("全屏／恢复"),this); m_fullScreenAction->setCheckable(true);
    m_fullScreenAction->setShortcut(QKeySequence(Qt::Key_F11)); addAction(m_fullScreenAction); railButton(m_fullScreenAction);
    connect(m_fullScreenAction,&QAction::triggered,this,[this](bool on){
        if(on){setProperty("readerWasMaximized",isMaximized());showFullScreen();}
        else if(property("readerWasMaximized").toBool())showMaximized();
        else showNormal();
        refreshReaderChrome();
    });
    railButton(m_settingsAction);
    auto* settings = menuBar()->addMenu(QStringLiteral("设置")); settings->addAction(m_settingsAction); settings->addAction(m_backupsAction);
    view->addSeparator();view->addAction(m_fullScreenAction);view->addAction(m_followAction);
    auto* layout = static_cast<QVBoxLayout*>(centralWidget()->layout());
    const int index = layout->indexOf(m_splitter); layout->removeWidget(m_splitter);
    auto* reader = new QWidget;
    auto* row = new QHBoxLayout(reader); row->setContentsMargins(0,0,0,0); row->setSpacing(0);
    row->addWidget(rail); row->addWidget(m_splitter,1); layout->insertWidget(index,reader,1);
    connect(m_navigationTabs,&QTabWidget::currentChanged,this,[this]{refreshReaderChrome();});

    // Page selection stays available above the canvas, independent of the
    // transient zoom/history groups. It uses the same navigation command.
    auto* pages = new QFrame(m_view->viewport()); pages->setObjectName(QStringLiteral("readerPagePicker"));
    pages->setAutoFillBackground(true);pages->setAttribute(Qt::WA_StyledBackground);
    pages->setStyleSheet(QStringLiteral(
        "QFrame#readerPagePicker{background:palette(window);border:1px solid palette(mid);border-radius:12px;}"
        "QLineEdit#readerPageNumber{color:palette(text);background:transparent;border:0;padding:0 6px;margin:0;}"
        "QLineEdit#readerPageNumber:focus{background:palette(base);border:1px solid palette(highlight);border-radius:5px;}"
        "QLabel#readerPageTotal{color:palette(window-text);background:transparent;border:0;padding:0;}"));
    auto* pageRow = new QHBoxLayout(pages); pageRow->setContentsMargins(5,4,5,4); pageRow->setSpacing(4);
    // Styled QSpinBox::NoButtons can give its internal editor a near-zero
    // width in Qt 6.11. Use one explicit editor with a numeric validator.
    m_chromePageNumber = new QLineEdit;m_chromePageNumber->setObjectName(QStringLiteral("readerPageNumber"));
    m_chromePageNumber->setMinimumHeight(44);m_chromePageNumber->setAlignment(Qt::AlignCenter);
    m_chromePageNumber->setAccessibleName(QStringLiteral("跳转页码"));m_chromePageNumber->setInputMethodHints(Qt::ImhDigitsOnly);
    m_chromePageNumber->setMaxLength(10);
    m_chromePageValidator = new QIntValidator(1,1,m_chromePageNumber);m_chromePageNumber->setValidator(m_chromePageValidator);
    m_chromePageNumber->installEventFilter(this);
    m_chromePageTotal = new QLabel;m_chromePageTotal->setObjectName(QStringLiteral("readerPageTotal"));
    auto* overview = new QToolButton; overview->setDefaultAction(m_overviewAction);configureReaderIconButton(overview);
    pageRow->addWidget(m_chromePageNumber);pageRow->addWidget(m_chromePageTotal);pageRow->addWidget(overview);
    connect(m_chromePageNumber,&QLineEdit::textEdited,this,[this]{
        m_chromePageDraft=true;m_chromePageDraftGeneration=m_documentGeneration;
    });
    connect(m_chromePageNumber,&QLineEdit::editingFinished,this,&MainWindow::commitChromePageNumber);
    // A lightweight filter on the page picker tracks viewport resizing.
    pages->setProperty("readerPagePicker",true); m_view->viewport()->installEventFilter(this);
    refreshReaderChrome();
}
void MainWindow::commitChromePageNumber()
{
    if(!m_chromePageNumber||!m_chromePageDraft)return;
    const auto generation=m_chromePageDraftGeneration;
    const bool acceptable=m_chromePageNumber->hasAcceptableInput();
    bool converted=false;const int requested=m_chromePageValidator->locale().toInt(m_chromePageNumber->text(),&converted);
    m_chromePageDraft=false;
    if(generation==m_documentGeneration&&m_chromePageNumber->isEnabled()&&acceptable&&converted
        &&requested>=1&&requested<=m_pageCount&&requested!=m_currentPage+1)
        goToPage(requested-1);
    refreshReaderChrome();
}
void MainWindow::refreshReaderChrome()
{
    if (!m_palettePinAction) return;
    for (const auto& item : QList<QPair<QAction*,QString>>{{m_openAction,"open"},{m_saveAction,"save"},{m_settingsAction,"settings"},
            {m_followAction,"mouse"},{m_backAction,"back"},{m_forwardAction,"forward"},{m_undoAction,"undo"},{m_redoAction,"redo"},
            {m_zoomOutAction,"minus"},{m_zoomInAction,"zoom-in"},{m_overviewAction,"overview"},{m_review->find,"search"}})
        item.first->setIcon(uiIcon(item.second,palette()));
    m_palettePinAction->setChecked(m_preferences.favoritesBar.pinned);
    m_palettePinAction->setEnabled(m_preferencesWritable && !m_preferencesBusy && !m_backupBusy && !m_fileBusy && !m_settingsDialog && !m_closingAll);
    m_palettePinAction->setIcon(uiIcon(m_preferences.favoritesBar.pinned?QStringLiteral("pin-toolbar"):QStringLiteral("unpin-toolbar"),palette()));
    m_palettePinAction->setToolTip(m_preferences.favoritesBar.pinned?QStringLiteral("解除工具栏位置与高度固定"):QStringLiteral("固定工具栏位置与高度"));
    m_fullScreenAction->setChecked(isFullScreen());
    m_fullScreenAction->setIcon(uiIcon(isFullScreen()?QStringLiteral("restore"):QStringLiteral("fullscreen"),palette()));
    m_pagesRail->defaultAction()->setChecked(m_preferences.sidebarVisible && m_navigationTabs->currentIndex()==0);
    m_annotationsRail->defaultAction()->setChecked(m_preferences.sidebarVisible && m_navigationTabs->currentIndex()==1);
    m_pagesRail->defaultAction()->setEnabled(m_sidebarAction->isEnabled());
    m_annotationsRail->defaultAction()->setEnabled(m_sidebarAction->isEnabled());
    m_pagesRail->setIcon(uiIcon(QStringLiteral("pages"),palette()));
    m_annotationsRail->setIcon(uiIcon(QStringLiteral("annotations"),palette()));
    m_readingControls->setHideDelaySeconds(m_preferences.controlsHideDelaySeconds);
    m_readingControls->setZoom(m_view->zoom()); m_readingControls->refreshAppearance();
    if (m_chromePageNumber) {
        const bool enabled=m_pageCount>0&&m_pageNumber->isEnabled();
        const bool preserveDraft=m_chromePageDraft&&m_chromePageNumber->hasFocus()&&enabled
            &&m_chromePageDraftGeneration==m_documentGeneration;
        if(!preserveDraft)m_chromePageDraft=false;
        const QSignalBlocker block(m_chromePageNumber);
        m_chromePageValidator->setRange(1,std::max(1,m_pageCount));
        if(!preserveDraft)m_chromePageNumber->setText(m_pageCount>0?QString::number(std::max(1,m_currentPage+1)):QStringLiteral("--"));
        const int digits=int(QString::number(std::max(1,m_pageCount)).size());
        m_chromePageNumber->setFixedWidth(std::max(44,m_chromePageNumber->fontMetrics().horizontalAdvance(QString(digits,QLatin1Char('8')))+24));
        m_chromePageNumber->setEnabled(enabled);
        m_chromePageNumber->setToolTip(m_pageCount>0?QStringLiteral("输入 1–%1，回车跳页，Esc 取消").arg(m_pageCount):QStringLiteral("未打开 PDF"));
        m_chromePageTotal->setText(m_pageCount>0?QStringLiteral("/ %1").arg(m_pageCount):QStringLiteral("/ --"));
        auto* pages = m_chromePageNumber->parentWidget(); pages->adjustSize();
        pages->move(std::max(4,m_view->viewport()->width()-pages->width()-12),12);pages->raise();
    }
}
bool MainWindow::eventFilter(QObject* watched,QEvent* event)
{
    if(m_chromePageNumber&&watched==m_chromePageNumber){
        if((event->type()==QEvent::ShortcutOverride||event->type()==QEvent::KeyPress)
            &&static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape){
            event->accept();
            if(event->type()==QEvent::KeyPress){m_chromePageDraft=false;refreshReaderChrome();m_view->setFocus();}
            return true;
        }
        if(event->type()==QEvent::FocusOut)commitChromePageNumber();
    }
    if (m_view && watched==m_view->viewport() && event->type()==QEvent::Resize) refreshReaderChrome();
    return QMainWindow::eventFilter(watched,event);
}
}
