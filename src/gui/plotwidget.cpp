#include "plotwidget.h"

#include <QMouseEvent>
#include <QPainter>

PlotWidget::PlotWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(300, 300);
    style_.sinceContact = false;
    style_.fontPx = 12;
    style_.markerNote = false;
    setToolTip(tr("Hollow markers: the needle is still in the drop. Click to show that moment."));
}

void PlotWidget::setSeries(const ld::SeriesSet& s) {
    series_ = s;
    update();
}

void PlotWidget::setCursor(double t) {
    style_.cursor = t;
    update();
}

void PlotWidget::setHighlight(int id) {
    style_.highlight = id;
    update();
}

void PlotWidget::setSinceContact(bool on) {
    style_.sinceContact = on;
    update();
}

void PlotWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QPalette& pal = palette();
    ld::PlotStyle st = style_;
    if (st.sinceContact) st.cursor = ld::kNaN;   // one cursor makes no sense on per-drop time axes
    st.background = pal.color(QPalette::Base);
    st.text = pal.color(QPalette::Text);
    st.grid = pal.color(QPalette::Base).lightness() > 128 ? QColor(0xe8e8e8) : QColor(0x3a3a3a);
    geom_ = ld::drawSeries(p, rect(), series_, st);
}

void PlotWidget::mousePressEvent(QMouseEvent* e) {
    if (style_.sinceContact) return;
    for (const QRect& b : geom_.boxes)
        if (b.contains(e->position().toPoint())) {
            emit timeClicked(geom_.t0 + (geom_.t1 - geom_.t0) * (e->position().x() - b.left()) / b.width());
            return;
        }
}

void PlotWidget::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) mousePressEvent(e);
}
