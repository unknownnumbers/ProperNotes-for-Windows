// SPDX-License-Identifier: GPL-3.0-only
#include "FavoritesBar.h"
#include "ToolIcon.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QList>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QPointer>
#include <QPair>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QScrollBar>
#include <QScroller>
#include <QSpinBox>
#include <QTabletEvent>
#include <QToolButton>
#include <QTouchEvent>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cstdlib>
#include <functional>

namespace propernotes {
namespace {
constexpr int MoveHandle=0, TopHandle=1, BottomHandle=2;
constexpr int ResizeEdgeHeight=10;
QString tooltip(const QString& text) {return QStringLiteral("<qt>")+text.toHtmlEscaped().replace(QLatin1Char('\n'),QStringLiteral("<br>"))+QStringLiteral("</qt>");}
bool sameRecipe(const FavoriteTool& a,const FavoriteTool& b)
{
    auto left=a,right=b;left.id=right.id=QUuid();left.name=right.name=QStringLiteral("工具");
    return favoriteToolsJson({left})==favoriteToolsJson({right});
}

class GeometryHandle final : public QToolButton {
public:
    using Gesture=std::function<void(int,const QPointF&)>;
    GeometryHandle(Gesture gesture,std::function<void(int,bool)> key,QWidget* parent)
        :QToolButton(parent),m_gesture(std::move(gesture)),m_key(std::move(key))
    {setAttribute(Qt::WA_AcceptTouchEvents);setAutoRaise(true);}
    void cancelContact(){m_mouse=m_touch=m_tablet=false;setDown(false);if(QWidget::mouseGrabber()==this)releaseMouse();}
protected:
    bool event(QEvent* event) override {
        if(event->type()==QEvent::TouchBegin||event->type()==QEvent::TouchUpdate||event->type()==QEvent::TouchEnd||event->type()==QEvent::TouchCancel){
            auto* touch=static_cast<QTouchEvent*>(event);
            if(event->type()==QEvent::TouchBegin){if(!isEnabled()||m_mouse||m_tablet||touch->points().isEmpty())return false;m_touch=true;setDown(true);m_last=touch->points().first().globalPosition();m_gesture(0,m_last);}
            else if(m_touch){if(!touch->points().isEmpty())m_last=touch->points().first().globalPosition();if(event->type()==QEvent::TouchUpdate)m_gesture(1,m_last);else{m_touch=false;setDown(false);m_gesture(event->type()==QEvent::TouchCancel?3:2,m_last);}}
            event->accept();return true;
        }
        if(event->type()==QEvent::UngrabMouse&&m_mouse){m_mouse=false;setDown(false);m_gesture(3,m_last);}
        if(event->type()==QEvent::Hide&&(m_mouse||m_touch||m_tablet)){m_mouse=m_touch=m_tablet=false;setDown(false);m_gesture(3,m_last);}
        return QToolButton::event(event);
    }
    void mousePressEvent(QMouseEvent* e) override {if(e->button()!=Qt::LeftButton||m_touch||m_tablet){e->ignore();return;}m_mouse=true;m_last=e->globalPosition();setDown(true);grabMouse();m_gesture(0,m_last);e->accept();}
    void mouseMoveEvent(QMouseEvent* e) override {if(!m_mouse){QToolButton::mouseMoveEvent(e);return;}m_last=e->globalPosition();m_gesture(1,m_last);e->accept();}
    void mouseReleaseEvent(QMouseEvent* e) override {if(!m_mouse||e->button()!=Qt::LeftButton){e->ignore();return;}m_mouse=false;m_last=e->globalPosition();setDown(false);releaseMouse();m_gesture(2,m_last);e->accept();}
    void tabletEvent(QTabletEvent* e) override {
        if(e->type()==QEvent::TabletPress&&isEnabled()&&!m_mouse&&!m_touch){m_tablet=true;m_last=e->globalPosition();m_gesture(0,m_last);setDown(true);}
        else if(e->type()==QEvent::TabletMove&&m_tablet){m_last=e->globalPosition();m_gesture(1,m_last);}
        else if(e->type()==QEvent::TabletRelease&&m_tablet){m_tablet=false;m_last=e->globalPosition();setDown(false);m_gesture(2,m_last);}
        else{e->ignore();return;}e->accept();
    }
    void keyPressEvent(QKeyEvent* e) override {
        if(e->key()==Qt::Key_Escape){m_gesture(3,m_last);m_mouse=m_touch=m_tablet=false;setDown(false);if(QWidget::mouseGrabber()==this)releaseMouse();e->accept();}
        else if(e->key()==Qt::Key_Up||e->key()==Qt::Key_Down||e->key()==Qt::Key_Left||e->key()==Qt::Key_Right){m_key(e->key(),e->modifiers().testFlag(Qt::ShiftModifier));e->accept();}
        else QToolButton::keyPressEvent(e);
    }
private:
    Gesture m_gesture;std::function<void(int,bool)> m_key;QPointF m_last;bool m_mouse=false,m_touch=false,m_tablet=false;
};

// The dialog owns an isolated draft. Saving uses the host's normal protected
// preference transaction. Merely browsing choices never changes the tool.
class FavoriteEditor final : public QDialog {
public:
    enum Outcome {Cancelled=0,SavePreset=1,UseTool=2,Manage=3};
    FavoriteEditor(const FavoriteTool& initial,bool library,bool writable,bool usable,QWidget* parent)
        :QDialog(parent),m_original(initial),m_color(initial.color),m_library(library)
    {
        setWindowTitle(library?QStringLiteral("工具与选项"):QStringLiteral("收藏工具选项"));
        setMinimumWidth(360);auto* box=new QVBoxLayout(this);m_form=new QFormLayout;m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        m_name=new QLineEdit(initial.name);m_name->setMaxLength(80);m_form->addRow(QStringLiteral("预设名称"),m_name);
        m_kind=new QComboBox;
        for(auto kind:{InkCanvas::Tool::Pen,InkCanvas::Tool::Highlighter,InkCanvas::Tool::ObjectEraser,InkCanvas::Tool::TextBox,InkCanvas::Tool::TextSelect})m_kind->addItem(favoriteToolName(kind),int(kind));
        for(auto kind:ShapeTools)m_kind->addItem(favoriteToolName(kind),int(kind));
        for(auto kind:{TextMarkupKind::Highlight,TextMarkupKind::Underline,TextMarkupKind::Squiggly,TextMarkupKind::StrikeOut})m_kind->addItem(textMarkupKindName(kind),MarkupOffset+int(kind));
        for(auto kind:{InkCanvas::Tool::Select,InkCanvas::Tool::Pan})m_kind->addItem(favoriteToolName(kind),int(kind));
        m_kind->setCurrentIndex(m_kind->findData(initial.markupKind?MarkupOffset+int(*initial.markupKind):int(initial.tool)));
        if(m_kind->currentIndex()<0)m_kind->setCurrentIndex(0);
        m_kind->setEnabled(library);m_form->addRow(QStringLiteral("工具"),m_kind);
        m_colorButton=new QPushButton(QStringLiteral("选择颜色…"));m_form->addRow(QStringLiteral("颜色"),m_colorButton);
        m_width=new QDoubleSpinBox;m_width->setSuffix(QStringLiteral(" pt"));m_width->setSingleStep(0.5);m_form->addRow(QStringLiteral("粗细"),m_width);
        m_opacity=new QDoubleSpinBox;m_opacity->setSuffix(QStringLiteral("%"));m_form->addRow(QStringLiteral("不透明度"),m_opacity);
        m_fillButton=new QPushButton(QStringLiteral("选择颜色…"));m_form->addRow(QStringLiteral("填充颜色"),m_fillButton);
        m_fillOpacity=new QSpinBox;m_fillOpacity->setRange(0,100);m_fillOpacity->setSuffix(QStringLiteral("%"));m_fillOpacity->setSpecialValueText(QStringLiteral("无填充"));m_form->addRow(QStringLiteral("填充不透明度"),m_fillOpacity);
        m_pressure=new QCheckBox(QStringLiteral("启用笔压"));m_form->addRow(m_pressure);
        m_sensitivity=new QSpinBox;m_sensitivity->setRange(0,100);m_form->addRow(QStringLiteral("笔压灵敏度"),m_sensitivity);
        m_mode=new QComboBox;m_mode->addItem(QStringLiteral("整笔擦除"),QStringLiteral("whole"));m_mode->addItem(QStringLiteral("局部擦除"),QStringLiteral("partial"));m_form->addRow(QStringLiteral("橡皮模式"),m_mode);
        m_radius=new QDoubleSpinBox;m_radius->setRange(1,100);m_radius->setSuffix(QStringLiteral(" pt"));m_form->addRow(QStringLiteral("橡皮半径"),m_radius);
        m_family=new QFontComboBox;m_form->addRow(QStringLiteral("字体"),m_family);
        m_size=new QDoubleSpinBox;m_size->setRange(4,144);m_size->setSuffix(QStringLiteral(" pt"));m_form->addRow(QStringLiteral("字号"),m_size);
        m_continuous=new QCheckBox(QStringLiteral("持续使用"));m_form->addRow(m_continuous);
        auto* body=new QWidget;body->setLayout(m_form);auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setWidget(body);box->addWidget(scroll,1);
        auto* buttons=new QDialogButtonBox;
        auto finish=[this](Outcome result){const auto candidate=value();QString error;if(!validFavoriteTools({candidate},&error)){QMessageBox::warning(this,QStringLiteral("无法使用工具"),error);return;}done(result);};
        auto* save=buttons->addButton(library?QStringLiteral("加入工具栏"):QStringLiteral("保存预设"),QDialogButtonBox::AcceptRole);save->setEnabled(writable);connect(save,&QPushButton::clicked,this,[finish]{finish(SavePreset);});
        if(library){auto* use=buttons->addButton(QStringLiteral("使用"),QDialogButtonBox::ActionRole);use->setEnabled(usable);connect(use,&QPushButton::clicked,this,[finish]{finish(UseTool);});}
        else{auto* manage=buttons->addButton(QStringLiteral("管理收藏…"),QDialogButtonBox::ActionRole);manage->setEnabled(writable);connect(manage,&QPushButton::clicked,this,[this]{done(Manage);});}
        buttons->addButton(QDialogButtonBox::Cancel);connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);box->addWidget(buttons);
        connect(m_colorButton,&QPushButton::clicked,this,[this]{const auto c=QColorDialog::getColor(m_color,this,QStringLiteral("工具颜色"));if(c.isValid()){m_color=c;updateColor();}});
        connect(m_fillButton,&QPushButton::clicked,this,[this]{const auto c=QColorDialog::getColor(m_fillColor,this,QStringLiteral("填充颜色"));if(c.isValid()){m_fillColor=c;if(m_fillOpacity->value()==0)m_fillOpacity->setValue(DefaultShapeFillOpacityPercent);updateColor();}});
        connect(m_kind,&QComboBox::currentIndexChanged,this,[this]{loadKind(false);});loadKind(true);
        resize(410,570);
    }
    FavoriteTool value()const {
        // Display rounding must not rewrite an untouched preset's exact
        // opacity, especially one-byte alpha values near zero.
        const auto opacityValue = [this](qreal original) {
            qreal scale = 1;
            for (int digit = 0; digit < m_opacity->decimals(); ++digit) scale *= 10;
            const qreal displayed = std::clamp(qRound64(original * 100 * scale) / scale,
                                               m_opacity->minimum(), m_opacity->maximum());
            return qFuzzyCompare(m_opacity->value() + 1, displayed + 1) ? original : m_opacity->value() / 100;
        };
        ToolPreferences tools=m_original.tools;tools.opacity=opacityValue(m_original.tools.opacity);tools.pressureEnabled=m_pressure->isChecked();tools.pressureSensitivity=m_sensitivity->value();tools.eraserMode=m_mode->currentData().toString();tools.eraserRadiusPt=m_radius->value();tools.textFamily=m_family->currentFont().family();tools.textSizePt=m_size->value();
        const bool single=!m_continuous->isChecked();tools.penSingleUse=tools.eraserSingleUse=tools.textSingleUse=single;const int selected=m_kind->currentData().toInt();
        QString name=m_name->text().trimmed();if(name.isEmpty())name=m_kind->currentText();FavoriteTool result;
        if(selected>=MarkupOffset){result=captureReviewFavorite(TextMarkupKind(selected-MarkupOffset),m_color,name,single);result.tools.opacity=std::clamp(tools.opacity,1.0/255,1.0);}
        else{const auto tool=InkCanvas::Tool(selected);if(tool==InkCanvas::Tool::Highlighter){tools.highlighterColor=m_color;tools.highlighterWidthPt=m_width->value();tools.highlighterOpacity=qRound(m_opacity->value());tools.highlighterSingleUse=single;}
            if(const auto shape=InkCanvas::shapeKind(tool)){
                const auto& originalStyle=m_original.tools.shapeToolPreference(*shape).style;
                auto& pref=tools.shapeToolPreference(*shape);pref.singleUse=single;pref.style.widthPt=m_width->value();pref.style.strokeColor=m_color;
                pref.style.strokeColor.setAlpha(std::clamp(qRound(opacityValue(originalStyle.strokeColor.alphaF())*255),1,255));
                if(isAreaShape(*shape)){
                    pref.style.fillColor=m_fillColor;
                    const int displayedFill=std::clamp(qRound(originalStyle.fillColor.alphaF()*100),m_fillOpacity->minimum(),m_fillOpacity->maximum());
                    pref.style.fillColor.setAlpha(m_fillOpacity->value()==displayedFill?originalStyle.fillColor.alpha():qRound(m_fillOpacity->value()*255/100.0));
                }
            }
            result=captureFavorite(tool,tools,m_color,m_width->value(),name);
        }result.id=m_original.id;return result;
    }
private:
    void updateColor(){m_colorButton->setIcon(uiIcon(QStringLiteral("plus"),palette(),m_color));m_colorButton->setText(m_color.name().toUpper());m_fillButton->setIcon(uiIcon(QStringLiteral("plus"),palette(),m_fillColor));m_fillButton->setText(m_fillColor.name().toUpper());}
    void loadKind(bool original) {
        const int selection=m_kind->currentData().toInt();const bool markup=selection>=MarkupOffset;
        const auto tool=markup?InkCanvas::Tool::TextSelect:InkCanvas::Tool(selection);const auto shape=markup?std::nullopt:InkCanvas::shapeKind(tool);
        const bool pen=tool==InkCanvas::Tool::Pen,highlighter=tool==InkCanvas::Tool::Highlighter,eraser=tool==InkCanvas::Tool::ObjectEraser,text=tool==InkCanvas::Tool::TextBox,fill=shape&&isAreaShape(*shape);
        m_form->setRowVisible(m_colorButton,pen||highlighter||text||markup||shape.has_value());m_form->setRowVisible(m_width,pen||highlighter||shape.has_value());m_form->setRowVisible(m_opacity,pen||highlighter||text||markup||shape.has_value());m_form->setRowVisible(m_fillButton,fill);m_form->setRowVisible(m_fillOpacity,fill);m_form->setRowVisible(m_pressure,pen);m_form->setRowVisible(m_sensitivity,pen);m_form->setRowVisible(m_mode,eraser);m_form->setRowVisible(m_radius,eraser);m_form->setRowVisible(m_family,text);m_form->setRowVisible(m_size,text);m_form->setRowVisible(m_continuous,pen||highlighter||eraser||text||markup||shape.has_value());
        m_width->setRange(highlighter?MinHighlighterWidthPt:0.5,highlighter?MaxHighlighterWidthPt:24);m_opacity->setDecimals(highlighter||shape?0:2);m_opacity->setRange(highlighter||shape?1.0:markup?100.0/255:0.0,100);
        const auto& t=m_original.tools;bool single=false;m_color=m_original.color;m_width->setValue(original?m_original.widthPt:2.5);m_opacity->setValue(t.opacity*100);
        m_pressure->setChecked(t.pressureEnabled);m_sensitivity->setValue(t.pressureSensitivity);m_mode->setCurrentIndex(m_mode->findData(t.eraserMode));m_radius->setValue(t.eraserRadiusPt);m_family->setCurrentFont(QFont(t.textFamily));m_size->setValue(t.textSizePt);
        if(pen)single=t.penSingleUse;if(highlighter){m_color=t.highlighterColor;m_width->setValue(t.highlighterWidthPt);m_opacity->setValue(t.highlighterOpacity);single=t.highlighterSingleUse;}if(eraser)single=t.eraserSingleUse;if(text)single=t.textSingleUse;if(markup)single=original&&m_original.reviewSingleUse;
        if(shape){const auto& pref=t.shapeToolPreference(*shape);m_color=pref.style.strokeColor;m_color.setAlpha(255);m_width->setValue(pref.style.widthPt);m_opacity->setValue(pref.style.strokeColor.alphaF()*100);m_fillColor=pref.style.fillColor;m_fillColor.setAlpha(255);m_fillOpacity->setValue(qRound(pref.style.fillColor.alphaF()*100));single=pref.singleUse;}
        m_continuous->setChecked(!single);if(!original&&m_library)m_name->setText(m_kind->currentText());updateColor();
    }
    static constexpr int MarkupOffset=100;
    FavoriteTool m_original;QColor m_color,m_fillColor{Qt::black};bool m_library;
    QFormLayout* m_form=nullptr;QLineEdit* m_name=nullptr;QComboBox* m_kind=nullptr;QPushButton* m_colorButton=nullptr;QDoubleSpinBox* m_width=nullptr;QDoubleSpinBox* m_opacity=nullptr;QPushButton* m_fillButton=nullptr;QSpinBox* m_fillOpacity=nullptr;QCheckBox* m_pressure=nullptr;QSpinBox* m_sensitivity=nullptr;QComboBox* m_mode=nullptr;QDoubleSpinBox* m_radius=nullptr;QFontComboBox* m_family=nullptr;QDoubleSpinBox* m_size=nullptr;QCheckBox* m_continuous=nullptr;
};
}

FavoritesBar::FavoritesBar(QWidget* parent):QWidget(parent),m_viewport(parent)
{
    setObjectName(QStringLiteral("propernotesFavoritesBar"));setAttribute(Qt::WA_StyledBackground);setAutoFillBackground(true);
    setStyleSheet(QStringLiteral("QWidget#propernotesFavoritesBar{border:1px solid palette(mid);border-radius:12px;background:palette(window);} QToolButton{border:0;border-radius:7px;padding:0;} QToolButton:hover{background:palette(alternate-base);} QToolButton:checked{background:palette(highlight);}"));
    setFixedWidth(80);auto* layout=new QVBoxLayout(this);layout->setSizeConstraint(QLayout::SetNoConstraint);layout->setContentsMargins(8,ResizeEdgeHeight,8,ResizeEdgeHeight);layout->setSpacing(2);
    auto handle=[this,layout](int kind,const QString& label,const QString& icon){auto* b=new GeometryHandle([this,kind](int phase,const QPointF& point){handleGesture(kind,phase,point);},[this,kind](int key,bool fast){keyboardGeometry(kind,key,fast);},this);b->setToolTip(label);b->setAccessibleName(label);b->setIcon(uiIcon(icon,palette()));configureReaderIconButton(b);b->setCursor(kind==MoveHandle?Qt::SizeAllCursor:Qt::SizeVerCursor);layout->addWidget(b,0,Qt::AlignHCenter);return b;};
    // Resize zones belong to the border, not to the list of toolbar buttons.
    auto edge=[this](int kind,const QString& label){
        auto* b=new GeometryHandle([this,kind](int phase,const QPointF& point){handleGesture(kind,phase,point);},
            [this,kind](int key,bool fast){keyboardGeometry(kind,key,fast);},this);
        b->setToolTip(label);b->setAccessibleName(label);b->setCursor(Qt::SizeVerCursor);
        b->setFocusPolicy(Qt::StrongFocus);
        b->setStyleSheet(QStringLiteral("QToolButton,QToolButton:hover,QToolButton:pressed,QToolButton:checked{"
            "background:transparent;border:0;border-radius:0;padding:0;margin:0;}"
            "QToolButton:focus{border:1px dotted palette(highlight);}"));
        return b;
    };
    m_topResize=edge(TopHandle,QStringLiteral("拖动工具栏上边缘调整高度，或使用上下方向键"));
    auto button=[this,layout](const QString& label,const QString& icon){auto* b=new QToolButton(this);b->setAutoRaise(true);configureReaderIconButton(b);b->setIcon(uiIcon(icon,palette()));b->setToolTip(label);b->setAccessibleName(label);layout->addWidget(b,0,Qt::AlignHCenter);return b;};
    m_pan=button(QStringLiteral("平移"),QStringLiteral("hand"));m_pan->setCheckable(true);m_select=button(QStringLiteral("框选／移动"),QStringLiteral("select"));m_select->setCheckable(true);m_selectMore=button(QStringLiteral("框选选项"),QStringLiteral("ellipsis"));m_selectMore->hide();
    auto separator=[this,layout]{auto* line=new QFrame(this);line->setFrameShape(QFrame::HLine);line->setFixedHeight(8);layout->addWidget(line);};separator();
    m_scroll=new QScrollArea(this);m_scroll->setObjectName(QStringLiteral("favoritesScroll"));m_scroll->setWidgetResizable(true);m_scroll->setFrameShape(QFrame::NoFrame);m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);m_scroll->setMinimumHeight(0);m_scroll->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Expanding);
    m_entries=new QWidget;m_entriesLayout=new QVBoxLayout(m_entries);m_entriesLayout->setContentsMargins(0,2,0,2);m_entriesLayout->setSpacing(2);m_scroll->setWidget(m_entries);m_scroll->viewport()->installEventFilter(this);QScroller::grabGesture(m_scroll->viewport(),QScroller::TouchGesture);layout->addWidget(m_scroll,1);separator();
    m_add=button(QStringLiteral("工具与选项：加入工具栏或使用"),QStringLiteral("plus"));m_move=handle(MoveHandle,QStringLiteral("移动工具栏，或使用方向键"),QStringLiteral("move"));
    m_bottomResize=edge(BottomHandle,QStringLiteral("拖动工具栏下边缘调整高度，或使用上下方向键"));
    connect(m_pan,&QToolButton::clicked,this,[this]{if(m_activationEnabled)Q_EMIT fixedToolRequested(InkCanvas::Tool::Pan);});connect(m_select,&QToolButton::clicked,this,[this]{if(m_activationEnabled)Q_EMIT fixedToolRequested(InkCanvas::Tool::Select);});connect(m_selectMore,&QToolButton::clicked,this,&FavoritesBar::selectionOptionsRequested);connect(m_add,&QToolButton::clicked,this,&FavoritesBar::openLibrary);
    m_visibilityAction=new QAction(QStringLiteral("显示收藏工具栏"),this);m_visibilityAction->setCheckable(true);connect(m_visibilityAction,&QAction::triggered,this,&FavoritesBar::setUserVisible);
    m_layoutTimer.setSingleShot(true);m_layoutTimer.setInterval(300);connect(&m_layoutTimer,&QTimer::timeout,this,&FavoritesBar::recordLayout);
    m_avoidanceTimer.setSingleShot(true);m_avoidanceTimer.setInterval(0);
    connect(&m_avoidanceTimer,&QTimer::timeout,this,[this]{
        if(m_hostClosing)return;
        if(m_gesture)handleGesture(m_gesture->kind,3,m_gesture->origin);
        constrainGeometry();
    });
    parent->installEventFilter(this);qApp->installEventFilter(this);setContextMenuPolicy(Qt::CustomContextMenu);connect(this,&QWidget::customContextMenuRequested,this,[this](const QPoint& point){showMenu(mapToGlobal(point));});resize(80,420);syncPlacementControls();rebuild();syncVisibility();
}
void FavoritesBar::setDockHosts(QWidget* viewport,QWidget* leftHost,QWidget* rightHost)
{
    if(!viewport||!leftHost||!rightHost)return;
    if(m_viewport&&m_viewport!=viewport)m_viewport->removeEventFilter(this);
    m_viewport=viewport;m_leftHost=leftHost;m_rightHost=rightHost;
    m_viewport->installEventFilter(this);
    if(!m_dockPreview){
        m_dockPreview=new QLabel(viewport);m_dockPreview->setAlignment(Qt::AlignCenter);
        m_dockPreview->setWordWrap(true);m_dockPreview->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_dockPreview->setStyleSheet(QStringLiteral("border:2px solid palette(highlight);border-radius:8px;background:palette(alternate-base);padding:8px;"));
        m_dockPreview->hide();
    }
    applyPlacement(m_preferences.floating,m_preferences.side);
}
void FavoritesBar::setReservedOverlays(const QList<QWidget*>& overlays)
{
    QList<QPointer<QWidget>> next;
    for(auto* overlay:overlays){
        if(!overlay||overlay==this||overlay==m_viewport)continue;
        bool found=false;for(const auto& known:next)found|=known==overlay;
        if(found)continue;
        next.append(overlay);
    }
    if(next!=m_reservedOverlays){
        for(const auto& overlay:m_reservedOverlays)if(overlay)overlay->removeEventFilter(this);
        m_reservedOverlays=next;
        for(const auto& overlay:m_reservedOverlays)if(overlay)overlay->installEventFilter(this);
    }
    // Calling again with the same widgets also invalidates hidden HUD geometry,
    // whose move/resize events may otherwise be delayed until their next show.
    scheduleAvoidanceReflow();
}
void FavoritesBar::scheduleAvoidanceReflow()
{
    const auto rectangles=reservedOverlayRects();
    if(rectangles==m_reservedGeometry)return;
    m_reservedGeometry=rectangles;m_avoidanceTimer.start();
}
void FavoritesBar::setFavorites(const QVector<FavoriteTool>& favorites)
{
    if(!validFavoriteTools(favorites))return;if(favoriteToolsJson(favorites)==favoriteToolsJson(m_favorites))return;
    if(favorites.size()>m_favorites.size()){for(const auto& favorite:favorites)if(indexOf(favorite.id)<0)m_revealId=favorite.id;}
    m_favorites=favorites;rebuild();
}
void FavoritesBar::setCurrentTool(InkCanvas::Tool tool,const ToolPreferences& tools,const QColor& color,qreal width)
{setCurrentTool(captureFavorite(tool,tools,color,width,QStringLiteral("当前工具")));}
void FavoritesBar::setCurrentTool(const FavoriteTool& current){m_currentTool=current;setActiveFavoriteId(m_activeId);}
void FavoritesBar::setActiveFavoriteId(const QUuid& id)
{
    const bool changed=m_activeId!=id;m_activeId=id;
    for(auto it=m_buttons.begin();it!=m_buttons.end();++it){const int i=indexOf(it.key());const bool active=it.key()==id&&i>=0&&sameRecipe(m_favorites[i],m_currentTool);it.value()->setChecked(active);m_moreButtons.value(it.key())->setVisible(active);}
    m_pan->setChecked(id.isNull()&&m_currentTool.tool==InkCanvas::Tool::Pan);m_select->setChecked(id.isNull()&&m_currentTool.tool==InkCanvas::Tool::Select);m_selectMore->setVisible(m_select->isChecked());constrainGeometry();
    if(changed&&m_buttons.contains(id)){QPointer<QWidget> row=m_buttons.value(id)->parentWidget();QTimer::singleShot(0,this,[this,row]{if(row)m_scroll->ensureWidgetVisible(row,0,0);});}
}
void FavoritesBar::setWritable(bool writable)
{
    std::optional<FavoritesBarPreferences> pending;
    const bool hadGesture=m_gesture.has_value();
    // An interrupted drag is not a completed layout change. Restore the last
    // committed placement before flushing a pending keyboard/migration save.
    if(!writable&&m_gesture)handleGesture(m_gesture->kind,3,m_gesture->origin);
    if(m_writable&&!writable&&(m_layoutTimer.isActive()||hadGesture)){m_layoutTimer.stop();pending=currentLayout();}
    m_writable=writable;if(!writable){m_gesture.reset();hideDockPreview();}syncPlacementControls();
    if(writable&&!m_hostClosing&&!m_gesture&&favoritesBarJson(currentLayout())!=favoritesBarJson(m_preferences))m_layoutTimer.start();
    if(pending&&!m_hostClosing&&favoritesBarJson(*pending)!=favoritesBarJson(m_preferences))Q_EMIT barPreferencesEdited(*pending);
}
void FavoritesBar::setActivationEnabled(bool enabled)
{m_activationEnabled=enabled;for(auto* b:m_buttons)b->setEnabled(enabled);m_pan->setEnabled(enabled);m_select->setEnabled(enabled);m_selectMore->setEnabled(enabled);m_add->setEnabled(enabled||m_writable);}
void FavoritesBar::setPinned(bool pinned)
{if(!m_writable||m_hostClosing)return;if(m_gesture)handleGesture(m_gesture->kind,3,m_gesture->origin);hideDockPreview();m_layoutTimer.stop();auto preferences=currentLayout();preferences.pinned=pinned;Q_EMIT barPreferencesEdited(preferences);}
void FavoritesBar::syncPlacementControls()
{
    const bool unlocked=!m_preferences.pinned;m_move->setVisible(unlocked);m_topResize->setVisible(unlocked&&m_floating);m_bottomResize->setVisible(unlocked&&m_floating);m_move->setEnabled(m_writable);m_topResize->setEnabled(m_writable);m_bottomResize->setEnabled(m_writable);m_add->setEnabled(m_activationEnabled||m_writable);
    if(auto* column=qobject_cast<QVBoxLayout*>(layout())){
        const int margin=unlocked&&m_floating?ResizeEdgeHeight:6;
        column->setContentsMargins(8,margin,8,margin);
    }
    placeResizeEdges();
}
void FavoritesBar::placeResizeEdges()
{
    if(!m_topResize||!m_bottomResize)return;
    m_topResize->setGeometry(0,0,width(),ResizeEdgeHeight);
    m_bottomResize->setGeometry(0,std::max(0,height()-ResizeEdgeHeight),width(),ResizeEdgeHeight);
    m_topResize->raise();m_bottomResize->raise();
}
void FavoritesBar::rebuild()
{
    const int scroll=m_scroll->verticalScrollBar()->value();m_buttons.clear();m_moreButtons.clear();while(auto* item=m_entriesLayout->takeAt(0)){if(auto* widget=item->widget()){widget->hide();widget->deleteLater();}delete item;}
    for(const auto& favorite:m_favorites){auto* row=new QWidget(m_entries);auto* layout=new QVBoxLayout(row);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);auto* button=new QToolButton(row);button->setAutoRaise(true);configureFavoriteButton(button,favorite);button->setCheckable(true);button->setEnabled(m_activationEnabled);button->setToolTip(tooltip(favoriteDescription(favorite)));button->setAccessibleName(favoriteDescription(favorite));layout->addWidget(button,0,Qt::AlignHCenter);m_buttons.insert(favorite.id,button);
        auto* more=new QToolButton(row);more->setAutoRaise(true);configureReaderIconButton(more);more->setIcon(uiIcon(QStringLiteral("ellipsis"),palette()));more->setToolTip(QStringLiteral("此收藏工具的选项"));more->setAccessibleName(more->toolTip());layout->addWidget(more,0,Qt::AlignHCenter);m_moreButtons.insert(favorite.id,more);m_entriesLayout->addWidget(row);
        connect(button,&QToolButton::clicked,this,[this,id=favorite.id]{const int index=indexOf(id);if(index>=0&&m_activationEnabled){const auto favorite=m_favorites[index];Q_EMIT activated(favorite);}});connect(more,&QToolButton::clicked,this,[this,id=favorite.id]{editFavorite(id);});button->setContextMenuPolicy(Qt::CustomContextMenu);connect(button,&QWidget::customContextMenuRequested,this,[this,button,id=favorite.id](const QPoint& point){showMenu(button->mapToGlobal(point),id);});}
    m_entriesLayout->addStretch();setActiveFavoriteId(m_activeId);
    const QUuid reveal=m_revealId;m_revealId={};QTimer::singleShot(0,this,[this,scroll,reveal]{m_scroll->verticalScrollBar()->setValue(scroll);if(m_buttons.contains(reveal))m_scroll->ensureWidgetVisible(m_buttons.value(reveal)->parentWidget(),0,0);});
}
int FavoritesBar::indexOf(const QUuid& id)const{for(int i=0;i<m_favorites.size();++i)if(m_favorites[i].id==id)return i;return -1;}
void FavoritesBar::openLibrary()
{
    if(!m_activationEnabled&&!m_writable)return;auto seed=m_currentTool;seed.id=QUuid::createUuid();seed.name=seed.markupKind?textMarkupKindName(*seed.markupKind):favoriteToolName(seed.tool);FavoriteEditor editor(seed,true,m_writable&&m_favorites.size()<128,m_activationEnabled,this);
    const int result=editor.exec();if(result!=FavoriteEditor::SavePreset&&result!=FavoriteEditor::UseTool)return;const auto favorite=editor.value();
    if(result==FavoriteEditor::SavePreset){if(!m_writable||m_favorites.size()>=128)return;auto candidate=m_favorites;candidate.append(favorite);if(m_activationEnabled)Q_EMIT activated(favorite);Q_EMIT favoritesEdited(candidate);}
    else if(m_activationEnabled)Q_EMIT activated(favorite);
}
void FavoritesBar::editFavorite(const QUuid& id)
{
    const int index=indexOf(id);if(index<0)return;FavoriteEditor editor(m_favorites[index],false,m_writable,false,this);const int result=editor.exec();if(result==FavoriteEditor::Manage){showMenu(mapToGlobal(QPoint(-180,40)),id);return;}if(result!=FavoriteEditor::SavePreset||!m_writable)return;const int current=indexOf(id);if(current<0)return;const auto favorite=editor.value();auto candidate=m_favorites;candidate[current]=favorite;if(m_activationEnabled)Q_EMIT activated(favorite);Q_EMIT favoritesEdited(candidate);
}
void FavoritesBar::showMenu(const QPoint& global,const QUuid& id)
{
    QMenu menu(this);auto* library=menu.addAction(QStringLiteral("工具与选项…"));connect(library,&QAction::triggered,this,&FavoritesBar::openLibrary);const int index=indexOf(id);
    if(index>=0){menu.addSeparator();auto* edit=menu.addAction(QStringLiteral("编辑选项…"));connect(edit,&QAction::triggered,this,[this,id]{editFavorite(id);});auto* copy=menu.addAction(QStringLiteral("复制收藏"));copy->setEnabled(m_writable&&m_favorites.size()<128);connect(copy,&QAction::triggered,this,[this,id]{const int i=indexOf(id);if(i<0||!m_writable||m_favorites.size()>=128)return;auto candidate=m_favorites;auto copy=candidate[i];copy.id=QUuid::createUuid();copy.name=copy.name.left(74);if(!copy.name.isEmpty()&&copy.name.back().isHighSurrogate())copy.name.chop(1);copy.name+=QStringLiteral(" 副本");candidate.insert(i+1,copy);Q_EMIT favoritesEdited(candidate);});
        auto* earlier=menu.addAction(QStringLiteral("上移"));auto* later=menu.addAction(QStringLiteral("下移"));earlier->setEnabled(m_writable&&index>0);later->setEnabled(m_writable&&index+1<m_favorites.size());connect(earlier,&QAction::triggered,this,[this,id]{auto candidate=m_favorites;const int i=indexOf(id);if(m_writable&&i>0){candidate.move(i,i-1);Q_EMIT favoritesEdited(candidate);}});connect(later,&QAction::triggered,this,[this,id]{auto candidate=m_favorites;const int i=indexOf(id);if(m_writable&&i>=0&&i+1<candidate.size()){candidate.move(i,i+1);Q_EMIT favoritesEdited(candidate);}});
        auto* remove=menu.addAction(QStringLiteral("移除收藏"));remove->setEnabled(m_writable);connect(remove,&QAction::triggered,this,[this,id]{const int i=indexOf(id);if(i<0||!m_writable)return;auto candidate=m_favorites;candidate.removeAt(i);Q_EMIT favoritesEdited(candidate);});}
    menu.addSeparator();
    for(const auto& placement:QList<QPair<QString,QString>>{{QStringLiteral("停靠到左侧"),QStringLiteral("left")},{QStringLiteral("停靠到右侧"),QStringLiteral("right")},{QStringLiteral("设为悬浮"),QString()}}){
        auto* action=menu.addAction(placement.first);action->setCheckable(true);
        action->setChecked(placement.second.isEmpty()?m_floating:!m_floating&&m_side==placement.second);
        action->setEnabled(m_writable&&!m_preferences.pinned&&m_leftHost&&m_rightHost);
        connect(action,&QAction::triggered,this,[this,side=placement.second]{
            if(!m_writable||m_preferences.pinned)return;
            applyPlacement(side.isEmpty(),side.isEmpty()?m_side:side);recordLayout();
        });
    }
    menu.addSeparator();auto* hide=menu.addAction(QStringLiteral("隐藏收藏工具栏"));connect(hide,&QAction::triggered,this,[this]{setUserVisible(false);});menu.exec(global);
}
void FavoritesBar::setBarPreferences(const FavoritesBarPreferences& preferences,bool forcePlacement)
{
    if(!validFavoritesBarPreferences(preferences)||m_hostClosing)return;
    if(m_gesture)handleGesture(m_gesture->kind,3,m_gesture->origin);
    const QScopedValueRollback<bool> applying(m_applying,true);
    if(forcePlacement){m_layoutTimer.stop();m_visibilityOverride.reset();}
    // Tool activation reapplies the stored preferences. Keep a lazy migration
    // or a queued user move when the incoming persisted placement is unchanged.
    const bool samePlacement=preferences.geometry==m_preferences.geometry
        &&preferences.floatingAnchor==m_preferences.floatingAnchor
        &&preferences.floatingEdgeOffset==m_preferences.floatingEdgeOffset
        &&preferences.floatingVerticalRatio==m_preferences.floatingVerticalRatio
        &&preferences.floatingPositionInitialized==m_preferences.floatingPositionInitialized;
    const bool sameMode=preferences.floating==m_preferences.floating&&preferences.side==m_preferences.side;
    const bool keepPending=!forcePlacement&&samePlacement&&sameMode;
    const bool floating=keepPending?m_floating:preferences.floating;
    const QString side=keepPending?m_side:preferences.side;
    if(forcePlacement||!samePlacement||!m_floatingPositionInitialized){
        m_floatingGeometry=QRect(preferences.geometry.topLeft(),QSize(80,preferences.geometry.height()));
        m_floatingAnchor=preferences.floatingAnchor;
        m_floatingEdgeOffset=preferences.floatingEdgeOffset;
        m_floatingVerticalRatio=preferences.floatingVerticalRatio;
        m_floatingPositionInitialized=preferences.floatingPositionInitialized;
    }
    m_preferences=preferences;
    if(m_visibilityOverride&&*m_visibilityOverride==preferences.visible)m_visibilityOverride.reset();
    m_gesture.reset();hideDockPreview();
    applyPlacement(floating,side);syncVisibility();
}
void FavoritesBar::applyPlacement(bool floating,const QString& side)
{
    if(!m_viewport||m_hostClosing)return;
    if(!floating&&(!m_leftHost||!m_rightHost))floating=true;
    const QScopedValueRollback<bool> applying(m_applying,true);
    QWidget* target=floating?m_viewport.data():(side==QStringLiteral("left")?m_leftHost.data():m_rightHost.data());
    if(parentWidget()!=target){
        if(parentWidget()&&parentWidget()->layout())parentWidget()->layout()->removeWidget(this);
        setParent(target);
        if(!floating&&target->layout())target->layout()->addWidget(this);
    }
    m_floating=floating;m_side=side==QStringLiteral("left")?QStringLiteral("left"):QStringLiteral("right");
    if(!floating)m_noFloatingSpace=false;
    setSizePolicy(QSizePolicy::Fixed,floating?QSizePolicy::Preferred:QSizePolicy::Expanding);
    syncPlacementControls();syncDockHosts();
    if(floating)constrainGeometry();
    syncVisibility();
}
void FavoritesBar::syncDockHosts()
{
    const bool visible=requestedVisible()&&!m_hostClosing;
    if(m_leftHost)m_leftHost->setVisible(visible&&!m_floating&&m_side==QStringLiteral("left"));
    if(m_rightHost)m_rightHost->setVisible(visible&&!m_floating&&m_side==QStringLiteral("right"));
    if(m_leftHost&&m_leftHost->parentWidget()&&m_leftHost->parentWidget()->layout())
        m_leftHost->parentWidget()->layout()->activate();
}
int FavoritesBar::minimumPaletteHeight()const
{
    // Only actual layout rows consume space. The transparent edge zones are
    // already contained in the margins and must never add 44px button rows.
    const auto* column=qobject_cast<QVBoxLayout*>(layout());
    if(!column)return 160;
    const auto margins=column->contentsMargins();
    int height=margins.top()+margins.bottom()+80,visible=0;
    for(int i=0;i<column->count();++i){
        const auto* item=column->itemAt(i);const auto* widget=item->widget();
        if(!widget||widget->isHidden())continue;
        ++visible;if(widget!=m_scroll)height+=widget->height();
    }
    return height+std::max(0,visible-1)*column->spacing();
}
void FavoritesBar::constrainGeometry()
{
    if(!m_viewport||m_hostClosing||!m_floating||m_gesture)return;
    const QScopedValueRollback<bool> applying(m_applying,true);
    const bool initialized=initializeFloatingPosition();
    applyFittedFloatingGeometry(relativeFloatingGeometry());
    if(initialized&&m_writable)m_layoutTimer.start();
}
QRect FavoritesBar::viewportBoundedGeometry(const QRect& geometry)const
{
    if(!m_viewport)return geometry;
    const int reserve=m_viewport->width()<420?140:84;
    const int availableHeight=std::min(1600,std::max(80,m_viewport->height()-reserve-8));
    const int minimum=std::min(minimumPaletteHeight(),availableHeight);
    const int height=std::clamp(geometry.height(),minimum,availableHeight);
    return QRect(std::clamp(geometry.x(),8,std::max(8,m_viewport->width()-88)),
        std::clamp(geometry.y(),8,std::max(8,m_viewport->height()-height-reserve)),80,height);
}
QList<QRect> FavoritesBar::reservedOverlayRects()const
{
    QList<QRect> result;
    if(!m_viewport)return result;
    for(const auto& overlay:m_reservedOverlays){
        if(!overlay||overlay->window()!=m_viewport->window())continue;
        // Deliberately ignore visibility: auto-hidden HUDs still need their
        // space when they reappear. Their owner keeps hidden geometry current.
        const QPoint origin=m_viewport->mapFromGlobal(overlay->mapToGlobal(QPoint(0,0)));
        const QRect rectangle=QRect(origin,overlay->size()).adjusted(-8,-8,8,8).intersected(m_viewport->rect());
        if(!rectangle.isEmpty())result.append(rectangle);
    }
    return result;
}
QRect FavoritesBar::boundedFloatingGeometry(const QRect& geometry,bool allowHorizontalShift)const
{
    if(!m_viewport)return geometry;
    const QRect wanted=viewportBoundedGeometry(geometry);
    const int reserve=m_viewport->width()<420?140:84;
    const int bottom=m_viewport->height()-reserve; // exclusive
    // Preserve every fixed control and one touch-sized part of the scroll
    // viewport. If that cannot fit, hiding is safer than clipping Add/Move.
    const int usableHeight=std::max(80,minimumPaletteHeight()-80+ReaderButtonSize);
    if(m_viewport->width()<96||bottom-8<usableHeight)return {};
    const auto reserved=reservedOverlayRects();
    QList<int> xs{wanted.x()};
    const int maxX=m_viewport->width()-88;
    auto addX=[&](int x){x=std::clamp(x,8,maxX);if(!xs.contains(x))xs.append(x);};
    if(allowHorizontalShift){
        addX(8);addX(maxX);
        for(const auto& rectangle:reserved){addX(rectangle.left()-80);addX(rectangle.right()+1);}
    }
    QRect best;
    int bestXDistance=0,bestYDistance=0;
    for(const int x:xs){
        QList<QPair<int,int>> blocks;
        for(const auto& rectangle:reserved){
            if(x>rectangle.right()||x+79<rectangle.left())continue;
            const int first=std::max(8,rectangle.top()),last=std::min(bottom,rectangle.bottom()+1);
            if(first<last)blocks.append(qMakePair(first,last));
        }
        std::sort(blocks.begin(),blocks.end(),[](const auto& left,const auto& right){
            return left.first==right.first?left.second<right.second:left.first<right.first;
        });
        auto consider=[&](int first,int last){
            const int height=std::min(wanted.height(),last-first);
            if(height<usableHeight)return;
            const int y=std::clamp(wanted.y(),first,last-height);
            const int xDistance=std::abs(x-wanted.x()),yDistance=std::abs(y-wanted.y());
            const bool better=best.isEmpty()||height>best.height()
                ||(height==best.height()&&(xDistance<bestXDistance
                    ||(xDistance==bestXDistance&&(yDistance<bestYDistance
                        ||(yDistance==bestYDistance&&x<best.x())))));
            if(better){best=QRect(x,y,80,height);bestXDistance=xDistance;bestYDistance=yDistance;}
        };
        int cursor=8;
        for(const auto& block:blocks){
            if(block.first>cursor)consider(cursor,block.first);
            cursor=std::max(cursor,block.second);
        }
        if(cursor<bottom)consider(cursor,bottom);
    }
    return best;
}
void FavoritesBar::applyFittedFloatingGeometry(const QRect& fitted)
{
    m_noFloatingSpace=fitted.isEmpty();
    if(m_noFloatingSpace){hide();return;}
    setGeometry(fitted);
    setVisible(requestedVisible()&&!m_hostClosing);raise();
}
bool FavoritesBar::initializeFloatingPosition()
{
    if(m_floatingPositionInitialized||!m_viewport||!m_floating||!m_viewport->isVisible())return false;
    const int reserve=m_viewport->width()<420?140:84;
    // Startup widgets can still have their constructor's 100x30 size. Require
    // room for the minimum visible rectangle, not for the desired full height:
    // a formerly tall toolbar must also migrate in an ordinary small window.
    if(m_viewport->width()<96||m_viewport->height()<reserve+88)return false;
    const QRect legacy=viewportBoundedGeometry(m_floatingGeometry);
    const int left=legacy.x(),right=m_viewport->width()-legacy.x()-legacy.width();
    m_floatingAnchor=left<right?QStringLiteral("left"):QStringLiteral("right");
    m_floatingEdgeOffset=std::clamp(std::min(left,right),8,100000);
    const int travel=m_viewport->height()-reserve-legacy.height()-8;
    if(travel>0)m_floatingVerticalRatio=std::clamp(qreal(legacy.y()-8)/travel,qreal(0),qreal(1));
    m_floatingPositionInitialized=true;
    return true;
}
QRect FavoritesBar::relativeFloatingGeometry()const
{
    if(!m_floatingPositionInitialized||!m_viewport)return boundedFloatingGeometry(m_floatingGeometry);
    const QRect fitted=viewportBoundedGeometry(QRect(8,8,80,m_floatingGeometry.height()));
    const int reserve=m_viewport->width()<420?140:84;
    const int travel=std::max(0,m_viewport->height()-reserve-fitted.height()-8);
    const int x=m_floatingAnchor==QStringLiteral("left")?m_floatingEdgeOffset
        :m_viewport->width()-fitted.width()-m_floatingEdgeOffset;
    return boundedFloatingGeometry(QRect(std::clamp(x,8,std::max(8,m_viewport->width()-88)),
        8+qRound(m_floatingVerticalRatio*travel),80,fitted.height()));
}
void FavoritesBar::rememberFloatingGeometry(bool resizeHeight)
{
    if(!m_viewport||!m_floating)return;
    const QRect placed=geometry();
    if(!resizeHeight){
        const int left=placed.x(),right=m_viewport->width()-placed.x()-placed.width();
        m_floatingAnchor=left<right?QStringLiteral("left"):QStringLiteral("right");
        m_floatingEdgeOffset=std::clamp(std::min(left,right),8,100000);
    }
    const int reserve=m_viewport->width()<420?140:84;
    const int travel=m_viewport->height()-reserve-placed.height()-8;
    // A small window can leave no travel at all. It must not erase a remembered
    // vertical ratio, nor reduce the desired height during a move-only action.
    if(travel>0)m_floatingVerticalRatio=std::clamp(qreal(placed.y()-8)/travel,qreal(0),qreal(1));
    m_floatingPositionInitialized=true;
    m_floatingGeometry=QRect(placed.topLeft(),QSize(80,resizeHeight?placed.height():m_floatingGeometry.height()));
}
void FavoritesBar::showDockPreview(const QPointF& global,const QRect& proposed)
{
    m_dockCandidate.clear();if(!m_dockPreview||!m_viewport||!m_leftHost||!m_rightHost)return;
    const QPoint point=m_viewport->mapFromGlobal(global.toPoint());
    const int outsideAllowance=m_floating?24:104; // A docked handle starts in an 80px side strip.
    if(point.x()>=-outsideAllowance&&point.x()<=m_viewport->width()+outsideAllowance&&point.y()>=-24&&point.y()<=m_viewport->height()+24){
        if(proposed.left()<=24)m_dockCandidate=QStringLiteral("left");
        else if(proposed.right()>=m_viewport->width()-24)m_dockCandidate=QStringLiteral("right");
    }
    if(!m_dockCandidate.isEmpty()){
        m_dockPreview->setText(m_dockCandidate==QStringLiteral("left")?QStringLiteral("停靠\n左侧"):QStringLiteral("停靠\n右侧"));
        const QRect fitted=boundedFloatingGeometry(QRect(m_dockCandidate==QStringLiteral("left")?8:std::max(8,m_viewport->width()-88),8,80,std::max(80,m_viewport->height()-16)),false);
        if(fitted.isEmpty()){m_dockPreview->hide();return;}m_dockPreview->setGeometry(fitted);
    }else if(m_gesture&&!m_gesture->startedFloating){
        m_dockPreview->setText(QStringLiteral("悬浮"));const QRect fitted=boundedFloatingGeometry(proposed);
        if(fitted.isEmpty()){m_dockPreview->hide();return;}m_dockPreview->setGeometry(fitted);
    }
    else{m_dockPreview->hide();return;}
    m_dockPreview->show();m_dockPreview->raise();
}
void FavoritesBar::hideDockPreview()
{
    m_dockCandidate.clear();if(m_dockPreview)m_dockPreview->hide();
}
void FavoritesBar::handleGesture(int kind,int phase,const QPointF& global)
{
    if(phase==3){
        if(m_gesture){
            const auto gesture=*m_gesture;m_gesture.reset();hideDockPreview();
            static_cast<GeometryHandle*>(gesture.kind==MoveHandle?m_move:gesture.kind==TopHandle?m_topResize:m_bottomResize)->cancelContact();
            if(gesture.startedFloating&&m_floating)constrainGeometry();
        }
        return;
    }
    if(phase==2){
        if(!m_gesture||m_gesture->kind!=kind)return;
        // Some devices publish a final position only on release.
        if((global-m_gesture->origin).toPoint().manhattanLength()>=8)handleGesture(kind,1,global);
        if(!m_gesture)return;
        const auto gesture=*m_gesture;
        // Preserve the input receiver until release. Docked drags use a ghost
        // preview so reparenting cannot cancel a mouse, pen or touch sequence.
        m_gesture.reset();const QString candidate=m_dockCandidate;hideDockPreview();
        if(kind==MoveHandle&&gesture.moved){
            if(!candidate.isEmpty())applyPlacement(false,candidate);
            else if(!gesture.startedFloating){
                applyPlacement(true,m_side);
                const QRect proposed(m_viewport->mapFromGlobal(global.toPoint())-gesture.grabOffset,m_floatingGeometry.size());
                const QScopedValueRollback<bool> applying(m_applying,true);
                applyFittedFloatingGeometry(boundedFloatingGeometry(proposed));
                if(!m_noFloatingSpace)rememberFloatingGeometry(false);
            }else if(geometry()!=gesture.geometry)rememberFloatingGeometry(false);
        }
        else if(kind!=MoveHandle&&gesture.moved&&geometry()!=gesture.geometry)rememberFloatingGeometry(true);
        recordLayout();return;
    }
    if(m_preferences.pinned||!m_writable||m_hostClosing||!m_viewport)return;
    if(phase==0){
        if(kind!=MoveHandle&&!m_floating)return;
        constrainGeometry();
        // This start snapshot already incorporates the current overlays. A
        // previously queued reflow must not cancel a contact that began later.
        m_reservedGeometry=reservedOverlayRects();m_avoidanceTimer.stop();
        m_layoutTimer.stop();hideDockPreview();
        const int floatingMoveInset=ResizeEdgeHeight+ReaderButtonSize/2+1;
        const QPoint offset=m_floating?m_viewport->mapFromGlobal(global.toPoint())-geometry().topLeft():QPoint(40,std::max(44,boundedFloatingGeometry(m_floatingGeometry).height()-floatingMoveInset));
        m_gesture=Gesture{kind,global,geometry(),offset,m_floating,false};return;
    }
    if(!m_gesture||m_gesture->kind!=kind)return;
    const auto start=m_gesture->geometry;const QPoint delta=(global-m_gesture->origin).toPoint();
    if(delta.manhattanLength()>=8)m_gesture->moved=true;
    if(!m_gesture->moved)return;
    if(kind==MoveHandle){
        const QRect proposed=m_gesture->startedFloating?start.translated(delta)
            :QRect(m_viewport->mapFromGlobal(global.toPoint())-m_gesture->grabOffset,m_floatingGeometry.size());
        if(m_gesture->startedFloating){const QScopedValueRollback<bool> applying(m_applying,true);applyFittedFloatingGeometry(boundedFloatingGeometry(proposed));}
        showDockPreview(global,proposed);return;
    }
    const int reserve=m_viewport->width()<420?140:84;const int bottom=m_viewport->height()-reserve;
    const int minimum=std::min(minimumPaletteHeight(),std::max(80,bottom-8));QRect proposed=start;
    if(kind==BottomHandle)proposed.setHeight(std::clamp(start.height()+delta.y(),minimum,std::max(minimum,bottom-start.top())));
    else{const int end=std::min(start.bottom()+1,bottom);const int top=std::clamp(start.top()+delta.y(),8,std::max(8,end-minimum));proposed.setTop(top);proposed.setBottom(end-1);}
    const QScopedValueRollback<bool> applying(m_applying,true);applyFittedFloatingGeometry(boundedFloatingGeometry(proposed));
}
void FavoritesBar::keyboardGeometry(int kind,int key,bool accelerated)
{
    if(m_preferences.pinned||!m_writable||m_hostClosing||m_gesture)return;
    if(kind!=MoveHandle&&(key==Qt::Key_Left||key==Qt::Key_Right))return;
    QPointer<QWidget> previousFocus=QApplication::focusWidget();
    if(!m_floating)applyPlacement(true,m_side);
    const QRect before=geometry();
    const int step=accelerated?24:8;QRect proposed=before;
    const QPoint delta(key==Qt::Key_Left?-step:key==Qt::Key_Right?step:0,key==Qt::Key_Up?-step:key==Qt::Key_Down?step:0);
    if(kind==MoveHandle)proposed.translate(delta);
    else if(kind==TopHandle)proposed.setTop(std::min(proposed.bottom()-minimumPaletteHeight()+1,proposed.top()+delta.y()));
    else proposed.setHeight(std::max(minimumPaletteHeight(),proposed.height()+delta.y()));
    {const QScopedValueRollback<bool> applying(m_applying,true);applyFittedFloatingGeometry(boundedFloatingGeometry(proposed));}
    if(!m_noFloatingSpace&&geometry()!=before)rememberFloatingGeometry(kind!=MoveHandle);
    if(previousFocus&&(previousFocus==this||isAncestorOf(previousFocus)))previousFocus->setFocus(Qt::OtherFocusReason);
    m_layoutTimer.start();
}
FavoritesBarPreferences FavoritesBar::currentLayout()const
{
    auto p=m_preferences;p.visible=requestedVisible();p.collapsed=false;p.floating=m_floating;p.side=m_side;
    p.geometry=QRect(std::clamp(m_floatingGeometry.x(),-100000,100000),std::clamp(m_floatingGeometry.y(),-100000,100000),80,std::clamp(m_floatingGeometry.height(),80,1600));
    p.floatingAnchor=m_floatingAnchor;p.floatingEdgeOffset=m_floatingEdgeOffset;
    p.floatingVerticalRatio=m_floatingVerticalRatio;p.floatingPositionInitialized=m_floatingPositionInitialized;
    return p;
}
void FavoritesBar::recordLayout()
{
    if(m_applying||!m_writable||m_hostClosing||(!isVisible()&&!m_noFloatingSpace))return;const auto p=currentLayout();if(favoritesBarJson(p)!=favoritesBarJson(m_preferences))Q_EMIT barPreferencesEdited(p);
}
bool FavoritesBar::requestedVisible()const{return m_visibilityOverride.value_or(m_preferences.visible);}
void FavoritesBar::syncVisibility(){m_visibilityAction->setChecked(requestedVisible());syncDockHosts();if(m_floating&&requestedVisible()&&!m_hostClosing)constrainGeometry();else setVisible(requestedVisible()&&!m_hostClosing);if(isVisible())raise();}
void FavoritesBar::setUserVisible(bool visible){if(m_hostClosing)return;m_visibilityOverride=visible;auto candidate=currentLayout();m_layoutTimer.stop();syncVisibility();Q_EMIT barPreferencesEdited(candidate);}
void FavoritesBar::prepareForHostClose(){m_hostClosing=true;m_gesture.reset();hideDockPreview();m_layoutTimer.stop();m_avoidanceTimer.stop();syncDockHosts();hide();}
bool FavoritesBar::eventFilter(QObject* watched,QEvent* e)
{
    const auto type=e->type();
    if(type==QEvent::Move||type==QEvent::Resize||type==QEvent::Show||type==QEvent::Hide
        ||type==QEvent::LayoutRequest||type==QEvent::ParentChange||type==QEvent::StyleChange||type==QEvent::FontChange){
        for(const auto& overlay:m_reservedOverlays)if(overlay==watched){scheduleAvoidanceReflow();break;}
    }
    if(m_gesture&&(e->type()==QEvent::ShortcutOverride||e->type()==QEvent::KeyPress)&&static_cast<QKeyEvent*>(e)->key()==Qt::Key_Escape){
        e->accept();
        if(e->type()==QEvent::KeyPress)handleGesture(m_gesture->kind,3,m_gesture->origin);
        return true;
    }
    if(m_gesture&&watched==window()&&e->type()==QEvent::WindowDeactivate)handleGesture(m_gesture->kind,3,m_gesture->origin);
    if(watched==m_scroll->viewport()&&e->type()==QEvent::Wheel){const auto* wheel=static_cast<QWheelEvent*>(e);auto* bar=m_scroll->verticalScrollBar();const int delta=wheel->pixelDelta().y()!=0?wheel->pixelDelta().y():qRound(wheel->angleDelta().y()*52.0/120);bar->setValue(bar->value()-delta);e->accept();return true;}
    if(watched==m_viewport&&!m_applying&&(e->type()==QEvent::Resize||e->type()==QEvent::Show)){
        // A resize changes the drag coordinate space. Cancel an active contact
        // and reflow its last committed position rather than saving a jump.
        if(m_gesture)handleGesture(m_gesture->kind,3,m_gesture->origin);
        constrainGeometry();
    }
    return QWidget::eventFilter(watched,e);
}
bool FavoritesBar::event(QEvent* e)
{
    const auto type=e->type();const bool result=QWidget::event(e);
    if(type==QEvent::Resize)placeResizeEdges();
    if(m_scroll&&(type==QEvent::PaletteChange||type==QEvent::ApplicationPaletteChange)){m_pan->setIcon(uiIcon(QStringLiteral("hand"),palette()));m_select->setIcon(uiIcon(QStringLiteral("select"),palette()));m_selectMore->setIcon(uiIcon(QStringLiteral("ellipsis"),palette()));m_add->setIcon(uiIcon(QStringLiteral("plus"),palette()));m_move->setIcon(uiIcon(QStringLiteral("move"),palette()));rebuild();}return result;
}
void FavoritesBar::closeEvent(QCloseEvent* e){if(!m_hostClosing)setUserVisible(false);e->accept();}
}
