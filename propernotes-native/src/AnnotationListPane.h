// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "InkData.h"
#include <QMap>
#include <QTimer>
#include <QWidget>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QShowEvent;

namespace propernotes {

class AnnotationListPane final : public QWidget {
    Q_OBJECT
public:
    explicit AnnotationListPane(QWidget* parent = nullptr);
    void setDocument(const QMap<int, PageAnnotations>& annotations, int currentPage);
    void updatePageAnnotations(int page, const PageAnnotations& annotations);
    void setCurrentPage(int page);
    void clearDocument();

signals:
    void annotationActivated(int page, const QUuid& id);

protected:
    void showEvent(QShowEvent* event) override;

private:
    enum class Kind { Pen, Highlighter, TextBox, Highlight, Underline, Squiggly, StrikeOut,
                      Line, Arrow, Rectangle, Ellipse };
    struct Entry {
        int page = -1;
        QUuid id;
        Kind kind = Kind::Pen;
        QString snippet;
    };
    static QString kindName(Kind kind);
    static QVector<Entry> pageEntries(int page, const PageAnnotations& annotations);
    void scheduleRefresh();
    void rebuildRows(bool locateSelection);
    void activateRow(int row);

    static constexpr qsizetype EntriesPerGroup = 100;
    QMap<int, QVector<Entry>> m_entriesByPage;
    QVector<Entry> m_filtered;
    QTimer m_refreshTimer;
    QComboBox* m_scope = nullptr;
    QComboBox* m_kind = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_count = nullptr;
    QLabel* m_empty = nullptr;
    QPushButton* m_previous = nullptr;
    QPushButton* m_next = nullptr;
    qsizetype m_offset = 0;
    int m_currentPage = -1;
    int m_selectedPage = -1;
    QUuid m_selectedId;
    bool m_hasDocument = false;
    bool m_dirty = false;
};

} // namespace propernotes
