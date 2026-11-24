#pragma once
// Drops over time in a video.
//
// Motion-JPEG recordings (the camera's own format) are read directly (mjpeg.h). Other formats are read through
// ffmpeg (next to the program, on PATH, or LIBREDROP_FFMPEG). Either way frames come as grey levels with the
// timestamps from the file, so a camera that drops frames still gives true times. A list of image
// files works too (times from --fps).
//
// Pass 1 finds the surface in up to `surfaceSamples` frames spread over the video and takes the median: stage and
// camera do not move, so one fixed surface gives a steadier baseline than finding it in every frame.
// Pass 2 measures every `step`-th frame with that surface and follows each drop by its position and width. The
// slide may be pushed sideways to free a drop from the needle: then all drops move together, and the common shift
// is found before the drops are matched.
#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>
#include <limits>
#include <vector>

#include "scene.h"

namespace ld {

struct VideoInfo {
    int w = 0, h = 0;
    std::vector<double> times;     // seconds, one per frame
    bool native = false;           // Motion-JPEG, read without ffmpeg
    QString format;
    QString error;
};

VideoInfo probeVideo(const QString& path);
// Calls f(frameIndex, image) for frames first, first+step, … up to last (inclusive); f returns false to stop.
bool readFrames(const QString& path, const VideoInfo& info, int step, int first, int last,
                const std::function<bool(int, const GrayImage&)>& f, QString* error);
// One frame (seeks to its time).
bool readFrameAt(const QString& path, const VideoInfo& info, int index, GrayImage* out, QString* error);

struct TrackPoint {
    int frame = 0;
    double t = 0;                  // seconds since the start of the video
    SceneDrop drop;                // x0/x1/xc in image coordinates; outline left out
};

struct DropTrack {
    int id = 0;
    std::vector<TrackPoint> points;
    double tContact() const { return points.empty() ? 0 : points.front().t; }
};

struct VideoOptions {
    SceneOptions scene;
    int step = 1;
    double start = 0, end = std::numeric_limits<double>::infinity();   // seconds
    int surfaceSamples = 40;
    int minPoints = 3;             // shorter tracks are noise
    double minDuration = 0.5;      // s
    double fps = 0;                // for image lists
    const std::atomic<bool>* cancel = nullptr;   // set to stop early (the result then has error "cancelled")
};

struct VideoResult {
    bool ok = false;
    QString error;
    int frames = 0;                // measured
    Baseline surface;
    int surfaceFrames = 0;         // frames the surface was found in (pass 1)
    std::vector<DropTrack> tracks;
    VideoInfo info;                // the video's size and frame times (empty for image lists)
};

using Progress = std::function<void(int done, int total)>;

// Compact copy of a recording: rows y0..y1 only (the band with the surface, the drops and their reflection),
// grey, JPEG at the given quality, Matroska Motion-JPEG with the original frame times. Typically 5–10× smaller than
// the camera's file and still read without ffmpeg.
struct CompactOptions {
    int y0 = 0, y1 = -1;           // rows kept (inclusive); y1 < 0: to the bottom
    int quality = 85;              // JPEG quality 1–100
    int step = 1;                  // keep every step-th frame
    double start = 0, end = std::numeric_limits<double>::infinity();
    const std::atomic<bool>* cancel = nullptr;
};
bool writeCompact(const QString& in, const QString& out, const CompactOptions& opt, const Progress& progress, QString* error);
// Rows worth keeping, from an analysis: the drops' tallest height above the surface (with room for the needle
// above) and their reflection below.
void suggestCrop(const VideoResult& r, int* y0, int* y1);
VideoResult analyzeVideo(const QString& path, const VideoOptions& opt, const Progress& progress = {});
VideoResult analyzeImages(const QStringList& files, const VideoOptions& opt, const Progress& progress = {});

}  // namespace ld
