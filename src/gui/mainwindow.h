#pragma once
// LibreDrop main window: open a video or image, analyse it in the background, look at any frame with what was
// found in it, choose the drops that count, and see their angle, base width and volume over time.
#include <QFutureWatcher>
#include <QMainWindow>
#include <QSplitter>
#include <atomic>
#include <memory>

#include "core/report.h"
#include "core/video.h"

class FrameView;
class PlotWidget;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;
class QSpinBox;
class QTimer;
class QTreeWidget;
class QCheckBox;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    void openFile(const QString& path);
    void openExample();   // the built-in recording (water on glass)

protected:
    void closeEvent(QCloseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    void buildUi();
    void open();
    void analyse();
    void cancel();
    void analysisDone();
    void exportTable();
    void exportPlot();
    void saveCompact();
    void showFrame(int index);
    void frameReady(int generation, int index, const QImage& image, const std::optional<ld::Scene>& scene);
    void updateTable();
    void updatePlot();
    QList<int> chosenIds() const;
    ld::VideoOptions options() const;
    ld::Units units() const;
    void liquidChanged();
    void saveSettings() const;
    double frameTime(int index) const;
    int frameAt(double t) const;
    void setBusy(bool busy);

    FrameView* view_ = nullptr;
    PlotWidget* plot_ = nullptr;
    QSlider* slider_ = nullptr;
    QLabel* timeLabel_ = nullptr;
    QTreeWidget* table_ = nullptr;
    QSplitter* split_ = nullptr;   // frame | side panel
    void fitTableWidth();                 // widen the side panel so every table column shows
    QLabel* checks_ = nullptr;
    QWidget* plotBox_ = nullptr;
    QDoubleSpinBox* scale_ = nullptr;
    QComboBox* liquid_ = nullptr;
    QDoubleSpinBox* density_ = nullptr;
    QDoubleSpinBox* tension_ = nullptr;
    QSpinBox* step_ = nullptr;
    QCheckBox* sinceContact_ = nullptr;
    QPushButton* analyseButton_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QTimer* frameTimer_ = nullptr;

    QString path_;
    bool isImage_ = false;
    ld::VideoInfo info_;
    ld::VideoResult result_;
    bool haveResult_ = false;
    ld::Units resultUnits_;
    std::shared_ptr<std::atomic<bool>> cancel_;
    QFutureWatcher<ld::VideoResult> watcher_;
    QFutureWatcher<QString> compactWatcher_;   // error text, empty when done
    int frameGeneration_ = 0;
    bool frameBusy_ = false;
    int pendingFrame_ = -1;
};
