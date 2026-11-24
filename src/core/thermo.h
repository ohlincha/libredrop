#pragma once
// Room conditions from PCsensor TEMPer-compatible USB sensors (USB 0c45:7401 TEMPer, 0c45:7402 TEMPerHUM with
// humidity, 0c45:7403 TH1000iso internal temperature), read directly over HID: Linux hidraw, Windows hid.dll,
// macOS IOKit. Same protocol as USB Thermo Logger (github.com/ohlincha/usb-thermo-logger), whose sensor names and
// offsets (sensors.json, keyed model@USB port) are applied here too.
//
// Evaporation depends on temperature and humidity: LibreDrop notes them with each measurement so that a glass
// reference and the separators it corrects can be checked for the same conditions.
#include <QString>
#include <vector>

#include "drop.h"

namespace ld {

struct ThermoReading {
    QString model, location, name;    // name: from USB Thermo Logger's settings, if set
    double temperature = kNaN;        // °C, offset applied
    double humidity = kNaN;           // %RH (TEMPerHUM only), offset applied
    QString error;                    // e.g. no permission (Linux: the udev rule), no reply
};

// One reading of every attached sensor (takes up to ~1.5 s per sensor that doesn't answer). Empty: none attached.
std::vector<ThermoReading> readThermometers();

// The best reading for room conditions: a sensor with humidity first, then any; NaN fields if none.
ThermoReading roomConditions(const std::vector<ThermoReading>& r);

// Decoding of the 8-byte reply (exposed for the tests); false for an unknown model or a short reply.
bool decodeThermo(int productId, const unsigned char* reply, int n, double* temperature, double* humidity);

}  // namespace ld
