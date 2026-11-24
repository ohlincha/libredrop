#pragma once
// Project ▸ Liquids… and Project ▸ Calibration…: tables of the project's liquid data (with sources) and its
// image scales per camera mode (from the calibration sphere, or from the needle's measured diameter).
#include <functional>

#include "core/project.h"

class QWidget;

// True if the user changed and accepted the table (the caller saves the project).
bool editLiquids(QWidget* parent, ld::Project& p);
// frameWidth/scale: the open recording's camera mode and current scale (0 if none); measureNeedlePx: the needle's
// width in pixels in the open frame (NaN if it can't be measured).
bool editCalibrations(QWidget* parent, ld::Project& p, int frameWidth, double scale, std::function<double()> measureNeedlePx);
