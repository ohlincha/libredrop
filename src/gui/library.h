#pragma once
// The project library (a dock beside the frame view): samples and their recordings, from project.json.
// Double-click a recording to open it; select samples (or recordings) and Compare; Edit… for a sample's
// fields or a recording's sample, liquid, date and volume.
#include <QWidget>
#include <optional>

#include "core/project.h"

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

class LibraryPanel : public QWidget {
    Q_OBJECT
public:
    explicit LibraryPanel(QWidget* parent = nullptr);
    bool hasProject() const { return project_.has_value(); }
    ld::Project& project() { return *project_; }
    void setProject(std::optional<ld::Project> p);
    bool save();                                     // after a change; reports errors
    ld::Recording* recordingFor(const QString& absolutePath);
    void refresh();
    void addRecordings(const QStringList& paths);    // asks whether to copy them into the project

signals:
    void openRecording(const QString& absolutePath);
    void compare(const QStringList& absolutePaths, const QStringList& sampleNames);

private:
    void edit(QTreeWidgetItem* it);
    void compareSelected();

    std::optional<ld::Project> project_;
    QLabel* title_ = nullptr;
    QTreeWidget* tree_ = nullptr;
};
