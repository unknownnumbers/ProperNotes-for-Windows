// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "InkData.h"

#include <QMainWindow>
#include <QMap>
#include <QByteArray>
#include <atomic>
#include <functional>
#include <memory>

class QAction;
class QCloseEvent;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QObject;
class QScrollArea;
class QSpinBox;
class QThread;
class QTimer;

namespace propernotes {
class InkCanvas;
struct DocumentSession;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    void openFile(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    void updateUi();
    void showError(const QString& message);
    void chooseOpenFile();
    void beginOpen(const QString& path);
    void resolveUnsaved(const std::function<void()>& continuation);
    void saveFile(bool saveAs, std::function<void()> afterSuccess = {});
    void performSave(const QString& path, const QByteArray& expectedTargetSha256,
                     const QMap<int, QVector<InkStroke>>& snapshot);
    void goToPage(int pageIndex);
    void requestRender();
    void changeZoom(qreal zoom, const QPointF& viewportAnchor);
    void fitWidth();
    void rememberCanvas();
    void finishFileOperation();

    InkCanvas* m_canvas = nullptr;
    QScrollArea* m_scroll = nullptr;
    QListWidget* m_pages = nullptr;
    QSpinBox* m_pageNumber = nullptr;
    QDoubleSpinBox* m_width = nullptr;
    QLabel* m_fileLabel = nullptr;
    QLabel* m_zoomLabel = nullptr;
    QLabel* m_pageLabel = nullptr;
    QLabel* m_documentBanner = nullptr;
    QAction* m_openAction = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_saveAsAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_penAction = nullptr;
    QAction* m_eraserAction = nullptr;
    QAction* m_panAction = nullptr;
    QAction* m_previousAction = nullptr;
    QAction* m_nextAction = nullptr;
    QAction* m_zoomInAction = nullptr;
    QAction* m_zoomOutAction = nullptr;
    QAction* m_fitAction = nullptr;
    QTimer* m_renderTimer = nullptr;
    QThread* m_workerThread = nullptr;
    QObject* m_workerContext = nullptr;
    std::shared_ptr<DocumentSession> m_session;
    std::shared_ptr<std::atomic<quint64>> m_renderEpoch;
    QMap<int, QVector<InkStroke>> m_pageStrokes;
    QString m_path;
    QString m_readOnlyReason;
    QColor m_color = QColor("#202833");
    int m_pageCount = 0;
    int m_currentPage = -1;
    int m_targetPage = -1;
    bool m_dirty = false;
    bool m_readOnly = false;
    bool m_fileBusy = false;
    bool m_rendering = false;
    bool m_updatingUi = false;
    bool m_allowClose = false;
    std::function<void()> m_afterSave;
};

} // namespace propernotes
