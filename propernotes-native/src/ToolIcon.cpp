// SPDX-License-Identifier: GPL-3.0-only
#include "ToolIcon.h"
#include <QCache>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace propernotes {
namespace {
void drawSymbol(QPainter& p, const QString& name)
{
    const auto line = [&p](qreal x1, qreal y1, qreal x2, qreal y2) { p.drawLine(QPointF(x1,y1), QPointF(x2,y2)); };
    if (name == "settings") {
        p.drawEllipse(QRectF(8,8,12,12)); p.drawEllipse(QRectF(12,12,4,4));
        for (int i=0;i<8;++i) { p.save(); p.translate(14,14); p.rotate(i*45); line(0,-8,0,-11); p.restore(); }
    } else if (name == "fullscreen" || name == "restore") {
        const bool restore=name=="restore";
        for (int i=0;i<4;++i) { p.save();p.translate(14,14);p.rotate(i*90);
            if (restore) {line(-9,-3,-3,-3);line(-3,-3,-3,-9);} else {line(-9,-3,-9,-9);line(-9,-9,-3,-9);}p.restore();}
    } else if (name == "hand") {
        QPainterPath path; path.moveTo(8,15);path.lineTo(8,7);path.cubicTo(8,4,11,4,11,7);path.lineTo(11,13);
        path.lineTo(11,4);path.cubicTo(11,1,14,1,14,4);path.lineTo(14,13);path.lineTo(14,5);path.cubicTo(14,2,17,2,17,5);
        path.lineTo(17,14);path.lineTo(17,8);path.cubicTo(17,5,20,5,20,8);path.lineTo(20,18);path.cubicTo(20,25,10,27,7,20);
        path.lineTo(4,15);path.cubicTo(2,12,5,10,8,15);p.drawPath(path);
    } else if (name == "pin" || name == "pin-off") {
        p.save();if(name=="pin-off"){p.translate(14,14);p.rotate(35);p.translate(-14,-14);}
        line(9,5,19,5);p.drawPolyline(QPolygonF{QPointF(10,5),QPointF(10,12),QPointF(7,16),QPointF(21,16),QPointF(18,12),QPointF(18,5)});line(14,16,14,25);p.restore();
    } else if (name == "pages") {
        p.drawRoundedRect(QRectF(6,3,17,21),1.5,1.5);line(3,7,3,26);line(3,26,19,26);line(10,8,19,8);line(10,13,19,13);
    } else if (name == "annotations") {
        p.drawRoundedRect(QRectF(4,3,20,18),2,2);line(9,21,9,26);line(9,26,15,21);line(8,8,20,8);line(8,13,17,13);
    } else if (name == "search") {p.drawEllipse(QRectF(4,3,15,15));line(17,17,25,25);
    } else if (name == "plus" || name == "minus" || name == "zoom-in") {line(5,14,23,14);if(name!="minus")line(14,5,14,23);
    } else if (name == "move") {
        line(14,3,14,25);line(3,14,25,14);
        for (const auto& d : {QPointF(0,-1),QPointF(1,0),QPointF(0,1),QPointF(-1,0)}) {const auto tip=QPointF(14,14)+d*11;const QPointF side(-d.y(),d.x());p.drawLine(tip,tip-d*4+side*3);p.drawLine(tip,tip-d*4-side*3);}
    } else if (name == "grip") {line(7,11,21,11);line(7,17,21,17);
    } else if (name == "ellipsis") {for(int x:{6,14,22})p.drawEllipse(QPointF(x,14),1,1);
    } else if (name == "select") {p.setPen(QPen(p.pen().color(),1.6,Qt::DashLine,Qt::RoundCap,Qt::RoundJoin));p.drawRect(QRectF(4,4,20,20));
    } else if (name == "chevron-up" || name == "chevron-down") {const bool down=name=="chevron-down";p.drawPolyline(QPolygonF{QPointF(6,down?10:18),QPointF(14,down?18:10),QPointF(22,down?10:18)});
    } else if (name == "back" || name == "forward") {const bool forward=name=="forward";line(4,14,24,14);p.drawPolyline(QPolygonF{QPointF(forward?16:12,6),QPointF(forward?24:4,14),QPointF(forward?16:12,22)});
    } else if (name == "undo" || name == "redo") {p.save();if(name=="redo"){p.translate(28,0);p.scale(-1,1);}p.drawPolyline(QPolygonF{QPointF(10,4),QPointF(4,10),QPointF(11,15)});QPainterPath path;path.moveTo(4,10);path.cubicTo(24,4,30,23,14,24);p.drawPath(path);p.restore();
    } else if (name == "save") {p.drawRoundedRect(QRectF(4,3,20,22),1,1);p.drawRect(QRectF(9,3,10,8));p.drawRect(QRectF(9,17,10,8));
    } else if (name == "open") {p.drawPolyline(QPolygonF{QPointF(3,22),QPointF(3,5),QPointF(11,5),QPointF(14,9),QPointF(25,9)});p.drawPolygon(QPolygonF{QPointF(3,23),QPointF(7,12),QPointF(26,12),QPointF(22,23)});
    } else {p.drawEllipse(QRectF(5,5,18,18));}
}
void drawFavorite(QPainter& p, const FavoriteTool& favorite, const QPalette& palette)
{
    QColor color=favorite.color;
    if (const auto shape=InkCanvas::shapeKind(favorite.tool)) color=favorite.tools.shapeToolPreference(*shape).style.strokeColor;
    if (favorite.tool==InkCanvas::Tool::ObjectEraser || favorite.tool==InkCanvas::Tool::Pan || favorite.tool==InkCanvas::Tool::Select || (favorite.tool==InkCanvas::Tool::TextSelect&&!favorite.markupKind)) color=palette.color(QPalette::WindowText);
    color.setAlpha(255);p.setPen(QPen(color,1.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);
    if (const auto shape=InkCanvas::shapeKind(favorite.tool)) {
        ShapeAnnotation sample;sample.kind=*shape;sample.style=favorite.tools.shapeToolPreference(*shape).style;sample.style.widthPt=1.7;sample.style.strokeColor=color;
        sample.start=isAreaShape(*shape)?QPointF(4,6):QPointF(5,23);sample.end=isAreaShape(*shape)?QPointF(24,23):QPointF(23,5);paintShape(p,sample);
    } else if(favorite.markupKind) {
        if(*favorite.markupKind==TextMarkupKind::Highlight){auto fill=color;fill.setAlpha(100);p.fillRect(QRectF(3,5,22,16),fill);}
        p.setPen(QPen(palette.color(QPalette::WindowText),1.4));p.drawLine(QPointF(5,8),QPointF(23,8));p.drawLine(QPointF(5,13),QPointF(21,13));
        p.setPen(QPen(color,1.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        if(*favorite.markupKind==TextMarkupKind::Underline)p.drawLine(QPointF(5,20),QPointF(23,20));
        if(*favorite.markupKind==TextMarkupKind::StrikeOut)p.drawLine(QPointF(4,12),QPointF(24,12));
        if(*favorite.markupKind==TextMarkupKind::Squiggly){QPolygonF wave;for(int x=5;x<=23;x+=3)wave<<QPointF(x,((x-5)/3)%2?18:21);p.drawPolyline(wave);}
    } else if(favorite.tool==InkCanvas::Tool::Pen) {p.drawPolygon(QPolygonF{QPointF(6,24),QPointF(8,18),QPointF(21,5),QPointF(25,9),QPointF(12,22)});p.drawLine(QPointF(8,18),QPointF(12,22));
    } else if(favorite.tool==InkCanvas::Tool::Highlighter) {p.setBrush(color);p.drawPolygon(QPolygonF{QPointF(5,18),QPointF(17,5),QPointF(24,12),QPointF(12,24),QPointF(5,24)});p.setPen(QPen(palette.color(QPalette::WindowText),1.3));p.drawLine(QPointF(5,18),QPointF(12,24));
    } else if(favorite.tool==InkCanvas::Tool::ObjectEraser) {p.drawPolygon(QPolygonF{QPointF(4,17),QPointF(16,5),QPointF(25,14),QPointF(15,24),QPointF(10,24)});p.drawLine(QPointF(9,12),QPointF(20,23));
    } else if(favorite.tool==InkCanvas::Tool::TextBox) {p.setPen(QPen(color,1.2,Qt::DashLine));p.drawRect(QRectF(3,3,22,22));p.setPen(QPen(color,1.8));p.drawLine(QPointF(8,8),QPointF(20,8));p.drawLine(QPointF(14,8),QPointF(14,22));
    } else if(favorite.tool==InkCanvas::Tool::TextSelect) {p.drawLine(QPointF(14,4),QPointF(14,24));p.drawLine(QPointF(10,4),QPointF(18,4));p.drawLine(QPointF(10,24),QPointF(18,24));
    } else drawSymbol(p,favorite.tool==InkCanvas::Tool::Select?QStringLiteral("select"):QStringLiteral("hand"));
}
}
QIcon uiIcon(const QString& name,const QPalette& palette,const QColor& color)
{
    static QCache<QString,QIcon> cache(128);
    const QColor ink = color.isValid() ? color : palette.color(QPalette::WindowText);
    const QString key = name + QLatin1Char('/') + ink.name(QColor::HexArgb);
    if (const auto* icon=cache.object(key)) return *icon;
    QPixmap pixels(64,64);pixels.fill(Qt::transparent);pixels.setDevicePixelRatio(2);
    QPainter p(&pixels);p.setRenderHint(QPainter::Antialiasing);p.translate(2,2);p.setPen(QPen(ink,1.7,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);drawSymbol(p,name);p.end();QIcon icon(pixels);cache.insert(key,new QIcon(icon));return icon;
}
QIcon favoriteIcon(const FavoriteTool& favorite,const QPalette& palette)
{
    QPixmap pixels(104,104);pixels.fill(Qt::transparent);pixels.setDevicePixelRatio(2);
    QPainter p(&pixels);p.setRenderHint(QPainter::Antialiasing);p.save();p.translate(8,12);p.scale(1.2,1.2);drawFavorite(p,favorite,palette);p.restore();
    const auto shape=InkCanvas::shapeKind(favorite.tool);
    const bool badge=!favorite.markupKind&&(shape || favorite.tool==InkCanvas::Tool::Pen || favorite.tool==InkCanvas::Tool::Highlighter || favorite.tool==InkCanvas::Tool::TextBox || favorite.tool==InkCanvas::Tool::ObjectEraser);
    if(badge){const qreal width=shape?favorite.tools.shapeToolPreference(*shape).style.widthPt:favorite.tool==InkCanvas::Tool::TextBox?favorite.tools.textSizePt:favorite.tool==InkCanvas::Tool::ObjectEraser?favorite.tools.eraserRadiusPt:favorite.widthPt;
        QFont font=p.font();font.setPointSizeF(8);font.setBold(false);p.setFont(font);const auto text=QString::number(width,'g',3);const qreal w=qMax(15.0,p.fontMetrics().horizontalAdvance(text)+5.0);QRectF r(51-w,1,w,15);p.setPen(QPen(palette.color(QPalette::Mid),0.7));p.setBrush(palette.color(QPalette::Window));p.drawRoundedRect(r,4,4);p.setPen(palette.color(QPalette::WindowText));p.drawText(r,Qt::AlignCenter,text);
    }p.end();return QIcon(pixels);
}
}
