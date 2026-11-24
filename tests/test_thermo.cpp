// USB thermometer replies decoded (values checked on hardware with USB Thermo Logger), and reading whatever is
// attached (none on the build machines: an empty list, no crash).
#include <QtTest>
#include <cmath>

#include "core/thermo.h"

using namespace ld;

class TestThermo : public QObject {
    Q_OBJECT
private slots:
    void decode() {
        double t, h;
        // TEMPerHUM: 25.50 °C (0x09F6), 727 humidity ticks = 23.8 %RH (reference hygrometer)
        const unsigned char hum[8] = {0x80, 0x04, 0x09, 0xF6, 0x02, 0xD7, 0x00, 0x00};
        QVERIFY(decodeThermo(0x7402, hum, 8, &t, &h));
        QCOMPARE(t, 25.5);
        QVERIFY(std::abs(h - 23.8) < 0.2);
        // TEMPer: 1/256 °C, negative too
        const unsigned char tp[8] = {0x80, 0x02, 0xFE, 0x80, 0x00, 0x00, 0x00, 0x00};
        QVERIFY(decodeThermo(0x7401, tp, 8, &t, &h));
        QCOMPARE(t, -1.5);
        QVERIFY(!std::isfinite(h));
        QVERIFY(!decodeThermo(0x1234, tp, 8, &t, &h));
        QVERIFY(!decodeThermo(0x7402, hum, 4, &t, &h));
    }
    void attached() {
        const auto r = readThermometers();
        for (const auto& x : r) qInfo("%s at %s: %.2f °C, %.1f %%RH %s", qPrintable(x.model), qPrintable(x.location),
                                      x.temperature, x.humidity, qPrintable(x.error));
        const ThermoReading c = roomConditions(r);
        QVERIFY(r.empty() || !c.model.isEmpty() || !r.front().error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestThermo)
#include "test_thermo.moc"
