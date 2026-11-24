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

QString tracksCsv(const VideoResult& r, const Units& u, const QList<int>& ids) {
    const QString L = u.lenUnit(), V = u.volUnit();
    QStringList out;
    out << QStringLiteral("drop,frame,time_s,since_contact_s,needle,ok,angle_deg,yl_left,yl_right,tangent_left,tangent_right,"
                          "circle,ellipse_left,ellipse_right,height_width,base_%1,height_%1,volume_%2,bond,fit_rms_px,x_%1,checks")
               .arg(L, V);
    for (const DropTrack& t : r.tracks)
        for (const TrackPoint& p : t.points) {
            if (!ids.isEmpty() && !ids.contains(t.id)) break;
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

int DropSeries::released() const {
    bool seenNeedle = false;
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (needle[i]) seenNeedle = true;
        else if (seenNeedle || i == 0) return int(i);
    }
    return -1;
}

SeriesSet seriesOf(const VideoResult& r, const Units& u) {
    SeriesSet s;
    s.lenUnit = u.lenUnit(), s.volUnit = u.volUnit();
    for (const DropTrack& t : r.tracks) {
        DropSeries d;
        d.id = t.id;
        if (!t.points.empty()) d.x = u.len(t.points.front().drop.xc);
        for (const TrackPoint& p : t.points) {
            const DropAnalysis& a = p.drop.a;
            const bool ok = usable(a);
            d.t.push_back(p.t);
            d.angle.push_back(angleOf(a));
            d.base.push_back(ok ? u.len(a.baseWidthPx) : kNaN);
            d.volume.push_back(ok && a.volumePx3 > 0 ? u.vol(a.volumePx3) : kNaN);
            d.needle.push_back(p.drop.onNeedle);
        }
        s.drops.push_back(std::move(d));
    }
    return s;
}

bool readTracksCsv(const QString& text, SeriesSet* out, QString* error) {
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.isEmpty()) {
        *error = QStringLiteral("empty table");
        return false;
    }
    const QStringList head = lines[0].trimmed().split(QLatin1Char(','));
    auto col = [&](const QString& prefix) {
        for (int i = 0; i < head.size(); ++i)
            if (head[i] == prefix || head[i].startsWith(prefix + QLatin1Char('_'))) return i;
        return -1;
    };
    const int cDrop = col("drop"), cT = col("time_s"), cNeedle = col("needle"), cOk = col("ok"), cAngle = col("angle_deg"),
              cBase = col("base"), cVol = col("volume"), cX = col("x");
    if (cDrop < 0 || cT < 0 || cNeedle < 0 || cOk < 0 || cAngle < 0 || cBase < 0 || cVol < 0) {
        *error = QStringLiteral("not a LibreDrop video table (columns drop, time_s, needle, ok, angle_deg, base_*, volume_*)");
        return false;
    }
    SeriesSet s;
    s.lenUnit = head[cBase].section(QLatin1Char('_'), 1);
    s.volUnit = head[cVol].section(QLatin1Char('_'), 1);
    auto val = [](const QStringList& f, int i) {
        bool ok = false;
        const double v = i >= 0 && i < f.size() ? f[i].toDouble(&ok) : kNaN;
        return ok ? v : kNaN;
    };
    for (int li = 1; li < lines.size(); ++li) {
        const QStringList f = lines[li].trimmed().split(QLatin1Char(','));
        if (f.size() < head.size() - 1) continue;
        const int id = f[cDrop].toInt();
        auto it = std::find_if(s.drops.begin(), s.drops.end(), [&](const DropSeries& d) { return d.id == id; });
        if (it == s.drops.end()) {
            s.drops.push_back({});
            it = s.drops.end() - 1;
            it->id = id;
            it->x = val(f, cX);
        }
        const bool ok = f[cOk] == QLatin1String("1");
        it->t.push_back(val(f, cT));
        it->angle.push_back(ok ? val(f, cAngle) : kNaN);
        it->base.push_back(ok ? val(f, cBase) : kNaN);
        it->volume.push_back(ok ? val(f, cVol) : kNaN);
        it->needle.push_back(f[cNeedle] == QLatin1String("1"));
    }
    *out = std::move(s);
    return true;
}

void selectDrops(SeriesSet* s, const QList<int>& ids) {
    if (ids.isEmpty()) return;
    s->drops.erase(std::remove_if(s->drops.begin(), s->drops.end(), [&](const DropSeries& d) { return !ids.contains(d.id); }),
                   s->drops.end());
}

TrackSummary summarize(const DropSeries& d) {
    TrackSummary s;
    s.id = d.id;
    s.points = int(d.t.size());
    if (d.t.empty()) return s;
    s.x = d.x;
    s.tContact = d.tContact();
    s.tEnd = d.t.back();
    for (double a : d.angle) s.failed += !std::isfinite(a);
    const int rel = d.released();
    if (rel < 0) return s;
    s.tRelease = d.t[std::size_t(rel)];
    auto from = [&](const std::vector<double>& v) { return std::vector<double>(v.begin() + rel, v.end()); };
    const std::vector<double> tt = from(d.t), ang = from(d.angle), base = from(d.base), vol = from(d.volume);
    // first and last few usable values
    auto edge = [](const std::vector<double>& v, bool first) {
        std::vector<double> got;
        for (std::size_t i = 0; i < v.size() && got.size() < 5; ++i) {
            const double x = v[first ? i : v.size() - 1 - i];
            if (std::isfinite(x)) got.push_back(x);
        }
        return median(got);
    };
    s.angleRelease = edge(ang, true), s.angleEnd = edge(ang, false);
    s.baseRelease = edge(base, true), s.baseEnd = edge(base, false);
    s.volumeRelease = edge(vol, true), s.volumeEnd = edge(vol, false);
    s.angleRate = slope(tt, ang), s.baseRate = slope(tt, base), s.volumeRate = slope(tt, vol);
    return s;
}

QColor dropColor(int id) {
    static const QColor colors[] = {QColor(0x1f77b4), QColor(0xd62728), QColor(0x2ca02c), QColor(0x9467bd),
                                    QColor(0xff7f0e), QColor(0x8c564b), QColor(0xe377c2), QColor(0x17becf)};
    return colors[std::size_t(std::max(0, id - 1)) % 8];
}

PlotGeometry drawSeries(QPainter& p, const QRect& area, const SeriesSet& r, const PlotStyle& st) {
    PlotGeometry g;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(area, st.background);
    struct Panel {
        QString title;
        std::vector<double> DropSeries::*v;
    };
    const Panel panels[] = {
        {QStringLiteral("contact angle (°)"), &DropSeries::angle},
        {QStringLiteral("base width (%1)").arg(r.lenUnit), &DropSeries::base},
        {QStringLiteral("volume (%1)").arg(r.volUnit == QLatin1String("uL") ? QStringLiteral("µL") : r.volUnit), &DropSeries::volume},
    };
    auto t0Of = [&](const DropSeries& d) { return st.sinceContact ? d.tContact() : 0.0; };
    double tMin = 1e300, tMax = -1e300;
    for (const DropSeries& d : r.drops)
        if (!d.t.empty()) tMin = std::min(tMin, d.t.front() - t0Of(d)), tMax = std::max(tMax, d.t.back() - t0Of(d));
    if (st.sinceContact || tMin > tMax) tMin = 0;
    if (std::isfinite(st.tMin)) tMin = st.tMin;
    if (std::isfinite(st.tMax)) tMax = st.tMax;
    if (!(tMax > tMin)) tMax = tMin + 1;
    const double tStep = niceStep(tMax - tMin, std::max(2, area.width() / 180));
    if (!std::isfinite(st.tMax)) tMax = std::ceil(tMax / tStep - 1e-9) * tStep;
    if (!std::isfinite(st.tMin)) tMin = std::floor(tMin / tStep + 1e-9) * tStep;
    g.t0 = tMin, g.t1 = tMax;
    QFont font = p.font();
    font.setPixelSize(st.fontPx);
    p.setFont(font);
    const int lh = st.fontPx + 6;
    const int perRow = std::max(1, (area.width() - 120) / (st.fontPx * 6 + 20));
    const int legendRows = st.legend ? (int(r.drops.size()) + perRow - 1) / perRow : 0;
    const int left = st.fontPx * 5, right = 16, top = 8 + lh * legendRows + lh, gap = lh * 2 + 4, bottom = lh * 2 + 6;
    const int ph = std::max(20, (area.height() - top - bottom - 2 * gap) / 3);
    for (int k = 0; k < 3; ++k) {
        const QRect box(area.left() + left, area.top() + top + k * (ph + gap), area.width() - left - right, ph);
        g.boxes[k] = box;
        std::vector<double> all;   // range: 2nd–98th percentile of all values, padded
        for (const DropSeries& t : r.drops)
            for (double v : t.*panels[k].v)
                if (std::isfinite(v)) all.push_back(v);
        double lo = 0, hi = 1;
        if (!all.empty()) {
            std::sort(all.begin(), all.end());
            lo = all[std::size_t(0.02 * double(all.size() - 1))], hi = all[std::size_t(0.98 * double(all.size() - 1))];
            const double pad = std::max(1e-9, 0.08 * (hi - lo) + (hi == lo ? std::abs(hi) * 0.1 + 1e-6 : 0));
            lo = std::max(0.0, lo - pad), hi += pad;   // angles, widths and volumes are positive
        }
        p.setPen(st.grid.darker(140));
        p.setBrush(Qt::NoBrush);
        p.drawRect(box);
        p.setPen(st.text);
        p.drawText(QRect(box.left(), box.top() - lh, box.width(), lh - 2), Qt::AlignLeft | Qt::AlignVCenter, panels[k].title);
        const double step = niceStep(hi - lo, std::max(2, ph / 50));
        lo = std::floor(lo / step + 1e-9) * step, hi = std::ceil(hi / step - 1e-9) * step;
        for (double v = lo; v <= hi + 0.5 * step; v += step) {   // y ticks
            const int y = box.bottom() - int(std::lround(box.height() * (v - lo) / (hi - lo)));
            p.setPen(st.grid);
            if (v > lo + 0.5 * step && v < hi - 0.5 * step) p.drawLine(box.left() + 1, y, box.right() - 1, y);
            p.setPen(st.text);
            p.drawText(QRect(area.left(), y - lh / 2, left - 6, lh), Qt::AlignRight | Qt::AlignVCenter, QString::number(v, 'g', 4));
        }
        for (double t = std::ceil(tMin / tStep - 1e-9) * tStep; t <= tMax + 1e-9 * tStep; t += tStep) {   // x ticks
            const int x = box.left() + int(std::lround(box.width() * (t - tMin) / (tMax - tMin)));
            p.drawText(QRect(x - 40, box.bottom() + 2, 80, lh), Qt::AlignCenter, QString::number(t, 'g', 4));
        }
        auto X = [&](double t) { return box.left() + box.width() * (t - tMin) / (tMax - tMin); };
        auto Y = [&](double v) { return box.bottom() - box.height() * (v - lo) / (hi - lo); };
        p.setClipRect(box.adjusted(1, 1, 0, 0));
        for (const DropSeries& t : r.drops) {
            const std::vector<double>& vals = t.*panels[k].v;
            const QColor c = dropColor(t.id);
            QPainterPath path;
            bool pen = false;
            for (std::size_t i = 0; i < t.t.size(); ++i) {
                const double v = vals[i];
                if (!std::isfinite(v)) {
                    pen = false;
                    continue;
                }
                const QPointF pt(X(t.t[i] - t0Of(t)), Y(v));
                if (pen) path.lineTo(pt);
                else path.moveTo(pt);
                pen = true;
                if (t.needle[i]) {   // on the needle: hollow markers
                    p.setPen(c);
                    p.setBrush(Qt::NoBrush);
                    p.drawEllipse(pt, 2.5, 2.5);
                }
            }
            p.setPen(QPen(c, t.id == st.highlight ? 3.0 : 1.6));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        }
        if (std::isfinite(st.cursor)) {
            p.setPen(QPen(st.text, 1, Qt::DashLine));
            p.drawLine(QPointF(X(st.cursor), box.top()), QPointF(X(st.cursor), box.bottom()));
        }
        p.setClipping(false);
    }
    p.setPen(st.text);
    p.drawText(QRect(area.left() + left, area.bottom() - lh, area.width() - left - right, lh), Qt::AlignCenter,
               st.sinceContact ? QStringLiteral("time since the drop touched the surface (s); hollow markers: needle still in the drop")
                               : QStringLiteral("time (s); hollow markers: needle still in the drop"));
    for (std::size_t ti = 0; st.legend && ti < r.drops.size(); ++ti) {
        p.setPen(QPen(dropColor(r.drops[ti].id), 3));
        const int x = area.left() + left + (st.fontPx * 6 + 20) * int(ti % std::size_t(perRow));
        const int y = area.top() + 4 + lh / 2 + lh * int(ti / std::size_t(perRow));
        p.drawLine(x, y, x + 22, y);
        p.setPen(st.text);
        p.drawText(QRect(x + 28, y - lh / 2, st.fontPx * 6, lh), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("drop %1").arg(r.drops[ti].id));
    }
    p.restore();
    return g;
}

QImage plotSeries(const SeriesSet& r, int W, int H) {
    QImage img(W, H, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    drawSeries(p, img.rect(), r, PlotStyle{});
    return img;
}

}  // namespace ld
