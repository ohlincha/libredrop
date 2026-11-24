#pragma once
// Compare samples: several recordings analysed with the same settings, each drop's soaking values, and per sample
// (recordings with the same sample name) the mean ± standard deviation. Sample names default to the file name
// without the camera tool's date/time and repeat number; tick/untick drops; export a CSV.
#include <QDialog>
#include <QFutureWatcher>
#include <functional>

#include "core/report.h"
#include "core/video.h"

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

struct CompareSettings {
    std::function<ld::VideoOptions(int frameWidth)> options;   // scale for that camera mode, liquid, step
    std::function<ld::SoakOptions()> soak;                      // times, evaporation (current liquid)
    std::function<QString()> describe;                         // one line: liquid, evaporation, scale
};

class CompareDialog : public QDialog {
    Q_OBJECT
public:
    CompareDialog(CompareSettings settings, QWidget* parent = nullptr);
    void addFiles(const QStringList& paths);

private:
    struct Drop {
        int id = 0;
        ld::Soaking k;
        ld::TrackSummary s;
    };
    struct File {
        QString path, sample, error, lenUnit;
        bool done = false;
        std::vector<Drop> drops;
    };
    void analyseAll();
    void rebuild();          // tree from files_
    void updateSummary();    // per sample, from the ticked drops
    void exportCsv();
    QString defaultSample(const QString& path) const;

    CompareSettings settings_;
    std::vector<File> files_;
    QTreeWidget* tree_ = nullptr;
    QTreeWidget* summary_ = nullptr;
    QLabel* info_ = nullptr;
    QPushButton* analyseButton_ = nullptr;
    QFutureWatcher<void> watcher_;
    std::vector<double> times_;
    struct SampleRow {
        QString name;
        int recordings = 0, drops = 0, absorbed = 0;
        std::vector<double> mean, sd;   // per column: angles at the times, sorptivity, absorbed after
    };
    std::vector<SampleRow> rows_;
};
