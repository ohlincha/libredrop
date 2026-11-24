#pragma once
// One camera frame with what was found in it: the surface, each drop's outline, contact points, tangents and
// angle. Drops are coloured by their id (the same colours as the plot); drops not chosen are grey.
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
    QSize sizeHint() const override { return {960, 420}; }

signals:
    void dropClicked(int id);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;

private:
    QRectF imageRect() const;
    QImage image_;
    std::optional<ld::Scene> scene_;
    QList<int> ids_, chosen_;
    QString message_;
};
