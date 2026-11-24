#pragma once
// Angle, base width and volume against time for the chosen drops, with a cursor at the shown frame.
// Clicking a panel moves to that time.
#include <QWidget>

#include "core/report.h"

class PlotWidget : public QWidget {
    Q_OBJECT
public:
    explicit PlotWidget(QWidget* parent = nullptr);
    void setSeries(const ld::SeriesSet& s);
    void setCursor(double t);              // video time, s
    void setHighlight(int id);
    void setSinceContact(bool on);
    bool sinceContact() const { return style_.sinceContact; }
    QSize sizeHint() const override { return {520, 560}; }

signals:
    void timeClicked(double t);            // video time, s (only when the x axis is the video time)

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;

private:
    ld::SeriesSet series_;
    ld::PlotStyle style_;
    ld::PlotGeometry geom_;
};
