// SPDX-License-Identifier: GPL-3.0-only
#include "WorkspaceStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace propernotes {
namespace {
constexpr auto Format = "ProperNotes.NativeWorkspace";
constexpr int Version = 1;
constexpr qsizetype MaximumPathLength = 4096;
constexpr qint64 MaximumExactInteger = 9'007'199'254'740'991LL;

bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}

bool validUnicode(const QString& text)
{
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text[i].isHighSurrogate()) {
            if (++i >= text.size() || !text[i].isLowSurrogate()) return false;
        } else if (text[i].isLowSurrogate()) {
            return false;
        }
    }
    return true;
}

bool pathPart(const QString& part, bool fileComponent = true)
{
    if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral("..")
        || part.endsWith(QChar('.')) || part.endsWith(QChar(' ')))
        return false;
    for (const QChar character : part) {
        if (character.category() == QChar::Other_Control
            || QStringLiteral("<>:\"|?*").contains(character))
            return false;
    }
    if (!fileComponent) return true; // UNC server/share names are not DOS devices.
    const QString base = part.section(QChar('.'), 0, 0).toUpper();
    if (QStringList { QStringLiteral("CON"), QStringLiteral("PRN"),
                      QStringLiteral("AUX"), QStringLiteral("NUL") }.contains(base))
        return false;
    if ((base.startsWith(QStringLiteral("COM")) || base.startsWith(QStringLiteral("LPT")))
        && base.size() == 4) {
        const QChar digit = base.back();
        if ((digit >= QChar('1') && digit <= QChar('9'))
            || digit == QChar(0x00b9) || digit == QChar(0x00b2) || digit == QChar(0x00b3))
            return false;
    }
    return true;
}

QByteArray digest(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

bool exactKeys(const QJsonObject& object, const QStringList& expected)
{
    const QStringList actual = object.keys();
    return QSet<QString>(actual.cbegin(), actual.cend()) == QSet<QString>(expected.cbegin(), expected.cend());
}

bool integer(const QJsonValue& value, qint64 low, qint64 high)
{
    const double number = value.toDouble(std::numeric_limits<double>::quiet_NaN());
    return value.isDouble() && std::isfinite(number) && number == std::floor(number)
        && number >= double(low) && number <= double(high);
}

bool utcTime(const QDateTime& time)
{
    return time.isValid() && time.offsetFromUtc() == 0 && time.date().year() >= 1 && time.date().year() <= 9999;
}

QString timeJson(const QDateTime& time)
{
    return time.isValid() ? time.toUTC().toString(Qt::ISODateWithMs) : QString();
}

bool readTime(const QJsonValue& value, bool optional, QDateTime& result)
{
    if (!value.isString()) return false;
    const QString text = value.toString();
    if (text.isEmpty() && optional) {
        result = {};
        return true;
    }
    if (text.size() > 32 || !text.endsWith(QChar('Z'))) return false;
    const QDateTime time = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!utcTime(time)) return false;
    result = time.toUTC();
    return true;
}

bool plainParents(const QString& path, QString* error)
{
    QString current = QFileInfo(path).absolutePath();
    while (!current.isEmpty()) {
        const QFileInfo item(current);
        if (item.isSymLink() || item.isJunction() || (item.exists() && !item.isDir()))
            return fail(error, QStringLiteral("工作区目录包含链接或非目录项, 已停止读写."));
        const QString parent = item.dir().absolutePath();
        if (parent == current) break;
        current = parent;
    }
    return true;
}

QJsonObject viewJson(const WorkspaceViewState& view)
{
    return { { "page", view.page }, { "pdfAnchor", QJsonArray { view.pdfAnchor.x(), view.pdfAnchor.y() } },
        { "viewportAnchor", QJsonArray { view.viewportAnchor.x(), view.viewportAnchor.y() } },
        { "zoom", view.zoom }, { "zoomMode", view.zoomMode }, { "layout", view.layout }, { "valid", view.valid } };
}

bool readPoint(const QJsonValue& value, QPointF& point)
{
    if (!value.isArray()) return false;
    const auto array = value.toArray();
    if (array.size() != 2 || !array[0].isDouble() || !array[1].isDouble()) return false;
    point = QPointF(array[0].toDouble(), array[1].toDouble());
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

bool readView(const QJsonValue& value, WorkspaceViewState& result)
{
    if (!value.isObject()) return false;
    const auto object = value.toObject();
    if (!exactKeys(object, { QStringLiteral("page"), QStringLiteral("pdfAnchor"), QStringLiteral("viewportAnchor"),
            QStringLiteral("zoom"), QStringLiteral("zoomMode"), QStringLiteral("layout"), QStringLiteral("valid") })
        || !integer(object.value("page"), -1, std::numeric_limits<int>::max())
        || !object.value("zoom").isDouble() || !object.value("zoomMode").isString()
        || !object.value("layout").isString() || !object.value("valid").isBool())
        return false;
    WorkspaceViewState candidate;
    candidate.page = object.value("page").toInt();
    candidate.zoom = object.value("zoom").toDouble();
    candidate.zoomMode = object.value("zoomMode").toString();
    candidate.layout = object.value("layout").toString();
    candidate.valid = object.value("valid").toBool();
    if (!readPoint(object.value("pdfAnchor"), candidate.pdfAnchor)
        || !readPoint(object.value("viewportAnchor"), candidate.viewportAnchor)
        || !WorkspaceStore::validateView(candidate))
        return false;
    result = candidate;
    return true;
}

QByteArray serialize(const WorkspaceValues& values)
{
    QJsonArray recent;
    for (const auto& entry : values.recent)
        recent.append(QJsonObject { { "path", entry.path }, { "openedUtc", timeJson(entry.openedUtc) } });
    QJsonArray reading;
    for (const auto& entry : values.readingStates)
        reading.append(QJsonObject { { "path", entry.path }, { "fileSize", double(entry.fileSize) },
            { "modifiedUtc", timeJson(entry.modifiedUtc) }, { "view", viewJson(entry.view) },
            { "hadUnsavedChanges", entry.hadUnsavedChanges } });
    QJsonArray paths;
    for (const QString& path : values.sessionPaths) paths.append(path);
    return QJsonDocument(QJsonObject { { "format", Format }, { "schemaVersion", Version },
        { "restoreEnabled", values.restoreEnabled }, { "recent", recent }, { "readingStates", reading },
        { "session", QJsonObject { { "paths", paths }, { "activePath", values.activePath } } } }).toJson(QJsonDocument::Indented);
}

bool validReadingEntry(const WorkspaceReadingEntry& entry, QString* error)
{
    if (WorkspaceStore::normalizedPath(entry.path).isEmpty() || entry.fileSize < -1 || entry.fileSize > MaximumExactInteger
        || (entry.modifiedUtc.isValid() && !utcTime(entry.modifiedUtc)))
        return fail(error, QStringLiteral("阅读登记的路径, 大小或 UTC 修改时间无效."));
    return WorkspaceStore::validateView(entry.view, error);
}

WorkspaceValues normalizedValues(WorkspaceValues values)
{
    for (auto& entry : values.recent) entry.path = WorkspaceStore::normalizedPath(entry.path);
    for (auto& entry : values.readingStates) entry.path = WorkspaceStore::normalizedPath(entry.path);
    for (auto& path : values.sessionPaths) path = WorkspaceStore::normalizedPath(path);
    if (!values.activePath.isEmpty()) {
        values.activePath = WorkspaceStore::normalizedPath(values.activePath);
        const QString activeKey = WorkspaceStore::pathKey(values.activePath);
        for (const QString& path : std::as_const(values.sessionPaths)) {
            if (WorkspaceStore::pathKey(path) == activeKey) {
                values.activePath = path;
                break;
            }
        }
    }
    return values;
}
} // namespace

WorkspaceStore::WorkspaceStore(QString applicationDirectory)
    : m_applicationDirectory(QFileInfo(applicationDirectory).canonicalFilePath()) {}

QString WorkspaceStore::filePath() const
{
    return m_applicationDirectory.isEmpty() ? QString()
        : QDir(m_applicationDirectory).filePath(QStringLiteral("propernotes-workspace.json"));
}

QString WorkspaceStore::normalizedPath(const QString& path)
{
    if (path.isEmpty() || path.size() > MaximumPathLength || !validUnicode(path)) return {};
    for (const QChar character : path) {
        if (character.category() == QChar::Other_Control) return {};
    }
    QString clean = QDir::fromNativeSeparators(path);
    if (clean.startsWith(QStringLiteral("//?/")) || clean.startsWith(QStringLiteral("//./"))
        || clean.startsWith(QStringLiteral("///"))) return {};
    const bool drive = clean.size() >= 3 && clean[0].isLetter() && clean[0].unicode() < 128
        && clean[1] == QChar(':') && clean[2] == QChar('/');
    const bool unc = clean.startsWith(QStringLiteral("//"));
    if (!drive && !unc) return {};
    clean = QDir::cleanPath(clean);
    if ((drive && (clean.size() < 4 || clean.mid(1, 2) != QStringLiteral(":/")))
        || (unc && (!clean.startsWith(QStringLiteral("//")) || clean.startsWith(QStringLiteral("///")))))
        return {};
    const QString tail = drive ? clean.mid(3) : clean.mid(2);
    const QStringList parts = tail.split(QChar('/'), Qt::KeepEmptyParts);
    if (parts.size() < (drive ? 1 : 3)) return {};
    for (qsizetype i = 0; i < parts.size(); ++i)
        if (!pathPart(parts[i], drive || i >= 2)) return {};
    if (drive) clean[0] = clean[0].toUpper();
    return clean;
}

QString WorkspaceStore::pathKey(const QString& path)
{
    return normalizedPath(path).toCaseFolded();
}

bool WorkspaceStore::validateView(const WorkspaceViewState& view, QString* error)
{
    const auto coordinate = [](qreal value) { return std::isfinite(value) && std::abs(value) <= 1e12; };
    if (view.page < -1 || (view.valid && view.page < 0)
        || !coordinate(view.pdfAnchor.x()) || !coordinate(view.pdfAnchor.y())
        || !std::isfinite(view.viewportAnchor.x()) || !std::isfinite(view.viewportAnchor.y())
        || view.viewportAnchor.x() < 0 || view.viewportAnchor.x() > 1
        || view.viewportAnchor.y() < 0 || view.viewportAnchor.y() > 1
        || !std::isfinite(view.zoom) || view.zoom < 0.000001 || view.zoom > 4
        || !QStringList { QStringLiteral("free"), QStringLiteral("fit-width"),
            QStringLiteral("fit-page"), QStringLiteral("fit-height") }.contains(view.zoomMode)
        || (view.zoomMode == QStringLiteral("free") && view.zoom < 0.25)
        || !QStringList { QStringLiteral("single"), QStringLiteral("facing"), QStringLiteral("single-continuous"),
            QStringLiteral("facing-continuous") }.contains(view.layout))
        return fail(error, QStringLiteral("阅读视角的页码, 锚点, 倍率或布局无效."));
    if (error) error->clear();
    return true;
}

bool WorkspaceStore::validate(const WorkspaceValues& values, QString* error)
{
    if (values.recent.size() > MaximumRecent || values.readingStates.size() > MaximumReadingStates
        || values.sessionPaths.size() > MaximumSessionPaths)
        return fail(error, QStringLiteral("工作区超过 100 个最近文件, 256 个阅读登记或 32 个会话标签的上限."));
    QSet<QString> seen;
    for (const auto& entry : values.recent) {
        const QString key = pathKey(entry.path);
        if (key.isEmpty() || seen.contains(key) || !utcTime(entry.openedUtc))
            return fail(error, QStringLiteral("最近文件含无效路径, 重复项或非 UTC 打开时间."));
        seen.insert(key);
    }
    seen.clear();
    for (const auto& entry : values.readingStates) {
        if (!validReadingEntry(entry, error)) return false;
        const QString key = pathKey(entry.path);
        if (seen.contains(key)) return fail(error, QStringLiteral("阅读登记包含重复的 Windows 路径."));
        seen.insert(key);
    }
    seen.clear();
    for (const QString& path : values.sessionPaths) {
        const QString key = pathKey(path);
        if (key.isEmpty() || seen.contains(key))
            return fail(error, QStringLiteral("会话标签包含无效或重复的 Windows 路径."));
        seen.insert(key);
    }
    if ((values.sessionPaths.isEmpty() && !values.activePath.isEmpty())
        || (!values.sessionPaths.isEmpty() && !seen.contains(pathKey(values.activePath))))
        return fail(error, QStringLiteral("活动文件必须属于当前会话, 空会话不可保留活动路径."));
    if (error) error->clear();
    return true;
}

bool WorkspaceStore::recordOpened(WorkspaceValues& values, const QString& path,
                                 const QDateTime& openedUtc, QString* error)
{
    const QString clean = normalizedPath(path);
    if (clean.isEmpty() || !utcTime(openedUtc))
        return fail(error, QStringLiteral("成功打开登记需要有效的绝对路径和 UTC 时间."));
    const QString key = pathKey(clean);
    auto recent = values.recent;
    recent.erase(std::remove_if(recent.begin(), recent.end(), [&](const auto& entry) {
        return pathKey(entry.path) == key;
    }), recent.end());
    recent.prepend({ clean, openedUtc.toUTC() });
    if (recent.size() > MaximumRecent) recent.resize(MaximumRecent);
    values.recent = std::move(recent);
    if (error) error->clear();
    return true;
}

bool WorkspaceStore::rememberReading(WorkspaceValues& values, const WorkspaceReadingEntry& entry, QString* error)
{
    if (!validReadingEntry(entry, error)) return false;
    if (values.readingStates.size() > MaximumReadingStates)
        return fail(error, QStringLiteral("已有阅读登记超出版本上限, 未修改它."));
    auto candidate = entry;
    candidate.path = normalizedPath(entry.path);
    if (candidate.modifiedUtc.isValid()) candidate.modifiedUtc = candidate.modifiedUtc.toUTC();
    const QString key = pathKey(candidate.path);
    auto reading = values.readingStates;
    for (qsizetype index = 0; index < reading.size(); ++index) {
        if (pathKey(reading[index].path) == key) {
            reading.removeAt(index);
            reading.append(std::move(candidate));
            values.readingStates = std::move(reading);
            if (error) error->clear();
            return true;
        }
    }
    if (reading.size() >= MaximumReadingStates) {
        QSet<QString> protectedPaths { key };
        for (const QString& path : std::as_const(values.sessionPaths)) protectedPaths.insert(pathKey(path));
        qsizetype victim = -1;
        for (qsizetype index = 0; index < reading.size(); ++index) {
            if (!protectedPaths.contains(pathKey(reading[index].path))) {
                victim = index;
                break;
            }
        }
        if (victim < 0)
            return fail(error, QStringLiteral("阅读登记已满且全部属于保留会话, 未丢弃受保护记录."));
        reading.removeAt(victim);
    }
    reading.append(std::move(candidate));
    values.readingStates = std::move(reading);
    if (error) error->clear();
    return true;
}

bool WorkspaceStore::setSession(WorkspaceValues& values, const QStringList& paths,
                               const QString& activePath, QString* error)
{
    WorkspaceValues candidate = values;
    candidate.sessionPaths = paths;
    candidate.activePath = activePath;
    if (!validate(candidate, error)) return false;
    candidate = normalizedValues(std::move(candidate));
    values.sessionPaths = std::move(candidate.sessionPaths);
    values.activePath = std::move(candidate.activePath);
    if (error) error->clear();
    return true;
}

bool WorkspaceStore::removeRecent(WorkspaceValues& values, const QString& path, QString* error)
{
    const QString key = pathKey(path);
    if (key.isEmpty()) return fail(error, QStringLiteral("最近文件路径无效, 未移除任何登记."));
    values.recent.erase(std::remove_if(values.recent.begin(), values.recent.end(), [&](const auto& entry) {
        return pathKey(entry.path) == key;
    }), values.recent.end());
    if (error) error->clear();
    return true;
}

bool WorkspaceStore::readCurrent(bool* exists, QByteArray* bytes, QString* error) const
{
    if (filePath().isEmpty() || !plainParents(filePath(), error)) return false;
    const QFileInfo item(filePath());
    *exists = item.exists() || item.isSymLink() || item.isJunction();
    bytes->clear();
    if (!*exists) return true;
    if (!item.isFile() || item.isSymLink() || item.isJunction() || item.size() > MaximumBytes)
        return fail(error, QStringLiteral("工作区文件不是普通文件, 或超过 4 MiB. 原文件保留."));
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("无法读取工作区: ") + file.errorString());
    *bytes = file.read(MaximumBytes + 1);
    if (file.error() != QFileDevice::NoError || bytes->size() > MaximumBytes || bytes->size() != file.size())
        return fail(error, QStringLiteral("工作区未完整读入或读取期间大小改变, 未覆盖原文件."));
    return true;
}

bool WorkspaceStore::load()
{
    m_values = {};
    m_loaded = true;
    m_writable = false;
    m_error.clear();
    if (m_applicationDirectory.isEmpty())
        return fail(&m_error, QStringLiteral("无法确认程序真实目录, 工作区只使用临时状态."));
    QByteArray bytes;
    if (!readCurrent(&m_baselineExists, &bytes, &m_error)) return false;
    m_baselineHash = m_baselineExists ? digest(bytes) : QByteArray();
    if (!m_baselineExists) {
        m_writable = true;
        return true;
    }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        return fail(&m_error, QStringLiteral("工作区文件损坏, 仅使用临时状态且不覆盖它."));
    const auto root = document.object();
    if (root.value("format").toString() != QString::fromLatin1(Format)
        || !integer(root.value("schemaVersion"), Version, Version))
        return fail(&m_error, QStringLiteral("工作区格式或版本未知, 未导入旧 Web 数据或覆盖原文件."));
    if (!exactKeys(root, { QStringLiteral("format"), QStringLiteral("schemaVersion"), QStringLiteral("restoreEnabled"),
            QStringLiteral("recent"), QStringLiteral("readingStates"), QStringLiteral("session") })
        || !root.value("restoreEnabled").isBool() || !root.value("recent").isArray()
        || !root.value("readingStates").isArray() || !root.value("session").isObject())
        return fail(&m_error, QStringLiteral("工作区含未知字段或字段类型无效, 原文件保留."));
    WorkspaceValues candidate;
    candidate.restoreEnabled = root.value("restoreEnabled").toBool();
    const auto recent = root.value("recent").toArray();
    const auto reading = root.value("readingStates").toArray();
    if (recent.size() > MaximumRecent || reading.size() > MaximumReadingStates)
        return fail(&m_error, QStringLiteral("工作区最近文件或阅读登记超出数量上限, 原文件保留."));
    for (const auto& value : recent) {
        if (!value.isObject()) return fail(&m_error, QStringLiteral("最近文件条目无效."));
        const auto object = value.toObject();
        WorkspaceRecentEntry entry;
        if (!exactKeys(object, { QStringLiteral("path"), QStringLiteral("openedUtc") })
            || !object.value("path").isString() || !readTime(object.value("openedUtc"), false, entry.openedUtc))
            return fail(&m_error, QStringLiteral("最近文件字段未知或类型无效, 原文件保留."));
        entry.path = object.value("path").toString();
        candidate.recent.append(std::move(entry));
    }
    for (const auto& value : reading) {
        if (!value.isObject()) return fail(&m_error, QStringLiteral("阅读状态条目无效."));
        const auto object = value.toObject();
        WorkspaceReadingEntry entry;
        if (!exactKeys(object, { QStringLiteral("path"), QStringLiteral("fileSize"), QStringLiteral("modifiedUtc"),
                QStringLiteral("view"), QStringLiteral("hadUnsavedChanges") })
            || !object.value("path").isString() || !integer(object.value("fileSize"), -1, MaximumExactInteger)
            || !readTime(object.value("modifiedUtc"), true, entry.modifiedUtc)
            || !readView(object.value("view"), entry.view) || !object.value("hadUnsavedChanges").isBool())
            return fail(&m_error, QStringLiteral("阅读状态字段未知或类型无效, 原文件保留."));
        entry.path = object.value("path").toString();
        entry.fileSize = qint64(object.value("fileSize").toDouble());
        entry.hadUnsavedChanges = object.value("hadUnsavedChanges").toBool();
        candidate.readingStates.append(std::move(entry));
    }
    const auto session = root.value("session").toObject();
    if (!exactKeys(session, { QStringLiteral("paths"), QStringLiteral("activePath") })
        || !session.value("paths").isArray() || !session.value("activePath").isString()
        || session.value("paths").toArray().size() > MaximumSessionPaths)
        return fail(&m_error, QStringLiteral("会话字段未知或类型无效, 原文件保留."));
    for (const auto& value : session.value("paths").toArray()) {
        if (!value.isString()) return fail(&m_error, QStringLiteral("会话路径条目无效, 原文件保留."));
        candidate.sessionPaths.append(value.toString());
    }
    candidate.activePath = session.value("activePath").toString();
    if (!validate(candidate, &m_error)) return false;
    m_values = normalizedValues(std::move(candidate));
    m_writable = true;
    return true;
}

bool WorkspaceStore::baselineUnchanged(QString* error) const
{
    bool exists = false;
    QByteArray bytes;
    if (!readCurrent(&exists, &bytes, error)) return false;
    if (exists != m_baselineExists || (exists && digest(bytes) != m_baselineHash))
        return fail(error, QStringLiteral("工作区已被外部创建, 修改或删除. 未覆盖, 请重新读取后再保存."));
    return true;
}

bool WorkspaceStore::save(const WorkspaceValues& values)
{
    m_error.clear();
    if (!canWrite()) return fail(&m_error, QStringLiteral("工作区尚未读取或已受写保护, 原文件保留."));
    if (!validate(values, &m_error)) return false;
    WorkspaceValues candidate = normalizedValues(values);
    const QByteArray bytes = serialize(candidate);
    if (bytes.size() > MaximumBytes)
        return fail(&m_error, QStringLiteral("工作区超过 4 MiB, 未覆盖原文件."));
    if (!plainParents(filePath(), &m_error)) return false;
    const QString lockPath = filePath() + QStringLiteral(".lock");
    const QFileInfo existingLock(lockPath);
    if (existingLock.exists() || existingLock.isSymLink() || existingLock.isJunction())
        return fail(&m_error, QStringLiteral("工作区排他锁仍存在, 未清锁或写入, 请确认其他实例已结束."));
    QLockFile lock(lockPath);
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) return fail(&m_error, QStringLiteral("另一个实例正在写工作区, 本次未保存."));
    if (!baselineUnchanged(&m_error)) return false;
    QSaveFile file(filePath());
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return fail(&m_error, QStringLiteral("工作区写入失败, 原状态保留: ") + file.errorString());
    }
    if (!baselineUnchanged(&m_error)) {
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) return fail(&m_error, QStringLiteral("工作区原子提交失败, 原状态保留: ") + file.errorString());
    m_values = std::move(candidate);
    m_baselineExists = true;
    m_baselineHash = digest(bytes);
    return true;
}

} // namespace propernotes
