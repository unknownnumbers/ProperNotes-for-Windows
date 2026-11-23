// SPDX-License-Identifier: GPL-3.0-only
#include "ToolIcon.h"
#include <QCache>
#include <QFile>
#include <QFontMetrics>
#include <QHash>
#include <QIconEngine>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>
#include <QToolButton>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <utility>

namespace propernotes {
namespace {
// The source SVGs retain the Lucide / Feather notices in licenses/lucide.txt.
// Keep vector geometry until Qt requests a size and screen pixel ratio.
class SvgIconEngine final : public QIconEngine {
public:
    SvgIconEngine(QByteArray source,QColor foreground,QColor disabled,QString name,
                  QByteArray overlay={})
        : m_source(std::move(source)),m_overlaySource(std::move(overlay)),m_foreground(foreground),
          m_disabledColor(disabled),m_name(std::move(name))
    {
        auto colored=[](QByteArray source,const QColor& color,const char* width) {
            source.replace("currentColor",color.name(QColor::HexRgb).toUtf8());
            source.replace("stroke-width=\"2\"",QByteArray("stroke-width=\"")+width+"\"");
            return source;
        };
        m_normal.load(colored(m_source,m_foreground,"1.75"));
        m_disabled.load(colored(m_source,m_disabledColor,"1.75"));
        if(!m_overlaySource.isEmpty()){
            m_overlay.load(colored(m_overlaySource,m_foreground,"2.8"));
            m_overlayDisabled.load(colored(m_overlaySource,m_disabledColor,"2.8"));
        }
    }
    QIconEngine* clone() const override {return new SvgIconEngine(m_source,m_foreground,m_disabledColor,m_name,m_overlaySource);}
    QString key() const override {return QStringLiteral("ProperNotesLucideSvg");}
    QString iconName() override {return m_name;}
    bool isNull() override {return !m_normal.isValid();}
    QSize actualSize(const QSize& size,QIcon::Mode,QIcon::State) override {return size;}
    void paint(QPainter* painter,const QRect& rect,QIcon::Mode mode,QIcon::State) override
    {
        const qreal edge=std::min(rect.width(),rect.height());
        const QRectF target(rect.x()+(rect.width()-edge)/2,rect.y()+(rect.height()-edge)/2,edge,edge);
        painter->save();painter->setRenderHint(QPainter::Antialiasing);
        if(m_overlay.isValid()){
            // A toolbar outline plus a pin distinguishes this from tool lock.
            const QRectF badge(target.x()+edge*.4,target.y()+edge*.38,edge*.6,edge*.62);
            QPainterPath base,cutout;base.addRect(target);cutout.addRoundedRect(badge.adjusted(-1,-1,0,0),edge*.08,edge*.08);
            painter->save();painter->setClipPath(base.subtracted(cutout),Qt::IntersectClip);
            (mode==QIcon::Disabled?m_disabled:m_normal).render(painter,target);painter->restore();
            (mode==QIcon::Disabled?m_overlayDisabled:m_overlay).render(painter,badge);
        }else (mode==QIcon::Disabled?m_disabled:m_normal).render(painter,target);
        painter->restore();
    }
    QPixmap pixmap(const QSize& size,QIcon::Mode mode,QIcon::State state) override {return scaledPixmap(size,mode,state,1);}
    QPixmap scaledPixmap(const QSize& size,QIcon::Mode mode,QIcon::State state,qreal scale) override
    {
        if(size.isEmpty()||!std::isfinite(scale)||scale<=0)return {};
        const QSize pixels(std::max(1,qCeil(size.width()*scale)),std::max(1,qCeil(size.height()*scale)));
        QPixmap result(pixels);result.fill(Qt::transparent);result.setDevicePixelRatio(scale);
        QPainter painter(&result);paint(&painter,QRect(QPoint(),size),mode,state);return result;
    }
private:
    QByteArray m_source,m_overlaySource;
    QColor m_foreground,m_disabledColor;
    QString m_name;
    QSvgRenderer m_normal,m_disabled,m_overlay,m_overlayDisabled;
};
QString assetName(const QString& name)
{
    static const QHash<QString,QString> aliases{
        {"fullscreen","maximize"},{"restore","minimize"},{"pages","files"},{"overview","layout-grid"},
        {"annotations","list"},{"grip","grip-horizontal"},{"back","arrow-left"},{"forward","arrow-right"},
        {"undo","undo-2"},{"redo","redo-2"},{"zoom-in","plus"},{"open","folder-open"},{"select","scan"},
        {"pen","pen-line"},{"textbox","type"},{"textselect","text-cursor"},{"arrow","move-up-right"},
        {"line","slash"},{"rectangle","square"},{"ellipse","circle"},{"squiggly","waves-horizontal"},
        {"strike","strikethrough"},{"highlight","scan-text"},
        {"pin-toolbar","panel-right"},{"unpin-toolbar","panel-right"}
    };
    return aliases.value(name,name);
}
QByteArray sourceSvg(const QString& asset)
{
    QFile file(QStringLiteral(":/propernotes/icons/lucide/%1.svg").arg(asset));
    return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray{};
}
QString favoriteSymbol(const FavoriteTool& favorite)
{
    if(favorite.markupKind){
        switch(*favorite.markupKind){
        case TextMarkupKind::Highlight:return QStringLiteral("highlight");
        case TextMarkupKind::Underline:return QStringLiteral("underline");
        case TextMarkupKind::StrikeOut:return QStringLiteral("strike");
        case TextMarkupKind::Squiggly:return QStringLiteral("squiggly");
        }
    }
    switch(favorite.tool){
    case InkCanvas::Tool::Pen:return QStringLiteral("pen");
    case InkCanvas::Tool::Highlighter:return QStringLiteral("highlighter");
    case InkCanvas::Tool::ObjectEraser:return QStringLiteral("eraser");
    case InkCanvas::Tool::TextBox:return QStringLiteral("textbox");
    case InkCanvas::Tool::TextSelect:return QStringLiteral("textselect");
    case InkCanvas::Tool::Pan:return QStringLiteral("hand");
    case InkCanvas::Tool::Select:return QStringLiteral("select");
    case InkCanvas::Tool::Line:return QStringLiteral("line");
    case InkCanvas::Tool::Arrow:return QStringLiteral("arrow");
    case InkCanvas::Tool::Rectangle:return QStringLiteral("rectangle");
    case InkCanvas::Tool::Ellipse:return QStringLiteral("ellipse");
    }
    return QStringLiteral("pen");
}
}
QIcon uiIcon(const QString& name,const QPalette& palette,const QColor& color)
{
    static QCache<QString,QIcon> cache(256);
    const auto asset=assetName(name);
    const QColor ink=color.isValid()?color:palette.color(QPalette::ButtonText);
    const QColor disabled=palette.color(QPalette::Disabled,QPalette::ButtonText);
    const QString key=name+QLatin1Char('/')+ink.name(QColor::HexArgb)+QLatin1Char('/')+disabled.name(QColor::HexArgb);
    if(const auto* icon=cache.object(key))return *icon;
    const auto source=sourceSvg(asset);if(source.isEmpty())return {};
    const auto overlay=name=="pin-toolbar"?sourceSvg(QStringLiteral("pin")):
        name=="unpin-toolbar"?sourceSvg(QStringLiteral("pin-off")):QByteArray{};
    QIcon icon(new SvgIconEngine(source,ink,disabled,name,overlay));cache.insert(key,new QIcon(icon));return icon;
}
QIcon favoriteIcon(const FavoriteTool& favorite,const QPalette& palette)
{
    QColor color=favorite.color;
    if(const auto shape=InkCanvas::shapeKind(favorite.tool))color=favorite.tools.shapeToolPreference(*shape).style.strokeColor;
    if(favorite.tool==InkCanvas::Tool::ObjectEraser||favorite.tool==InkCanvas::Tool::Pan||favorite.tool==InkCanvas::Tool::Select
       ||(favorite.tool==InkCanvas::Tool::TextSelect&&!favorite.markupKind))color=palette.color(QPalette::ButtonText);
    color.setAlpha(255);return uiIcon(favoriteSymbol(favorite),palette,color);
}
void configureReaderIconButton(QToolButton* button)
{
    button->setProperty("readerIcon",true);
    button->setFixedSize(ReaderButtonSize,ReaderButtonSize);
    button->setIconSize(QSize(ReaderGlyphSize,ReaderGlyphSize));
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    // Text-toolbar padding must never shrink an icon's content rectangle.
    button->setStyleSheet(QStringLiteral("QToolButton{padding:0;margin:0;border:1px solid transparent;border-radius:7px;}"
        "QToolButton:hover{background:palette(highlight);}QToolButton:checked{background:palette(highlight);border-color:palette(mid);}"
        "QToolButton::menu-indicator{width:0;height:0;}"));
}
void configureFavoriteButton(QToolButton* button,const FavoriteTool& favorite)
{
    configureReaderIconButton(button);
    button->setIcon(favoriteIcon(favorite,button->palette()));
    auto* badge=button->findChild<QLabel*>(QStringLiteral("favoriteWidthBadge"),Qt::FindDirectChildrenOnly);
    if(!badge){
        badge=new QLabel(button);badge->setObjectName(QStringLiteral("favoriteWidthBadge"));
        badge->setAttribute(Qt::WA_TransparentForMouseEvents);badge->setAlignment(Qt::AlignCenter);
        auto font=badge->font();font.setPointSizeF(8.25);font.setBold(false);badge->setFont(font);
        badge->setStyleSheet(QStringLiteral("QLabel{padding:0 2px;border:1px solid palette(mid);border-radius:4px;"
                                           "color:palette(window-text);background:palette(window);}"));
    }
    const auto shape=InkCanvas::shapeKind(favorite.tool);
    // Reserve the same top band for every favorite, including badge-free tools.
    // Font metrics keep the badge clear of the 24px glyph under text scaling.
    const int badgeHeight=badge->fontMetrics().height()+2;
    button->setFixedSize(52,std::max(52,badgeHeight+ReaderGlyphSize+10));
    button->setStyleSheet(button->styleSheet()+QStringLiteral("QToolButton{padding-top:%1px;}").arg(badgeHeight));
    const bool visible=!favorite.markupKind&&(shape||favorite.tool==InkCanvas::Tool::Pen||favorite.tool==InkCanvas::Tool::Highlighter
        ||favorite.tool==InkCanvas::Tool::TextBox||favorite.tool==InkCanvas::Tool::ObjectEraser);
    badge->setVisible(visible);
    if(visible){
        const qreal width=shape?favorite.tools.shapeToolPreference(*shape).style.widthPt:
            favorite.tool==InkCanvas::Tool::TextBox?favorite.tools.textSizePt:
            favorite.tool==InkCanvas::Tool::ObjectEraser?favorite.tools.eraserRadiusPt:favorite.widthPt;
        badge->setText(QString::number(width,'g',3));badge->adjustSize();badge->move(button->width()-badge->width()-1,0);
    }
}
}
