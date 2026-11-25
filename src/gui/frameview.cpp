#include "frameview.h"

#include <algorithm>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <cmath>

#include "core/report.h"

FrameView::FrameView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 160);
    setToolTip(tr("<p>Dots: the detected edge. Line: the fitted Young–Laplace shape.</p>"
                  "<p>Click a drop to zoom to it; wheel: zoom; drag: move; double-click: all drops / whole frame.</p>"));
    setMessage(tr("Open a video or an image (File ▸ Open, or drop a file here)."));
}

void FrameView::setFrame(const QImage& image, const std::optional<ld::Scene>& scene, const QList<int>& ids) {
    const bool newSize = image.size() != image_.size();
    image_ = image;
    scene_ = scene;
    ids_ = ids;
    if (newSize) mode_ = autoZoom_ ? Mode::Drops : Mode::Whole, view_ = {};
    updateAutoView();
    update();
}

void FrameView::setChosen(const QList<int>& ids) {
    chosen_ = ids;
    update();
}

void FrameView::setMessage(const QString& text) {
    message_ = text;
    update();
}

void FrameView::setPxPerMm(double v) {
    pxPerMm_ = v;
    update();
}

void FrameView::drawScaleBar(QPainter& p) {
    if (!(pxPerMm_ > 0) || image_.isNull()) return;
    const double perMm = pxPerMm_ * scale();   // widget px per mm
    double mm = 0.01;
    for (double m : {0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0})
        if (m * perMm <= 160) mm = m;
    const double len = mm * perMm;
    const QString label = mm >= 1 ? tr("%1 mm").arg(mm) : tr("%1 µm").arg(mm * 1000);
    const QRectF r(width() - len - 16, height() - 30, len, 6);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(QColor(0, 0, 0, 160), 1));
    p.setBrush(QColor(255, 255, 255, 230));
    p.drawRect(r);
    p.setPen(QColor(255, 255, 255));
    QFont f = font();
    f.setBold(true);
    p.setFont(f);
    const QRectF tr_(r.left() - 40, r.top() - p.fontMetrics().height() - 2, r.width() + 80, p.fontMetrics().height());
    p.setPen(QColor(0, 0, 0, 170));
    p.drawText(tr_.translated(1, 1), Qt::AlignHCenter | Qt::AlignBottom, label);
    p.setPen(QColor(255, 255, 255));
    p.drawText(tr_, Qt::AlignHCenter | Qt::AlignBottom, label);
    p.restore();
}

void FrameView::setAutoZoom(bool on) {
    autoZoom_ = on;
    mode_ = on ? Mode::Drops : Mode::Whole;
    view_ = {};
    updateAutoView();
    update();
}

void FrameView::resetView() {
    mode_ = Mode::Drops;
    focusId_ = 0;
    view_ = {};
    updateAutoView();
    update();
}

QRectF FrameView::dropsRect(int onlyId) const {
    QRectF r;
    if (!scene_ || !scene_->ok) return r;
    for (int i = 0; i < int(scene_->drops.size()); ++i) {
        const ld::SceneDrop& d = scene_->drops[std::size_t(i)];
        const int id = i < ids_.size() ? ids_[i] : 0;
        if (onlyId ? id != onlyId : (id == 0 || (!chosen_.isEmpty() && !chosen_.contains(id)))) continue;
        double top = 1e9, l = 1e9, rr = -1e9;
        for (const ld::Pt& q : d.a.outline) top = std::min(top, q.y), l = std::min(l, q.x + d.x0), rr = std::max(rr, q.x + d.x0);
        if (top > 1e8) continue;
        const double base = scene_->surface.p0.y + std::tan(scene_->surface.angle) * (0.5 * (l + rr) - scene_->surface.p0.x);
        const double h = std::max(10.0, base - top);
        // the drop, its contact region and a little of the reflection
        r |= QRectF(QPointF(l - 0.15 * (rr - l) - 10, top - 0.4 * h - 10), QPointF(rr + 0.15 * (rr - l) + 10, base + 0.5 * h + 10));
    }
    return r;
}

void FrameView::fit(const QRectF& r) {
    if (r.isEmpty() || width() <= 0 || height() <= 0) return;
    QRectF v = r;
    const double aspect = double(width()) / height();
    if (v.width() / v.height() < aspect) v.setWidth(v.height() * aspect);
    else v.setHeight(v.width() / aspect);
    v.moveCenter(r.center());
    view_ = v;
    clampView();
}

void FrameView::clampView() {
    if (image_.isNull() || view_.isEmpty() || width() <= 0 || height() <= 0) return;
    const double W = image_.width(), H = image_.height(), aspect = double(width()) / height();
    // largest view: the whole frame fits; smallest: 16 widget pixels per image pixel
    const double maxW = std::max(W, H * aspect), minW = std::max(4.0, width() / 16.0);
    const double w = std::clamp(view_.width(), minW, maxW);
    QRectF v(0, 0, w, w / aspect);
    v.moveCenter(view_.center());
    // per axis: centred if the frame is narrower than the view, else no empty margin beside the frame
    auto place = [](double lo, double len, double size) {
        return len >= size ? (size - len) / 2 : std::clamp(lo, 0.0, size - len);
    };
    v.moveLeft(place(v.left(), v.width(), W));
    v.moveTop(place(v.top(), v.height(), H));
    view_ = v;
}

void FrameView::updateAutoView() {
    if (image_.isNull()) return;
    const QRectF whole(0, 0, image_.width(), image_.height());
    if (mode_ == Mode::Whole || (view_.isEmpty() && mode_ == Mode::Manual)) {
        fit(whole);
        return;
    }
    if (mode_ == Mode::Manual) return;
    const QRectF want = dropsRect(mode_ == Mode::Drop ? focusId_ : 0);
    if (want.isEmpty()) {
        if (view_.isEmpty()) fit(whole);
        return;
    }
    // keep the view while the drops stay well inside it (no jumping while stepping through frames)
    const QRectF inner = view_.adjusted(view_.width() * 0.02, view_.height() * 0.02, -view_.width() * 0.02, -view_.height() * 0.02);
    if (view_.isEmpty() || !inner.contains(want) || (want.width() < 0.45 * view_.width() && want.height() < 0.45 * view_.height()))
        fit(want);
}

double FrameView::scale() const { return view_.isEmpty() ? 1 : std::min(width() / view_.width(), height() / view_.height()); }

QPointF FrameView::toWidget(double x, double y) const {
    const double s = scale();
    return {(x + 0.5 - view_.center().x()) * s + width() / 2.0, (y + 0.5 - view_.center().y()) * s + height() / 2.0};
}

QPointF FrameView::toImage(const QPointF& w) const {
    const double s = scale();
    return {(w.x() - width() / 2.0) / s + view_.center().x() - 0.5, (w.y() - height() / 2.0) / s + view_.center().y() - 0.5};
}

void FrameView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Window).darker(115));
    if (image_.isNull()) {
        p.setPen(palette().color(QPalette::WindowText));
        p.drawText(rect().adjusted(20, 20, -20, -20), Qt::AlignCenter | Qt::TextWordWrap, message_);
        return;
    }
    if (view_.isEmpty()) updateAutoView();
    const double s = scale();
    {   // the image (pixels visible when zoomed in: no smoothing above 3×)
        p.save();
        p.setRenderHint(QPainter::SmoothPixmapTransform, s < 3);
        p.translate(toWidget(-0.5, -0.5));
        p.scale(s, s);
        p.drawImage(QPointF(0, 0), image_);
        p.restore();
    }
    drawScaleBar(p);
    if (!scene_ || !scene_->ok) return;
    p.setRenderHint(QPainter::Antialiasing);
    auto map = [&](double x, double y) { return toWidget(x, y); };
    if (!autoZoom_) {   // live camera: a thin box around each drop and its angle, nothing else
        const ld::Baseline& b = scene_->surface;
        for (const ld::SceneDrop& d : scene_->drops) {
            double top = 1e18;
            for (const ld::Pt& q : d.a.outline) top = std::min(top, q.y);
            const double base = b.p0.y + std::tan(b.angle) * (d.xc - b.p0.x);
            if (top > base) top = base - 4;
            const QRectF r(map(d.x0 - 6, top - 6), map(d.x1 + 6, base + 3));
            p.setPen(QPen(QColor(255, 255, 255, 170), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r);
            const ld::AngleResult* m = d.a.method(QStringLiteral("young-laplace"));
            if (!m || !m->ok) m = d.a.method(QStringLiteral("tangent"));
            if (m && m->ok) {
                const QString t = QStringLiteral("%1°").arg(m->mean(), 0, 'f', 0);
                p.setPen(QColor(255, 255, 255, 210));
                p.drawText(r.topLeft() + QPointF(2, -4), t);
            }
        }
        return;
    }
    const ld::Baseline& bl = scene_->surface;
    const double tn = std::tan(bl.angle);
    p.setPen(QPen(QColor(255, 200, 0), 1.2, Qt::DashLine));
    p.drawLine(map(0, bl.p0.y - tn * bl.p0.x), map(image_.width() - 1, bl.p0.y + tn * (image_.width() - 1 - bl.p0.x)));
    QFont f = font();
    f.setBold(true);
    p.setFont(f);
    {   // surface label at the left edge of the view
        const double xl = std::max(0.0, view_.left());
        const QPointF e = map(xl, bl.p0.y + tn * (xl - bl.p0.x));
        const QString label = tr("surface");
        const QRectF tb(std::max(4.0, e.x() + 4), e.y() - p.fontMetrics().height() - 3,
                        p.fontMetrics().horizontalAdvance(label) + 6, p.fontMetrics().height());
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 140));
        p.drawRoundedRect(tb, 3, 3);
        p.setPen(QColor(255, 200, 0));
        p.drawText(tb, Qt::AlignCenter, label);
    }
    const double dot = std::clamp(0.35 * s, 1.0, 2.5);
    for (int i = 0; i < int(scene_->drops.size()); ++i) {
        const ld::SceneDrop& d = scene_->drops[std::size_t(i)];
        const int id = i < ids_.size() ? ids_[i] : 0;
        const bool chosen = id > 0 && (chosen_.isEmpty() || chosen_.contains(id));
        const QColor c = chosen ? ld::dropColor(id) : QColor(150, 150, 150);
        // fitted Young–Laplace profile
        if (d.a.fitted.size() > 1) {
            QPainterPath path;
            for (std::size_t k = 0; k < d.a.fitted.size(); ++k) {
                const QPointF q = map(d.a.fitted[k].x + d.x0, d.a.fitted[k].y);
                if (k == 0) path.moveTo(q);
                else path.lineTo(q);
            }
            p.setPen(QPen(c, 1.3));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        }
        // detected edge: light dots (every point when zoomed in)
        p.setPen(Qt::NoPen);
        p.setBrush(chosen ? QColor(255, 255, 160) : QColor(200, 200, 200));
        const std::size_t stride = s > 1.5 ? 1 : 2;
        for (std::size_t k = 0; k < d.a.outline.size(); k += stride) {
            const ld::Pt q = d.a.outline[k];
            p.drawEllipse(map(q.x + d.x0, q.y), dot, dot);
        }
        const ld::AngleResult* m = d.a.method(QStringLiteral("young-laplace"));
        if (!m || !m->ok) m = d.a.method(QStringLiteral("tangent"));
        double topY = 1e9;
        for (const ld::Pt& q : d.a.outline) topY = std::min(topY, q.y);
        if (m && m->ok) {
            const ld::Baseline& b = d.a.baseline;
            const double len = std::max(25.0, 0.35 * d.a.baseWidthPx);
            for (int side = 0; side < 2; ++side) {
                const ld::Pt cp = side == 0 ? m->contactL : m->contactR;
                const double th = (side == 0 ? m->left : m->right) * M_PI / 180;
                const ld::Pt end{cp.x + (side == 0 ? 1 : -1) * len * std::cos(th), cp.y + len * std::sin(th)};
                const ld::Pt a = b.toImage(cp), e = b.toImage(end);
                p.setPen(QPen(c, 1.5, Qt::DashLine));
                p.drawLine(map(a.x + d.x0, a.y), map(e.x + d.x0, e.y));
                p.setPen(QPen(c, 1.5));
                p.setBrush(Qt::white);
                p.drawEllipse(map(a.x + d.x0, a.y), 3.5, 3.5);
            }
        }
        if (topY < 1e8) {
            const double ang = ld::angleOf(d.a);
            const ld::AngleResult* yl = d.a.method(QStringLiteral("young-laplace"));
            QString label = (id > 0 ? QStringLiteral("%1: ").arg(id) : QString()) +
                            (std::isfinite(ang) ? QStringLiteral("%1°").arg(ang, 0, 'f', 1) : tr("—"));
            if (yl && yl->ok && s > 0.6) label += tr("   fit %1 px rms").arg(yl->rmsPx, 0, 'f', 2);
            const QPointF at = map(d.xc, topY) - QPointF(0, 10);
            const QRectF box(at.x() - 150, at.y() - 20, 300, 20);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 150));
            const QRectF tb = p.boundingRect(box, Qt::AlignCenter, label).adjusted(-4, -1, 4, 1);
            p.drawRoundedRect(tb, 3, 3);
            p.setPen(c.lighter(chosen ? 150 : 100));
            p.drawText(box, Qt::AlignCenter, label);
        }
    }
    // key and zoom factor
    f.setBold(false);
    p.setFont(f);
    const QString key = tr("· edge   — fit   %1×").arg(s, 0, 'f', s < 1 ? 2 : 1);
    const QRectF kb(8, height() - p.fontMetrics().height() - 8, p.fontMetrics().horizontalAdvance(key) + 10,
                    p.fontMetrics().height() + 2);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 140));
    p.drawRoundedRect(kb, 3, 3);
    p.setPen(QColor(230, 230, 230));
    p.drawText(kb, Qt::AlignCenter, key);
}

void FrameView::mousePressEvent(QMouseEvent* e) {
    dragStart_ = e->position();
    dragView_ = view_;
    dragged_ = false;
}

void FrameView::mouseMoveEvent(QMouseEvent* e) {
    if (!(e->buttons() & Qt::LeftButton) || image_.isNull()) return;
    const QPointF d = e->position() - dragStart_;
    if (!dragged_ && d.manhattanLength() < 4) return;
    dragged_ = true;
    mode_ = Mode::Manual;
    view_ = dragView_.translated(-d / scale());
    clampView();
    update();
}

void FrameView::mouseReleaseEvent(QMouseEvent* e) {
    if (dragged_ || !scene_ || image_.isNull() || !autoZoom_) return;   // live: a click never moves the view
    const QPointF q = toImage(e->position());
    for (int i = 0; i < int(scene_->drops.size()); ++i) {
        const ld::SceneDrop& d = scene_->drops[std::size_t(i)];
        if (q.x() >= d.x0 && q.x() <= d.x1 && i < ids_.size() && ids_[i] > 0) {
            mode_ = Mode::Drop;   // zoom to this drop
            focusId_ = ids_[i];
            fit(dropsRect(focusId_));
            update();
            emit dropClicked(ids_[i]);
            return;
        }
    }
}

void FrameView::mouseDoubleClickEvent(QMouseEvent*) {
    mode_ = mode_ == Mode::Whole && autoZoom_ ? Mode::Drops : Mode::Whole;
    view_ = {};
    updateAutoView();
    update();
}

void FrameView::wheelEvent(QWheelEvent* e) {
    if (image_.isNull() || view_.isEmpty()) return;
    const double f = std::pow(0.8, e->angleDelta().y() / 120.0);
    const QPointF at = toImage(e->position());
    QRectF v(0, 0, view_.width() * f, view_.height() * f);
    v.moveTopLeft(at - QPointF((at.x() - view_.left()) * f, (at.y() - view_.top()) * f));   // cursor point stays put
    view_ = v;
    clampView();
    mode_ = Mode::Manual;
    update();
}

void FrameView::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (mode_ == Mode::Manual) {   // keep the centre and the zoom
        const QPointF c = view_.center();
        const double sc = scale();
        if (sc > 0 && !view_.isEmpty()) {
            view_ = QRectF(0, 0, width() / sc, height() / sc);
            view_.moveCenter(c);
            clampView();
        }
        return;
    }
    view_ = {};
    updateAutoView();
}
