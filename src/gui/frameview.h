#pragma once
// One camera frame with what was found in it: the surface, each drop's detected edge (dots), the fitted
// Young–Laplace profile (line), contact points, tangents and angle. Drops are coloured by their id (the same
// colours as the plot); drops not chosen are grey.
//
// View: zoomed to the drops by default (the frame is wide and the drops small); click a drop to zoom to it, wheel
// to zoom at the cursor, drag to pan, double-click to go back (drops ↔ whole frame).
#include <QImage>
#include <QList>
#include <QWidget>
#include <optional>

#include "core/scene.h"

class FrameView : public QWidget {
    Q_OBJECT
public:
    explicit FrameView(QWidget* parent = nullptr);
    // ids: track id per scene drop (0 = not tracked)
    void setFrame(const QImage& image, const std::optional<ld::Scene>& scene, const QList<int>& ids);
    void setChosen(const QList<int>& ids);   // empty: all
    void setMessage(const QString& text);    // shown when there is no image
    void resetView();                        // back to the drops (new file)
    void setAutoZoom(bool on);
    void setPxPerMm(double v);               // > 0: a scale bar in the corner               // off: whole frame, never zooms by itself (live camera)
    QSize sizeHint() const override { return {960, 520}; }

signals:
    void dropClicked(int id);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    enum class Mode { Drops, Drop, Whole, Manual };
    QRectF dropsRect(int onlyId = 0) const;  // image region around the drops (all, or one id)
    void fit(const QRectF& r);               // view = r widened to the widget's aspect ratio
    void updateAutoView();
    void clampView();                        // zoom between 16x and the whole frame; the image stays in view
    double scale() const;
    QPointF toWidget(double x, double y) const;
    QPointF toImage(const QPointF& w) const;

    QImage image_;
    std::optional<ld::Scene> scene_;
    QList<int> ids_, chosen_;
    QString message_;
    Mode mode_ = Mode::Drops;
    int focusId_ = 0;
    QRectF view_;                            // image region shown
    QPointF dragStart_;
    QRectF dragView_;
    bool dragged_ = false;
    bool autoZoom_ = true;
    double pxPerMm_ = 0;
    void drawScaleBar(class QPainter& p);
};
