// SPDX-License-Identifier: GPL-3.0-only
#include "FavoriteTools.h"
#include <QJsonValue>
#include <QSet>
#include <QPair>
#include <algorithm>
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
    case InkCanvas::Tool::Highlighter: return QStringLiteral("freehand-highlighter");
    case InkCanvas::Tool::ObjectEraser: return QStringLiteral("eraser");
    case InkCanvas::Tool::TextBox: return QStringLiteral("text-box");
    case InkCanvas::Tool::Select: return QStringLiteral("select");
    case InkCanvas::Tool::Pan: return QStringLiteral("pan");
    case InkCanvas::Tool::Line:
    case InkCanvas::Tool::Arrow:
    case InkCanvas::Tool::Rectangle:
    case InkCanvas::Tool::Ellipse: return shapeToolPreferenceToken(*InkCanvas::shapeKind(tool));
    case InkCanvas::Tool::TextSelect: return QStringLiteral("text-select");
    }
    return {};
}
QString markupToken(TextMarkupKind kind)
{
    switch (kind) {
    case TextMarkupKind::Highlight: return QStringLiteral("highlight");
    case TextMarkupKind::Underline: return QStringLiteral("underline");
    case TextMarkupKind::StrikeOut: return QStringLiteral("strikeout");
    case TextMarkupKind::Squiggly: return QStringLiteral("squiggly");
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
    if (favorite.markupKind)
        return {{"kind", markupToken(*favorite.markupKind)}, {"color", favorite.color.name(QColor::HexRgb)},
                {"opacity", t.opacity}, {"singleUse", favorite.reviewSingleUse}};
    if (favorite.tool == InkCanvas::Tool::Pen)
        return {{"color",favorite.color.name(QColor::HexRgb)},{"widthPt",favorite.widthPt},
                {"opacity",t.opacity},{"pressureEnabled",t.pressureEnabled},
                {"pressureSensitivity",t.pressureSensitivity},{"singleUse",t.penSingleUse}};
    if (favorite.tool == InkCanvas::Tool::Highlighter)
        return {{"color", favorite.color.name(QColor::HexRgb)}, {"widthPt", favorite.widthPt},
                {"opacityPercent", t.highlighterOpacity}, {"singleUse", t.highlighterSingleUse}};
    if (favorite.tool == InkCanvas::Tool::ObjectEraser)
        return {{"mode",t.eraserMode},{"radiusPt",t.eraserRadiusPt},{"singleUse",t.eraserSingleUse}};
    if (favorite.tool == InkCanvas::Tool::TextBox)
        return {{"color",favorite.color.name(QColor::HexRgb)},{"opacity",t.opacity},
                {"family",t.textFamily},{"sizePt",t.textSizePt},{"singleUse",t.textSingleUse}};
    if (const auto shape = InkCanvas::shapeKind(favorite.tool))
        return shapeToolPreferenceJson(t.shapeToolPreference(*shape));
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

QString textMarkupKindName(TextMarkupKind kind)
{
    switch (kind) {
    case TextMarkupKind::Highlight: return QStringLiteral("高亮");
    case TextMarkupKind::Underline: return QStringLiteral("下划线");
    case TextMarkupKind::StrikeOut: return QStringLiteral("删除线");
    case TextMarkupKind::Squiggly: return QStringLiteral("波浪线");
    }
    return QStringLiteral("审阅");
}

FavoriteTool captureReviewFavorite(TextMarkupKind kind, const QColor& color, const QString& name, bool singleUse)
{
    FavoriteTool favorite;
    favorite.tool = InkCanvas::Tool::TextSelect;
    favorite.markupKind = kind;
    favorite.name = name.isEmpty() ? textMarkupKindName(kind) : name;
    favorite.color = color;
    favorite.color.setAlpha(255);
    favorite.tools.opacity = color.alphaF();
    favorite.reviewSingleUse = singleUse;
    return favorite;
}

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
    } else if (tool == InkCanvas::Tool::Highlighter) {
        result.color = source.highlighterColor;
        result.color.setAlpha(255);
        result.widthPt = source.highlighterWidthPt;
        result.tools.highlighterColor = result.color;
        result.tools.highlighterWidthPt = result.widthPt;
        result.tools.highlighterOpacity = source.highlighterOpacity;
        result.tools.highlighterSingleUse = source.highlighterSingleUse;
    } else if (tool == InkCanvas::Tool::ObjectEraser) {
        result.tools.eraserMode = source.eraserMode;
        result.tools.eraserRadiusPt = source.eraserRadiusPt;
        result.tools.eraserSingleUse = source.eraserSingleUse;
    } else if (tool == InkCanvas::Tool::TextBox) {
        result.tools.textFamily = source.textFamily;
        result.tools.textSizePt = source.textSizePt;
        result.tools.textSingleUse = source.textSingleUse;
    } else if (const auto shape = InkCanvas::shapeKind(tool)) {
        result.tools.shapeToolPreference(*shape) = source.shapeToolPreference(*shape);
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
    } else if (favorite.tool == InkCanvas::Tool::Highlighter) {
        tools.highlighterColor = favorite.color;
        tools.highlighterWidthPt = favorite.widthPt;
        tools.highlighterOpacity = favorite.tools.highlighterOpacity;
        tools.highlighterSingleUse = favorite.tools.highlighterSingleUse;
    } else if (favorite.tool == InkCanvas::Tool::ObjectEraser) {
        tools.eraserMode = favorite.tools.eraserMode;
        tools.eraserRadiusPt = favorite.tools.eraserRadiusPt;
        tools.eraserSingleUse = favorite.tools.eraserSingleUse;
    } else if (favorite.tool == InkCanvas::Tool::TextBox) {
        color = favorite.color; tools.opacity = favorite.tools.opacity;
        tools.textFamily = favorite.tools.textFamily;
        tools.textSizePt = favorite.tools.textSizePt;
        tools.textSingleUse = favorite.tools.textSingleUse;
    } else if (const auto shape = InkCanvas::shapeKind(favorite.tool)) {
        tools.shapeToolPreference(*shape) = favorite.tools.shapeToolPreference(*shape);
    }
}

QString favoriteToolName(InkCanvas::Tool tool)
{
    switch (tool) {
    case InkCanvas::Tool::Pen: return QStringLiteral("笔");
    case InkCanvas::Tool::Highlighter: return QStringLiteral("自由荧光笔");
    case InkCanvas::Tool::ObjectEraser: return QStringLiteral("橡皮");
    case InkCanvas::Tool::TextBox: return QStringLiteral("文字框");
    case InkCanvas::Tool::Select: return QStringLiteral("框选 / 移动");
    case InkCanvas::Tool::Pan: return QStringLiteral("平移");
    case InkCanvas::Tool::TextSelect: return QStringLiteral("选文字");
    case InkCanvas::Tool::Line: return QStringLiteral("直线");
    case InkCanvas::Tool::Arrow: return QStringLiteral("箭头");
    case InkCanvas::Tool::Rectangle: return QStringLiteral("矩形");
    case InkCanvas::Tool::Ellipse: return QStringLiteral("椭圆");
    }
    return QStringLiteral("未知工具");
}

QString favoriteDescription(const FavoriteTool& favorite)
{
    if (favorite.markupKind)
        return favorite.name + QLatin1Char('\n') + QStringLiteral("正文%1 · %2% 不透明度 · %3")
            .arg(textMarkupKindName(*favorite.markupKind)).arg(qRound(favorite.tools.opacity * 100))
            .arg(favorite.reviewSingleUse ? QStringLiteral("单次") : QStringLiteral("持续"));
    QString detail = favoriteToolName(favorite.tool);
    if (favorite.tool == InkCanvas::Tool::Pen)
        detail += QStringLiteral(" · %1 pt · %2% 不透明度 · %3").arg(favorite.widthPt).arg(qRound(favorite.tools.opacity*100))
            .arg(favorite.tools.pressureEnabled ? QStringLiteral("压感 %1").arg(favorite.tools.pressureSensitivity) : QStringLiteral("固定线宽"));
    else if (favorite.tool == InkCanvas::Tool::Highlighter)
        detail += QStringLiteral(" · %1 pt · %2% 不透明度 · 固定线宽")
            .arg(favorite.widthPt).arg(favorite.tools.highlighterOpacity);
    else if (favorite.tool == InkCanvas::Tool::ObjectEraser)
        detail += QStringLiteral(" · %1 · 半径 %2 pt").arg(favorite.tools.eraserMode == QStringLiteral("partial") ? QStringLiteral("局部") : QStringLiteral("整笔"))
            .arg(favorite.tools.eraserRadiusPt);
    else if (favorite.tool == InkCanvas::Tool::TextBox)
        detail += QStringLiteral(" · %1 %2 pt · %3% 不透明度").arg(favorite.tools.textFamily).arg(favorite.tools.textSizePt).arg(qRound(favorite.tools.opacity*100));
    else if (const auto shape = InkCanvas::shapeKind(favorite.tool)) {
        const auto& style = favorite.tools.shapeToolPreference(*shape).style;
        detail += QStringLiteral(" · %1 pt · %2% 线条不透明度").arg(style.widthPt).arg(qRound(style.strokeColor.alphaF() * 100));
        if (isAreaShape(*shape)) {
            detail += style.fillColor.alpha() == 0 ? QStringLiteral(" · 无填充")
                : QStringLiteral(" · %1% 填充不透明度").arg(qRound(style.fillColor.alphaF() * 100));
        }
    }
    std::optional<bool> single;
    switch (favorite.tool) {
    case InkCanvas::Tool::Pen: single = favorite.tools.penSingleUse; break;
    case InkCanvas::Tool::Highlighter: single = favorite.tools.highlighterSingleUse; break;
    case InkCanvas::Tool::ObjectEraser: single = favorite.tools.eraserSingleUse; break;
    case InkCanvas::Tool::TextBox: single = favorite.tools.textSingleUse; break;
    default: break;
    }
    if (const auto shape = InkCanvas::shapeKind(favorite.tool)) single = favorite.tools.shapeToolPreference(*shape).singleUse;
    if (single)
        detail += *single ? QStringLiteral(" · 单次") : QStringLiteral(" · 持续");
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
        if (f.id.isNull() || ids.contains(f.id) || !safeName(f.name,80)) return fail();
        ids.insert(f.id);
        if (f.markupKind) {
            if (f.tool != InkCanvas::Tool::TextSelect || markupToken(*f.markupKind).isEmpty() || !f.color.isValid() ||
                f.color.alpha() != 255 || !std::isfinite(f.tools.opacity) || f.tools.opacity < 1.0 / 255 || f.tools.opacity > 1)
                return fail();
            continue;
        }
        if (f.reviewSingleUse || kind(f.tool).isEmpty()) return fail();
        if (f.tool == InkCanvas::Tool::Pen || f.tool == InkCanvas::Tool::TextBox) {
            if (!f.color.isValid() || f.color.alpha()!=255 || !std::isfinite(f.tools.opacity) || f.tools.opacity<0 || f.tools.opacity>1) return fail();
        }
        if (f.tool == InkCanvas::Tool::Pen && (!std::isfinite(f.widthPt) || f.widthPt<0.5 || f.widthPt>24 ||
            f.tools.pressureSensitivity<0 || f.tools.pressureSensitivity>100)) return fail();
        if (f.tool == InkCanvas::Tool::Highlighter
            && (!validHighlighterStyle(f.color, f.widthPt, f.tools.highlighterOpacity)
                || f.tools.highlighterColor != f.color || f.tools.highlighterWidthPt != f.widthPt))
            return fail();
        if (f.tool == InkCanvas::Tool::ObjectEraser &&
            ((f.tools.eraserMode!=QStringLiteral("whole") && f.tools.eraserMode!=QStringLiteral("partial")) ||
             !std::isfinite(f.tools.eraserRadiusPt) || f.tools.eraserRadiusPt<1 || f.tools.eraserRadiusPt>100)) return fail();
        if (f.tool == InkCanvas::Tool::TextBox && (!safeName(f.tools.textFamily,160) ||
            !std::isfinite(f.tools.textSizePt) || f.tools.textSizePt<4 || f.tools.textSizePt>144)) return fail();
        if (const auto shape = InkCanvas::shapeKind(f.tool)) {
            if (!validShapeToolPreference(*shape, f.tools.shapeToolPreference(*shape))) return fail();
        }
    }
    return true;
}

QJsonArray favoriteToolsJson(const QVector<FavoriteTool>& favorites)
{
    QJsonArray result;
    for (const auto& f : favorites) result.append(QJsonObject{{"id",f.id.toString(QUuid::WithoutBraces)},
        {"name",f.name},{"tool",f.markupKind ? QStringLiteral("text-review") : kind(f.tool)},{"style",styleJson(f)}});
    return result;
}

bool readFavoriteTools(const QJsonArray& array, QVector<FavoriteTool>& favorites, int settingsVersion)
{
    if (array.size()>MaximumFavorites) return false;
    const bool hasReviewUse = settingsVersion >= 6;
    QStringList reviewStyleKeys{QStringLiteral("kind"), QStringLiteral("color"), QStringLiteral("opacity")};
    if (hasReviewUse) reviewStyleKeys.append(QStringLiteral("singleUse"));
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
        else if (tool == "freehand-highlighter" && settingsVersion >= 5) f.tool = InkCanvas::Tool::Highlighter;
        else if(tool=="text-box")f.tool=InkCanvas::Tool::TextBox;else if(tool=="select")f.tool=InkCanvas::Tool::Select;
        else if(tool=="pan")f.tool=InkCanvas::Tool::Pan;
        else if(tool=="text-review")f.tool=InkCanvas::Tool::TextSelect;
        else if(tool=="text-select" && settingsVersion >= 10)f.tool=InkCanvas::Tool::TextSelect;
        else {
            if (settingsVersion < 9) return false;
            const auto match = std::find_if(ShapeTools.cbegin(), ShapeTools.cend(), [&tool](auto candidate) { return kind(candidate) == tool; });
            if (match == ShapeTools.cend()) return false;
            f.tool = *match;
        }
        const auto style=object.value("style").toObject();
        if (tool == QStringLiteral("text-review")) {
            if (!exactKeys(style, reviewStyleKeys) || !style.value("kind").isString() ||
                !readColor(style, f.color) || !number(style, "opacity", 1.0 / 255, 1) ||
                (hasReviewUse && !style.value("singleUse").isBool())) return false;
            for (auto candidate : {TextMarkupKind::Highlight, TextMarkupKind::Underline, TextMarkupKind::StrikeOut, TextMarkupKind::Squiggly})
                if (style.value("kind").toString() == markupToken(candidate)) f.markupKind = candidate;
            if (!f.markupKind) return false;
            f.tools.opacity = style.value("opacity").toDouble();
            f.reviewSingleUse = hasReviewUse && style.value("singleUse").toBool();
        } else if(f.tool==InkCanvas::Tool::Pen){
            if(!exactKeys(style,{"color","widthPt","opacity","pressureEnabled","pressureSensitivity","singleUse"}) ||
                !readColor(style,f.color)||!number(style,"widthPt",0.5,24)||!number(style,"opacity",0,1)||
                !number(style,"pressureSensitivity",0,100,true)||!style.value("pressureEnabled").isBool()||!style.value("singleUse").isBool())return false;
            f.widthPt=style.value("widthPt").toDouble();f.tools.opacity=style.value("opacity").toDouble();
            f.tools.pressureEnabled=style.value("pressureEnabled").toBool();f.tools.pressureSensitivity=style.value("pressureSensitivity").toInt();
            f.tools.penSingleUse=style.value("singleUse").toBool();
        } else if (f.tool == InkCanvas::Tool::Highlighter) {
            if (!exactKeys(style, {"color", "widthPt", "opacityPercent", "singleUse"}) || !readColor(style, f.color)
                || !number(style, "widthPt", MinHighlighterWidthPt, MaxHighlighterWidthPt)
                || !number(style, "opacityPercent", MinHighlighterOpacity, MaxHighlighterOpacity, true)
                || !style.value("singleUse").isBool())
                return false;
            f.widthPt = style.value("widthPt").toDouble();
            f.tools.highlighterColor = f.color;
            f.tools.highlighterWidthPt = f.widthPt;
            f.tools.highlighterOpacity = style.value("opacityPercent").toInt();
            f.tools.highlighterSingleUse = style.value("singleUse").toBool();
        }else if(f.tool==InkCanvas::Tool::ObjectEraser){
            if(!exactKeys(style,{"mode","radiusPt","singleUse"})||!style.value("mode").isString()||
                !number(style,"radiusPt",1,100)||!style.value("singleUse").isBool())return false;
            f.tools.eraserMode=style.value("mode").toString();f.tools.eraserRadiusPt=style.value("radiusPt").toDouble();f.tools.eraserSingleUse=style.value("singleUse").toBool();
        }else if(f.tool==InkCanvas::Tool::TextBox){
            if(!exactKeys(style,{"color","opacity","family","sizePt","singleUse"})||!readColor(style,f.color)||
                !number(style,"opacity",0,1)||!style.value("family").isString()||!number(style,"sizePt",4,144)||!style.value("singleUse").isBool())return false;
            f.tools.opacity=style.value("opacity").toDouble();f.tools.textFamily=style.value("family").toString();f.tools.textSizePt=style.value("sizePt").toDouble();f.tools.textSingleUse=style.value("singleUse").toBool();
        } else if (const auto shape = InkCanvas::shapeKind(f.tool)) {
            if (!readShapeToolPreference(style, *shape, f.tools.shapeToolPreference(*shape))) return false;
        } else if (!style.isEmpty()) return false;
        result.append(f);
    }
    if(!validFavoriteTools(result))return false;
    favorites=std::move(result);return true;
}

bool validFavoritesBarPreferences(const FavoritesBarPreferences& p)
{
    return (p.orientation==QStringLiteral("vertical")||p.orientation==QStringLiteral("horizontal")) &&
        (p.side==QStringLiteral("left")||p.side==QStringLiteral("right")||p.side==QStringLiteral("top")||p.side==QStringLiteral("bottom")) && p.geometry.width()>=80 && p.geometry.width()<=1200 &&
        p.geometry.height()>=80 && p.geometry.height()<=1600 && p.geometry.x()>=-100000 && p.geometry.x()<=100000 && p.geometry.y()>=-100000 && p.geometry.y()<=100000 &&
        (p.floatingAnchor==QStringLiteral("left")||p.floatingAnchor==QStringLiteral("right")) &&
        p.floatingEdgeOffset>=8 && p.floatingEdgeOffset<=100000 &&
        std::isfinite(p.floatingVerticalRatio) && p.floatingVerticalRatio>=0.0 && p.floatingVerticalRatio<=1.0 &&
        p.horizontalGeometry.width()>=80 && p.horizontalGeometry.width()<=1600 && p.horizontalGeometry.height()==80 &&
        p.horizontalGeometry.x()>=-100000 && p.horizontalGeometry.x()<=100000 && p.horizontalGeometry.y()>=-100000 && p.horizontalGeometry.y()<=100000 &&
        (p.horizontalAnchor==QStringLiteral("top")||p.horizontalAnchor==QStringLiteral("bottom")) &&
        p.horizontalEdgeOffset>=8 && p.horizontalEdgeOffset<=100000 &&
        std::isfinite(p.horizontalRatio) && p.horizontalRatio>=0.0 && p.horizontalRatio<=1.0;
}
QJsonObject favoritesBarJson(const FavoritesBarPreferences& p)
{
    return {{"visible",p.visible},{"collapsed",p.collapsed},{"orientation",p.orientation},{"side",p.side},{"floating",p.floating},{"pinned",p.pinned},
        {"geometry",QJsonObject{{"x",p.geometry.x()},{"y",p.geometry.y()},{"width",p.geometry.width()},{"height",p.geometry.height()}}},
        {"floatingPosition",QJsonObject{{"anchor",p.floatingAnchor},{"edgeOffset",p.floatingEdgeOffset},
            {"verticalRatio",p.floatingVerticalRatio},{"initialized",p.floatingPositionInitialized}}},
        {"horizontalPosition",QJsonObject{
            {"geometry",QJsonObject{{"x",p.horizontalGeometry.x()},{"y",p.horizontalGeometry.y()},
                {"width",p.horizontalGeometry.width()},{"height",p.horizontalGeometry.height()}}},
            {"anchor",p.horizontalAnchor},{"edgeOffset",p.horizontalEdgeOffset},
            {"ratio",p.horizontalRatio},{"initialized",p.horizontalPositionInitialized}}}};
}
bool readFavoritesBar(const QJsonObject& object, FavoritesBarPreferences& result, int settingsVersion)
{
    const bool hasPin = settingsVersion >= 7;
    const bool hasRelativePosition = settingsVersion >= 12;
    const bool hasOrientation = settingsVersion >= 13;
    QStringList keys{"visible", "collapsed", "side", "floating", "geometry"};
    if (hasPin) keys.append(QStringLiteral("pinned"));
    if (hasRelativePosition) keys.append(QStringLiteral("floatingPosition"));
    if (hasOrientation) { keys.append(QStringLiteral("orientation")); keys.append(QStringLiteral("horizontalPosition")); }
    if(!exactKeys(object,keys)||!object.value("visible").isBool()||
        !object.value("collapsed").isBool()||!object.value("floating").isBool()||!object.value("side").isString()||
        !object.value("geometry").isObject() || (hasPin && !object.value("pinned").isBool()))return false;
    const auto geometry=object.value("geometry").toObject();
    if(!exactKeys(geometry,{"x","y","width","height"})||!number(geometry,"x",-100000,100000,true)||
        !number(geometry,"y",-100000,100000,true)||!number(geometry,"width",80,1200,true)||!number(geometry,"height",80,1600,true))return false;
    FavoritesBarPreferences p;p.visible=object.value("visible").toBool();p.collapsed=object.value("collapsed").toBool();
    p.side=object.value("side").toString();p.floating=object.value("floating").toBool();p.geometry={geometry.value("x").toInt(),geometry.value("y").toInt(),geometry.value("width").toInt(),geometry.value("height").toInt()};
    p.pinned = hasPin && object.value("pinned").toBool();
    if (!hasOrientation && p.side!=QStringLiteral("left") && p.side!=QStringLiteral("right")) return false;
    if (hasRelativePosition) {
        if (!object.value("floatingPosition").isObject()) return false;
        const auto position = object.value("floatingPosition").toObject();
        if (!exactKeys(position,{"anchor","edgeOffset","verticalRatio","initialized"}) ||
            !position.value("anchor").isString() || !position.value("initialized").isBool() ||
            !number(position,"edgeOffset",8,100000,true) || !number(position,"verticalRatio",0,1)) return false;
        p.floatingAnchor = position.value("anchor").toString();
        p.floatingEdgeOffset = position.value("edgeOffset").toInt();
        p.floatingVerticalRatio = position.value("verticalRatio").toDouble();
        p.floatingPositionInitialized = position.value("initialized").toBool();
    }
    if (hasOrientation) {
        if (!object.value("orientation").isString() || !object.value("horizontalPosition").isObject()) return false;
        const auto position = object.value("horizontalPosition").toObject();
        if (!exactKeys(position,{"geometry","anchor","edgeOffset","ratio","initialized"}) ||
            !position.value("geometry").isObject() || !position.value("anchor").isString() ||
            !position.value("initialized").isBool() || !number(position,"edgeOffset",8,100000,true) ||
            !number(position,"ratio",0,1)) return false;
        const auto horizontalGeometry = position.value("geometry").toObject();
        if (!exactKeys(horizontalGeometry,{"x","y","width","height"}) ||
            !number(horizontalGeometry,"x",-100000,100000,true) || !number(horizontalGeometry,"y",-100000,100000,true) ||
            !number(horizontalGeometry,"width",80,1600,true) || !number(horizontalGeometry,"height",80,80,true)) return false;
        p.orientation = object.value("orientation").toString();
        p.horizontalGeometry = {horizontalGeometry.value("x").toInt(),horizontalGeometry.value("y").toInt(),
            horizontalGeometry.value("width").toInt(),horizontalGeometry.value("height").toInt()};
        p.horizontalAnchor = position.value("anchor").toString();
        p.horizontalEdgeOffset = position.value("edgeOffset").toInt();
        p.horizontalRatio = position.value("ratio").toDouble();
        p.horizontalPositionInitialized = position.value("initialized").toBool();
    }
    if(!validFavoritesBarPreferences(p))return false;
    if (settingsVersion < 10) {
        // Old dock widgets stored desktop coordinates. Do not interpret them
        // as positions within the PDF viewport or restore a separate window.
        p.geometry = QRect(100000,72,80,std::clamp(p.geometry.height(),320,1200));
        p.side = QStringLiteral("right"); p.floating = true; p.collapsed = false;
    }
    if (settingsVersion == 10) p.floating = true;
    result=p;return true;
}
} // namespace propernotes
