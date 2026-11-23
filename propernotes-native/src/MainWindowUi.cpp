// SPDX-License-Identifier: GPL-3.0-only
#include "MainWindow.h"
#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace propernotes {
void MainWindow::syncStrokeStyleControls(const ToolPreferences& tools, qreal penWidth)
{
    const QScopedValueRollback<bool> updating(m_updatingUi, true);
    const bool highlighter = m_tool == InkCanvas::Tool::Highlighter;
    m_width->setRange(highlighter ? MinHighlighterWidthPt : 0.5, highlighter ? MaxHighlighterWidthPt : 24.0);
    m_width->setValue(highlighter ? tools.highlighterWidthPt : penWidth);
    m_opacity->setDecimals(highlighter ? 0 : 2);
    m_opacity->setRange(highlighter ? MinHighlighterOpacity : 0, MaxHighlighterOpacity);
    m_opacity->setValue(highlighter ? tools.highlighterOpacity : tools.opacity * 100);
    m_pressureField->setVisible(!highlighter);
    m_sensitivityField->setVisible(!highlighter);
    m_colorAction->setToolTip(highlighter ? QStringLiteral("自由荧光笔颜色") : QStringLiteral("笔与新文字框颜色"));
    m_width->setToolTip(highlighter ? QStringLiteral("自由荧光笔粗细") : QStringLiteral("笔的粗细"));
    QPixmap swatch(14, 14);
    swatch.fill(highlighter ? tools.highlighterColor : m_color);
    m_colorAction->setIcon(QIcon(swatch));
}

void MainWindow::buildUi() {
    auto* files=addToolBar(QStringLiteral("文件")); files->setMovable(false);
    files->addWidget(new QLabel(QStringLiteral("  ProperNotes  ")));
    m_openAction=files->addAction(QStringLiteral("打开 PDF")); m_openAction->setShortcut(QKeySequence::Open);
    m_saveAction=files->addAction(QStringLiteral("保存")); m_saveAction->setShortcut(QKeySequence::Save);
    m_saveAsAction=files->addAction(QStringLiteral("另存为")); m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    m_fileLabel=new QLabel; m_fileLabel->setTextFormat(Qt::PlainText); files->addWidget(m_fileLabel);
    m_sidebarAction=files->addAction(QStringLiteral("页面侧栏")); m_sidebarAction->setCheckable(true);
    m_overviewAction=files->addAction(QStringLiteral("展开页面"));
    m_settingsAction=files->addAction(QStringLiteral("设置")); m_backupsAction=files->addAction(QStringLiteral("本地备份"));
    connect(m_openAction,&QAction::triggered,this,&MainWindow::chooseOpenFile);
    connect(m_saveAction,&QAction::triggered,this,[this]{saveFile(false);});
    connect(m_saveAsAction,&QAction::triggered,this,[this]{saveFile(true);});
    connect(m_settingsAction,&QAction::triggered,this,[this]{openSettings();});
    connect(m_backupsAction,&QAction::triggered,this,[this]{openSettings(true);});
    connect(m_overviewAction,&QAction::triggered,this,&MainWindow::openPageOverview);
    connect(m_sidebarAction,&QAction::triggered,this,[this](bool on){auto p=m_preferences;p.sidebarVisible=on;persistPreferences(p);});
    addToolBarBreak(); auto* tools=addToolBar(QStringLiteral("工具")); tools->setMovable(false);
    auto* toolGroup=new QActionGroup(this); toolGroup->setExclusive(true);
    auto addTool=[&](const QString& name,InkCanvas::Tool tool){auto* a=tools->addAction(name);a->setCheckable(true);toolGroup->addAction(a);connect(a,&QAction::triggered,this,[this,tool]{chooseTool(tool);});return a;};
    m_penAction=addTool(QStringLiteral("笔"),InkCanvas::Tool::Pen);
    m_highlighterAction=addTool(QStringLiteral("自由荧光笔"),InkCanvas::Tool::Highlighter);
    m_highlighterAction->setToolTip(QStringLiteral("直接在 PDF 页面上划线，扫描页也可用。"));
    m_eraserAction=addTool(QStringLiteral("橡皮"),InkCanvas::Tool::ObjectEraser);
    m_selectAction=addTool(QStringLiteral("框选 / 移动"),InkCanvas::Tool::Select);
    m_pasteInkAction = tools->addAction(QStringLiteral("粘贴笔迹"));
    m_pasteInkAction->setShortcut(QKeySequence::Paste);
    m_pasteInkAction->setToolTip(QStringLiteral("粘贴笔迹到当前页（Ctrl+V）"));
    connect(m_pasteInkAction, &QAction::triggered, this, &MainWindow::pasteInk);
    m_cutInkAction = new QAction(QStringLiteral("剪切笔迹"), this);
    m_copyInkAction = new QAction(QStringLiteral("复制笔迹"), this);
    m_cutInkAction->setShortcut(QKeySequence::Cut);
    m_copyInkAction->setShortcut(QKeySequence::Copy);
    addActions({m_cutInkAction, m_copyInkAction});
    connect(m_cutInkAction, &QAction::triggered, this, [this] { copySelectedInk(true); });
    connect(m_copyInkAction, &QAction::triggered, this, [this] { copySelectedInk(false); });
    m_textAction=addTool(QStringLiteral("文字框"),InkCanvas::Tool::TextBox);
    m_panAction=addTool(QStringLiteral("平移"),InkCanvas::Tool::Pan);
    m_penAction->setChecked(true);
    m_followAction=tools->addAction(QStringLiteral("鼠标与手跟随工具"));m_followAction->setCheckable(true);
    m_followAction->setToolTip(QStringLiteral("关闭时，鼠标左键和单指可平移、手动选字、框选及移动已有批注。开启后也可用当前工具写、擦或自动审阅。笔使用当前工具，两指负责浏览。"));
    m_lockAction=tools->addAction(QStringLiteral("持续使用"));m_lockAction->setCheckable(true);
    m_lockAction->setToolTip(QStringLiteral("选中为持续使用；取消后，成功完成一次操作回到上一个持续工具。"));
    m_undoAction=tools->addAction(QStringLiteral("撤销"));m_undoAction->setShortcut(QKeySequence::Undo);
    m_redoAction=tools->addAction(QStringLiteral("重做"));m_redoAction->setShortcuts({QKeySequence(Qt::CTRL|Qt::Key_Y),QKeySequence(Qt::CTRL|Qt::SHIFT|Qt::Key_Z)});
    connect(m_undoAction,&QAction::triggered,this,[this]{editHistory(false);});
    connect(m_redoAction,&QAction::triggered,this,[this]{editHistory(true);});
    connect(m_followAction,&QAction::triggered,this,[this](bool on){auto p=m_preferences;p.tools.inputFollowsTool=on;persistPreferences(p);});
    connect(m_lockAction,&QAction::triggered,this,[this](bool locked){
        if (m_tool == InkCanvas::Tool::TextSelect) {
            setActiveReviewSingleUse(!locked);
            return;
        }
        if(m_preferencesBusy||m_backupBusy||m_fileBusy||!m_preferencesWritable||locked==!isSingleUse(m_tool))return;
        auto p=m_preferences;p.tools=effectiveToolPreferences();p.penColor=m_color;p.penWidthPt=effectivePenWidth();
        if(m_tool==InkCanvas::Tool::Pen)p.tools.penSingleUse=!locked;
        if(m_tool==InkCanvas::Tool::Highlighter)p.tools.highlighterSingleUse=!locked;
        if(m_tool==InkCanvas::Tool::ObjectEraser)p.tools.eraserSingleUse=!locked;
        if(m_tool==InkCanvas::Tool::TextBox)p.tools.textSingleUse=!locked;
        beginPendingDefaultChange();persistPreferences(p);});
    addToolBarBreak();auto* style=addToolBar(QStringLiteral("笔与对象属性"));style->setMovable(false);
    m_colorAction=style->addAction(QStringLiteral("颜色"));
    m_width=new QDoubleSpinBox;m_width->setRange(0.5,24);m_width->setSingleStep(0.5);m_width->setSuffix(QStringLiteral(" pt"));style->addWidget(m_width);
    m_opacity=new QDoubleSpinBox;m_opacity->setRange(0,100);m_opacity->setSuffix(QStringLiteral("% 不透明度"));style->addWidget(m_opacity);
    m_pressure=new QCheckBox(QStringLiteral("笔压"));m_pressureField=style->addWidget(m_pressure);
    m_sensitivity=new QSpinBox;m_sensitivity->setRange(0,100);m_sensitivity->setPrefix(QStringLiteral("灵敏度 "));m_sensitivityField=style->addWidget(m_sensitivity);
    m_eraseMode=new QComboBox;m_eraseMode->addItem(QStringLiteral("整笔擦除"),"whole");m_eraseMode->addItem(QStringLiteral("局部擦除"),"partial");style->addWidget(m_eraseMode);
    m_eraserSize=new QDoubleSpinBox;m_eraserSize->setRange(1,100);m_eraserSize->setPrefix(QStringLiteral("橡皮半径 "));m_eraserSize->setSuffix(QStringLiteral(" pt"));style->addWidget(m_eraserSize);
    m_textFamily=new QFontComboBox;style->addWidget(m_textFamily);
    m_textSize=new QDoubleSpinBox;m_textSize->setRange(4,144);m_textSize->setSuffix(QStringLiteral(" pt 字号"));style->addWidget(m_textSize);
    for(auto* spin:{m_width,m_opacity,m_eraserSize,m_textSize})spin->setKeyboardTracking(false);
    m_sensitivity->setKeyboardTracking(false);
    auto saveStyle=[this](const QString& field){
        if(m_updatingUi||m_preferencesBusy||m_backupBusy||m_fileBusy||!m_preferencesWritable)return;
        const auto old=effectiveToolPreferences();const qreal oldWidth=effectivePenWidth();
        const auto changed=[](QDoubleSpinBox* spin,qreal original){const qreal scale=std::pow(10.0,spin->decimals());return !qFuzzyCompare(spin->value()+1,std::round(original*scale)/scale+1);};
        bool modified=false;auto p=m_preferences;p.tools=old;p.penColor=m_color;p.penWidthPt=oldWidth;
        const bool highlighter = m_tool == InkCanvas::Tool::Highlighter;
        if(field==QStringLiteral("width")) {
            modified=changed(m_width,highlighter ? old.highlighterWidthPt : oldWidth);
            if(highlighter) p.tools.highlighterWidthPt=m_width->value();
            else p.penWidthPt=m_width->value();
        } else if(field==QStringLiteral("opacity")) {
            modified=changed(m_opacity,highlighter ? old.highlighterOpacity : old.opacity*100);
            if(highlighter) p.tools.highlighterOpacity=int(m_opacity->value());
            else p.tools.opacity=m_opacity->value()/100.;
        }
        else if(field==QStringLiteral("radius")){modified=changed(m_eraserSize,old.eraserRadiusPt);p.tools.eraserRadiusPt=m_eraserSize->value();}
        else if(field==QStringLiteral("text-size")){modified=changed(m_textSize,old.textSizePt);p.tools.textSizePt=m_textSize->value();}
        else if(field==QStringLiteral("sensitivity")){modified=m_sensitivity->value()!=old.pressureSensitivity;p.tools.pressureSensitivity=m_sensitivity->value();}
        else if(field==QStringLiteral("pressure")){modified=m_pressure->isChecked()!=old.pressureEnabled;p.tools.pressureEnabled=m_pressure->isChecked();}
        else if(field==QStringLiteral("erase-mode")){modified=m_eraseMode->currentData().toString()!=old.eraserMode;p.tools.eraserMode=m_eraseMode->currentData().toString();}
        else if(field==QStringLiteral("family")){modified=m_textFamily->currentFont().family()!=old.textFamily;p.tools.textFamily=m_textFamily->currentFont().family();}
        if(!modified)return;beginPendingDefaultChange();persistPreferences(p);
    };
    connect(m_width,&QDoubleSpinBox::editingFinished,this,[saveStyle]{saveStyle(QStringLiteral("width"));});
    connect(m_opacity,&QDoubleSpinBox::editingFinished,this,[saveStyle]{saveStyle(QStringLiteral("opacity"));});
    connect(m_eraserSize,&QDoubleSpinBox::editingFinished,this,[saveStyle]{saveStyle(QStringLiteral("radius"));});
    connect(m_textSize,&QDoubleSpinBox::editingFinished,this,[saveStyle]{saveStyle(QStringLiteral("text-size"));});
    connect(m_sensitivity,&QSpinBox::editingFinished,this,[saveStyle]{saveStyle(QStringLiteral("sensitivity"));});
    connect(m_pressure,&QCheckBox::clicked,this,[saveStyle](bool){saveStyle(QStringLiteral("pressure"));});
    connect(m_eraseMode,&QComboBox::activated,this,[saveStyle](int){saveStyle(QStringLiteral("erase-mode"));});
    connect(m_textFamily,&QFontComboBox::currentFontChanged,this,[this,saveStyle](const QFont&){if(!m_updatingUi)saveStyle(QStringLiteral("family"));});
    connect(m_colorAction,&QAction::triggered,this,[this]{
        if(m_preferencesBusy||m_backupBusy||m_fileBusy||!m_preferencesWritable)return;
        m_view->finishGesture();
        const auto toolSerial = m_toolSerial;
        const bool highlighter = m_tool == InkCanvas::Tool::Highlighter;
        auto p=m_preferences;p.tools=effectiveToolPreferences();p.penWidthPt=effectivePenWidth();p.penColor=m_color;
        QColor& color = highlighter ? p.tools.highlighterColor : p.penColor;
        const QColor chosen=QColorDialog::getColor(color,this,m_colorAction->toolTip());
        if(!chosen.isValid() || chosen==color || m_toolSerial!=toolSerial)return;
        color=chosen;
        beginPendingDefaultChange();persistPreferences(p);
    });
    addToolBarBreak();auto* read=addToolBar(QStringLiteral("阅读"));read->setMovable(false);
    m_backAction=read->addAction(QStringLiteral("视角返回"));m_backAction->setShortcut(QKeySequence(Qt::ALT|Qt::Key_Left));
    m_forwardAction=read->addAction(QStringLiteral("视角前进"));m_forwardAction->setShortcut(QKeySequence(Qt::ALT|Qt::Key_Right));
    connect(m_backAction,&QAction::triggered,this,[this]{navigateHistory(-1);});connect(m_forwardAction,&QAction::triggered,this,[this]{navigateHistory(1);});
    m_previousAction=read->addAction(QStringLiteral("上一页"));m_pageNumber=new QSpinBox;m_pageNumber->setRange(1,1);m_pageNumber->setKeyboardTracking(false);read->addWidget(m_pageNumber);m_nextAction=read->addAction(QStringLiteral("下一页"));
    m_layout=new QComboBox;m_layout->addItem(QStringLiteral("单页连续"),"single-continuous");m_layout->addItem(QStringLiteral("双页连续"),"facing-continuous");m_layout->addItem(QStringLiteral("单页"),"single");m_layout->addItem(QStringLiteral("双页"),"facing");read->addWidget(m_layout);
    m_zoomOutAction=read->addAction(QStringLiteral("−"));m_zoomLabel=new QLabel;read->addWidget(m_zoomLabel);m_zoomInAction=read->addAction(QStringLiteral("＋"));
    m_fitAction=read->addAction(QStringLiteral("适宽"));m_fitPageAction=read->addAction(QStringLiteral("适页"));m_fitHeightAction=read->addAction(QStringLiteral("适高"));
    connect(m_previousAction,&QAction::triggered,this,[this]{const bool facing=m_view->layoutMode()==DocumentView::LayoutMode::FacingPages||m_view->layoutMode()==DocumentView::LayoutMode::FacingContinuous;goToPage(m_currentPage-(facing?2:1));});
    connect(m_nextAction,&QAction::triggered,this,[this]{const bool facing=m_view->layoutMode()==DocumentView::LayoutMode::FacingPages||m_view->layoutMode()==DocumentView::LayoutMode::FacingContinuous;goToPage(m_currentPage+(facing?2:1));});
    connect(m_pageNumber,&QSpinBox::editingFinished,this,[this]{if(!m_updatingUi)goToPage(m_pageNumber->value()-1);});
    connect(m_zoomInAction,&QAction::triggered,this,[this]{changeZoom(m_view->zoom()*1.2,m_view->viewport()->rect().center());});connect(m_zoomOutAction,&QAction::triggered,this,[this]{changeZoom(m_view->zoom()/1.2,m_view->viewport()->rect().center());});
    connect(m_fitAction,&QAction::triggered,this,&MainWindow::fitWidth);connect(m_fitPageAction,&QAction::triggered,this,[this]{m_view->setZoomMode(DocumentView::ZoomMode::FitPage);});connect(m_fitHeightAction,&QAction::triggered,this,[this]{m_view->setZoomMode(DocumentView::ZoomMode::FitHeight);});
    connect(m_layout,&QComboBox::activated,this,[this](int){auto p=m_preferences;p.tools.layout=m_layout->currentData().toString();persistPreferences(p);});
    auto* central=new QWidget;auto* box=new QVBoxLayout(central);box->setContentsMargins(0,0,0,0);box->setSpacing(0);
    m_preferencesBanner=new QLabel;m_documentBanner=new QLabel;
    for(auto* banner:{m_preferencesBanner,m_documentBanner}){banner->setTextFormat(Qt::PlainText);banner->setWordWrap(true);banner->setStyleSheet(QStringLiteral("padding:8px;background:#fff5d8;color:#795700;"));banner->hide();box->addWidget(banner);}
    m_splitter=new QSplitter;m_splitter->setChildrenCollapsible(false);m_navigation=new QWidget;m_navigation->setMinimumWidth(112);m_navigation->setMaximumWidth(400);
    auto* nav=new QVBoxLayout(m_navigation);nav->setContentsMargins(0,0,0,0);nav->addWidget(new QLabel(QStringLiteral(" 页面预览")));m_pages=new QListWidget;m_pages->setIconSize(QSize(120,170));m_pages->setUniformItemSizes(true);nav->addWidget(m_pages);m_splitter->addWidget(m_navigation);
    m_view=new DocumentView;m_splitter->addWidget(m_view);m_splitter->setStretchFactor(1,1);m_splitter->setSizes({160,1080});box->addWidget(m_splitter,1);setCentralWidget(central);
    buildTextReviewUi(read, central);
    m_pageLabel=new QLabel;statusBar()->addPermanentWidget(m_pageLabel);
    m_renderTimer=new QTimer(this);m_renderTimer->setSingleShot(true);m_renderTimer->setInterval(180);connect(m_renderTimer,&QTimer::timeout,this,&MainWindow::requestRender);
    m_sidebarTimer=new QTimer(this);m_sidebarTimer->setSingleShot(true);m_sidebarTimer->setInterval(600);
    connect(m_splitter,&QSplitter::splitterMoved,this,[this]{if(!m_updatingUi&&!m_preferencesBusy&&!m_settingsDialog)m_sidebarTimer->start();});
    connect(m_sidebarTimer,&QTimer::timeout,this,[this]{if(m_preferencesBusy||!m_preferencesWritable||!m_navigation->isVisible())return;auto p=m_preferences;p.sidebarWidth=std::clamp(m_splitter->sizes().value(0,p.sidebarWidth),112,400);if(p.sidebarWidth!=m_preferences.sidebarWidth)persistPreferences(p);});
    m_backupTimer=new QTimer(this);m_backupTimer->setSingleShot(true);connect(m_backupTimer,&QTimer::timeout,this,[this]{createBackup(false);});
    m_sidebarPreviewTimer=new QTimer(this);m_sidebarPreviewTimer->setSingleShot(true);m_sidebarPreviewTimer->setInterval(80);connect(m_sidebarPreviewTimer,&QTimer::timeout,this,&MainWindow::refreshSidebarPreview);
    connect(m_pages->verticalScrollBar(),&QScrollBar::valueChanged,this,[this]{m_sidebarPreviewTimer->start();});
    connect(m_pages,&QListWidget::currentRowChanged,this,[this](int p){if(!m_updatingUi)goToPage(p);});
    connect(m_view,&DocumentView::renderRequested,this,[this](int p,qreal s,quint64 id){requestPageRender(p,s,id,0);});connect(m_view,&DocumentView::renderCancelled,this,[this](quint64 id){cancelPageRender(id,0);});
    connect(m_view,&DocumentView::annotationsCommitted,this,&MainWindow::commitAnnotations);connect(m_view,&DocumentView::normalToolCompleted,this,&MainWindow::toolCompleted);
    connect(m_view,&DocumentView::currentPageChanged,this,[this](int p){m_currentPage=p;m_targetPage=p;updateUi();m_sidebarPreviewTimer->start();});
    connect(m_view,&DocumentView::viewStateChanged,this,&MainWindow::viewChanged);
    connect(m_view,&DocumentView::textEditingChanged,this,[this](bool){updateUi();});connect(m_view,&DocumentView::selectionChanged,this,[this]{updateUi();});
    connect(m_view, &DocumentView::inkCutRequested, this, [this] { copySelectedInk(true); });
    connect(m_view, &DocumentView::inkCopyRequested, this, [this] { copySelectedInk(false); });
    connect(m_view,&DocumentView::activity,this,[this](const QString& s){statusBar()->showMessage(s,8000);});
    applyPreferences(m_preferences);
}
} // namespace propernotes
