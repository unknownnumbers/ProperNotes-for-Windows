// SPDX-License-Identifier: GPL-3.0-only
#include "BackupStore.h"
#include "PdfDocument.h"

#include <QCoreApplication>
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
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <stdexcept>

namespace propernotes {
namespace {
constexpr auto ManifestName = ".propernotes-backups.json";
constexpr auto LockName = ".propernotes-native-backup.lock";
constexpr qint64 MaxPdfBytes = 512LL * 1024 * 1024;
constexpr qint64 MaxManifestBytes = 16LL * 1024 * 1024;
constexpr int MaxGroups = 10000;
constexpr int MaxSnapshots = 100000;
constexpr int MaxRetention = 1000;

[[noreturn]] void fail(const QString& text) { throw std::runtime_error(text.toUtf8().constData()); }
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool validUuid(const QString& value)
{
    const QUuid parsed(value);
    return !parsed.isNull() && parsed.toString(QUuid::WithoutBraces) == value;
}
bool validHex(const QString& value)
{
    static const QRegularExpression expression(QStringLiteral("^[0-9a-f]{64}$"));
    return expression.match(value).hasMatch();
}
QString comparable(QString value)
{
    value = QDir::cleanPath(QDir::fromNativeSeparators(value));
#ifdef Q_OS_WIN
    value = value.toCaseFolded();
#endif
    return value;
}
bool samePath(const QString& a, const QString& b) { return comparable(a) == comparable(b); }
bool within(const QString& root, const QString& path)
{
    const QString base = comparable(root);
    const QString child = comparable(path);
    return child == base || child.startsWith(base.endsWith('/') ? base : base + '/');
}
bool linked(const QFileInfo& info) { return info.isSymLink() || info.isJunction(); }
bool occupied(const QString& path)
{
    const QFileInfo info(path);
    return info.exists() || linked(info);
}
QString absolute(const QString& path)
{
    if (path.isEmpty() || path.size() > 32768 || path.contains(QChar(0))
        || !QDir::isAbsolutePath(path))
        fail(QStringLiteral("请选择普通绝对路径."));
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}
QString rootPath(const QString& path)
{
    const QString result = absolute(path);
#ifdef Q_OS_WIN
    static const QRegularExpression drive(QStringLiteral("^[A-Za-z]:/"));
    if (!drive.match(result).hasMatch())
        fail(QStringLiteral("备份目录当前只支持本地驱动器, 不支持 UNC 或设备路径."));
    const QStringList parts = result.mid(3).split('/', Qt::SkipEmptyParts);
    static const QRegularExpression device(QStringLiteral("^(con|prn|aux|nul|com[0-9]|lpt[0-9])$"),
                                           QRegularExpression::CaseInsensitiveOption);
    for (const QString& part : parts) {
        if (part.endsWith('.') || part.endsWith(' ') || part.contains(':')
            || part.contains(QRegularExpression(QStringLiteral("[<>\"|?*\\x00-\\x1f]")))
            || device.match(part.section('.', 0, 0)).hasMatch())
            fail(QStringLiteral("备份路径包含设备名或不安全的路径别名."));
    }
#endif
    return result;
}

// Check every existing component before following it. Missing owned payload
// directories are allowed for listing and interrupted-operation recovery.
bool plainDirectory(const QString& path, bool create)
{
    const QString clean = absolute(path);
#ifdef Q_OS_WIN
    QString cursor = clean.left(3);
    const auto parts = clean.mid(3).split('/', Qt::SkipEmptyParts);
#else
    QString cursor = QStringLiteral("/");
    const auto parts = clean.mid(1).split('/', Qt::SkipEmptyParts);
#endif
    auto check = [&](const QString& directory) {
        QFileInfo info(directory);
        if (linked(info))
            fail(QStringLiteral("路径含符号链接或目录联接, 已停止以保护原数据: ") + directory);
        if (!info.exists()) {
            if (!create)
                return false;
            if (!QDir().mkdir(directory))
                fail(QStringLiteral("无法创建备份目录: ") + directory);
            info.refresh();
        }
        if (!info.isDir() || linked(info))
            fail(QStringLiteral("路径不是普通目录: ") + directory);
        return true;
    };
    if (!check(cursor))
        return false;
    for (const QString& part : parts) {
        cursor = QDir(cursor).filePath(part);
        if (!check(cursor))
            return false;
    }
    return true;
}

QString documentId(const QString& canonicalSource)
{
    return QString::fromLatin1(QCryptographicHash::hash(comparable(canonicalSource).toUtf8(),
                                                       QCryptographicHash::Sha256).toHex());
}

QByteArray readManifestBytes(const QString& path, bool allowMissing)
{
    plainDirectory(QFileInfo(path).absolutePath(), false);
    const QFileInfo info(path);
    if (linked(info))
        fail(QStringLiteral("备份索引不是普通文件, 未覆盖它."));
    if (!info.exists()) {
        if (allowMissing) return {};
        fail(QStringLiteral("备份索引已被移走, 已停止操作."));
    }
    if (!info.isFile() || info.size() < 1 || info.size() > MaxManifestBytes)
        fail(QStringLiteral("备份索引大小或类型无效, 原索引保留."));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) fail(file.errorString());
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError || bytes.size() != info.size())
        fail(QStringLiteral("备份索引未完整读入."));
    return bytes;
}

void exactKeys(const QJsonObject& object, const QSet<QString>& keys)
{
    if (object.size() != keys.size())
        fail(QStringLiteral("备份索引含缺失或未知字段, 仅保留原数据."));
    for (const QString& key : object.keys()) {
        if (!keys.contains(key))
            fail(QStringLiteral("备份索引版本或字段未知, 不进行修改."));
    }
}

struct Snapshot {
    QString id;
    QDateTime created;
    qint64 size = 0;
    QByteArray hash;
    QByteArray fingerprint;
    QString phase = QStringLiteral("writing");
    bool hasPayload() const { return size >= 5 && hash.size() == 32; }
};
struct Group {
    QString document;
    QString directory;
    QString source;
    QVector<Snapshot> snapshots;
};
struct Root {
    QString path;
    QString id;
    QString revision;
    QVector<Group> groups;
    QByteArray originalBytes;
    bool exists = false;
};

Root readRoot(const QString& requested, bool allowNew)
{
    Root root;
    root.path = rootPath(requested);
    const bool directoryExists = plainDirectory(root.path, allowNew);
    if (!directoryExists)
        return root;
    root.originalBytes = readManifestBytes(QDir(root.path).filePath(ManifestName), true);
    if (root.originalBytes.isEmpty()) {
        if (allowNew) {
            do { root.id = uuid(); } while (occupied(QDir(root.path).filePath(root.id)));
            root.revision = uuid();
        }
        return root;
    }
    root.exists = true;
    QJsonParseError parseError;
    const auto json = QJsonDocument::fromJson(root.originalBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject())
        fail(QStringLiteral("备份索引已损坏, 不重置、不清理现有副本."));
    const QJsonObject object = json.object();
    exactKeys(object, { "format", "version", "rootId", "revision", "groups" });
    if (object.value("format").toString() != QStringLiteral("ProperNotes.NativeBackups")
        || object.value("version").toDouble(-1) != 1)
        fail(QStringLiteral("此目录的备份格式或版本不受支持, 原索引和副本保留."));
    root.id = object.value("rootId").toString();
    root.revision = object.value("revision").toString();
    if (!validUuid(root.id) || !validUuid(root.revision) || !object.value("groups").isArray()
        || object.value("groups").toArray().size() > MaxGroups)
        fail(QStringLiteral("备份根标识或分组索引无效."));
    QSet<QString> documents, directories, snapshotIds;
    int total = 0;
    for (const auto& groupValue : object.value("groups").toArray()) {
        if (!groupValue.isObject()) fail(QStringLiteral("备份分组记录无效."));
        const auto entry = groupValue.toObject();
        exactKeys(entry, { "documentId", "directoryId", "sourcePath", "snapshots" });
        Group group;
        group.document = entry.value("documentId").toString();
        group.directory = entry.value("directoryId").toString();
        group.source = entry.value("sourcePath").toString();
        if (!validHex(group.document) || !validUuid(group.directory)
            || group.source != absolute(group.source) || documentId(group.source) != group.document
            || documents.contains(group.document) || directories.contains(group.directory)
            || !entry.value("snapshots").isArray())
            fail(QStringLiteral("备份分组路径或身份不一致, 未猜测 PDF 文件名."));
        documents.insert(group.document);
        directories.insert(group.directory);
        for (const auto& snapshotValue : entry.value("snapshots").toArray()) {
            if (++total > MaxSnapshots || !snapshotValue.isObject())
                fail(QStringLiteral("备份记录数量超出范围或记录无效."));
            const auto record = snapshotValue.toObject();
            exactKeys(record, { "id", "createdUtc", "size", "sha256", "fingerprint", "phase" });
            Snapshot snapshot;
            snapshot.id = record.value("id").toString();
            snapshot.created = QDateTime::fromString(record.value("createdUtc").toString(), Qt::ISODateWithMs);
            snapshot.phase = record.value("phase").toString();
            const double size = record.value("size").toDouble(-1);
            const QString hash = record.value("sha256").toString();
            const QString fingerprint = record.value("fingerprint").toString();
            if (!validUuid(snapshot.id) || snapshotIds.contains(snapshot.id)
                || !snapshot.created.isValid() || snapshot.created.offsetFromUtc() != 0
                || !std::isfinite(size) || std::floor(size) != size || size < 0 || size > MaxPdfBytes
                || !validHex(fingerprint)
                || (snapshot.phase != "writing" && snapshot.phase != "ready" && snapshot.phase != "removing"))
                fail(QStringLiteral("备份副本标识、大小、阶段或摘要无效."));
            snapshot.size = qint64(size);
            snapshot.hash = hash.isEmpty() ? QByteArray() : QByteArray::fromHex(hash.toLatin1());
            snapshot.fingerprint = QByteArray::fromHex(fingerprint.toLatin1());
            if ((!hash.isEmpty() && !validHex(hash))
                || (snapshot.phase != "writing" && (!snapshot.hasPayload() || hash.isEmpty()))
                || (snapshot.phase == "writing" && !snapshot.hasPayload() && (size != 0 || !hash.isEmpty())))
                fail(QStringLiteral("备份副本的阶段与校验数据不匹配."));
            snapshotIds.insert(snapshot.id);
            group.snapshots.append(std::move(snapshot));
        }
        root.groups.append(std::move(group));
    }
    return root;
}

void unchanged(const Root& root)
{
    if (!plainDirectory(root.path, false)) fail(QStringLiteral("备份目录已被移走."));
    const auto current = readManifestBytes(QDir(root.path).filePath(ManifestName), !root.exists);
    if (current != root.originalBytes)
        fail(QStringLiteral("备份索引已被外部修改, 已停止覆盖或清理."));
}

QByteArray rootJson(const Root& root)
{
    if (root.groups.size() > MaxGroups) fail(QStringLiteral("备份分组数量超过上限."));
    qsizetype totalSnapshots = 0;
    QJsonArray groups;
    for (const auto& group : root.groups) {
        totalSnapshots += group.snapshots.size();
        if (totalSnapshots > MaxSnapshots) fail(QStringLiteral("备份副本索引数量超过上限."));
        QJsonArray snapshots;
        for (const auto& snapshot : group.snapshots)
            snapshots.append(QJsonObject { { "id", snapshot.id },
                { "createdUtc", snapshot.created.toUTC().toString(Qt::ISODateWithMs) },
                { "size", double(snapshot.size) }, { "sha256", QString::fromLatin1(snapshot.hash.toHex()) },
                { "fingerprint", QString::fromLatin1(snapshot.fingerprint.toHex()) }, { "phase", snapshot.phase } });
        groups.append(QJsonObject { { "documentId", group.document }, { "directoryId", group.directory },
                                    { "sourcePath", group.source }, { "snapshots", snapshots } });
    }
    return QJsonDocument(QJsonObject { { "format", "ProperNotes.NativeBackups" }, { "version", 1 },
        { "rootId", root.id }, { "revision", root.revision }, { "groups", groups } }).toJson(QJsonDocument::Indented);
}

void commit(Root& root)
{
    unchanged(root);
    root.revision = uuid();
    const QByteArray bytes = rootJson(root);
    if (bytes.size() > MaxManifestBytes) fail(QStringLiteral("备份索引超过 16 MiB, 未覆盖原索引."));
    QSaveFile file(QDir(root.path).filePath(ManifestName));
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) fail(file.errorString());
    if (file.write(bytes) != bytes.size()) fail(file.errorString());
    unchanged(root);
    if (!file.commit()) fail(file.errorString());
    root.originalBytes = bytes;
    root.exists = true;
}

std::unique_ptr<QLockFile> lockRoot(const QString& path, bool create)
{
    if (!plainDirectory(path, create)) fail(QStringLiteral("备份目录不存在."));
    const QString name = QDir(path).filePath(LockName);
    const QFileInfo info(name);
    if (linked(info) || (info.exists() && !info.isFile()))
        fail(QStringLiteral("备份锁路径不是普通文件, 未修改它."));
    auto lock = std::make_unique<QLockFile>(name);
    lock->setStaleLockTime(0);
    if (!lock->tryLock(0)) fail(QStringLiteral("备份目录正被其他实例使用或锁状态不明, 请稍后重试."));
    return lock;
}

QString payload(const Root& root, const Group& group, const Snapshot& snapshot, bool createParents = false)
{
    if (!validUuid(root.id) || !validUuid(group.directory) || !validUuid(snapshot.id))
        fail(QStringLiteral("备份路径标识无效."));
    const QString parent = QDir(root.path).filePath(root.id + '/' + group.directory);
    if (!within(root.path, parent)) fail(QStringLiteral("备份路径越界."));
    plainDirectory(parent, createParents);
    const QString file = QDir(parent).filePath(snapshot.id + QStringLiteral(".pdf"));
    if (!within(parent, file) || linked(QFileInfo(file)))
        fail(QStringLiteral("备份副本路径已被替换或越界."));
    return file;
}

int groupIndex(const Root& root, const QString& document)
{
    for (int i = 0; i < root.groups.size(); ++i)
        if (root.groups[i].document == document) return i;
    return -1;
}
int snapshotIndex(const Group& group, const QString& id)
{
    for (int i = 0; i < group.snapshots.size(); ++i)
        if (group.snapshots[i].id == id) return i;
    return -1;
}
int ensureGroup(Root& root, const QString& source)
{
    const QString id = documentId(source);
    const int found = groupIndex(root, id);
    if (found >= 0) {
        if (!samePath(root.groups[found].source, source)) fail(QStringLiteral("备份分组源身份冲突."));
        return found;
    }
    if (root.groups.size() >= MaxGroups) fail(QStringLiteral("备份分组数量超过上限."));
    Group group;
    group.source = source;
    group.document = id;
    do { group.directory = uuid(); }
    while (occupied(QDir(root.path).filePath(root.id + '/' + group.directory)));
    root.groups.append(std::move(group));
    return root.groups.size() - 1;
}

void requireKey(const Root& root, const QString& id)
{
    if (!root.exists || root.id != id || !validUuid(id))
        fail(QStringLiteral("备份根身份已改变, 请刷新管理器后重试."));
}

void verify(const Root& root, const Group& group, const Snapshot& snapshot, bool missingAllowed = false)
{
    unchanged(root);
    const QString path = payload(root, group, snapshot);
    const QFileInfo info(path);
    if (!info.exists() && missingAllowed) return;
    if (!snapshot.hasPayload() || !info.isFile() || linked(info) || info.size() != snapshot.size)
        fail(QStringLiteral("备份副本缺失、未完成、大小不符或被替换, 已保留记录: ") + snapshot.id);
    QString error;
    if (PdfDocument::fileSha256(path, &error) != snapshot.hash)
        fail(QStringLiteral("备份 SHA256 校验失败, 未恢复或删除: ") + snapshot.id + ' ' + error);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || !file.read(1024).contains("%PDF-"))
        fail(QStringLiteral("备份不是可识别的 PDF 副本."));
    unchanged(root);
}

// The manifest retains a removal phase until the matching file has been
// removed. Interrupted deletion remains owned and visible on the next read.
void removeSnapshot(Root& root, int groupNumber, const QString& snapshotId)
{
    auto& group = root.groups[groupNumber];
    const int index = snapshotIndex(group, snapshotId);
    if (index < 0) fail(QStringLiteral("备份副本记录已改变."));
    Snapshot snapshot = group.snapshots[index];
    verify(root, group, snapshot, true);
    const QString path = payload(root, group, snapshot);
    if (occupied(path)) {
        if (!snapshot.hasPayload()) fail(QStringLiteral("未完成副本无可验证摘要, 保留文件供手动检查."));
        group.snapshots[index].phase = QStringLiteral("removing");
        commit(root);
        snapshot = group.snapshots[index];
        verify(root, group, snapshot);
        if (!QFile::remove(path)) fail(QStringLiteral("无法移除已验证的备份文件, 记录保留: ") + snapshot.id);
    }
    group.snapshots.removeAt(index);
    commit(root);
}

QString restoreTarget(const QString& requested, const QString& source)
{
    const QString path = absolute(requested);
    plainDirectory(QFileInfo(path).absolutePath(), false);
    const QFileInfo info(path);
    if (linked(info) || (info.exists() && !info.isFile()))
        fail(QStringLiteral("恢复目标不是普通文件路径."));
    const QString canonical = info.exists() ? info.canonicalFilePath()
        : QDir(QFileInfo(path).dir().canonicalPath()).filePath(info.fileName());
    if (canonical.isEmpty() || samePath(canonical, source))
        fail(QStringLiteral("恢复必须另存为副本, 不覆盖源 PDF."));
    QString parent = QFileInfo(canonical).absolutePath();
    for (;;) {
        if (occupied(QDir(parent).filePath(ManifestName)))
            fail(QStringLiteral("不能把恢复目标放在备份存储目录内."));
        const QString next = QFileInfo(parent).dir().absolutePath();
        if (samePath(next, parent)) break;
        parent = next;
    }
    return canonical;
}

void copyVerified(const Root& sourceRoot, const Group& group, const Snapshot& snapshot,
                  const QString& target, const QByteArray& expectedTarget,
                  const std::function<void()>& extraCheck = {})
{
    verify(sourceRoot, group, snapshot);
    const QString source = payload(sourceRoot, group, snapshot);
    const bool existed = occupied(target);
    QByteArray expected = expectedTarget;
    if (expected.size() == 64) expected = QByteArray::fromHex(expected);
    auto targetUnchanged = [&] {
        plainDirectory(QFileInfo(target).absolutePath(), false);
        const QFileInfo info(target);
        if (linked(info) || (info.exists() && !info.isFile())) fail(QStringLiteral("目标路径类型已改变."));
        if (existed) {
            if (expected.size() != 32 || PdfDocument::fileSha256(target) != expected)
                fail(QStringLiteral("恢复目标已存在或已被外部修改, 未覆盖它."));
        } else if (occupied(target)) {
            fail(QStringLiteral("目标在复制期间已出现, 未覆盖它."));
        }
        if (extraCheck) extraCheck();
    };
    targetUnchanged();
    QLockFile targetLock(target + QStringLiteral(".propernotes-copy.lock"));
    targetLock.setStaleLockTime(0);
    if (!targetLock.tryLock(0)) fail(QStringLiteral("复制目标正被其他操作使用."));
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) fail(input.errorString());
    QSaveFile output(target);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) fail(output.errorString());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 total = 0;
    while (!input.atEnd()) {
        const QByteArray bytes = input.read(1024 * 1024);
        if (bytes.isEmpty()) {
            if (input.error() != QFileDevice::NoError || !input.atEnd())
                fail(QStringLiteral("副本读取未能继续, 未提交目标文件."));
            break;
        }
        total += bytes.size();
        if (total > MaxPdfBytes || total > snapshot.size) fail(QStringLiteral("复制期间备份大小发生变化."));
        hash.addData(bytes);
        if (output.write(bytes) != bytes.size()) fail(output.errorString());
    }
    if (total != snapshot.size || hash.result() != snapshot.hash)
        fail(QStringLiteral("复制内容校验失败, 未提交目标文件."));
    verify(sourceRoot, group, snapshot);
    targetUnchanged();
    if (!output.commit()) fail(output.errorString());
}
} // namespace

QString BackupStore::defaultRoot()
{
    const QString directory = QFileInfo(QCoreApplication::applicationDirPath()).canonicalFilePath();
    return QDir(directory.isEmpty() ? QCoreApplication::applicationDirPath() : directory)
        .filePath(QStringLiteral("Backups"));
}

QString BackupStore::documentIdForPath(const QString& sourcePath)
{
    const QFileInfo info(sourcePath);
    const QString canonical = info.exists() ? info.canonicalFilePath() : absolute(sourcePath);
    return canonical.isEmpty() ? QString() : documentId(canonical);
}

BackupCatalog BackupStore::list(const QStringList& rootPaths) const
{
    BackupCatalog catalog;
    QSet<QString> visited;
    if (rootPaths.size() > 128) {
        catalog.warnings.append(QStringLiteral("备份目录数量超过上限, 请减少登记目录."));
        return catalog;
    }
    for (const QString& requested : rootPaths) {
        BackupRootInfo rootInfo;
        rootInfo.rootPath = requested;
        try {
            Root root = readRoot(requested, false);
            if (visited.contains(comparable(root.path))) continue;
            visited.insert(comparable(root.path));
            rootInfo.rootPath = root.path;
            rootInfo.rootId = root.id;
            rootInfo.initialized = root.exists;
            rootInfo.writable = root.exists && QFileInfo(root.path).isWritable();
            for (const auto& group : root.groups) {
                BackupGroupInfo item;
                item.rootPath = root.path;
                item.rootId = root.id;
                item.documentId = group.document;
                item.groupId = group.directory;
                item.sourcePath = group.source;
                item.fileName = QFileInfo(group.source).fileName();
                for (const auto& snapshot : group.snapshots) {
                    BackupSnapshotInfo info;
                    info.id = snapshot.id;
                    info.createdUtc = snapshot.created;
                    info.size = snapshot.size;
                    info.sha256 = snapshot.hash;
                    try {
                        const QFileInfo file(payload(root, group, snapshot));
                        info.available = snapshot.hasPayload() && file.isFile() && !linked(file) && file.size() == snapshot.size;
                        if (!info.available) info.problem = QStringLiteral("副本缺失、未完成或大小不符, 记录保留.");
                        else if (snapshot.phase != "ready") info.problem = QStringLiteral("有未完成的存储操作, 恢复或清理时会完整校验.");
                    } catch (const std::exception& error) { info.problem = QString::fromUtf8(error.what()); }
                    item.totalBytes += snapshot.size;
                    item.snapshots.append(std::move(info));
                }
                std::sort(item.snapshots.begin(), item.snapshots.end(), [](const auto& a, const auto& b) {
                    return a.createdUtc == b.createdUtc ? a.id > b.id : a.createdUtc > b.createdUtc;
                });
                catalog.groups.append(std::move(item));
            }
        } catch (const std::exception& error) {
            rootInfo.error = QString::fromUtf8(error.what());
            catalog.warnings.append(requested + QStringLiteral(": ") + rootInfo.error);
        }
        catalog.roots.append(std::move(rootInfo));
    }
    return catalog;
}

// Mutation methods follow below. Every action uses a fresh validated manifest
// under its root lock; no caller-supplied filename is used as ownership proof.

BackupResult BackupStore::create(const QString& requestedRoot, const PdfDocument& document, int retainCount) const
{
    BackupResult result;
    try {
        if (retainCount < 0 || retainCount > MaxRetention)
            fail(QStringLiteral("每个 PDF 保留数量须为 0 到 1000."));
        if (retainCount == 0) {
            result.ok = true;
            result.skipped = true;
            return result; // No initialization, pruning, or deletion when disabled.
        }
        if (!document.hasDocument()) fail(QStringLiteral("尚未打开 PDF."));
        const QString directory = rootPath(requestedRoot);
        auto lock = lockRoot(directory, true);
        Root root = readRoot(directory, true);
        const QString source = absolute(document.currentPath());
        if (within(root.path, source)) fail(QStringLiteral("不在同一备份存储树中递归备份恢复副本."));
        const int g = ensureGroup(root, source);
        const QByteArray fingerprint = document.recoveryFingerprint();
        if (fingerprint.size() != 32) fail(QStringLiteral("无法取得当前 PDF 恢复状态."));
        int latest = -1;
        for (int i = 0; i < root.groups[g].snapshots.size(); ++i) {
            const auto& candidate = root.groups[g].snapshots[i];
            if (candidate.phase != "ready") continue;
            if (latest < 0 || candidate.created > root.groups[g].snapshots[latest].created)
                latest = i;
        }
        bool latestValid = false;
        if (latest >= 0) {
            try {
                verify(root, root.groups[g], root.groups[g].snapshots[latest]);
                latestValid = true;
            } catch (const std::exception& error) {
                result.warnings.append(QStringLiteral("旧副本未通过校验, 保留并尝试创建新副本: ")
                                       + QString::fromUtf8(error.what()));
            }
        }
        if (latestValid && root.groups[g].snapshots[latest].fingerprint == fingerprint) {
            result.ok = true;
            result.skipped = true;
            return result;
        }
        Snapshot snapshot;
        bool collision = false;
        do {
            snapshot.id = uuid();
            collision = occupied(payload(root, root.groups[g], snapshot));
            for (const auto& group : root.groups)
                collision = collision || snapshotIndex(group, snapshot.id) >= 0;
        } while (collision);
        snapshot.created = QDateTime::currentDateTimeUtc();
        snapshot.fingerprint = fingerprint;
        // Prepare only the owned directories. The exact file claim, including
        // its creator-provided hash/size, is committed before the PDF write.
        commit(root); // Establish root/group directory ownership first.
        const QString file = payload(root, root.groups[g], snapshot, true);
        bool duplicate = false;
        QString error;
        QByteArray receipt;
        const bool written = document.writeRecoveryCopyClaimed(file,
            [&](const QByteArray& hash, qint64 size) {
                unchanged(root);
                payload(root, root.groups[g], snapshot); // Recheck parents after serialization.
                if (hash.size() != 32 || size < 5 || size > MaxPdfBytes)
                    fail(QStringLiteral("恢复副本的创建摘要或大小无效."));
                if (latestValid && root.groups[g].snapshots[latest].hash == hash
                    && root.groups[g].snapshots[latest].size == size) {
                    verify(root, root.groups[g], root.groups[g].snapshots[latest]);
                    root.groups[g].snapshots[latest].fingerprint = fingerprint;
                    commit(root);
                    duplicate = true;
                    return false;
                }
                snapshot.hash = hash;
                snapshot.size = size;
                root.groups[g].snapshots.append(snapshot);
                commit(root); // Recoverable writing journal, before payload commit.
                return true;
            }, &error, &receipt);
        if (duplicate) {
            result.ok = true;
            result.skipped = true;
            result.changed = true; // Only the observed state token was updated.
            return result;
        }
        if (!written) fail(error.isEmpty() ? QStringLiteral("未创建恢复副本, 已有备份未清理.") : error);
        const int index = snapshotIndex(root.groups[g], snapshot.id);
        if (index < 0 || receipt != snapshot.hash) fail(QStringLiteral("恢复副本的提交收据不匹配."));
        verify(root, root.groups[g], root.groups[g].snapshots[index]);
        root.groups[g].snapshots[index].phase = QStringLiteral("ready");
        commit(root);
        result.ok = true;
        result.changed = true;
        result.affectedSnapshots = 1;

        // Retention begins only after the new PDF and ready manifest are both
        // durable. Validate every selected old copy before deleting any of them.
        QVector<Snapshot> ready;
        for (const auto& old : root.groups[g].snapshots)
            if (old.phase == "ready" && old.id != snapshot.id) ready.append(old);
        std::sort(ready.begin(), ready.end(), [](const auto& a, const auto& b) {
            return a.created == b.created ? a.id < b.id : a.created < b.created;
        });
        // The just-committed recovery copy must survive clock rollback and
        // timestamp ties; retention removes only older record identities.
        const qsizetype removeCount = std::max(qsizetype(0), ready.size() + 1 - retainCount);
        try {
            for (qsizetype i = 0; i < removeCount; ++i)
                verify(root, root.groups[g], ready[i]);
            for (qsizetype i = 0; i < removeCount; ++i)
                removeSnapshot(root, g, ready[i].id);
        } catch (const std::exception& retentionError) {
            result.warnings.append(QStringLiteral("新恢复副本已提交; 旧副本保留或清理未完成: ")
                                   + QString::fromUtf8(retentionError.what()));
        }
    } catch (const std::exception& error) {
        result.error = QString::fromUtf8(error.what());
    }
    return result;
}

BackupResult BackupStore::removeGroups(const QVector<BackupGroupKey>& keys) const
{
    BackupResult result;
    if (keys.isEmpty()) { result.ok = true; result.skipped = true; return result; }
    QSet<QString> done;
    for (const auto& key : keys) {
        try {
            const QString directory = rootPath(key.rootPath);
            const QString identity = comparable(directory) + '/' + key.rootId + '/' + key.documentId;
            if (done.contains(identity)) continue;
            done.insert(identity);
            auto lock = lockRoot(directory, false);
            Root root = readRoot(directory, false);
            requireKey(root, key.rootId);
            const int g = groupIndex(root, key.documentId);
            if (g < 0) fail(QStringLiteral("待清理 PDF 分组已改变, 请刷新管理器."));
            const auto snapshots = root.groups[g].snapshots;
            for (const auto& snapshot : snapshots) verify(root, root.groups[g], snapshot, true);
            for (const auto& snapshot : snapshots) {
                removeSnapshot(root, g, snapshot.id);
                ++result.affectedSnapshots;
                result.changed = true;
            }
        } catch (const std::exception& error) {
            result.warnings.append(key.rootPath + QStringLiteral(": ") + QString::fromUtf8(error.what()));
        }
    }
    result.ok = result.warnings.isEmpty();
    if (!result.ok) result.error = QStringLiteral("部分备份未清理, 未验证的文件和记录已保留.");
    return result;
}

BackupResult BackupStore::restoreCopy(const BackupSnapshotKey& key, const QString& requestedTarget,
                                      const QByteArray& expectedTargetSha256) const
{
    BackupResult result;
    try {
        const QString directory = rootPath(key.rootPath);
        auto lock = lockRoot(directory, false);
        Root root = readRoot(directory, false);
        requireKey(root, key.rootId);
        const int g = groupIndex(root, key.documentId);
        if (g < 0) fail(QStringLiteral("恢复分组已改变."));
        const int s = snapshotIndex(root.groups[g], key.snapshotId);
        if (s < 0) fail(QStringLiteral("恢复副本已改变."));
        const QString target = restoreTarget(requestedTarget, root.groups[g].source);
        copyVerified(root, root.groups[g], root.groups[g].snapshots[s], target, expectedTargetSha256);
        result.ok = true;
        result.changed = true;
        result.affectedSnapshots = 1;
    } catch (const std::exception& error) { result.error = QString::fromUtf8(error.what()); }
    return result;
}

BackupResult BackupStore::migrateRoot(const QString& requestedSource, const QString& requestedTarget) const
{
    BackupResult result;
    try {
        const QString sourcePath = rootPath(requestedSource), targetPath = rootPath(requestedTarget);
        if (samePath(sourcePath, targetPath)) { result.ok = true; result.skipped = true; return result; }
        if (within(sourcePath, targetPath) || within(targetPath, sourcePath))
            fail(QStringLiteral("迁移目录不能互相嵌套, 原备份保留."));
        // A deterministic order prevents two cooperating instances from taking
        // the source/destination root locks in opposite orders.
        const bool sourceFirst = comparable(sourcePath) < comparable(targetPath);
        auto first = lockRoot(sourceFirst ? sourcePath : targetPath, !sourceFirst);
        auto second = lockRoot(sourceFirst ? targetPath : sourcePath, sourceFirst);
        Root source = readRoot(sourcePath, false);
        if (!source.exists) fail(QStringLiteral("迁移来源没有可识别的备份索引."));
        Root target = readRoot(targetPath, true);
        // No source copy is deleted until every copy has been verified and all
        // destination records committed. A failure can leave extra safe copies.
        for (const auto& group : source.groups)
            for (const auto& snapshot : group.snapshots) verify(source, group, snapshot);

        for (const auto& sourceGroup : source.groups) {
            const int g = ensureGroup(target, sourceGroup.source);
            for (const auto& sourceSnapshot : sourceGroup.snapshots) {
                int existing = snapshotIndex(target.groups[g], sourceSnapshot.id);
                if (existing >= 0) {
                    const auto& saved = target.groups[g].snapshots[existing];
                    if (saved.hash != sourceSnapshot.hash || saved.size != sourceSnapshot.size)
                        fail(QStringLiteral("迁移目标含冲突的副本 ID, 来源未清理."));
                    const QString existingPath = payload(target, target.groups[g], saved);
                    if (!occupied(existingPath)) {
                        target.groups[g].snapshots[existing].phase = QStringLiteral("writing");
                        commit(target);
                        payload(target, target.groups[g], target.groups[g].snapshots[existing], true);
                        copyVerified(source, sourceGroup, sourceSnapshot, existingPath, {}, [&] { unchanged(target); });
                        result.changed = true;
                    }
                    verify(target, target.groups[g], target.groups[g].snapshots[existing]);
                    if (saved.phase != "ready") {
                        target.groups[g].snapshots[existing].phase = QStringLiteral("ready");
                        commit(target);
                    }
                    ++result.affectedSnapshots;
                    continue;
                }
                for (const auto& other : target.groups) {
                    if (snapshotIndex(other, sourceSnapshot.id) >= 0)
                        fail(QStringLiteral("迁移副本 ID 与另一文档冲突, 原副本保留."));
                }
                Snapshot pending = sourceSnapshot;
                pending.phase = QStringLiteral("writing");
                target.groups[g].snapshots.append(pending);
                commit(target);
                const QString destination = payload(target, target.groups[g], pending, true);
                if (occupied(destination)) fail(QStringLiteral("迁移目标 UUID 路径已存在, 未覆盖它."));
                copyVerified(source, sourceGroup, sourceSnapshot, destination, {}, [&] { unchanged(target); });
                const int s = snapshotIndex(target.groups[g], pending.id);
                verify(target, target.groups[g], target.groups[g].snapshots[s]);
                target.groups[g].snapshots[s].phase = QStringLiteral("ready");
                commit(target);
                ++result.affectedSnapshots;
                result.changed = true;
            }
        }
        // Recheck the whole source and destination before any source deletion.
        unchanged(source);
        unchanged(target);
        for (const auto& group : source.groups) {
            const int g = groupIndex(target, group.document);
            if (g < 0) fail(QStringLiteral("迁移目标分组缺失, 来源未清理."));
            for (const auto& snapshot : group.snapshots) {
                verify(source, group, snapshot);
                const int s = snapshotIndex(target.groups[g], snapshot.id);
                if (s < 0) fail(QStringLiteral("迁移目标记录缺失, 来源未清理."));
                verify(target, target.groups[g], target.groups[g].snapshots[s]);
            }
        }
        result.ok = true; // All destination copies are now verified and committed.
        for (int g = 0; g < source.groups.size(); ++g) {
            const auto snapshots = source.groups[g].snapshots;
            for (const auto& snapshot : snapshots) {
                try {
                    const int destinationGroup = groupIndex(target, source.groups[g].document);
                    const int destinationSnapshot = snapshotIndex(target.groups[destinationGroup], snapshot.id);
                    verify(target, target.groups[destinationGroup], target.groups[destinationGroup].snapshots[destinationSnapshot]);
                    removeSnapshot(source, g, snapshot.id);
                    result.changed = true;
                } catch (const std::exception& cleanupError) {
                    result.warnings.append(QStringLiteral("目标副本已保留; 部分旧目录数据未清理: ")
                                           + QString::fromUtf8(cleanupError.what()));
                }
            }
        }
    } catch (const std::exception& error) {
        result.ok = false;
        result.error = QString::fromUtf8(error.what());
    }
    return result;
}

} // namespace propernotes
