#include "frameview.h"

#include <QMouseEvent>
#include <QPainter>
#include <cmath>

#include "core/report.h"

FrameView::FrameView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 160);
    setMessage(tr("Open a video or an image (File ▸ Open, or drop a file here)."));
}

void FrameView::setFrame(const QImage& image, const std::optional<ld::Scene>& scene, const QList<int>& ids) {
    image_ = image;
    scene_ = scene;
    ids_ = ids;
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

QRectF FrameView::imageRect() const {
    if (image_.isNull()) return {};
    const double s = std::min(double(width()) / image_.width(), double(height()) / image_.height());
    const double w = image_.width() * s, h = image_.height() * s;
    return {(width() - w) / 2, (height() - h) / 2, w, h};
}

void FrameView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Window).darker(115));
    if (image_.isNull()) {
        p.setPen(palette().color(QPalette::WindowText));
        p.drawText(rect().adjusted(20, 20, -20, -20), Qt::AlignCenter | Qt::TextWordWrap, message_);
        return;
    }
    const QRectF r = imageRect();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(r, image_);
    if (!scene_ || !scene_->ok) return;
    p.setRenderHint(QPainter::Antialiasing);
    const double s = r.width() / image_.width();
    auto map = [&](double x, double y) { return QPointF(r.left() + (x + 0.5) * s, r.top() + (y + 0.5) * s); };
    // surface
    const ld::Baseline& bl = scene_->surface;
    const double tn = std::tan(bl.angle);
    p.setPen(QPen(QColor(255, 200, 0), 1.2, Qt::DashLine));
    p.drawLine(map(0, bl.p0.y - tn * bl.p0.x), map(image_.width() - 1, bl.p0.y + tn * (image_.width() - 1 - bl.p0.x)));
    QFont f = font();
    f.setBold(true);
    p.setFont(f);
    for (int i = 0; i < int(scene_->drops.size()); ++i) {
        const ld::SceneDrop& d = scene_->drops[std::size_t(i)];
        const int id = i < ids_.size() ? ids_[i] : 0;
        const bool chosen = id > 0 && (chosen_.isEmpty() || chosen_.contains(id));
        const QColor c = chosen ? ld::dropColor(id) : QColor(150, 150, 150);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        for (std::size_t k = 0; k < d.a.outline.size(); k += 2) {
            const ld::Pt q = d.a.outline[k];
            p.drawEllipse(map(q.x + d.x0, q.y), 1.3, 1.3);
        }
        const ld::AngleResult* m = d.a.method(QStringLiteral("young-laplace"));
        if (!m || !m->ok) m = d.a.method(QStringLiteral("tangent"));
        double topY = 1e9, cx = d.xc;
        for (const ld::Pt& q : d.a.outline) topY = std::min(topY, q.y);
        if (m && m->ok) {
            const ld::Baseline& b = d.a.baseline;
            const double len = std::max(25.0, 0.35 * d.a.baseWidthPx);
            for (int side = 0; side < 2; ++side) {
                const ld::Pt cp = side == 0 ? m->contactL : m->contactR;
                const double th = (side == 0 ? m->left : m->right) * M_PI / 180;
                const ld::Pt end{cp.x + (side == 0 ? 1 : -1) * len * std::cos(th), cp.y + len * std::sin(th)};
                const ld::Pt a = b.toImage(cp), e = b.toImage(end);
                p.setPen(QPen(c, 2));
                p.drawLine(map(a.x + d.x0, a.y), map(e.x + d.x0, e.y));
                p.setBrush(Qt::white);
                p.drawEllipse(map(a.x + d.x0, a.y), 3.5, 3.5);
            }
        }
        if (topY < 1e8) {
            const double ang = ld::angleOf(d.a);
            const QString label = (id > 0 ? QStringLiteral("%1: ").arg(id) : QString()) +
                                  (std::isfinite(ang) ? QStringLiteral("%1°").arg(ang, 0, 'f', 1) : tr("—"));
            const QPointF at = map(cx, topY) - QPointF(0, 8);
            const QRectF box(at.x() - 60, at.y() - 20, 120, 20);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 150));
            const QRectF tb = p.boundingRect(box, Qt::AlignCenter, label).adjusted(-4, -1, 4, 1);
            p.drawRoundedRect(tb, 3, 3);
            p.setPen(c.lighter(chosen ? 150 : 100));
            p.drawText(box, Qt::AlignCenter, label);
        }
    }
}

void FrameView::mousePressEvent(QMouseEvent* e) {
    if (!scene_ || image_.isNull()) return;
    const QRectF r = imageRect();
    const double s = r.width() / image_.width();
    const double x = (e->position().x() - r.left()) / s;
    for (int i = 0; i < int(scene_->drops.size()); ++i) {
        const ld::SceneDrop& d = scene_->drops[std::size_t(i)];
        if (x >= d.x0 && x <= d.x1 && i < ids_.size() && ids_[i] > 0) {
            emit dropClicked(ids_[i]);
            return;
        }
    }
}
