#include "camera.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFile>
#include <QThread>

#include <algorithm>
#include <cmath>

#include "mjpeg.h"
#include "simulate.h"

#ifdef __linux__
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#endif

namespace ld {

namespace {

const char* const kDemo = ":/examples/water_on_glass.mkv";

// Replays a Motion-JPEG recording at its own frame times, looping.
class SimCamera : public Camera {
public:
    bool load(const QString& file, QString* error) {
        file_ = file;
        if (!reader_.open(file, error)) {
            if (error && error->isEmpty()) *error = QStringLiteral("%1 is not a Motion-JPEG recording").arg(file);
            return false;
        }
        if (reader_.frames() < 2) {
            if (error) *error = QStringLiteral("%1 has too few frames").arg(file);
            return false;
        }
        const auto& t = reader_.times();
        fps_ = (reader_.frames() - 1) / std::max(1e-6, t.back() - t.front());
        return true;
    }
    QString name() const override {
        return file_ == QLatin1String(kDemo) ? QStringLiteral("Demo: example recording (water on glass)")
                                             : QStringLiteral("Simulated: %1").arg(QFileInfo(file_).fileName());
    }
    std::vector<CameraMode> modes() const override { return {{reader_.width(), reader_.height(), fps_}}; }
    CameraMode mode() const override { return modes().front(); }
    bool start(const CameraMode&, QString*) override {
        clock_.start();
        next_ = 0;
        loopStart_ = 0;
        seq_ = 0;
        return true;
    }
    bool grab(CameraFrame* out, QString* error) override {
        const auto& t = reader_.times();
        if (next_ >= reader_.frames()) {   // loop: continue the clock after a frame's gap
            loopStart_ += t.back() - t.front() + 1.0 / fps_;
            next_ = 0;
        }
        const double due = loopStart_ + t[next_] - t.front();
        const double now = clock_.nsecsElapsed() * 1e-9;
        if (due > now) QThread::usleep(static_cast<unsigned long>((due - now) * 1e6));
        out->jpeg = reader_.jpeg(next_);
        out->t = due;
        out->sequence = seq_++;
        ++next_;
        if (out->jpeg.isEmpty()) {
            if (error) *error = QStringLiteral("cannot read frame %1 of %2").arg(next_ - 1).arg(file_);
            return false;
        }
        return true;
    }
    void stop() override {}

private:
    QString file_;
    mutable MjpegReader reader_;
    double fps_ = 30;
    QElapsedTimer clock_;
    int next_ = 0, seq_ = 0;
    double loopStart_ = 0;
};

// The simulator (core/simulate): a scenario drawn frame by frame and looped. Time is the scenario's: frames that
// take longer to draw than their interval make the live view slower than real time, never the recorded times wrong.
class SynthCamera : public Camera {
public:
    bool load(const QString& name, QString* error) {
        if (!simPreset(name, &spec_)) {
            if (error) *error = QStringLiteral("no simulator scenario %1").arg(name);
            return false;
        }
        return true;
    }
    QString name() const override { return QStringLiteral("Simulator: %1").arg(spec_.title); }
    std::vector<CameraMode> modes() const override { return {{spec_.width, spec_.height, spec_.fps}}; }
    CameraMode mode() const override { return modes().front(); }
    bool start(const CameraMode&, QString*) override {
        clock_.start();
        frame_ = 0;
        return true;
    }
    bool grab(CameraFrame* out, QString*) override {
        const double t = frame_ / spec_.fps;
        const double due = t, now = clock_.nsecsElapsed() * 1e-9;
        if (due > now) QThread::usleep(static_cast<unsigned long>((due - now) * 1e6));
        out->jpeg = encodeJpeg(simFrame(spec_, std::fmod(t, spec_.seconds)), 90);
        out->t = t;
        out->sequence = frame_++;
        return true;
    }
    void stop() override {}

private:
    SimSpec spec_;
    QElapsedTimer clock_;
    int frame_ = 0;
};

#ifdef __linux__
int xioctl(int fd, unsigned long req, void* arg) {
    int r;
    do r = ioctl(fd, req, arg);
    while (r == -1 && errno == EINTR);
    return r;
}

struct NamedControl {
    const char* name;
    unsigned id;
};
const NamedControl kControls[] = {
    {"brightness", V4L2_CID_BRIGHTNESS},
    {"contrast", V4L2_CID_CONTRAST},
    {"saturation", V4L2_CID_SATURATION},
    {"hue", V4L2_CID_HUE},
    {"white_balance_automatic", V4L2_CID_AUTO_WHITE_BALANCE},
    {"gamma", V4L2_CID_GAMMA},
    {"gain", V4L2_CID_GAIN},
    {"power_line_frequency", V4L2_CID_POWER_LINE_FREQUENCY},
    {"white_balance_temperature", V4L2_CID_WHITE_BALANCE_TEMPERATURE},
    {"sharpness", V4L2_CID_SHARPNESS},
    {"backlight_compensation", V4L2_CID_BACKLIGHT_COMPENSATION},
    {"auto_exposure", V4L2_CID_EXPOSURE_AUTO},
    {"exposure_time_absolute", V4L2_CID_EXPOSURE_ABSOLUTE},
    {"exposure_dynamic_framerate", V4L2_CID_EXPOSURE_AUTO_PRIORITY},
};

bool hasMjpeg(int fd) {
    v4l2_fmtdesc d{};
    d.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (d.index = 0; xioctl(fd, VIDIOC_ENUM_FMT, &d) == 0; ++d.index)
        if (d.pixelformat == V4L2_PIX_FMT_MJPEG) return true;
    return false;
}

class V4l2Camera : public Camera {
public:
    ~V4l2Camera() override {
        stop();
        if (fd_ >= 0) ::close(fd_);
    }
    bool openDevice(const QString& dev, QString* error) {
        dev_ = dev;
        fd_ = ::open(QFile::encodeName(dev).constData(), O_RDWR | O_NONBLOCK);
        if (fd_ < 0) {
            *error = QStringLiteral("cannot open %1: %2").arg(dev, QString::fromLocal8Bit(strerror(errno)));
            return false;
        }
        v4l2_capability cap{};
        if (xioctl(fd_, VIDIOC_QUERYCAP, &cap) < 0 || !((cap.capabilities & V4L2_CAP_DEVICE_CAPS ? cap.device_caps : cap.capabilities) & V4L2_CAP_VIDEO_CAPTURE)) {
            *error = QStringLiteral("%1 is not a video capture device").arg(dev);
            return false;
        }
        card_ = QString::fromUtf8(reinterpret_cast<const char*>(cap.card)).trimmed();
        if (const int c = card_.indexOf(QStringLiteral(": ")); c > 0 && card_.mid(c + 2).startsWith(card_.left(c)))
            card_ = card_.mid(c + 2);   // "HD USB Camera: HD USB Camera"
        if (!hasMjpeg(fd_)) {
            *error = QStringLiteral("%1 (%2) does not send Motion-JPEG").arg(card_, dev);
            return false;
        }
        return true;
    }
    QString name() const override { return QStringLiteral("%1 (%2)").arg(card_, dev_); }
    std::vector<CameraMode> modes() const override {
        std::vector<CameraMode> out;
        v4l2_frmsizeenum fs{};
        fs.pixel_format = V4L2_PIX_FMT_MJPEG;
        for (fs.index = 0; xioctl(fd_, VIDIOC_ENUM_FRAMESIZES, &fs) == 0; ++fs.index) {
            if (fs.type != V4L2_FRMSIZE_TYPE_DISCRETE) break;
            CameraMode m{int(fs.discrete.width), int(fs.discrete.height), 0};
            v4l2_frmivalenum fi{};
            fi.pixel_format = V4L2_PIX_FMT_MJPEG;
            fi.width = fs.discrete.width, fi.height = fs.discrete.height;
            for (fi.index = 0; xioctl(fd_, VIDIOC_ENUM_FRAMEINTERVALS, &fi) == 0; ++fi.index) {
                if (fi.type != V4L2_FRMIVAL_TYPE_DISCRETE) {
                    m.fps = std::max(m.fps, double(fi.stepwise.min.denominator) / std::max(1u, fi.stepwise.min.numerator));
                    break;
                }
                m.fps = std::max(m.fps, double(fi.discrete.denominator) / std::max(1u, fi.discrete.numerator));
            }
            out.push_back(m);
        }
        std::sort(out.begin(), out.end(), [](const CameraMode& a, const CameraMode& b) { return a.w * a.h > b.w * b.h; });
        return out;
    }
    CameraMode mode() const override { return mode_; }
    bool start(const CameraMode& m, QString* error) override {
        stop();
        v4l2_format f{};
        f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        f.fmt.pix.width = m.w, f.fmt.pix.height = m.h;
        f.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
        f.fmt.pix.field = V4L2_FIELD_ANY;
        if (xioctl(fd_, VIDIOC_S_FMT, &f) < 0) return fail(error, "cannot set the format");
        mode_ = {int(f.fmt.pix.width), int(f.fmt.pix.height), m.fps};
        if (m.fps > 0) {
            v4l2_streamparm p{};
            p.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            p.parm.capture.timeperframe.numerator = 1000;
            p.parm.capture.timeperframe.denominator = unsigned(std::lround(m.fps * 1000));
            if (xioctl(fd_, VIDIOC_S_PARM, &p) == 0 && p.parm.capture.timeperframe.numerator)
                mode_.fps = double(p.parm.capture.timeperframe.denominator) / p.parm.capture.timeperframe.numerator;
        }
        v4l2_requestbuffers rb{};
        rb.count = 8;   // room for a stall of a few frames at 120 fps
        rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        rb.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd_, VIDIOC_REQBUFS, &rb) < 0 || rb.count < 2) return fail(error, "cannot get capture buffers");
        for (unsigned i = 0; i < rb.count; ++i) {
            v4l2_buffer b{};
            b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            b.memory = V4L2_MEMORY_MMAP;
            b.index = i;
            if (xioctl(fd_, VIDIOC_QUERYBUF, &b) < 0) return fail(error, "cannot query a buffer");
            void* p = mmap(nullptr, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, b.m.offset);
            if (p == MAP_FAILED) return fail(error, "cannot map a buffer");
            bufs_.push_back({p, b.length});
            if (xioctl(fd_, VIDIOC_QBUF, &b) < 0) return fail(error, "cannot queue a buffer");
        }
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (xioctl(fd_, VIDIOC_STREAMON, &type) < 0) return fail(error, "cannot start streaming");
        streaming_ = true;
        return true;
    }
    bool grab(CameraFrame* out, QString* error) override {
        if (!streaming_) return fail(error, "not started");
        pollfd p{fd_, POLLIN, 0};
        int r;
        do r = poll(&p, 1, 2000);
        while (r < 0 && errno == EINTR);
        if (r == 0) {
            if (error) *error = QStringLiteral("no frame from the camera for 2 s (unplugged?)");
            return false;
        }
        if (r < 0) return fail(error, "poll");
        v4l2_buffer b{};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd_, VIDIOC_DQBUF, &b) < 0) {
            if (errno == EAGAIN) return grab(out, error);
            return fail(error, "cannot take a frame");
        }
        out->jpeg = QByteArray(static_cast<const char*>(bufs_[b.index].first), int(b.bytesused));
        out->t = b.timestamp.tv_sec + b.timestamp.tv_usec * 1e-6;
        out->sequence = int(b.sequence);
        const bool bad = b.flags & V4L2_BUF_FLAG_ERROR;
        xioctl(fd_, VIDIOC_QBUF, &b);
        if (bad || out->jpeg.size() < 4) return grab(out, error);   // a damaged frame: skip it
        return true;
    }
    void stop() override {
        if (streaming_) {
            v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            xioctl(fd_, VIDIOC_STREAMOFF, &type);
            streaming_ = false;
        }
        for (auto& b : bufs_) munmap(b.first, b.second);
        bufs_.clear();
        if (fd_ >= 0) {
            v4l2_requestbuffers rb{};
            rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            rb.memory = V4L2_MEMORY_MMAP;
            xioctl(fd_, VIDIOC_REQBUFS, &rb);
        }
    }
    std::vector<CameraControl> controls() const override {
        std::vector<CameraControl> out;
        for (const NamedControl& c : kControls) {
            v4l2_queryctrl q{};
            q.id = c.id;
            if (xioctl(fd_, VIDIOC_QUERYCTRL, &q) < 0 || (q.flags & V4L2_CTRL_FLAG_DISABLED)) continue;
            v4l2_control v{};
            v.id = c.id;
            if (xioctl(fd_, VIDIOC_G_CTRL, &v) < 0) continue;
            out.push_back({QString::fromLatin1(c.name), q.minimum, q.maximum, v.value, q.default_value});
        }
        return out;
    }
    bool setControl(const QString& name, int value) override {
        for (const NamedControl& c : kControls) {
            if (name != QLatin1String(c.name)) continue;
            v4l2_control v{};
            v.id = c.id;
            v.value = value;
            return xioctl(fd_, VIDIOC_S_CTRL, &v) == 0;
        }
        return false;
    }

private:
    bool fail(QString* error, const char* what) {
        if (error) *error = QStringLiteral("%1: %2 (%3)").arg(dev_, QLatin1String(what), QString::fromLocal8Bit(strerror(errno)));
        return false;
    }
    QString dev_, card_;
    int fd_ = -1;
    bool streaming_ = false;
    std::vector<std::pair<void*, std::size_t>> bufs_;
    CameraMode mode_;
};
#endif

}  // namespace

QString Camera::demoId() { return QStringLiteral("sim:") + QLatin1String(kDemo); }

std::vector<CameraInfo> Camera::list(bool withDemo) {
    std::vector<CameraInfo> out;
#ifdef __linux__
    QDir d(QStringLiteral("/dev"));
    QStringList devs = d.entryList({QStringLiteral("video*")}, QDir::System);
    std::sort(devs.begin(), devs.end(), [](const QString& a, const QString& b) { return a.mid(5).toInt() < b.mid(5).toInt(); });
    for (const QString& n : devs) {
        const QString dev = QStringLiteral("/dev/") + n;
        V4l2Camera c;
        QString err;
        if (c.openDevice(dev, &err)) out.push_back({dev, c.name()});
    }
#endif
    if (withDemo)
        for (const SimSpec& p : simPresets()) out.push_back({QStringLiteral("synth:") + p.name, QStringLiteral("Simulator: ") + p.title});
    const QString sim = qEnvironmentVariable("LIBREDROP_SIM_CAMERA");
    if (!sim.isEmpty()) out.push_back({QStringLiteral("sim:") + sim, QStringLiteral("Simulated: %1").arg(QFileInfo(sim).fileName())});
    if (withDemo && QFile::exists(QLatin1String(kDemo))) out.push_back({demoId(), QStringLiteral("Demo: example recording (water on glass)")});
    return out;
}

std::unique_ptr<Camera> Camera::open(const QString& id, QString* error) {
    if (id.startsWith(QLatin1String("synth:"))) {
        auto c = std::make_unique<SynthCamera>();
        if (!c->load(id.mid(6), error)) return nullptr;
        return c;
    }
    if (id.startsWith(QLatin1String("sim:"))) {
        auto c = std::make_unique<SimCamera>();
        if (!c->load(id.mid(4), error)) return nullptr;
        return c;
    }
#ifdef __linux__
    auto c = std::make_unique<V4l2Camera>();
    if (!c->openDevice(id, error)) return nullptr;
    return c;
#else
    *error = QStringLiteral("live cameras are only supported on Linux so far");
    return nullptr;
#endif
}

bool Camera::applyOssilaSettings(int exposure100us) {
    if (controls().empty()) return false;
    setControl(QStringLiteral("auto_exposure"), 1);   // manual first, or the exposure time is not accepted
    const std::pair<const char*, int> s[] = {
        {"brightness", 0}, {"contrast", 32}, {"hue", 0}, {"saturation", 64}, {"sharpness", 0}, {"gamma", 72},
        {"gain", 0}, {"backlight_compensation", 2}, {"exposure_dynamic_framerate", 0}, {"white_balance_automatic", 1},
        {"exposure_time_absolute", exposure100us},
    };
    bool ok = true;
    for (const auto& [n, v] : s) ok &= setControl(QLatin1String(n), v);
    return ok;
}

std::vector<std::pair<QString, int>> Camera::settings() const {
    std::vector<std::pair<QString, int>> out;
    for (const CameraControl& c : controls()) out.emplace_back(c.name, c.value);
    return out;
}

}  // namespace ld
