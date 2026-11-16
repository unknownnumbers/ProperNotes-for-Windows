// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace propernotes {
class PdfDocument;

struct BackupGroupKey {
    QString rootPath;
    QString rootId;
    QString documentId;
};

struct BackupSnapshotKey {
    QString rootPath;
    QString rootId;
    QString documentId;
    QString snapshotId;
};

struct BackupSnapshotInfo {
    QString id;
    QDateTime createdUtc;
    qint64 size = 0;
    QByteArray sha256; // Raw 32-byte digest, as with PdfDocument::fileSha256().
    // Existence/type/size availability only. Restore and deletion additionally
    // verify the complete stored SHA256 before touching any destination/file.
    bool available = false;
    QString problem;
};

struct BackupGroupInfo {
    QString rootPath;
    QString rootId;
    QString documentId; // SHA256 of the canonical source-path identity, not its filename.
    QString groupId;    // Owned UUID directory declared by the root manifest.
    QString sourcePath;
    QString fileName;
    QVector<BackupSnapshotInfo> snapshots;
    qint64 totalBytes = 0;
    BackupGroupKey key() const { return { rootPath, rootId, documentId }; }
};

struct BackupRootInfo {
    QString rootPath;
    QString rootId;
    bool initialized = false;
    bool writable = false;
    QString error;
};

struct BackupCatalog {
    QVector<BackupRootInfo> roots;
    QVector<BackupGroupInfo> groups;
    QStringList warnings;
};

struct BackupResult {
    bool ok = false;
    bool changed = false;
    bool skipped = false;
    qsizetype affectedSnapshots = 0;
    QString error;
    QStringList warnings;
    explicit operator bool() const { return ok; }
};

// Stateless storage operations. Call on the PDF worker, not the GUI thread.
// Settings own the registered roots and active root; listing never initializes
// a root. Only versioned, owned manifests and UUID payloads may be modified.
class BackupStore final {
public:
    static QString defaultRoot();
    static QString documentIdForPath(const QString& sourcePath);
    BackupCatalog list(const QStringList& rootPaths) const;
    // retainCount == 0 skips creation and preserves all previous snapshots.
    // Only a changed recovery state creates a new complete-PDF snapshot.
    BackupResult create(const QString& rootPath, const PdfDocument& document, int retainCount) const;
    // A caller implements current/multi/all selection with explicit group keys.
    BackupResult removeGroups(const QVector<BackupGroupKey>& groups) const;
    // Always restores to a separate file, never the group's original source.
    // Replacing another existing destination requires its confirmed digest.
    BackupResult restoreCopy(const BackupSnapshotKey& snapshot, const QString& targetPath,
                             const QByteArray& expectedTargetSha256 = {}) const;
    // Copies and verifies first, commits destination metadata, then removes only
    // matching owned source records/files. Failure preserves source material.
    BackupResult migrateRoot(const QString& sourceRootPath, const QString& targetRootPath) const;
};
} // namespace propernotes

Q_DECLARE_METATYPE(propernotes::BackupGroupKey)
Q_DECLARE_METATYPE(propernotes::BackupSnapshotKey)
Q_DECLARE_METATYPE(propernotes::BackupCatalog)
Q_DECLARE_METATYPE(propernotes::BackupResult)
