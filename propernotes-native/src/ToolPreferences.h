// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <cmath>

namespace propernotes {
struct ToolPreferences {
    bool inputFollowsTool = false;
    bool pressureEnabled = true;
    int pressureSensitivity = 50;
    qreal opacity = 1;
    QString eraserMode = QStringLiteral("whole");
    qreal eraserRadiusPt = 8;
    bool penSingleUse = false;
    bool eraserSingleUse = false;
    bool textSingleUse = false;
    QString textFamily = QStringLiteral("Segoe UI");
    qreal textSizePt = 12;
    QString layout = QStringLiteral("single-continuous");
    int thumbnailSize = 180;
};
inline bool validToolPreferences(const ToolPreferences& v) {
    for (qsizetype i=0;i<v.textFamily.size();++i) {
        const QChar c=v.textFamily.at(i);
        if(c.isNull() || c.category()==QChar::Other_Control) return false;
        if(c.isHighSurrogate()) {
            if(i+1>=v.textFamily.size() || !v.textFamily.at(i+1).isLowSurrogate()) return false;
            ++i;
        } else if(c.isLowSurrogate()) return false;
    }
    return v.pressureSensitivity >= 0 && v.pressureSensitivity <= 100 &&
        std::isfinite(v.opacity) && v.opacity >= 0 && v.opacity <= 1 &&
        (v.eraserMode == QStringLiteral("whole") || v.eraserMode == QStringLiteral("partial")) &&
        std::isfinite(v.eraserRadiusPt) && v.eraserRadiusPt >= 1 && v.eraserRadiusPt <= 100 &&
        !v.textFamily.trimmed().isEmpty() && v.textFamily.size() <= 160 &&
        !v.textFamily.contains(QChar('\n')) && !v.textFamily.contains(QChar('\r')) &&
        std::isfinite(v.textSizePt) && v.textSizePt >= 4 && v.textSizePt <= 144 &&
        QStringList{QStringLiteral("single"),QStringLiteral("facing"),QStringLiteral("single-continuous"),QStringLiteral("facing-continuous")}.contains(v.layout) &&
        v.thumbnailSize >= 80 && v.thumbnailSize <= 280;
}
inline QJsonObject toolPreferencesJson(const ToolPreferences& v) {
    return {{"inputFollowsTool",v.inputFollowsTool},{"pressureEnabled",v.pressureEnabled},
        {"pressureSensitivity",v.pressureSensitivity},{"opacity",v.opacity},{"eraserMode",v.eraserMode},
        {"eraserRadiusPt",v.eraserRadiusPt},{"penSingleUse",v.penSingleUse},
        {"eraserSingleUse",v.eraserSingleUse},{"textSingleUse",v.textSingleUse},
        {"textFamily",v.textFamily},{"textSizePt",v.textSizePt},{"layout",v.layout},
        {"thumbnailSize",v.thumbnailSize}};
}
inline bool readToolPreferences(const QJsonObject& o, ToolPreferences& result) {
    const auto keys=o.keys(); const auto expected=toolPreferencesJson(ToolPreferences{}).keys();
    if (QSet<QString>(keys.begin(),keys.end()) != QSet<QString>(expected.begin(),expected.end())) return false;
    for (const auto& key : {"inputFollowsTool","pressureEnabled","penSingleUse","eraserSingleUse","textSingleUse"})
        if (!o.value(QLatin1String(key)).isBool()) return false;
    for (const auto& key : {"pressureSensitivity","opacity","eraserRadiusPt","textSizePt","thumbnailSize"})
        if (!o.value(QLatin1String(key)).isDouble() || !std::isfinite(o.value(QLatin1String(key)).toDouble())) return false;
    for (const auto& key : {"eraserMode","textFamily","layout"}) if (!o.value(QLatin1String(key)).isString()) return false;
    const qreal sensitivity=o.value("pressureSensitivity").toDouble(), thumb=o.value("thumbnailSize").toDouble();
    if (sensitivity != std::floor(sensitivity) || thumb != std::floor(thumb) || sensitivity<0 || sensitivity>100 || thumb<80 || thumb>280) return false;
    ToolPreferences v;
    v.inputFollowsTool=o.value("inputFollowsTool").toBool(); v.pressureEnabled=o.value("pressureEnabled").toBool();
    v.pressureSensitivity=int(sensitivity); v.opacity=o.value("opacity").toDouble();
    v.eraserMode=o.value("eraserMode").toString(); v.eraserRadiusPt=o.value("eraserRadiusPt").toDouble();
    v.penSingleUse=o.value("penSingleUse").toBool(); v.eraserSingleUse=o.value("eraserSingleUse").toBool();
    v.textSingleUse=o.value("textSingleUse").toBool(); v.textFamily=o.value("textFamily").toString();
    v.textSizePt=o.value("textSizePt").toDouble(); v.layout=o.value("layout").toString(); v.thumbnailSize=int(thumb);
    if (!validToolPreferences(v)) return false;
    result=v; return true;
}
} // namespace propernotes
