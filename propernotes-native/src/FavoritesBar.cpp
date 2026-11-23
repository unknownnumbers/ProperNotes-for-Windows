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
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QPointer>
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
#include <functional>

namespace propernotes {
namespace {
constexpr int MoveHandle=0, TopHandle=1, BottomHandle=2;
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
protected:
    bool event(QEvent* event) override {
        if(event->type()==QEvent::TouchBegin||event->type()==QEvent::TouchUpdate||event->type()==QEvent::TouchEnd||event->type()==QEvent::TouchCancel){
            auto* touch=static_cast<QTouchEvent*>(event);
            if(event->type()==QEvent::TouchBegin){if(!isEnabled()||m_mouse||touch->points().isEmpty())return false;m_touch=true;setDown(true);m_last=touch->points().first().globalPosition();m_gesture(0,m_last);}
            else if(m_touch){if(!touch->points().isEmpty())m_last=touch->points().first().globalPosition();if(event->type()==QEvent::TouchUpdate)m_gesture(1,m_last);else{m_touch=false;setDown(false);m_gesture(2,m_last);}}
            event->accept();return true;
        }
        if(event->type()==QEvent::UngrabMouse&&m_mouse){m_mouse=false;setDown(false);m_gesture(2,m_last);}
        if(event->type()==QEvent::Hide&&(m_mouse||m_touch)){m_mouse=m_touch=false;setDown(false);m_gesture(2,m_last);}
        return QToolButton::event(event);
    }
    void mousePressEvent(QMouseEvent* e) override {if(e->button()!=Qt::LeftButton||m_touch){e->ignore();return;}m_mouse=true;m_last=e->globalPosition();setDown(true);grabMouse();m_gesture(0,m_last);e->accept();}
    void mouseMoveEvent(QMouseEvent* e) override {if(!m_mouse){QToolButton::mouseMoveEvent(e);return;}m_last=e->globalPosition();m_gesture(1,m_last);e->accept();}
    void mouseReleaseEvent(QMouseEvent* e) override {if(!m_mouse||e->button()!=Qt::LeftButton){e->ignore();return;}m_mouse=false;m_last=e->globalPosition();setDown(false);releaseMouse();m_gesture(2,m_last);e->accept();}
    void tabletEvent(QTabletEvent* e) override {if(e->type()==QEvent::TabletPress){m_last=e->globalPosition();m_gesture(0,m_last);setDown(true);}else if(e->type()==QEvent::TabletMove){m_last=e->globalPosition();m_gesture(1,m_last);}else if(e->type()==QEvent::TabletRelease){m_last=e->globalPosition();m_gesture(2,m_last);setDown(false);}e->accept();}
    void keyPressEvent(QKeyEvent* e) override {if(e->key()==Qt::Key_Up||e->key()==Qt::Key_Down||e->key()==Qt::Key_Left||e->key()==Qt::Key_Right){m_key(e->key(),e->modifiers().testFlag(Qt::ShiftModifier));e->accept();}else QToolButton::keyPressEvent(e);}
private:
    Gesture m_gesture;std::function<void(int,bool)> m_key;QPointF m_last;bool m_mouse=false,m_touch=false;
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

FavoritesBar::FavoritesBar(QWidget* parent):QWidget(parent)
{
    setObjectName(QStringLiteral("propernotesFavoritesBar"));setAttribute(Qt::WA_StyledBackground);setAutoFillBackground(true);
    setStyleSheet(QStringLiteral("QWidget#propernotesFavoritesBar{border:1px solid palette(mid);border-radius:12px;background:palette(window);} QToolButton{border:0;border-radius:7px;padding:0;} QToolButton:hover{background:palette(alternate-base);} QToolButton:checked{background:palette(highlight);}"));
    setFixedWidth(80);auto* layout=new QVBoxLayout(this);layout->setContentsMargins(8,6,8,6);layout->setSpacing(2);
    auto handle=[this,layout](int kind,const QString& label,const QString& icon){auto* b=new GeometryHandle([this,kind](int phase,const QPointF& point){handleGesture(kind,phase,point);},[this,kind](int key,bool fast){keyboardGeometry(kind,key,fast);},this);b->setToolTip(label);b->setAccessibleName(label);b->setIcon(uiIcon(icon,palette()));b->setIconSize(QSize(24,24));b->setFixedHeight(44);b->setCursor(kind==MoveHandle?Qt::SizeAllCursor:Qt::SizeVerCursor);layout->addWidget(b);return b;};
    m_topResize=handle(TopHandle,QStringLiteral("拖动上沿调整高度，或使用上下方向键"),QStringLiteral("grip"));
    auto button=[this,layout](const QString& label,const QString& icon,int height=44){auto* b=new QToolButton(this);b->setAutoRaise(true);b->setFixedHeight(height);b->setIcon(uiIcon(icon,palette()));b->setIconSize(QSize(26,26));b->setToolTip(label);b->setAccessibleName(label);layout->addWidget(b);return b;};
    m_pan=button(QStringLiteral("平移"),QStringLiteral("hand"));m_pan->setCheckable(true);m_select=button(QStringLiteral("框选／移动"),QStringLiteral("select"));m_select->setCheckable(true);m_selectMore=button(QStringLiteral("框选选项"),QStringLiteral("ellipsis"));m_selectMore->hide();
    auto separator=[this,layout]{auto* line=new QFrame(this);line->setFrameShape(QFrame::HLine);line->setFixedHeight(8);layout->addWidget(line);};separator();
    m_scroll=new QScrollArea(this);m_scroll->setObjectName(QStringLiteral("favoritesScroll"));m_scroll->setWidgetResizable(true);m_scroll->setFrameShape(QFrame::NoFrame);m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);m_scroll->setMinimumHeight(0);m_scroll->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Expanding);
    m_entries=new QWidget;m_entriesLayout=new QVBoxLayout(m_entries);m_entriesLayout->setContentsMargins(0,2,0,2);m_entriesLayout->setSpacing(2);m_scroll->setWidget(m_entries);m_scroll->viewport()->installEventFilter(this);QScroller::grabGesture(m_scroll->viewport(),QScroller::TouchGesture);layout->addWidget(m_scroll,1);separator();
    m_add=button(QStringLiteral("工具与选项：加入工具栏或使用"),QStringLiteral("plus"));m_move=handle(MoveHandle,QStringLiteral("移动工具栏，或使用方向键"),QStringLiteral("move"));m_bottomResize=handle(BottomHandle,QStringLiteral("拖动下沿调整高度，或使用上下方向键"),QStringLiteral("grip"));
    connect(m_pan,&QToolButton::clicked,this,[this]{if(m_activationEnabled)Q_EMIT fixedToolRequested(InkCanvas::Tool::Pan);});connect(m_select,&QToolButton::clicked,this,[this]{if(m_activationEnabled)Q_EMIT fixedToolRequested(InkCanvas::Tool::Select);});connect(m_selectMore,&QToolButton::clicked,this,&FavoritesBar::selectionOptionsRequested);connect(m_add,&QToolButton::clicked,this,&FavoritesBar::openLibrary);
    m_visibilityAction=new QAction(QStringLiteral("显示收藏工具栏"),this);m_visibilityAction->setCheckable(true);connect(m_visibilityAction,&QAction::triggered,this,&FavoritesBar::setUserVisible);
    m_layoutTimer.setSingleShot(true);m_layoutTimer.setInterval(300);connect(&m_layoutTimer,&QTimer::timeout,this,&FavoritesBar::recordLayout);
    parent->installEventFilter(this);setContextMenuPolicy(Qt::CustomContextMenu);connect(this,&QWidget::customContextMenuRequested,this,[this](const QPoint& point){showMenu(mapToGlobal(point));});resize(80,420);syncPlacementControls();rebuild();syncVisibility();
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
    if(m_writable&&!writable&&(m_layoutTimer.isActive()||m_gesture)){m_layoutTimer.stop();pending=currentLayout();}
    m_writable=writable;if(!writable)m_gesture.reset();syncPlacementControls();
    if(pending&&!m_hostClosing&&favoritesBarJson(*pending)!=favoritesBarJson(m_preferences))Q_EMIT barPreferencesEdited(*pending);
}
void FavoritesBar::setActivationEnabled(bool enabled)
{m_activationEnabled=enabled;for(auto* b:m_buttons)b->setEnabled(enabled);m_pan->setEnabled(enabled);m_select->setEnabled(enabled);m_selectMore->setEnabled(enabled);m_add->setEnabled(enabled||m_writable);}
void FavoritesBar::setPinned(bool pinned)
{if(!m_writable||m_hostClosing)return;m_gesture.reset();m_layoutTimer.stop();auto preferences=currentLayout();preferences.pinned=pinned;Q_EMIT barPreferencesEdited(preferences);}
void FavoritesBar::syncPlacementControls()
{
    const bool unlocked=!m_preferences.pinned;m_move->setVisible(unlocked);m_topResize->setVisible(unlocked);m_bottomResize->setVisible(unlocked);m_move->setEnabled(m_writable);m_topResize->setEnabled(m_writable);m_bottomResize->setEnabled(m_writable);m_add->setEnabled(m_activationEnabled||m_writable);
}
void FavoritesBar::rebuild()
{
    const int scroll=m_scroll->verticalScrollBar()->value();m_buttons.clear();m_moreButtons.clear();while(auto* item=m_entriesLayout->takeAt(0)){if(auto* widget=item->widget()){widget->hide();widget->deleteLater();}delete item;}
    for(const auto& favorite:m_favorites){auto* row=new QWidget(m_entries);auto* layout=new QVBoxLayout(row);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);auto* button=new QToolButton(row);button->setAutoRaise(true);button->setFixedSize(60,52);button->setIcon(favoriteIcon(favorite,palette()));button->setIconSize(QSize(52,52));button->setCheckable(true);button->setEnabled(m_activationEnabled);button->setToolTip(tooltip(favoriteDescription(favorite)));button->setAccessibleName(favoriteDescription(favorite));layout->addWidget(button);m_buttons.insert(favorite.id,button);
        auto* more=new QToolButton(row);more->setAutoRaise(true);more->setFixedSize(60,44);more->setIcon(uiIcon(QStringLiteral("ellipsis"),palette()));more->setIconSize(QSize(22,22));more->setToolTip(QStringLiteral("此收藏工具的选项"));more->setAccessibleName(more->toolTip());layout->addWidget(more);m_moreButtons.insert(favorite.id,more);m_entriesLayout->addWidget(row);
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
    menu.addSeparator();auto* hide=menu.addAction(QStringLiteral("隐藏收藏工具栏"));connect(hide,&QAction::triggered,this,[this]{setUserVisible(false);});menu.exec(global);
}
void FavoritesBar::setBarPreferences(const FavoritesBarPreferences& preferences)
{
    if(!validFavoritesBarPreferences(preferences)||m_hostClosing)return;const bool geometryChanged=!m_preferencesApplied||preferences.geometry!=m_preferences.geometry;const QScopedValueRollback<bool> applying(m_applying,true);m_preferences=preferences;m_preferencesApplied=true;if(m_visibilityOverride&&*m_visibilityOverride==preferences.visible)m_visibilityOverride.reset();m_gesture.reset();syncPlacementControls();if(geometryChanged)setGeometry(QRect(preferences.geometry.topLeft(),QSize(80,preferences.geometry.height())));constrainGeometry();syncVisibility();
}
int FavoritesBar::minimumPaletteHeight()const
{
    int height=12+16+80;for(auto* button:{m_pan,m_select,m_selectMore,m_add,m_move,m_topResize,m_bottomResize})if(!button->isHidden())height+=button->height()+2;return height;
}
void FavoritesBar::constrainGeometry()
{
    if(!parentWidget()||m_hostClosing)return;const QScopedValueRollback<bool> applying(m_applying,true);const int availableHeight=std::max(80,parentWidget()->height()-92);const int minimum=std::min(minimumPaletteHeight(),availableHeight);const int height=std::clamp(this->height(),minimum,availableHeight);const int x=std::clamp(this->x(),8,std::max(8,parentWidget()->width()-width()-8));const int y=std::clamp(this->y(),8,std::max(8,parentWidget()->height()-height-84));setGeometry(x,y,width(),height);raise();
}
void FavoritesBar::handleGesture(int kind,int phase,const QPointF& global)
{
    if(phase==2){if(m_gesture){m_gesture.reset();recordLayout();}return;}if(m_preferences.pinned||!m_writable||m_hostClosing)return;if(phase==0){m_layoutTimer.stop();m_gesture=Gesture{kind,global,geometry()};return;}if(!m_gesture||m_gesture->kind!=kind)return;
    const auto start=m_gesture->geometry;const QPoint delta=(global-m_gesture->origin).toPoint();const int bottom=parentWidget()->height()-84;const int minimum=std::min(minimumPaletteHeight(),std::max(80,bottom-8));QRect proposed=start;
    if(kind==MoveHandle)proposed.translate(delta);else if(kind==BottomHandle)proposed.setHeight(std::clamp(start.height()+delta.y(),minimum,std::max(minimum,bottom-start.top())));else{const int end=std::min(start.bottom()+1,bottom);const int top=std::clamp(start.top()+delta.y(),8,std::max(8,end-minimum));proposed.setTop(top);proposed.setBottom(end-1);}const QScopedValueRollback<bool> applying(m_applying,true);setGeometry(proposed);constrainGeometry();
}
void FavoritesBar::keyboardGeometry(int kind,int key,bool accelerated)
{
    if(m_preferences.pinned||!m_writable||m_hostClosing)return;if(kind!=MoveHandle&&(key==Qt::Key_Left||key==Qt::Key_Right))return;const int step=accelerated?24:8;const QPointF delta(key==Qt::Key_Left?-step:key==Qt::Key_Right?step:0,key==Qt::Key_Up?-step:key==Qt::Key_Down?step:0);handleGesture(kind,0,{});handleGesture(kind,1,delta);handleGesture(kind,2,delta);
}
FavoritesBarPreferences FavoritesBar::currentLayout()const
{
    auto p=m_preferences;p.visible=requestedVisible();p.collapsed=false;p.floating=true;p.geometry=QRect(std::clamp(x(),-100000,100000),std::clamp(y(),-100000,100000),80,std::clamp(height(),80,1600));return p;
}
void FavoritesBar::recordLayout()
{
    if(m_applying||!m_writable||m_hostClosing||!isVisible())return;const auto p=currentLayout();if(favoritesBarJson(p)!=favoritesBarJson(m_preferences))Q_EMIT barPreferencesEdited(p);
}
bool FavoritesBar::requestedVisible()const{return m_visibilityOverride.value_or(m_preferences.visible);}
void FavoritesBar::syncVisibility(){m_visibilityAction->setChecked(requestedVisible());setVisible(requestedVisible()&&!m_hostClosing);if(isVisible()){constrainGeometry();raise();}}
void FavoritesBar::setUserVisible(bool visible){if(m_hostClosing)return;m_visibilityOverride=visible;auto candidate=currentLayout();m_layoutTimer.stop();syncVisibility();Q_EMIT barPreferencesEdited(candidate);}
void FavoritesBar::prepareForHostClose(){m_hostClosing=true;m_gesture.reset();m_layoutTimer.stop();hide();}
bool FavoritesBar::eventFilter(QObject* watched,QEvent* e)
{
    if(watched==m_scroll->viewport()&&e->type()==QEvent::Wheel){const auto* wheel=static_cast<QWheelEvent*>(e);auto* bar=m_scroll->verticalScrollBar();const int delta=wheel->pixelDelta().y()!=0?wheel->pixelDelta().y():qRound(wheel->angleDelta().y()*52.0/120);bar->setValue(bar->value()-delta);e->accept();return true;}
    if(watched==parentWidget()&&(e->type()==QEvent::Resize||e->type()==QEvent::Show))constrainGeometry();return QWidget::eventFilter(watched,e);
}
bool FavoritesBar::event(QEvent* e)
{
    const auto type=e->type();const bool result=QWidget::event(e);if(m_scroll&&!m_applying&&!m_hostClosing&&!m_gesture&&(type==QEvent::Move||type==QEvent::Resize))m_layoutTimer.start();
    if(m_scroll&&(type==QEvent::PaletteChange||type==QEvent::ApplicationPaletteChange)){m_pan->setIcon(uiIcon(QStringLiteral("hand"),palette()));m_select->setIcon(uiIcon(QStringLiteral("select"),palette()));m_selectMore->setIcon(uiIcon(QStringLiteral("ellipsis"),palette()));m_add->setIcon(uiIcon(QStringLiteral("plus"),palette()));m_move->setIcon(uiIcon(QStringLiteral("move"),palette()));m_topResize->setIcon(uiIcon(QStringLiteral("grip"),palette()));m_bottomResize->setIcon(uiIcon(QStringLiteral("grip"),palette()));rebuild();}return result;
}
void FavoritesBar::closeEvent(QCloseEvent* e){if(!m_hostClosing)setUserVisible(false);e->accept();}
}
