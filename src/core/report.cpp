#include "report.h"

#include <QPainter>
#include <QPainterPath>
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace ld {

bool usable(const DropAnalysis& a) {
    if (!a.ok) return false;
    for (const Check& c : a.checks)
        if (c.level == Check::Error) return false;
    return true;
}

double angleOf(const DropAnalysis& a) {
    if (!usable(a)) return kNaN;
    for (const char* m : {"young-laplace", "tangent", "circle"}) {
        const AngleResult* r = a.method(QString::fromLatin1(m));
        if (r && r->ok) return r->mean();
    }
    return kNaN;
}

namespace {

QString num(double v, int prec = 3) { return std::isfinite(v) ? QString::number(v, 'f', prec) : QString(); }

double median(std::vector<double> v) {
    v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return !std::isfinite(x); }), v.end());
    if (v.empty()) return kNaN;
    std::nth_element(v.begin(), v.begin() + long(v.size() / 2), v.end());
    return v[v.size() / 2];
}

// A round tick step (1, 2 or 5 × 10^k) giving about n intervals over the range.
double niceStep(double range, int n) {
    const double raw = range / n, mag = std::pow(10.0, std::floor(std::log10(raw))), f = raw / mag;
    return (f < 1.5 ? 1 : f < 3.5 ? 2 : f < 7.5 ? 5 : 10) * mag;
}

double slope(const std::vector<double>& t, const std::vector<double>& y) {
    double st = 0, sy = 0, stt = 0, sty = 0;
    int n = 0;
    for (std::size_t i = 0; i < t.size(); ++i)
        if (std::isfinite(y[i])) st += t[i], sy += y[i], stt += t[i] * t[i], sty += t[i] * y[i], ++n;
    const double det = n * stt - st * st;
    return n >= 3 && det > 1e-12 ? (n * sty - st * sy) / det : kNaN;
}

}  // namespace

QString tracksCsv(const VideoResult& r, const Units& u) {
    const QString L = u.lenUnit(), V = u.volUnit();
    QStringList out;
    out << QStringLiteral("drop,frame,time_s,since_contact_s,needle,ok,angle_deg,yl_left,yl_right,tangent_left,tangent_right,"
                          "circle,ellipse_left,ellipse_right,height_width,base_%1,height_%1,volume_%2,bond,fit_rms_px,x_%1,checks")
               .arg(L, V);
    for (const DropTrack& t : r.tracks)
        for (const TrackPoint& p : t.points) {
            const DropAnalysis& a = p.drop.a;
            auto m = [&](const char* name) { return a.method(QString::fromLatin1(name)); };
            auto side = [&](const char* name, bool left) {
                const AngleResult* x = m(name);
                return x && x->ok ? num(left ? x->left : x->right, 2) : QString();
            };
            QStringList checks;
            for (const Check& c : a.checks) checks << c.id;
            const AngleResult* yl = m("young-laplace");
            out << QStringList{QString::number(t.id), QString::number(p.frame), num(p.t, 4), num(p.t - t.tContact(), 4),
                               p.drop.onNeedle ? "1" : "0", usable(a) ? "1" : "0", num(angleOf(a), 2),
                               side("young-laplace", true), side("young-laplace", false), side("tangent", true),
                               side("tangent", false), side("circle", true), side("ellipse", true), side("ellipse", false),
                               side("height-width", true), num(u.len(a.baseWidthPx), 4), num(u.len(a.heightPx), 4),
                               num(u.vol(a.volumePx3), 5), num(a.bond, 4), yl && yl->ok ? num(yl->rmsPx, 2) : QString(),
                               num(u.len(p.drop.xc), 3), checks.join(QLatin1Char(';'))}
                       .join(QLatin1Char(','));
        }
    return out.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

TrackSummary summarize(const DropTrack& t, const Units& u) {
    TrackSummary s;
    s.id = t.id;
    s.points = int(t.points.size());
    if (t.points.empty()) return s;
    s.x = u.len(t.points.front().drop.xc);
    s.tContact = t.tContact();
    s.tEnd = t.points.back().t;
    for (const TrackPoint& p : t.points) s.failed += !usable(p.drop.a);
    const int rel = t.released();
    if (rel < 0) return s;
    s.tRelease = t.points[std::size_t(rel)].t;
    std::vector<double> tt, ang, base, vol;
    for (std::size_t i = std::size_t(rel); i < t.points.size(); ++i) {
        const DropAnalysis& a = t.points[i].drop.a;
        tt.push_back(t.points[i].t);
        ang.push_back(angleOf(a));
        base.push_back(usable(a) ? u.len(a.baseWidthPx) : kNaN);
        vol.push_back(usable(a) && a.volumePx3 > 0 ? u.vol(a.volumePx3) : kNaN);
    }
    const std::size_t k = std::min<std::size_t>(5, tt.size());
    auto head = [&](const std::vector<double>& v) { return median({v.begin(), v.begin() + long(k)}); };
    auto tail = [&](const std::vector<double>& v) { return median({v.end() - long(k), v.end()}); };
    s.angleRelease = head(ang), s.angleEnd = tail(ang);
    s.baseRelease = head(base), s.baseEnd = tail(base);
    s.volumeRelease = head(vol), s.volumeEnd = tail(vol);
    s.angleRate = slope(tt, ang), s.baseRate = slope(tt, base), s.volumeRate = slope(tt, vol);
    return s;
}

QImage plotTracks(const VideoResult& r, const Units& u, int W, int H) {
    QImage img(W, H, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor colors[] = {QColor(0x1f77b4), QColor(0xd62728), QColor(0x2ca02c), QColor(0x9467bd), QColor(0xff7f0e),
                             QColor(0x8c564b), QColor(0xe377c2), QColor(0x17becf)};
    struct Panel {
        QString title;
        std::function<double(const DropAnalysis&)> f;
    };
    const Panel panels[] = {
        {QStringLiteral("contact angle (deg)"), [](const DropAnalysis& a) { return angleOf(a); }},
        {QStringLiteral("base width (%1)").arg(u.lenUnit()), [&](const DropAnalysis& a) { return usable(a) ? u.len(a.baseWidthPx) : kNaN; }},
        {QStringLiteral("volume (%1)").arg(u.volUnit()),
         [&](const DropAnalysis& a) { return usable(a) && a.volumePx3 > 0 ? u.vol(a.volumePx3) : kNaN; }},
    };
    double tMax = 0;
    for (const DropTrack& t : r.tracks)
        if (!t.points.empty()) tMax = std::max(tMax, t.points.back().t - t.tContact());
    if (tMax <= 0) tMax = 1;
    const double tStep = niceStep(tMax, 6);
    tMax = std::ceil(tMax / tStep - 1e-9) * tStep;
    const int perRow = std::max(1, (W - 120) / 90);
    const int legendRows = (int(r.tracks.size()) + perRow - 1) / perRow;
    const int left = 90, right = 20, top = 30 + 20 * legendRows, gap = 50, bottom = 50;
    const int ph = (H - top - bottom - 2 * gap) / 3;
    QFont font = p.font();
    font.setPixelSize(14);
    p.setFont(font);
    for (int k = 0; k < 3; ++k) {
        const QRect box(left, top + k * (ph + gap), W - left - right, ph);
        // range: 2nd–98th percentile of all values, padded
        std::vector<double> all;
        for (const DropTrack& t : r.tracks)
            for (const TrackPoint& q : t.points) {
                const double v = panels[k].f(q.drop.a);
                if (std::isfinite(v)) all.push_back(v);
            }
        double lo = 0, hi = 1;
        if (!all.empty()) {
            std::sort(all.begin(), all.end());
            lo = all[std::size_t(0.02 * double(all.size() - 1))], hi = all[std::size_t(0.98 * double(all.size() - 1))];
            const double pad = std::max(1e-9, 0.08 * (hi - lo) + (hi == lo ? std::abs(hi) * 0.1 + 1e-6 : 0));
            lo -= pad, hi += pad;
            lo = std::max(0.0, lo);   // angles, widths and volumes are positive
        }
        p.setPen(QColor(0x999999));
        p.drawRect(box);
        p.setPen(Qt::black);
        p.drawText(QRect(box.left(), box.top() - 24, box.width(), 20), Qt::AlignLeft | Qt::AlignVCenter, panels[k].title);
        const double step = niceStep(hi - lo, 4);
        lo = std::floor(lo / step + 1e-9) * step, hi = std::ceil(hi / step - 1e-9) * step;
        for (double v = lo; v <= hi + 0.5 * step; v += step) {   // y ticks
            const int y = box.bottom() - int(std::lround(box.height() * (v - lo) / (hi - lo)));
            p.setPen(QColor(0xeeeeee));
            if (v > lo + 0.5 * step && v < hi - 0.5 * step) p.drawLine(box.left() + 1, y, box.right() - 1, y);
            p.setPen(Qt::black);
            p.drawText(QRect(0, y - 10, left - 8, 20), Qt::AlignRight | Qt::AlignVCenter, QString::number(v, 'g', 4));
        }
        for (double t = 0; t <= tMax + 0.5 * tStep; t += tStep) {   // x ticks
            const int x = box.left() + int(std::lround(box.width() * t / tMax));
            p.drawText(QRect(x - 40, box.bottom() + 4, 80, 18), Qt::AlignCenter, QString::number(t, 'g', 4));
        }
        auto X = [&](double t) { return box.left() + box.width() * t / tMax; };
        auto Y = [&](double v) { return box.bottom() - box.height() * (v - lo) / (hi - lo); };
        p.setClipRect(box);
        for (std::size_t ti = 0; ti < r.tracks.size(); ++ti) {
            const DropTrack& t = r.tracks[ti];
            const QColor c = colors[ti % 8];
            QPainterPath path;
            bool pen = false;
            for (const TrackPoint& q : t.points) {
                const double v = panels[k].f(q.drop.a);
                if (!std::isfinite(v)) {
                    pen = false;
                    continue;
                }
                const QPointF pt(X(q.t - t.tContact()), Y(v));
                if (pen) path.lineTo(pt);
                else path.moveTo(pt);
                pen = true;
                if (q.drop.onNeedle) {   // on the needle: hollow markers
                    p.setPen(c);
                    p.setBrush(Qt::NoBrush);
                    p.drawEllipse(pt, 2.5, 2.5);
                }
            }
            p.setPen(QPen(c, 1.6));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        }
        p.setClipping(false);
    }
    p.setPen(Qt::black);
    p.drawText(QRect(left, H - 24, W - left - right, 20), Qt::AlignCenter,
               QStringLiteral("time since the drop touched the surface (s); hollow markers: needle still in the drop"));
    for (std::size_t ti = 0; ti < r.tracks.size(); ++ti) {   // legend
        p.setPen(QPen(colors[ti % 8], 3));
        const int x = left + 90 * int(ti % std::size_t(perRow)), y = 12 + 20 * int(ti / std::size_t(perRow));
        p.drawLine(x, y, x + 22, y);
        p.setPen(Qt::black);
        p.drawText(x + 28, y + 5, QStringLiteral("drop %1").arg(r.tracks[ti].id));
    }
    return img;
}

}  // namespace ld
