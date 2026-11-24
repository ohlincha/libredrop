#pragma once
// Tables, summaries and plots of drops over time (video results).
#include <QImage>
#include <QString>
#include <vector>

#include "video.h"

namespace ld {

// Units: mm, mm² and µL when the scale is known (pxPerMm > 0), else px.
struct Units {
    double pxPerMm = 0;
    double len(double px) const { return pxPerMm > 0 ? px / pxPerMm : px; }
    double vol(double px3) const { return pxPerMm > 0 ? px3 / (pxPerMm * pxPerMm * pxPerMm) : px3; }
    QString lenUnit() const { return pxPerMm > 0 ? QStringLiteral("mm") : QStringLiteral("px"); }
    QString volUnit() const { return pxPerMm > 0 ? QStringLiteral("uL") : QStringLiteral("px3"); }
};

// Measured and without errors (e.g. not cut off by the image edge).
bool usable(const DropAnalysis& a);

// The angle a track point is plotted and summarised with: Young–Laplace mean, else tangent, else circle.
double angleOf(const DropAnalysis& a);

// One row per drop per frame. Columns are listed in the header line.
QString tracksCsv(const VideoResult& r, const Units& u);

struct TrackSummary {
    int id = 0;
    double x = 0;                       // base centre at the first frame
    double tContact = 0, tRelease = -1, tEnd = 0;
    double angleRelease = kNaN, angleEnd = kNaN;     // median over a few frames
    double baseRelease = kNaN, baseEnd = kNaN;
    double volumeRelease = kNaN, volumeEnd = kNaN;
    double angleRate = kNaN, baseRate = kNaN, volumeRate = kNaN;   // per second, line fit after release
    int points = 0, failed = 0;
};
TrackSummary summarize(const DropTrack& t, const Units& u);

// Angle, base width and volume against time since contact, one colour per drop.
QImage plotTracks(const VideoResult& r, const Units& u, int width = 1200, int height = 900);

}  // namespace ld
