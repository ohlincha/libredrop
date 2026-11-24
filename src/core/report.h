#pragma once
// Tables, summaries and plots of drops over time (video results).
#include <QColor>
#include <QImage>
#include <QRect>
#include <QList>
#include <QString>
#include <vector>

#include "video.h"

class QPainter;

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
QString tracksCsv(const VideoResult& r, const Units& u, const QList<int>& ids = {});   // ids: only these drops

// One drop over time in output units (from a video result, or read back from a table): what summaries and plots
// are made from, so a drop can be picked out after the analysis without measuring again.
struct DropSeries {
    int id = 0;
    double x = kNaN;                    // base centre at the first frame (output length unit)
    std::vector<double> t;              // s since the start of the video
    std::vector<double> angle, base, volume;   // NaN where not usable
    std::vector<char> needle;           // needle still in the drop
    double tContact() const { return t.empty() ? 0 : t.front(); }
    int released() const;               // first index after the needle has left the drop, or -1
};
struct SeriesSet {
    std::vector<DropSeries> drops;
    QString lenUnit = QStringLiteral("px"), volUnit = QStringLiteral("px3");
};
SeriesSet seriesOf(const VideoResult& r, const Units& u);
// Reads a table written by tracksCsv; false (and *error) if it is not one.
bool readTracksCsv(const QString& text, SeriesSet* out, QString* error);
// Keeps the drops whose ids are listed (all if the list is empty).
void selectDrops(SeriesSet* s, const QList<int>& ids);

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
TrackSummary summarize(const DropSeries& d);

// Angle, base width and volume against time, one colour per drop (the same colour for a drop id everywhere).
QColor dropColor(int id);
struct PlotStyle {
    bool sinceContact = true;          // x = time since each drop's contact; else the video's time
    double cursor = kNaN;              // a dashed line at this x
    double tMin = kNaN, tMax = kNaN;   // x range (else from the data)
    int highlight = -1;                // drawn thicker
    bool legend = true;
    int fontPx = 14;
    QColor background = Qt::white, text = Qt::black, grid = QColor(0xeeeeee);
};
struct PlotGeometry {
    QRect boxes[3];                    // angle, base, volume panels
    double t0 = 0, t1 = 1;             // x range of the panels
};
PlotGeometry drawSeries(QPainter& p, const QRect& area, const SeriesSet& s, const PlotStyle& style);
QImage plotSeries(const SeriesSet& s, int width = 1200, int height = 900);

}  // namespace ld
