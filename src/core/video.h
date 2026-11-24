#pragma once
// Drops over time in a video.
//
// Frames are read through ffmpeg (on PATH, or the program named by LIBREDROP_FFMPEG), as grey levels, with the
// timestamps from the file (ffprobe), so a camera that drops frames still gives true times. A list of image
// files works too (times from --fps).
//
// Pass 1 finds the surface in up to `surfaceSamples` frames spread over the video and takes the median: stage and
// camera do not move, so one fixed surface gives a steadier baseline than finding it in every frame.
// Pass 2 measures every `step`-th frame with that surface and follows each drop by its position and width. The
// slide may be pushed sideways to free a drop from the needle: then all drops move together, and the common shift
// is found before the drops are matched.
#include <QString>
#include <QStringList>
#include <functional>
#include <limits>
#include <vector>

#include "scene.h"

namespace ld {

struct VideoInfo {
    int w = 0, h = 0;
    std::vector<double> times;     // seconds, one per frame
    QString error;
};

VideoInfo probeVideo(const QString& path);
// Calls f(frameIndex, image) for frames first, first+step, … up to last (inclusive); f returns false to stop.
bool readFrames(const QString& path, const VideoInfo& info, int step, int first, int last,
                const std::function<bool(int, const GrayImage&)>& f, QString* error);

struct TrackPoint {
    int frame = 0;
    double t = 0;                  // seconds since the start of the video
    SceneDrop drop;                // x0/x1/xc in image coordinates; outline left out
};

struct DropTrack {
    int id = 0;
    std::vector<TrackPoint> points;
    double tContact() const { return points.empty() ? 0 : points.front().t; }
    // First frame after the needle has left the drop (the start of the free drop), or -1.
    int released() const;
};

struct VideoOptions {
    SceneOptions scene;
    int step = 1;
    double start = 0, end = std::numeric_limits<double>::infinity();   // seconds
    int surfaceSamples = 40;
    int minPoints = 3;             // shorter tracks are noise
    double minDuration = 0.5;      // s
    double fps = 0;                // for image lists
};

struct VideoResult {
    bool ok = false;
    QString error;
    int frames = 0;                // measured
    Baseline surface;
    int surfaceFrames = 0;         // frames the surface was found in (pass 1)
    std::vector<DropTrack> tracks;
};

using Progress = std::function<void(int done, int total)>;
VideoResult analyzeVideo(const QString& path, const VideoOptions& opt, const Progress& progress = {});
VideoResult analyzeImages(const QStringList& files, const VideoOptions& opt, const Progress& progress = {});

}  // namespace ld
