#pragma once
// LibreDrop main window: open a video or image, analyse it in the background, look at any frame with what was
// found in it, choose the drops that count, and see their angle, base width and volume over time.
#include <QFutureWatcher>
#include <QMainWindow>
#include <QSplitter>
#include <QElapsedTimer>
#include <QDateTime>
#include <atomic>
#include <memory>

#include "core/project.h"
#include "core/thermo.h"
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
    bool eventFilter(QObject* o, QEvent* e) override;

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
    QTreeWidget* soakTable_ = nullptr;
    class CompareDialog* compare_ = nullptr;
    class MeasureWindow* measure_ = nullptr;
    class LibraryPanel* library_ = nullptr;
    // room conditions from an attached USB thermometer (status bar; logged into the open project)
    QLabel* conditions_ = nullptr;
    QTimer* thermoTimer_ = nullptr;
    QFutureWatcher<std::vector<ld::ThermoReading>> thermoWatcher_;
    QDateTime lastConditionsLog_;
    void readConditions();
    void conditionsRead();
    class QDockWidget* libraryDock_ = nullptr;
    void newProject();
    void applyProjectContext();          // a project recording was opened: its liquid, evaporation, scale
    ld::Recording* currentRecording();   // the open file's entry in the project, if it is one     // soaking: angle at fixed times, sorptivity, absorption
    class QTabWidget* tabs_ = nullptr;
    class QLineEdit* soakTimes_ = nullptr;
    QDoubleSpinBox* evaporation_ = nullptr;
    ld::SoakOptions soakOptions() const;
    QLabel* soakHint_ = nullptr;           // what to do first (glass reference) / which correction is used
    class QPushButton* glassRef_ = nullptr;
    QString evaporationSource_;            // where the current liquid's evaporation value came from
    void updateSoakHint();
    void useAsGlassReference();
    void loadEvaporation();                // the current liquid's stored value
    QSplitter* split_ = nullptr;   // frame | side panel
    void fitTableWidth();
    bool panelDragged_ = false;     // the user set the divider: keep it
public:
    void openCompare(const QStringList& files, const QStringList& samples = {});   // Compare samples (with these added)
    bool openProject(const QString& dir);
    void openMeasure();   // the live camera and recording
private:                 // widen the side panel so every table column shows
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
    // playback: frames follow the clock (frames that can't be shown in time are skipped)
    class QToolButton* playButton_ = nullptr;
    class QComboBox* speed_ = nullptr;
    QTimer* playTimer_ = nullptr;
    QElapsedTimer playClock_;
    double playFrom_ = 0;
    void setPlaying(bool on);

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
