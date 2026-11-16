// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QMetaType>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace propernotes {

// Independent of widgets and PDF handles. Anchors use canonical PDF page
// coordinates; viewportAnchor is a 0..1 fraction of the reading viewport.
struct WorkspaceViewState {
    int page = -1;
    QPointF pdfAnchor;
    QPointF viewportAnchor = QPointF(0.5, 0.5);
    qreal zoom = 1;
    QString zoomMode = QStringLiteral("fit-width"); // free / fit-width / fit-page / fit-height
    QString layout = QStringLiteral("single-continuous");
    bool valid = false;
};

struct WorkspaceRecentEntry {
    QString path;
    QDateTime openedUtc;
};

struct WorkspaceReadingEntry {
    QString path;
    qint64 fileSize = -1; // Unknown, or the size observed on a successful open/save.
    QDateTime modifiedUtc; // Invalid means unknown; this is a hint, not file identity.
    WorkspaceViewState view;
    bool hadUnsavedChanges = false; // Warning only, never recovery of document contents.
};

struct WorkspaceValues {
    // Recent order is newest first. Removing it never removes reading/session data.
    QVector<WorkspaceRecentEntry> recent;
    // Oldest first. rememberReading refreshes a path to the end and, when
    // full, evicts the oldest entry outside the current session/new path.
    QVector<WorkspaceReadingEntry> readingStates;
    QStringList sessionPaths; // Tab order, including missing/unloaded paths.
    QString activePath;
    bool restoreEnabled = true;
};

using ReadingState = WorkspaceViewState;
using RecentEntry = WorkspaceRecentEntry;
using ReadingEntry = WorkspaceReadingEntry;

// A single file worker owns each instance. This store never opens, changes or
// deletes a listed PDF/backup. A failed load is write-protected until a later
// successful explicit reload. Save failures leave values/baseline unchanged.
class WorkspaceStore final {
public:
    static constexpr int MaximumRecent = 100;
    static constexpr int MaximumReadingStates = 256;
    static constexpr int MaximumSessionPaths = 32;
    static constexpr qint64 MaximumBytes = 4LL * 1024 * 1024;

    explicit WorkspaceStore(QString applicationDirectory);
    bool load();
    bool save(const WorkspaceValues& values);
    const WorkspaceValues& values() const { return m_values; }
    bool canWrite() const { return m_loaded && m_writable; }
    QString lastError() const { return m_error; }
    QString filePath() const;

    // Lexical absolute path validation only. Missing files stay registered.
    // The host supplies a canonical PdfDocument::currentPath after success.
    static QString normalizedPath(const QString& path);
    static QString normalizePath(const QString& path) { return normalizedPath(path); }
    static QString pathKey(const QString& path); // Windows case-folded identity.
    static bool validate(const WorkspaceValues& values, QString* error = nullptr);
    static bool validateView(const WorkspaceViewState& view, QString* error = nullptr);

    // Pure value helpers; persistence remains an explicit save() transaction.
    // recordOpened also handles a successful Save As (new path). Session/tab
    // replacement is separate, so the old source's reading entry is retained.
    static bool recordOpened(WorkspaceValues& values, const QString& path,
                             const QDateTime& openedUtc = QDateTime::currentDateTimeUtc(),
                             QString* error = nullptr);
    static bool rememberReading(WorkspaceValues& values, const WorkspaceReadingEntry& entry,
                                QString* error = nullptr);
    static bool setSession(WorkspaceValues& values, const QStringList& paths,
                           const QString& activePath, QString* error = nullptr);
    static bool removeRecent(WorkspaceValues& values, const QString& path,
                             QString* error = nullptr);
    static void clearRecent(WorkspaceValues& values) { values.recent.clear(); }

private:
    bool readCurrent(bool* exists, QByteArray* bytes, QString* error) const;
    bool baselineUnchanged(QString* error) const;
    QString m_applicationDirectory;
    WorkspaceValues m_values;
    QByteArray m_baselineHash;
    bool m_baselineExists = false;
    bool m_loaded = false;
    bool m_writable = false;
    QString m_error;
};

} // namespace propernotes

Q_DECLARE_METATYPE(propernotes::WorkspaceViewState)
Q_DECLARE_METATYPE(propernotes::WorkspaceRecentEntry)
Q_DECLARE_METATYPE(propernotes::WorkspaceReadingEntry)
Q_DECLARE_METATYPE(propernotes::WorkspaceValues)
