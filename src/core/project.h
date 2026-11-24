#pragma once
// A LibreDrop project: one local folder with everything for a study (copy it to back up or share).
//
//   <project>/project.json   samples, recordings, liquids, evaporation references, calibrations
//   <project>/recordings/    the videos (or compact copies)
//   <project>/results/       per-recording results (the video table, CSV): no re-analysis on reopening
//
// Paths in project.json are relative to the project folder. One user at a time (no locking).
#include <QString>
#include <QStringList>
#include <vector>

#include "drop.h"

namespace ld {

struct Sample {
    QString name;                                   // unique in the project
    QString material, batch, side, direction, treatment, notes;
    bool control = false;                           // a control/reference (glass, PTFE, a known separator)
};

struct Recording {
    QString file;                                   // relative to the project folder
    QString sample, liquid;
    QString date;                                   // yyyy-MM-dd (from the capture .json, else the file time)
    double volumeUl = kNaN;                         // dispensed, if noted
    QString notes;
    QList<int> excluded;                            // drop ids not to use
    bool glassReference = false;                    // used for the evaporation of its liquid that day
};

struct LiquidData {
    QString name;
    double density = kNaN;                          // kg/m³
    double tension = kNaN;                          // mN/m
    double viscosity = kNaN;                        // mPa·s
    double dispersive = kNaN, polar = kNaN;         // parts of the surface tension, mN/m
    double temperature = 25;                        // °C the values refer to
    QString source;                                 // reference(s); "uncertain" where it is
};

struct EvaporationRef {
    QString liquid, date;                           // date: yyyy-MM-dd
    double rate = kNaN;                             // µL/s (positive: loss)
    QString recording;                              // the glass recording it came from (or "entered")
};

struct Calibration {
    int frameWidth = 0;                             // camera mode
    double pxPerMm = kNaN;
    double needleMm = kNaN;                         // needle outer diameter, if measured
    QString date, method;                           // e.g. "10 mm sphere", "needle"
};

struct Project {
    QString dir;                                    // absolute path of the project folder
    QString name;
    std::vector<Sample> samples;
    std::vector<Recording> recordings;
    std::vector<LiquidData> liquids;
    std::vector<EvaporationRef> evaporation;
    std::vector<Calibration> calibrations;

    // A new project in dir (made if needed; refuses a folder that already has a project.json).
    static bool create(const QString& dir, const QString& name, Project* out, QString* error);
    static bool load(const QString& dir, Project* out, QString* error);
    bool save(QString* error) const;

    QString absolute(const QString& relative) const;
    QString relative(const QString& absolute) const;
    QString resultPath(const Recording& r) const;   // results/<file name>.csv

    Sample* sample(const QString& name);
    const LiquidData* liquid(const QString& name) const;
    // the evaporation for this liquid on that date; else the latest earlier one; else none (NaN)
    EvaporationRef evaporationFor(const QString& liquid, const QString& date) const;
    // the newest calibration for this frame width (NaN if none)
    Calibration calibrationFor(int frameWidth) const;

    // Adds a recording (copied into recordings/ if it is outside the project), with sample, liquid, date and
    // volume from the capture tool's .json next to it when there is one; the sample is created if new.
    bool addRecording(const QString& path, bool copy, Recording* added, QString* error);
};

// Reads the capture tool's <video>.json: sample fields, date. False if there is none.
bool readCaptureJson(const QString& videoPath, Recording* r, Sample* s);

}  // namespace ld
