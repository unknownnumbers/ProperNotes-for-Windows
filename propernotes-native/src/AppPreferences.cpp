// SPDX-License-Identifier: GPL-3.0-only
#include "AppPreferences.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <cmath>
#include <utility>

namespace propernotes {
namespace {
constexpr qint64 MaximumSettingsBytes = 256 * 1024;
constexpr int MaximumRoots = 32;
constexpr int SettingsSchemaVersion = 12;

bool fail(QString* error, const QString& message)
{
    if (error)
        *error = message;
    return false;
}

bool directoryEntry(const QString& path)
{
    return !path.isEmpty() && path.size() <= 4096 &&
           !path.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f]"))) &&
           (path == QStringLiteral("Backups") || QDir::isAbsolutePath(path));
}

bool sameDirectory(const QString& a, const QString& b)
{
    return QDir::cleanPath(a).compare(QDir::cleanPath(b), Qt::CaseInsensitive) == 0;
}

bool exactKeys(const QJsonObject& object, const QStringList& keys)
{
    const QStringList actual = object.keys();
    return QSet<QString>(actual.begin(), actual.end()) == QSet<QString>(keys.begin(), keys.end());
}

bool integer(const QJsonValue& value, int low, int high)
{
    return value.isDouble() && std::isfinite(value.toDouble()) &&
           value.toDouble() == std::floor(value.toDouble()) && value.toDouble() >= low && value.toDouble() <= high;
}

QByteArray digest(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

bool plainParents(const QString& path, QString* error)
{
    QString current = QFileInfo(path).absolutePath();
    while (!current.isEmpty()) {
        const QFileInfo item(current);
        if (item.exists() && (!item.isDir() || item.isSymLink() || item.isJunction()))
            return fail(error, QStringLiteral("设置目录包含链接或非目录项，已停止读写。"));
        const QString parent = item.dir().absolutePath();
        if (parent == current)
            break;
        current = parent;
    }
    return true;
}
} // namespace

AppPreferences::AppPreferences(QString applicationDirectory)
    : m_applicationDirectory(QFileInfo(applicationDirectory).canonicalFilePath()),
      m_applicationDirectoryAlias(QDir::cleanPath(std::move(applicationDirectory))) {}

QString AppPreferences::filePath() const
{
    return QDir(m_applicationDirectory).filePath(QStringLiteral("propernotes-native-settings.json"));
}

QString AppPreferences::resolveDirectory(const QString& stored) const
{
    if (m_applicationDirectory.isEmpty())
        return {};
    return stored == QStringLiteral("Backups")
        ? QDir(m_applicationDirectory).filePath(QStringLiteral("Backups")) : QDir::cleanPath(stored);
}

QString AppPreferences::normalizeDirectory(const QString& selected) const
{
    const QString clean = QDir::cleanPath(selected.trimmed());
    return sameDirectory(clean, resolveDirectory(QStringLiteral("Backups"))) ||
           sameDirectory(clean, QDir(m_applicationDirectoryAlias).filePath(QStringLiteral("Backups")))
        ? QStringLiteral("Backups") : clean;
}

bool AppPreferences::validate(const AppPreferenceValues& value, QString* error)
{
    if (!validFavoriteTools(value.favorites, error)) return false;
    if (!validFavoritesBarPreferences(value.favoritesBar))
        return fail(error, QStringLiteral("工具收藏栏位置或布局无效，原设置保留。"));
    if (!validToolPreferences(value.tools))
        return fail(error, QStringLiteral("工具或阅读偏好无效，未覆盖设置。"));
    if (!QStringList{QStringLiteral("system"), QStringLiteral("light"), QStringLiteral("dark")}.contains(value.theme))
        return fail(error, QStringLiteral("界面主题无效。"));
    if (value.controlsHideDelaySeconds < 1 || value.controlsHideDelaySeconds > 30)
        return fail(error, QStringLiteral("底角控件隐藏时间须为 1–30 秒。"));
    if (value.sidebarWidth < 112 || value.sidebarWidth > 400 || !value.penColor.isValid() || value.penColor.alpha() != 255 ||
        !std::isfinite(value.penWidthPt) || value.penWidthPt < 0.5 || value.penWidthPt > 24)
        return fail(error, QStringLiteral("侧栏或笔样式超出当前支持范围。"));
    if (value.backupIntervalMinutes < 1 || value.backupIntervalMinutes > 1440 || value.backupRetainCount < 0 || value.backupRetainCount > 1000)
        return fail(error, QStringLiteral("备份间隔须为 1–1440 分钟，每份 PDF 保留数量须为 0–1000。"));
    if (!directoryEntry(value.backupDirectory) || value.backupDirectories.isEmpty() || value.backupDirectories.size() > MaximumRoots)
        return fail(error, QStringLiteral("备份目录无效，或已登记目录超过 32 个。"));
    bool activeFound = false;
    QSet<QString> roots;
    for (const QString& root : value.backupDirectories) {
        if (!directoryEntry(root) || roots.contains(QDir::cleanPath(root).toCaseFolded()))
            return fail(error, QStringLiteral("备份目录登记包含无效或重复项。"));
        roots.insert(QDir::cleanPath(root).toCaseFolded());
        activeFound |= sameDirectory(root, value.backupDirectory);
    }
    return activeFound || fail(error, QStringLiteral("当前备份目录必须保留在登记列表中。"));
}

bool AppPreferences::readCurrent(bool* exists, QByteArray* bytes, QString* error) const
{
    if (!plainParents(filePath(), error))
        return false;
    const QFileInfo item(filePath());
    *exists = item.exists() || item.isSymLink() || item.isJunction();
    bytes->clear();
    if (!*exists)
        return true;
    if (!item.isFile() || item.isSymLink() || item.isJunction() || item.size() > MaximumSettingsBytes)
        return fail(error, QStringLiteral("设置文件不是普通文件，或超过 256 KiB；未覆盖它。"));
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("无法读取设置文件：") + file.errorString());
    *bytes = file.read(MaximumSettingsBytes + 1);
    if (file.error() != QFileDevice::NoError || bytes->size() > MaximumSettingsBytes)
        return fail(error, QStringLiteral("设置文件读取失败或大小发生变化，未覆盖它。"));
    return true;
}

bool AppPreferences::load()
{
    m_values = {};
    m_loaded = true;
    m_writable = false;
    m_error.clear();
    if (m_applicationDirectory.isEmpty())
        return fail(&m_error, QStringLiteral("无法确认程序真实目录，设置与自动备份已暂停。"));
    QByteArray bytes;
    if (!readCurrent(&m_baselineExists, &bytes, &m_error))
        return false;
    m_baselineHash = m_baselineExists ? digest(bytes) : QByteArray();
    if (!m_baselineExists) {
        m_writable = true;
        return true;
    }
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        return fail(&m_error, QStringLiteral("设置文件已损坏；采用临时默认值，不覆盖原文件。"));
    const QJsonObject root = document.object();
    if (!integer(root.value(QStringLiteral("schemaVersion")), 1, SettingsSchemaVersion))
        return fail(&m_error, QStringLiteral("设置版本不受支持；采用临时默认值，不覆盖原文件。"));
    QStringList rootKeys{QStringLiteral("schemaVersion"), QStringLiteral("theme"), QStringLiteral("sidebarVisible"), QStringLiteral("sidebarWidth"),
                        QStringLiteral("penColor"), QStringLiteral("penWidthPt"), QStringLiteral("backup")};
    const int schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt();
    if (schemaVersion >= 2) rootKeys.append(QStringLiteral("tools"));
    if (schemaVersion >= 3) { rootKeys.append(QStringLiteral("favorites")); rootKeys.append(QStringLiteral("favoritesBar")); }
    if (schemaVersion >= 8) rootKeys.append(QStringLiteral("editTextMarkups"));
    if (schemaVersion >= 10) rootKeys.append(QStringLiteral("controlsHideDelaySeconds"));
    if (!exactKeys(root, rootKeys))
        return fail(&m_error, QStringLiteral("设置文件含未知字段；未覆盖原文件。"));
    const QJsonObject backup = root.value(QStringLiteral("backup")).toObject();
    if (!root.value(QStringLiteral("theme")).isString() || !root.value(QStringLiteral("sidebarVisible")).isBool() ||
        !integer(root.value(QStringLiteral("sidebarWidth")), 112, 400) || !root.value(QStringLiteral("penColor")).isString() ||
        !root.value(QStringLiteral("penWidthPt")).isDouble() || !root.value(QStringLiteral("backup")).isObject() ||
        !exactKeys(backup, {QStringLiteral("intervalMinutes"), QStringLiteral("retainCount"), QStringLiteral("directory"), QStringLiteral("knownDirectories")}) ||
        !integer(backup.value(QStringLiteral("intervalMinutes")), 1, 1440) || !integer(backup.value(QStringLiteral("retainCount")), 0, 1000) ||
        !backup.value(QStringLiteral("directory")).isString() || !backup.value(QStringLiteral("knownDirectories")).isArray())
        return fail(&m_error, QStringLiteral("设置字段类型或范围无效；未覆盖原文件。"));
    AppPreferenceValues candidate;
    if (schemaVersion >= 10) {
        const auto delay = root.value(QStringLiteral("controlsHideDelaySeconds"));
        if (!integer(delay, 1, 30))
            return fail(&m_error, QStringLiteral("底角控件隐藏时间无效，原设置文件保留。"));
        candidate.controlsHideDelaySeconds = delay.toInt();
    }
    if (schemaVersion >= 2 &&
        (!root.value(QStringLiteral("tools")).isObject()
         || !readToolPreferences(root.value(QStringLiteral("tools")).toObject(), candidate.tools, schemaVersion)))
        return fail(&m_error, QStringLiteral("工具设置字段无效，原设置文件保留。"));
    if (schemaVersion >= 3 &&
        (!root.value(QStringLiteral("favorites")).isArray() || !root.value(QStringLiteral("favoritesBar")).isObject() ||
         !readFavoriteTools(root.value(QStringLiteral("favorites")).toArray(), candidate.favorites, schemaVersion) ||
         !readFavoritesBar(root.value(QStringLiteral("favoritesBar")).toObject(), candidate.favoritesBar, schemaVersion)))
        return fail(&m_error, QStringLiteral("工具收藏或收藏栏设置无效，原设置文件保留。"));
    if (schemaVersion >= 8) {
        const auto editTextMarkups = root.value(QStringLiteral("editTextMarkups"));
        if (!editTextMarkups.isBool())
            return fail(&m_error, QStringLiteral("正文审阅标记设置无效，原设置文件保留。"));
        candidate.editTextMarkups = editTextMarkups.toBool();
    }
    candidate.theme = root.value(QStringLiteral("theme")).toString();
    candidate.sidebarVisible = root.value(QStringLiteral("sidebarVisible")).toBool();
    candidate.sidebarWidth = root.value(QStringLiteral("sidebarWidth")).toInt();
    const QString color = root.value(QStringLiteral("penColor")).toString();
    if (!QRegularExpression(QStringLiteral("^#[0-9a-fA-F]{6}$")).match(color).hasMatch())
        return fail(&m_error, QStringLiteral("设置中的笔颜色无效；未覆盖原文件。"));
    candidate.penColor = QColor(color);
    candidate.penWidthPt = root.value(QStringLiteral("penWidthPt")).toDouble();
    candidate.backupIntervalMinutes = backup.value(QStringLiteral("intervalMinutes")).toInt();
    candidate.backupRetainCount = backup.value(QStringLiteral("retainCount")).toInt();
    candidate.backupDirectory = backup.value(QStringLiteral("directory")).toString();
    candidate.backupDirectories.clear();
    for (const QJsonValue& entry : backup.value(QStringLiteral("knownDirectories")).toArray()) {
        if (!entry.isString())
            return fail(&m_error, QStringLiteral("备份目录登记无效；未覆盖原文件。"));
        candidate.backupDirectories.append(entry.toString());
    }
    candidate.backupDirectory = normalizeDirectory(candidate.backupDirectory);
    for (QString& directory : candidate.backupDirectories)
        directory = normalizeDirectory(directory);
    if (!validate(candidate, &m_error))
        return false;
    m_values = candidate;
    m_writable = true;
    return true;
}

bool AppPreferences::baselineUnchanged(QString* error) const
{
    bool exists = false;
    QByteArray bytes;
    if (!readCurrent(&exists, &bytes, error))
        return false;
    if (exists != m_baselineExists || (exists && digest(bytes) != m_baselineHash))
        return fail(error, QStringLiteral("设置文件已被外部修改、创建或删除；未覆盖。请关闭设置并重新读取后再调整。"));
    return true;
}

bool AppPreferences::save(const AppPreferenceValues& requested)
{
    m_error.clear();
    if (!canWrite())
        return fail(&m_error, QStringLiteral("当前设置文件只读或尚未读取，未覆盖它。请保留并检查原设置文件。"));
    AppPreferenceValues candidate = requested;
    candidate.backupDirectory = normalizeDirectory(candidate.backupDirectory);
    for (QString& directory : candidate.backupDirectories)
        directory = normalizeDirectory(directory);
    if (!validate(candidate, &m_error))
        return false;
    QLockFile lock(filePath() + QStringLiteral(".lock"));
    lock.setStaleLockTime(30000);
    if (!lock.tryLock(0))
        return fail(&m_error, QStringLiteral("另一个程序正在写设置；本次未保存。"));
    if (!baselineUnchanged(&m_error))
        return false;
    QJsonArray directories;
    for (const QString& path : candidate.backupDirectories)
        directories.append(path);
    QJsonObject backup{{QStringLiteral("intervalMinutes"), candidate.backupIntervalMinutes},
                       {QStringLiteral("retainCount"), candidate.backupRetainCount},
                       {QStringLiteral("directory"), candidate.backupDirectory},
                       {QStringLiteral("knownDirectories"), directories}};
    const QJsonObject root{{QStringLiteral("schemaVersion"), SettingsSchemaVersion}, {QStringLiteral("theme"), candidate.theme},
                           {QStringLiteral("sidebarVisible"), candidate.sidebarVisible}, {QStringLiteral("sidebarWidth"), candidate.sidebarWidth},
                           {QStringLiteral("penColor"), candidate.penColor.name(QColor::HexRgb)}, {QStringLiteral("penWidthPt"), candidate.penWidthPt},
                           {QStringLiteral("backup"), backup}, {QStringLiteral("tools"), toolPreferencesJson(candidate.tools)},
                           {QStringLiteral("editTextMarkups"), candidate.editTextMarkups},
                           {QStringLiteral("controlsHideDelaySeconds"), candidate.controlsHideDelaySeconds},
                           {QStringLiteral("favorites"),favoriteToolsJson(candidate.favorites)},
                           {QStringLiteral("favoritesBar"),favoritesBarJson(candidate.favoritesBar)}};
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (bytes.size() > MaximumSettingsBytes)
        return fail(&m_error, QStringLiteral("设置内容超过当前大小上限，未保存。"));
    QSaveFile file(filePath());
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return fail(&m_error, QStringLiteral("设置写入失败，原状态保留：") + file.errorString());
    }
    // Best-effort conflict detection immediately before atomic replacement.
    // Other applications do not participate in this process's lock protocol.
    if (!baselineUnchanged(&m_error)) {
        file.cancelWriting();
        return false;
    }
    if (!file.commit())
        return fail(&m_error, QStringLiteral("设置提交失败，原状态保留：") + file.errorString());
    m_values = candidate;
    m_baselineExists = true;
    m_baselineHash = digest(bytes);
    return true;
}

} // namespace propernotes
