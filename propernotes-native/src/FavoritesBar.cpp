// SPDX-License-Identifier: GPL-3.0-only
#include "FavoritesBar.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QSpinBox>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <utility>

namespace propernotes {
namespace {
constexpr auto DragFormat = "application/x-propernotes-favorite-tool";
QIcon toolIcon(const FavoriteTool& favorite)
{
    QPixmap pixmap(56,56);pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);p.setRenderHint(QPainter::Antialiasing);p.scale(2,2);
    const bool coloredTool = favorite.markupKind || favorite.tool == InkCanvas::Tool::Pen
        || favorite.tool == InkCanvas::Tool::Highlighter || favorite.tool == InkCanvas::Tool::TextBox;
    const QColor color = coloredTool ? favorite.color : QColor("#3977bd");
    p.setPen(QPen(color,1.6,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);
    if (favorite.markupKind) {
        const auto kind = *favorite.markupKind;
        if (kind == TextMarkupKind::Highlight) {
            QColor fill = color;
            fill.setAlpha(124);
            p.fillRect(QRectF(3, 6, 22, 15), fill);
        }
        p.setPen(QPen(QColor("#475569"), 1.5));
        p.drawLine(QPointF(5, 9), QPointF(23, 9));
        p.drawLine(QPointF(5, 14), QPointF(21, 14));
        p.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (kind == TextMarkupKind::Underline) p.drawLine(QPointF(5, 20), QPointF(23, 20));
        if (kind == TextMarkupKind::StrikeOut) p.drawLine(QPointF(4, 12), QPointF(24, 12));
        if (kind == TextMarkupKind::Squiggly) {
            QPolygonF wave;
            for (int x = 5; x <= 23; x += 3) wave << QPointF(x, ((x - 5) / 3) % 2 ? 18 : 21);
            p.drawPolyline(wave);
        }
    } else if(favorite.tool==InkCanvas::Tool::TextSelect){
        p.drawLine(QPointF(14,5), QPointF(14,23));
        p.drawLine(QPointF(10,5), QPointF(18,5));
        p.drawLine(QPointF(10,23), QPointF(18,23));
    } else if (favorite.tool == InkCanvas::Tool::Highlighter) {
        p.setBrush(color);
        p.setPen(QPen(QColor("#475569"), 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPolygonF marker;
        marker << QPointF(6, 18) << QPointF(17, 6) << QPointF(23, 12) << QPointF(12, 24) << QPointF(6, 24);
        p.drawPolygon(marker);
        p.drawLine(QPointF(6, 18), QPointF(12, 24));
    } else if(favorite.tool==InkCanvas::Tool::Pen){
        p.drawLine(QPointF(7,21),QPointF(21,7));p.drawLine(QPointF(10,23),QPointF(23,10));
        p.drawLine(QPointF(21,7),QPointF(23,10));p.drawLine(QPointF(7,21),QPointF(6,24));p.drawLine(QPointF(6,24),QPointF(10,23));
    }else if(favorite.tool==InkCanvas::Tool::ObjectEraser){
        QPolygonF shape;shape<<QPointF(5,17)<<QPointF(16,6)<<QPointF(24,14)<<QPointF(14,24)<<QPointF(11,24);p.drawPolygon(shape);
        p.drawLine(QPointF(10,12),QPointF(19,21));
        if(favorite.tools.eraserMode==QStringLiteral("partial")){p.setPen(QPen(color,1,Qt::DashLine));p.drawLine(QPointF(3,25),QPointF(25,25));}
    }else if(favorite.tool==InkCanvas::Tool::TextBox){
        p.setPen(QPen(color,1,Qt::DashLine));p.drawRect(QRectF(3,4,22,20));p.setPen(QPen(color,1.8));
        p.drawLine(QPointF(8,9),QPointF(20,9));p.drawLine(QPointF(14,9),QPointF(14,21));p.drawLine(QPointF(11,21),QPointF(17,21));
    }else if(favorite.tool==InkCanvas::Tool::Select){
        p.setPen(QPen(color,1.3,Qt::DashLine));p.drawRect(QRectF(4,4,19,18));p.setPen(QPen(color,1.5));
        p.drawLine(QPointF(15,14),QPointF(24,23));p.drawLine(QPointF(24,23),QPointF(18,23));p.drawLine(QPointF(24,23),QPointF(24,17));
    }else{
        p.drawLine(QPointF(14,3),QPointF(14,25));p.drawLine(QPointF(3,14),QPointF(25,14));
        for(const auto& d:QVector<QPointF>{{0,-1},{1,0},{0,1},{-1,0}}){
            const QPointF tip=QPointF(14,14)+d*11;const QPointF side(-d.y(),d.x());
            p.drawLine(tip,tip-d*4+side*3);p.drawLine(tip,tip-d*4-side*3);
        }
    }
    pixmap.setDevicePixelRatio(2);return QIcon(pixmap);
}
QString literalButtonText(QString text){return text.replace(QChar('&'),QStringLiteral("&&"));}
QString tooltip(const QString& text){return QStringLiteral("<qt>")+text.toHtmlEscaped().replace(QLatin1Char('\n'),QStringLiteral("<br>"))+QStringLiteral("</qt>");}
QIcon positionPinIcon()
{
    QIcon icon;
    for (const bool pinned : {false, true}) {
        QPixmap pixmap(28, 28);
        pixmap.setDevicePixelRatio(2);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(7, 7);
        if (!pinned) painter.rotate(34);
        painter.setPen(QPen(QColor(pinned ? "#387bc5" : "#718096"), 1.35,
                            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(-2.5, -4.5), QPointF(2.5, -4.5));
        painter.drawPolyline(QPolygonF{QPointF(-1.5, -4.5), QPointF(-1.7, -1.8), QPointF(-3.1, 0.5),
                                       QPointF(3.1, 0.5), QPointF(1.7, -1.8), QPointF(1.5, -4.5)});
        painter.drawLine(QPointF(0, 0.5), QPointF(0, 5.2));
        painter.end();
        icon.addPixmap(pixmap, QIcon::Normal, pinned ? QIcon::On : QIcon::Off);
    }
    return icon;
}
bool sameRecipe(const FavoriteTool& a,const FavoriteTool& b)
{
    if (a.markupKind || b.markupKind)
        return a.markupKind == b.markupKind && a.color == b.color
            && qFuzzyCompare(a.tools.opacity, b.tools.opacity) && a.reviewSingleUse == b.reviewSingleUse;
    auto left=captureFavorite(a.tool,a.tools,a.color,a.widthPt,favoriteToolName(a.tool));
    auto right=captureFavorite(b.tool,b.tools,b.color,b.widthPt,favoriteToolName(b.tool));
    left.id=QUuid();right.id=QUuid();return favoriteToolsJson({left})==favoriteToolsJson({right});
}

class FavoriteButton final : public QToolButton {
public:
    FavoriteButton(const QUuid& id,const QString& token,std::function<bool()> writable,QWidget* parent)
        :QToolButton(parent),m_id(id),m_token(token),m_writable(std::move(writable)){}
protected:
    void mousePressEvent(QMouseEvent* event)override{m_start=event->position().toPoint();QToolButton::mousePressEvent(event);}
    void mouseMoveEvent(QMouseEvent* event)override{
        if(event->buttons().testFlag(Qt::LeftButton)&&m_writable()&&(event->position().toPoint()-m_start).manhattanLength()>=QApplication::startDragDistance()){
            setDown(false);auto* drag=new QDrag(this);auto* mime=new QMimeData;
            mime->setData(DragFormat,m_token.toUtf8()+QByteArray("\n")+m_id.toString(QUuid::WithoutBraces).toUtf8());drag->setMimeData(mime);
            drag->setPixmap(icon().pixmap(QSize(28,28)));drag->exec(Qt::MoveAction);drag->deleteLater();return;
        }QToolButton::mouseMoveEvent(event);
    }
private:QUuid m_id;QString m_token;std::function<bool()> m_writable;QPoint m_start;
};

// A draft editor only. Nothing becomes active or persistent until the host
// successfully commits the emitted candidate to its protected settings store.
class FavoriteEditor final : public QDialog {
public:
    FavoriteEditor(const FavoriteTool& original,QWidget* parent):QDialog(parent),m_original(original),m_color(original.color){
        setWindowTitle(QStringLiteral("编辑工具收藏"));auto* box=new QVBoxLayout(this);auto* form=new QFormLayout;
        m_name=new QLineEdit(original.name);m_name->setMaxLength(80);form->addRow(QStringLiteral("名称"),m_name);
        m_kind=new QComboBox;
        for (const auto kind : {InkCanvas::Tool::Pen, InkCanvas::Tool::Highlighter, InkCanvas::Tool::ObjectEraser,
                               InkCanvas::Tool::TextBox, InkCanvas::Tool::Select, InkCanvas::Tool::Pan})
            m_kind->addItem(favoriteToolName(kind), int(kind));
        for (auto kind : {TextMarkupKind::Highlight, TextMarkupKind::Underline, TextMarkupKind::StrikeOut, TextMarkupKind::Squiggly})
            m_kind->addItem(textMarkupKindName(kind), MarkupOffset + int(kind));
        m_kind->setCurrentIndex(m_kind->findData(original.markupKind ? MarkupOffset + int(*original.markupKind) : int(original.tool)));
        form->addRow(QStringLiteral("工具"),m_kind);
        m_colorButton=new QToolButton;m_colorButton->setText(QStringLiteral("选择颜色…"));form->addRow(QStringLiteral("颜色"),m_colorButton);
        m_width=new QDoubleSpinBox;m_width->setRange(0.5,24);m_width->setSuffix(QStringLiteral(" pt"));form->addRow(QStringLiteral("笔宽"),m_width);
        m_opacity=new QDoubleSpinBox;m_opacity->setRange(0,100);m_opacity->setSuffix(QStringLiteral("%"));form->addRow(QStringLiteral("不透明度"),m_opacity);
        m_pressure=new QCheckBox(QStringLiteral("启用笔压"));form->addRow(m_pressure);
        m_sensitivity=new QSpinBox;m_sensitivity->setRange(0,100);form->addRow(QStringLiteral("笔压灵敏度"),m_sensitivity);
        m_mode=new QComboBox;m_mode->addItem(QStringLiteral("整笔擦除"),"whole");m_mode->addItem(QStringLiteral("局部擦除"),"partial");form->addRow(QStringLiteral("擦除模式"),m_mode);
        m_radius=new QDoubleSpinBox;m_radius->setRange(1,100);m_radius->setSuffix(QStringLiteral(" pt"));form->addRow(QStringLiteral("橡皮半径"),m_radius);
        m_family=new QFontComboBox;form->addRow(QStringLiteral("字体"),m_family);
        m_size=new QDoubleSpinBox;m_size->setRange(4,144);m_size->setSuffix(QStringLiteral(" pt"));form->addRow(QStringLiteral("字号"),m_size);
        m_single=new QCheckBox(QStringLiteral("完成一次后返回上个持续工具"));form->addRow(m_single);
        box->addLayout(form);auto* hint=new QLabel(QStringLiteral("仅保存本工具适用的样式。不会更改已存在批注、其他收藏、输入开关或阅读布局。"));hint->setWordWrap(true);box->addWidget(hint);
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);box->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::accepted,this,[this]{auto candidate=value();QString error;if(!validFavoriteTools({candidate},&error)){QMessageBox::warning(this,QStringLiteral("无法保存收藏"),error);return;}accept();});
        connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
        connect(m_colorButton,&QToolButton::clicked,this,[this]{auto chosen=QColorDialog::getColor(m_color,this,QStringLiteral("工具颜色"));if(chosen.isValid()){m_color=chosen;updateColor();}});
        connect(m_kind,&QComboBox::currentIndexChanged,this,[this](int){showKind();});
        showKind();
        const bool highlighter = original.tool == InkCanvas::Tool::Highlighter;
        m_width->setValue(original.widthPt);
        m_opacity->setValue(highlighter ? original.tools.highlighterOpacity : original.tools.opacity * 100);
        m_pressure->setChecked(original.tools.pressureEnabled);m_sensitivity->setValue(original.tools.pressureSensitivity);
        m_mode->setCurrentIndex(m_mode->findData(original.tools.eraserMode));m_radius->setValue(original.tools.eraserRadiusPt);m_family->setCurrentFont(QFont(original.tools.textFamily));m_size->setValue(original.tools.textSizePt);
        bool single = false;
        switch (original.tool) {
        case InkCanvas::Tool::Pen: single = original.tools.penSingleUse; break;
        case InkCanvas::Tool::Highlighter: single = original.tools.highlighterSingleUse; break;
        case InkCanvas::Tool::ObjectEraser: single = original.tools.eraserSingleUse; break;
        case InkCanvas::Tool::TextBox: single = original.tools.textSingleUse; break;
        case InkCanvas::Tool::TextSelect: single = original.markupKind && original.reviewSingleUse; break;
        default: break;
        }
        m_single->setChecked(single);
        updateColor();
    }
    FavoriteTool value()const{
        ToolPreferences tools;tools.opacity=m_opacity->value()/100;tools.pressureEnabled=m_pressure->isChecked();tools.pressureSensitivity=m_sensitivity->value();
        tools.eraserMode=m_mode->currentData().toString();tools.eraserRadiusPt=m_radius->value();tools.textFamily=m_family->currentFont().family();tools.textSizePt=m_size->value();
        tools.penSingleUse=tools.eraserSingleUse=tools.textSingleUse=m_single->isChecked();
        const int selection = m_kind->currentData().toInt();
        const QString name = m_name->text().trimmed();
        FavoriteTool result;
        if (selection >= MarkupOffset) {
            result = captureReviewFavorite(TextMarkupKind(selection - MarkupOffset), m_color, name, m_single->isChecked());
            result.tools.opacity = tools.opacity;
        } else {
            if (selection == int(InkCanvas::Tool::Highlighter)) {
                tools.highlighterColor = m_color;
                tools.highlighterWidthPt = m_width->value();
                tools.highlighterOpacity = qRound(m_opacity->value());
                tools.highlighterSingleUse = m_single->isChecked();
            }
            result = captureFavorite(InkCanvas::Tool(selection),tools,m_color,m_width->value(),name);
        }
        result.id=m_original.id;
        return result;
    }
private:
    static constexpr int MarkupOffset = 100;
    void updateColor(){m_colorButton->setStyleSheet(QStringLiteral("QToolButton{color:%1;}").arg(m_color.name()));}
    void showKind()
    {
        const int selection = m_kind->currentData().toInt();
        const bool markup = selection >= MarkupOffset;
        const bool pen = selection == int(InkCanvas::Tool::Pen);
        const bool highlighter = selection == int(InkCanvas::Tool::Highlighter);
        const bool eraser = selection == int(InkCanvas::Tool::ObjectEraser);
        const bool text = selection == int(InkCanvas::Tool::TextBox);
        const bool changeAppearance = pen || highlighter || text || markup;
        m_colorButton->setEnabled(changeAppearance);
        m_opacity->setEnabled(changeAppearance);
        m_opacity->setDecimals(highlighter ? 0 : 2);
        m_opacity->setRange(highlighter ? qreal(MinHighlighterOpacity) : (markup ? 0.4 : 0), MaxHighlighterOpacity);
        m_width->setEnabled(pen || highlighter);
        m_width->setRange(highlighter ? MinHighlighterWidthPt : 0.5, highlighter ? MaxHighlighterWidthPt : 24);
        m_pressure->setEnabled(pen);
        m_sensitivity->setEnabled(pen);
        m_mode->setEnabled(eraser);
        m_radius->setEnabled(eraser);
        m_family->setEnabled(text);
        m_size->setEnabled(text);
        m_single->setEnabled(pen || highlighter || eraser || text || markup);
    }
    FavoriteTool m_original;QColor m_color;QLineEdit* m_name;QComboBox* m_kind;QToolButton* m_colorButton;QDoubleSpinBox* m_width;QDoubleSpinBox* m_opacity;
    QCheckBox* m_pressure;QSpinBox* m_sensitivity;QComboBox* m_mode;QDoubleSpinBox* m_radius;QFontComboBox* m_family;QDoubleSpinBox* m_size;QCheckBox* m_single;
};
} // namespace

FavoritesBar::FavoritesBar(QMainWindow* parent):QDockWidget(QStringLiteral("工具收藏"),parent),m_mainWindow(parent),m_dragToken(QUuid::createUuid().toString())
{
    setObjectName(QStringLiteral("propernotesFavoritesBar"));setAllowedAreas(Qt::LeftDockWidgetArea|Qt::RightDockWidgetArea);
    setFeatures(DockWidgetMovable|DockWidgetFloatable|DockWidgetClosable);setAcceptDrops(true);
    setAttribute(Qt::WA_QuitOnClose, false);
    auto* title=new QWidget;auto* titleLayout=new QHBoxLayout(title);titleLayout->setContentsMargins(4,2,4,2);
    m_collapse=new QToolButton;m_collapse->setText(QStringLiteral("▾"));m_collapse->setToolTip(QStringLiteral("折叠工具收藏"));titleLayout->addWidget(m_collapse);
    auto* label=new QLabel(QStringLiteral("收藏"));titleLayout->addWidget(label);titleLayout->addStretch();
    m_pin = new QToolButton(title);
    m_pin->setAutoRaise(true);
    m_pin->setCheckable(true);
    m_pin->setIcon(positionPinIcon());
    m_pin->setIconSize(QSize(14, 14));
    titleLayout->addWidget(m_pin);
    m_menu=new QToolButton;m_menu->setText(QStringLiteral("⋮"));m_menu->setToolTip(QStringLiteral("管理收藏栏"));titleLayout->addWidget(m_menu);
    auto* closeButton = new QToolButton(title);
    closeButton->setAutoRaise(true);
    const QIcon closeIcon = style()->standardIcon(QStyle::SP_TitleBarCloseButton);
    if (closeIcon.isNull()) closeButton->setText(QStringLiteral("×"));
    else closeButton->setIcon(closeIcon);
    closeButton->setIconSize(QSize(14, 14));
    closeButton->setToolTip(QStringLiteral("隐藏收藏栏"));
    closeButton->setAccessibleName(closeButton->toolTip());
    titleLayout->addWidget(closeButton);
    setTitleBarWidget(title);
    connect(closeButton, &QToolButton::clicked, this, [this] { close(); });
    m_contents=new QWidget;auto* contentsLayout=new QVBoxLayout(m_contents);contentsLayout->setContentsMargins(4,4,4,4);contentsLayout->setSpacing(4);
    m_current=new QToolButton;m_current->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);m_current->setIconSize(QSize(24,24));m_current->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);m_current->setMinimumHeight(34);contentsLayout->addWidget(m_current);
    auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);scroll->setFrameShape(QFrame::NoFrame);
    m_entries=new QWidget;m_entriesLayout=new QVBoxLayout(m_entries);m_entriesLayout->setContentsMargins(0,0,0,0);m_entriesLayout->setSpacing(3);scroll->setWidget(m_entries);contentsLayout->addWidget(scroll);scroll->setObjectName(QStringLiteral("favoritesScroll"));
    setWidget(m_contents);setMinimumWidth(84);resize(160,480);
    m_layoutTimer.setSingleShot(true);m_layoutTimer.setInterval(300);connect(&m_layoutTimer,&QTimer::timeout,this,&FavoritesBar::recordLayout);
    connect(m_collapse,&QToolButton::clicked,this,[this]{auto p=m_preferences;p.collapsed=!p.collapsed;proposeLayout(p);});
    connect(m_pin, &QToolButton::clicked, this, [this](bool pinned) {
        m_pin->setChecked(m_preferences.pinned);
        if (!m_writable || !m_hostAvailable || !isFloating()) return;
        auto preferences = currentLayout();
        preferences.pinned = pinned;
        m_layoutTimer.stop();
        proposeLayout(preferences);
    });
    connect(m_menu,&QToolButton::clicked,this,[this]{showMenu(m_menu->mapToGlobal(QPoint(0,m_menu->height())));});
    connect(this, &QDockWidget::topLevelChanged, this, [this](bool) {
        syncPlacementControls();
        if (!m_applying) m_layoutTimer.start();
    });
    connect(this,&QDockWidget::dockLocationChanged,this,[this](Qt::DockWidgetArea){if(!m_applying)m_layoutTimer.start();});
    m_visibilityAction = new QAction(windowTitle(), this);
    m_visibilityAction->setCheckable(true);
    connect(m_visibilityAction, &QAction::triggered, this, &FavoritesBar::setUserVisible);
    m_hostAvailable = parent->isVisible() && !parent->isMinimized();
    parent->installEventFilter(this);
    syncVisibility();
    setContextMenuPolicy(Qt::CustomContextMenu);connect(this,&QWidget::customContextMenuRequested,this,[this](const QPoint& point){showMenu(mapToGlobal(point));});
    setCurrentTool(InkCanvas::Tool::Pen,ToolPreferences{},QColor("#202833"),2.5);rebuild();setWritable(false);
}

void FavoritesBar::setFavorites(const QVector<FavoriteTool>& favorites)
{
    if(!validFavoriteTools(favorites))return;
    if(favoriteToolsJson(favorites)==favoriteToolsJson(m_favorites)){m_favorites=favorites;return;}
    m_favorites=favorites;rebuild();
}
void FavoritesBar::setCurrentTool(InkCanvas::Tool tool,const ToolPreferences& tools,const QColor& color,qreal width)
{setCurrentTool(captureFavorite(tool,tools,color,width,QStringLiteral("当前工具")));}
void FavoritesBar::setCurrentTool(const FavoriteTool& current)
{
    m_currentTool=current;m_current->setIcon(toolIcon(current));
    m_current->setText(literalButtonText(current.markupKind ? textMarkupKindName(*current.markupKind) : favoriteToolName(current.tool)));
    m_current->setToolTip(tooltip(QStringLiteral("当前工具\n")+favoriteDescription(current)));
}
void FavoritesBar::setActiveFavoriteId(const QUuid& id)
{
    m_activeId=id;
    for(auto it=m_buttons.begin();it!=m_buttons.end();++it){const int i=indexOf(it.key());it.value()->setChecked(it.key()==id&&i>=0&&sameRecipe(m_favorites[i],m_currentTool));}
}
void FavoritesBar::setWritable(bool writable)
{
    if(m_writable&&!writable&&m_layoutTimer.isActive()){m_layoutTimer.stop();recordLayout();}
    m_writable=writable;m_collapse->setEnabled(writable);
    syncPlacementControls();
}
void FavoritesBar::syncPlacementControls()
{
    const bool floating = isFloating();
    const bool pinned = floating && m_preferences.pinned;
    setFeatures(m_writable && !pinned ? DockWidgetFeatures(DockWidgetMovable | DockWidgetFloatable | DockWidgetClosable)
                                     : DockWidgetFeatures(DockWidgetClosable));
    m_pin->setVisible(floating);
    m_pin->setEnabled(m_writable && floating);
    m_pin->setChecked(pinned);
    m_pin->setToolTip(pinned ? QStringLiteral("解锁收藏栏位置") : QStringLiteral("固定收藏栏位置"));
    m_pin->setAccessibleName(m_pin->toolTip());

    // A floating QDockWidget can still move without DockWidgetMovable.
    // Filter its chrome before Qt's drag and resize handlers. Child controls
    // receive their events normally, and programmatic geometry is unaffected.
    if (pinned) installEventFilter(this);
    else removeEventFilter(this);
}
void FavoritesBar::setActivationEnabled(bool enabled)
{m_activationEnabled=enabled;for(auto* button:m_buttons)button->setEnabled(enabled);}

void FavoritesBar::rebuild()
{
    m_buttons.clear();while(auto* item=m_entriesLayout->takeAt(0)){if(auto* widget=item->widget())widget->deleteLater();delete item;}
    for(const auto& favorite:m_favorites){
        auto* button=new FavoriteButton(favorite.id,m_dragToken,[this]{return m_writable;},m_entries);button->setText(literalButtonText(favorite.name));button->setIcon(toolIcon(favorite));button->setIconSize(QSize(24,24));
        button->setCheckable(true);button->setChecked(favorite.id==m_activeId&&sameRecipe(favorite,m_currentTool));button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);button->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);button->setMinimumHeight(34);button->setEnabled(m_activationEnabled);
        button->setToolTip(tooltip(favoriteDescription(favorite)));m_entriesLayout->addWidget(button);m_buttons.insert(favorite.id,button);
        connect(button,&QToolButton::clicked,this,[this,id=favorite.id]{const int index=indexOf(id);if(index>=0&&m_activationEnabled)Q_EMIT activated(m_favorites[index]);});
        button->setContextMenuPolicy(Qt::CustomContextMenu);connect(button,&QWidget::customContextMenuRequested,this,[this,button,id=favorite.id](const QPoint& point){showMenu(button->mapToGlobal(point),id);});
    }
    if(m_favorites.isEmpty()){auto* label=new QLabel(QStringLiteral("暂无收藏\n使用菜单添加"));label->setWordWrap(true);m_entriesLayout->addWidget(label);}
    m_entriesLayout->addStretch();
}

int FavoritesBar::indexOf(const QUuid& id)const
{for(int index=0;index<m_favorites.size();++index)if(m_favorites[index].id==id)return index;return -1;}

void FavoritesBar::showMenu(const QPoint& global,const QUuid& id)
{
    QMenu menu(this);auto* add=menu.addAction(QStringLiteral("收藏当前工具…"));add->setEnabled(m_writable&&m_favorites.size()<128);
    connect(add,&QAction::triggered,this,&FavoritesBar::addCurrentRequested);
    const int index=indexOf(id);
    if(index>=0){
        menu.addSeparator();auto* edit=menu.addAction(QStringLiteral("编辑收藏…"));auto* copy=menu.addAction(QStringLiteral("复制收藏"));auto* remove=menu.addAction(QStringLiteral("删除收藏…"));
        edit->setEnabled(m_writable);copy->setEnabled(m_writable&&m_favorites.size()<128);remove->setEnabled(m_writable);
        connect(edit,&QAction::triggered,this,[this,id]{editFavorite(id);});
        connect(copy,&QAction::triggered,this,[this,id]{const int i=indexOf(id);if(i<0||!m_writable||m_favorites.size()>=128)return;auto candidate=m_favorites;auto copy=candidate[i];copy.id=QUuid::createUuid();copy.name=copy.name.left(74);if(!copy.name.isEmpty()&&copy.name.back().isHighSurrogate())copy.name.chop(1);copy.name+=QStringLiteral(" 副本");candidate.insert(i+1,copy);Q_EMIT favoritesEdited(candidate);});
        connect(remove,&QAction::triggered,this,[this,id]{const int i=indexOf(id);if(i<0||!m_writable)return;QMessageBox box(QMessageBox::Question,QStringLiteral("删除工具收藏"),{},QMessageBox::Yes|QMessageBox::No,this);box.setTextFormat(Qt::PlainText);box.setText(QStringLiteral("删除收藏“%1”？已存在批注保持原样。").arg(m_favorites[i].name));box.setDefaultButton(QMessageBox::No);if(box.exec()!=QMessageBox::Yes)return;auto candidate=m_favorites;const int current=indexOf(id);if(current<0)return;candidate.removeAt(current);Q_EMIT favoritesEdited(candidate);});
        auto* earlier=menu.addAction(QStringLiteral("上移"));auto* later=menu.addAction(QStringLiteral("下移"));earlier->setEnabled(m_writable&&index>0);later->setEnabled(m_writable&&index+1<m_favorites.size());
        connect(earlier,&QAction::triggered,this,[this,id]{auto p=m_favorites;const int i=indexOf(id);if(m_writable&&i>0){p.move(i,i-1);Q_EMIT favoritesEdited(p);}});
        connect(later,&QAction::triggered,this,[this,id]{auto p=m_favorites;const int i=indexOf(id);if(m_writable&&i>=0&&i+1<p.size()){p.move(i,i+1);Q_EMIT favoritesEdited(p);}});
    }
    menu.addSeparator();auto* fold=menu.addAction(m_preferences.collapsed?QStringLiteral("展开收藏栏"):QStringLiteral("折叠收藏栏"));fold->setEnabled(m_writable);
    connect(fold,&QAction::triggered,this,[this]{auto p=m_preferences;p.collapsed=!p.collapsed;proposeLayout(p);});
    const bool movable = m_writable && !m_preferences.pinned;
    for(const auto& side:QStringList{QStringLiteral("left"),QStringLiteral("right")}){auto* action=menu.addAction(side==QStringLiteral("left")?QStringLiteral("停靠左侧"):QStringLiteral("停靠右侧"));action->setEnabled(movable);connect(action,&QAction::triggered,this,[this,side]{auto p=m_preferences;p.side=side;p.floating=false;p.visible=true;proposeLayout(p);});}
    auto* floating=menu.addAction(QStringLiteral("浮动收藏栏"));floating->setEnabled(movable);connect(floating,&QAction::triggered,this,[this]{auto p=m_preferences;p.floating=true;p.visible=true;proposeLayout(p);});
    auto* hide=menu.addAction(QStringLiteral("隐藏收藏栏"));connect(hide,&QAction::triggered,this,[this]{close();});
    menu.exec(global);
}

void FavoritesBar::editFavorite(const QUuid& id)
{
    const int index=indexOf(id);if(index<0||!m_writable)return;FavoriteEditor editor(m_favorites[index],this);
    if(editor.exec()!=QDialog::Accepted||!m_writable)return;const int current=indexOf(id);if(current<0)return;
    auto candidate=m_favorites;candidate[current]=editor.value();Q_EMIT favoritesEdited(candidate);
}

void FavoritesBar::setBarPreferences(const FavoritesBarPreferences& preferences)
{
    if (!validFavoritesBarPreferences(preferences) || m_hostClosing) return;
    const bool placementChanged = !m_preferencesApplied || preferences.side != m_preferences.side
        || preferences.floating != m_preferences.floating;
    const bool geometryChanged = placementChanged || preferences.geometry != m_preferences.geometry;
    const bool collapsedChanged = !m_preferencesApplied || preferences.collapsed != m_preferences.collapsed;
    m_preferences = preferences;
    m_preferencesApplied = true;
    if (m_visibilityOverride && *m_visibilityOverride == preferences.visible) m_visibilityOverride.reset();

    QScopedValueRollback<bool> applying(m_applying, true);
    const auto area = preferences.side == QStringLiteral("right") ? Qt::RightDockWidgetArea : Qt::LeftDockWidgetArea;
    if (placementChanged) {
        if (!preferences.floating) {
            setFloating(false);
            m_mainWindow->addDockWidget(area, this);
        } else if (!isFloating()) {
            m_mainWindow->addDockWidget(area, this);
            setFloating(true);
        }
    }
    if (preferences.floating && geometryChanged) {
        QRect restored = preferences.geometry;
        QScreen* screen = QGuiApplication::screenAt(restored.center());
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (screen) {
            const QRect available = screen->availableGeometry();
            restored.setSize(restored.size().boundedTo(available.size()));
            restored.moveLeft(std::clamp(restored.left(), available.left(),
                std::max(available.left(), available.right() - restored.width() + 1)));
            restored.moveTop(std::clamp(restored.top(), available.top(),
                std::max(available.top(), available.bottom() - restored.height() + 1)));
        }
        setGeometry(restored);
    }
    if (collapsedChanged) {
        if (auto* scroll = m_contents->findChild<QScrollArea*>(QStringLiteral("favoritesScroll")))
            scroll->setVisible(!preferences.collapsed);
        m_collapse->setText(preferences.collapsed ? QStringLiteral("▸") : QStringLiteral("▾"));
        m_collapse->setToolTip(preferences.collapsed ? QStringLiteral("展开工具收藏") : QStringLiteral("折叠工具收藏"));
    }
    syncPlacementControls();
    syncVisibility();
}
void FavoritesBar::proposeLayout(FavoritesBarPreferences preferences)
{
    if (!m_writable || !validFavoritesBarPreferences(preferences)) return;
    preferences.visible = requestedVisible();
    Q_EMIT barPreferencesEdited(preferences);
}
FavoritesBarPreferences FavoritesBar::currentLayout()
{
    auto p = m_preferences;
    p.visible = requestedVisible();
    if (!m_hostAvailable) return p;
    p.floating=isFloating();
    p.pinned = p.floating && p.pinned;
    const auto area=m_mainWindow->dockWidgetArea(this);if(area==Qt::LeftDockWidgetArea)p.side=QStringLiteral("left");else if(area==Qt::RightDockWidgetArea)p.side=QStringLiteral("right");
    if(p.floating){const QRect actual=geometry();p.geometry=QRect(std::clamp(actual.x(),-100000,100000),std::clamp(actual.y(),-100000,100000),std::clamp(actual.width(),80,1200),std::clamp(actual.height(),80,1600));}
    return p;
}
void FavoritesBar::recordLayout()
{
    if(m_applying||!m_writable||!m_hostAvailable||m_hostClosing)return;
    const auto p = currentLayout();
    if(favoritesBarJson(p)!=favoritesBarJson(m_preferences))Q_EMIT barPreferencesEdited(p);
}

bool FavoritesBar::requestedVisible() const
{
    return m_visibilityOverride.value_or(m_preferences.visible);
}

void FavoritesBar::syncVisibility()
{
    QScopedValueRollback<bool> applying(m_applying, true);
    m_visibilityAction->setChecked(requestedVisible());
    setVisible(requestedVisible() && m_hostAvailable && !m_hostClosing);
}

void FavoritesBar::setUserVisible(bool visible)
{
    if (m_hostClosing) return;
    m_visibilityOverride = visible;
    const auto settings = currentLayout();
    m_layoutTimer.stop();
    syncVisibility();
    // Visibility changes take effect immediately. The host can save them,
    // queue them while busy, or leave them as a session override if read-only.
    Q_EMIT barPreferencesEdited(settings);
}

void FavoritesBar::prepareForHostClose()
{
    m_hostClosing = true;
    m_layoutTimer.stop();
    syncVisibility();
}

bool FavoritesBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == this && m_preferences.pinned && isFloating()) {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
            if (static_cast<QMouseEvent*>(event)->button() != Qt::LeftButton) break;
            [[fallthrough]];
        case QEvent::MouseMove:
        case QEvent::NonClientAreaMouseButtonPress:
        case QEvent::NonClientAreaMouseButtonRelease:
        case QEvent::NonClientAreaMouseButtonDblClick:
        case QEvent::NonClientAreaMouseMove:
            event->accept();
            return true;
        default:
            break;
        }
    }
    if (watched == m_mainWindow && !m_hostClosing) {
        const auto type = event->type();
        if (type == QEvent::Hide || type == QEvent::Show || type == QEvent::WindowStateChange) {
            m_hostAvailable = type != QEvent::Hide && (type == QEvent::Show || m_mainWindow->isVisible())
                && !m_mainWindow->isMinimized();
            syncVisibility();
        }
    }
    return QDockWidget::eventFilter(watched, event);
}

bool FavoritesBar::event(QEvent* event)
{const bool result=QDockWidget::event(event);if(m_contents&&!m_applying&&m_hostAvailable&&!m_hostClosing&&(event->type()==QEvent::Move||event->type()==QEvent::Resize))m_layoutTimer.start();return result;}
void FavoritesBar::closeEvent(QCloseEvent* event)
{
    if (m_hostAvailable && !m_hostClosing) setUserVisible(false);
    event->accept();
}
void FavoritesBar::dragEnterEvent(QDragEnterEvent* event)
{
    const auto data=event->mimeData()->data(DragFormat);if(m_writable&&data.size()<256&&data.startsWith(m_dragToken.toUtf8()+QByteArray("\n"))){const QUuid id(QString::fromUtf8(data.mid(m_dragToken.toUtf8().size()+1)));if(indexOf(id)>=0)event->acceptProposedAction();}
}
void FavoritesBar::dragMoveEvent(QDragMoveEvent* event)
{if(m_writable&&event->mimeData()->hasFormat(DragFormat))event->acceptProposedAction();}
void FavoritesBar::dropEvent(QDropEvent* event)
{
    const auto data=event->mimeData()->data(DragFormat);if(!m_writable||data.size()>=256||!data.startsWith(m_dragToken.toUtf8()+QByteArray("\n")))return;
    const int from=indexOf(QUuid(QString::fromUtf8(data.mid(m_dragToken.toUtf8().size()+1))));if(from<0)return;
    int insertion=m_favorites.size();for(int i=0;i<m_favorites.size();++i){const auto* button=m_buttons.value(m_favorites[i].id);if(button&&event->position().y()<button->mapTo(this,button->rect().center()).y()){insertion=i;break;}}
    if(insertion>from)--insertion;if(insertion==from){event->acceptProposedAction();return;}
    auto candidate=m_favorites;candidate.move(from,insertion);Q_EMIT favoritesEdited(candidate);event->acceptProposedAction();
}
} // namespace propernotes
