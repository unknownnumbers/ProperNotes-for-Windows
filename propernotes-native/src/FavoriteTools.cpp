// SPDX-License-Identifier: GPL-3.0-only
#include "FavoriteTools.h"
#include <QJsonValue>
#include <QSet>
#include <QPair>
#include <cmath>
#include <utility>

namespace propernotes {
namespace {
constexpr qsizetype MaximumFavorites = 128;
bool safeName(const QString& value, qsizetype maximum)
{
    if (value.trimmed().isEmpty() || value.size() > maximum) return false;
    for (qsizetype index = 0; index < value.size(); ++index) {
        const QChar c = value[index];
        if (c.isNull() || c.category() == QChar::Other_Control) return false;
        if (c.isHighSurrogate()) {
            if (index + 1 >= value.size() || !value[index + 1].isLowSurrogate()) return false;
            ++index;
        } else if (c.isLowSurrogate()) return false;
    }
    return true;
}
QString kind(InkCanvas::Tool tool)
{
    switch (tool) {
    case InkCanvas::Tool::Pen: return QStringLiteral("pen");
    case InkCanvas::Tool::ObjectEraser: return QStringLiteral("eraser");
    case InkCanvas::Tool::TextBox: return QStringLiteral("text-box");
    case InkCanvas::Tool::Select: return QStringLiteral("select");
    case InkCanvas::Tool::Pan: return QStringLiteral("pan");
    }
    return {};
}
bool exactKeys(const QJsonObject& object, const QStringList& expected)
{
    const auto keys = object.keys();
    return QSet<QString>(keys.begin(), keys.end()) == QSet<QString>(expected.begin(), expected.end());
}
bool number(const QJsonObject& object, const char* key, qreal low, qreal high, bool integral = false)
{
    const auto value = object.value(QLatin1String(key));
    const qreal n = value.toDouble(-1);
    return value.isDouble() && std::isfinite(n) && n >= low && n <= high && (!integral || n == std::floor(n));
}
QJsonObject styleJson(const FavoriteTool& favorite)
{
    const auto& t = favorite.tools;
    if (favorite.tool == InkCanvas::Tool::Pen)
        return {{"color",favorite.color.name(QColor::HexRgb)},{"widthPt",favorite.widthPt},
                {"opacity",t.opacity},{"pressureEnabled",t.pressureEnabled},
                {"pressureSensitivity",t.pressureSensitivity},{"singleUse",t.penSingleUse}};
    if (favorite.tool == InkCanvas::Tool::ObjectEraser)
        return {{"mode",t.eraserMode},{"radiusPt",t.eraserRadiusPt},{"singleUse",t.eraserSingleUse}};
    if (favorite.tool == InkCanvas::Tool::TextBox)
        return {{"color",favorite.color.name(QColor::HexRgb)},{"opacity",t.opacity},
                {"family",t.textFamily},{"sizePt",t.textSizePt},{"singleUse",t.textSingleUse}};
    return {};
}
bool readColor(const QJsonObject& style, QColor& color)
{
    const auto value = style.value("color");
    if (!value.isString() || value.toString().size() != 7 || !value.toString().startsWith(QChar('#'))) return false;
    for (const auto c : value.toString().mid(1))
        if (!((c >= QChar('0') && c <= QChar('9')) || (c.toLower() >= QChar('a') && c.toLower() <= QChar('f')))) return false;
    color = QColor(value.toString());
    return color.isValid() && color.alpha() == 255;
}
} // namespace

FavoriteTool captureFavorite(InkCanvas::Tool tool, const ToolPreferences& source, const QColor& color,
                             qreal widthPt, const QString& name)
{
    FavoriteTool result;
    result.tool = tool;
    result.name = name.isEmpty() ? favoriteToolName(tool) : name;
    if (tool == InkCanvas::Tool::Pen || tool == InkCanvas::Tool::TextBox) {
        result.color = color;
        result.color.setAlpha(255);
        result.tools.opacity = source.opacity * color.alphaF();
    }
    if (tool == InkCanvas::Tool::Pen) {
        result.widthPt = widthPt;
        result.tools.pressureEnabled = source.pressureEnabled;
        result.tools.pressureSensitivity = source.pressureSensitivity;
        result.tools.penSingleUse = source.penSingleUse;
    } else if (tool == InkCanvas::Tool::ObjectEraser) {
        result.tools.eraserMode = source.eraserMode;
        result.tools.eraserRadiusPt = source.eraserRadiusPt;
        result.tools.eraserSingleUse = source.eraserSingleUse;
    } else if (tool == InkCanvas::Tool::TextBox) {
        result.tools.textFamily = source.textFamily;
        result.tools.textSizePt = source.textSizePt;
        result.tools.textSingleUse = source.textSingleUse;
    }
    return result;
}

void applyFavorite(const FavoriteTool& favorite, ToolPreferences& tools, QColor& color, qreal& widthPt)
{
    if (favorite.tool == InkCanvas::Tool::Pen) {
        color = favorite.color; widthPt = favorite.widthPt;
        tools.opacity = favorite.tools.opacity;
        tools.pressureEnabled = favorite.tools.pressureEnabled;
        tools.pressureSensitivity = favorite.tools.pressureSensitivity;
        tools.penSingleUse = favorite.tools.penSingleUse;
    } else if (favorite.tool == InkCanvas::Tool::ObjectEraser) {
        tools.eraserMode = favorite.tools.eraserMode;
        tools.eraserRadiusPt = favorite.tools.eraserRadiusPt;
        tools.eraserSingleUse = favorite.tools.eraserSingleUse;
    } else if (favorite.tool == InkCanvas::Tool::TextBox) {
        color = favorite.color; tools.opacity = favorite.tools.opacity;
        tools.textFamily = favorite.tools.textFamily;
        tools.textSizePt = favorite.tools.textSizePt;
        tools.textSingleUse = favorite.tools.textSingleUse;
    }
}

QString favoriteToolName(InkCanvas::Tool tool)
{
    switch (tool) {
    case InkCanvas::Tool::Pen: return QStringLiteral("笔");
    case InkCanvas::Tool::ObjectEraser: return QStringLiteral("橡皮");
    case InkCanvas::Tool::TextBox: return QStringLiteral("文字框");
    case InkCanvas::Tool::Select: return QStringLiteral("框选 / 移动");
    case InkCanvas::Tool::Pan: return QStringLiteral("平移");
    }
    return QStringLiteral("未知工具");
}

QString favoriteDescription(const FavoriteTool& favorite)
{
    QString detail = favoriteToolName(favorite.tool);
    if (favorite.tool == InkCanvas::Tool::Pen)
        detail += QStringLiteral(" · %1 pt · %2% 不透明度 · %3").arg(favorite.widthPt).arg(qRound(favorite.tools.opacity*100))
            .arg(favorite.tools.pressureEnabled ? QStringLiteral("压感 %1").arg(favorite.tools.pressureSensitivity) : QStringLiteral("固定线宽"));
    else if (favorite.tool == InkCanvas::Tool::ObjectEraser)
        detail += QStringLiteral(" · %1 · 半径 %2 pt").arg(favorite.tools.eraserMode == QStringLiteral("partial") ? QStringLiteral("局部") : QStringLiteral("整笔"))
            .arg(favorite.tools.eraserRadiusPt);
    else if (favorite.tool == InkCanvas::Tool::TextBox)
        detail += QStringLiteral(" · %1 %2 pt · %3% 不透明度").arg(favorite.tools.textFamily).arg(favorite.tools.textSizePt).arg(qRound(favorite.tools.opacity*100));
    if (favorite.tool == InkCanvas::Tool::Pen || favorite.tool == InkCanvas::Tool::ObjectEraser || favorite.tool == InkCanvas::Tool::TextBox) {
        const bool single = favorite.tool == InkCanvas::Tool::Pen ? favorite.tools.penSingleUse :
            favorite.tool == InkCanvas::Tool::ObjectEraser ? favorite.tools.eraserSingleUse : favorite.tools.textSingleUse;
        detail += single ? QStringLiteral(" · 单次") : QStringLiteral(" · 持续");
    }
    return favorite.name + QLatin1Char('\n') + detail;
}

QVector<FavoriteTool> defaultFavoriteTools()
{
    QVector<FavoriteTool> result;
    const QVector<QPair<QString,QColor>> pens{{QStringLiteral("黑笔"),QColor("#202833")},{QStringLiteral("红笔"),QColor("#dc4e52")},
        {QStringLiteral("蓝笔"),QColor("#2c72c7")},{QStringLiteral("绿笔"),QColor("#218367")}};
    for (qsizetype i=0;i<pens.size();++i) {
        auto favorite = captureFavorite(InkCanvas::Tool::Pen,ToolPreferences{},pens[i].second,2.5,pens[i].first);
        favorite.id = QUuid(QStringLiteral("00000000-0000-4000-8000-%1").arg(i+1,12,10,QChar('0')));
        result.append(favorite);
    }
    return result;
}

bool validFavoriteTools(const QVector<FavoriteTool>& favorites, QString* error)
{
    const auto fail = [&] { if(error)*error=QStringLiteral("工具收藏字段无效、重复或超过 128 项，原设置保留。");return false; };
    if (favorites.size()>MaximumFavorites) return fail();
    QSet<QUuid> ids;
    for (const auto& f : favorites) {
        if (f.id.isNull() || ids.contains(f.id) || !safeName(f.name,80) || kind(f.tool).isEmpty()) return fail();
        ids.insert(f.id);
        if (f.tool == InkCanvas::Tool::Pen || f.tool == InkCanvas::Tool::TextBox) {
            if (!f.color.isValid() || f.color.alpha()!=255 || !std::isfinite(f.tools.opacity) || f.tools.opacity<0 || f.tools.opacity>1) return fail();
        }
        if (f.tool == InkCanvas::Tool::Pen && (!std::isfinite(f.widthPt) || f.widthPt<0.5 || f.widthPt>24 ||
            f.tools.pressureSensitivity<0 || f.tools.pressureSensitivity>100)) return fail();
        if (f.tool == InkCanvas::Tool::ObjectEraser &&
            ((f.tools.eraserMode!=QStringLiteral("whole") && f.tools.eraserMode!=QStringLiteral("partial")) ||
             !std::isfinite(f.tools.eraserRadiusPt) || f.tools.eraserRadiusPt<1 || f.tools.eraserRadiusPt>100)) return fail();
        if (f.tool == InkCanvas::Tool::TextBox && (!safeName(f.tools.textFamily,160) ||
            !std::isfinite(f.tools.textSizePt) || f.tools.textSizePt<4 || f.tools.textSizePt>144)) return fail();
    }
    return true;
}

QJsonArray favoriteToolsJson(const QVector<FavoriteTool>& favorites)
{
    QJsonArray result;
    for (const auto& f : favorites) result.append(QJsonObject{{"id",f.id.toString(QUuid::WithoutBraces)},
        {"name",f.name},{"tool",kind(f.tool)},{"style",styleJson(f)}});
    return result;
}

bool readFavoriteTools(const QJsonArray& array, QVector<FavoriteTool>& favorites)
{
    if (array.size()>MaximumFavorites) return false;
    QVector<FavoriteTool> result;
    for (const auto& item : array) {
        if (!item.isObject()) return false;
        const auto object=item.toObject();
        if (!exactKeys(object,{"id","name","tool","style"}) || !object.value("id").isString() ||
            !object.value("name").isString() || !object.value("tool").isString() || !object.value("style").isObject()) return false;
        FavoriteTool f; f.id=QUuid(object.value("id").toString());f.name=object.value("name").toString();
        if (f.id.toString(QUuid::WithoutBraces).compare(object.value("id").toString(),Qt::CaseInsensitive)!=0) return false;
        const QString tool=object.value("tool").toString();
        if(tool=="pen")f.tool=InkCanvas::Tool::Pen;else if(tool=="eraser")f.tool=InkCanvas::Tool::ObjectEraser;
        else if(tool=="text-box")f.tool=InkCanvas::Tool::TextBox;else if(tool=="select")f.tool=InkCanvas::Tool::Select;
        else if(tool=="pan")f.tool=InkCanvas::Tool::Pan;else return false;
        const auto style=object.value("style").toObject();
        if(f.tool==InkCanvas::Tool::Pen){
            if(!exactKeys(style,{"color","widthPt","opacity","pressureEnabled","pressureSensitivity","singleUse"}) ||
                !readColor(style,f.color)||!number(style,"widthPt",0.5,24)||!number(style,"opacity",0,1)||
                !number(style,"pressureSensitivity",0,100,true)||!style.value("pressureEnabled").isBool()||!style.value("singleUse").isBool())return false;
            f.widthPt=style.value("widthPt").toDouble();f.tools.opacity=style.value("opacity").toDouble();
            f.tools.pressureEnabled=style.value("pressureEnabled").toBool();f.tools.pressureSensitivity=style.value("pressureSensitivity").toInt();
            f.tools.penSingleUse=style.value("singleUse").toBool();
        }else if(f.tool==InkCanvas::Tool::ObjectEraser){
            if(!exactKeys(style,{"mode","radiusPt","singleUse"})||!style.value("mode").isString()||
                !number(style,"radiusPt",1,100)||!style.value("singleUse").isBool())return false;
            f.tools.eraserMode=style.value("mode").toString();f.tools.eraserRadiusPt=style.value("radiusPt").toDouble();f.tools.eraserSingleUse=style.value("singleUse").toBool();
        }else if(f.tool==InkCanvas::Tool::TextBox){
            if(!exactKeys(style,{"color","opacity","family","sizePt","singleUse"})||!readColor(style,f.color)||
                !number(style,"opacity",0,1)||!style.value("family").isString()||!number(style,"sizePt",4,144)||!style.value("singleUse").isBool())return false;
            f.tools.opacity=style.value("opacity").toDouble();f.tools.textFamily=style.value("family").toString();f.tools.textSizePt=style.value("sizePt").toDouble();f.tools.textSingleUse=style.value("singleUse").toBool();
        }else if(!style.isEmpty())return false;
        result.append(f);
    }
    if(!validFavoriteTools(result))return false;
    favorites=std::move(result);return true;
}

bool validFavoritesBarPreferences(const FavoritesBarPreferences& p)
{
    return (p.side==QStringLiteral("left")||p.side==QStringLiteral("right")) && p.geometry.width()>=80 && p.geometry.width()<=1200 &&
        p.geometry.height()>=80 && p.geometry.height()<=1600 && p.geometry.x()>=-100000 && p.geometry.x()<=100000 && p.geometry.y()>=-100000 && p.geometry.y()<=100000;
}
QJsonObject favoritesBarJson(const FavoritesBarPreferences& p)
{
    return {{"visible",p.visible},{"collapsed",p.collapsed},{"side",p.side},{"floating",p.floating},
        {"geometry",QJsonObject{{"x",p.geometry.x()},{"y",p.geometry.y()},{"width",p.geometry.width()},{"height",p.geometry.height()}}}};
}
bool readFavoritesBar(const QJsonObject& object, FavoritesBarPreferences& result)
{
    if(!exactKeys(object,{"visible","collapsed","side","floating","geometry"})||!object.value("visible").isBool()||
        !object.value("collapsed").isBool()||!object.value("floating").isBool()||!object.value("side").isString()||!object.value("geometry").isObject())return false;
    const auto geometry=object.value("geometry").toObject();
    if(!exactKeys(geometry,{"x","y","width","height"})||!number(geometry,"x",-100000,100000,true)||
        !number(geometry,"y",-100000,100000,true)||!number(geometry,"width",80,1200,true)||!number(geometry,"height",80,1600,true))return false;
    FavoritesBarPreferences p;p.visible=object.value("visible").toBool();p.collapsed=object.value("collapsed").toBool();
    p.side=object.value("side").toString();p.floating=object.value("floating").toBool();p.geometry={geometry.value("x").toInt(),geometry.value("y").toInt(),geometry.value("width").toInt(),geometry.value("height").toInt()};
    if(!validFavoritesBarPreferences(p))return false;result=p;return true;
}
} // namespace propernotes
