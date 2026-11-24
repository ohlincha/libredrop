#include "thermo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>

#if defined(Q_OS_LINUX)
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <setupapi.h>
extern "C" {
#include <hidsdi.h>
}
#elif defined(Q_OS_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#endif

namespace ld {

namespace {

constexpr int kVid = 0x0c45;
const unsigned char kRead[8] = {0x01, 0x80, 0x33, 0x01, 0x00, 0x00, 0x00, 0x00};

QString modelName(int pid) {
    switch (pid) {
    case 0x7401: return QStringLiteral("TEMPer");
    case 0x7402: return QStringLiteral("TEMPerHUM");
    case 0x7403: return QStringLiteral("TH1000iso");
    default: return QStringLiteral("0c45:%1").arg(pid, 4, 16, QLatin1Char('0'));
    }
}

int s16(const unsigned char* b, int i) { return int(short((b[i] << 8) | b[i + 1])); }
int u16(const unsigned char* b, int i) { return (b[i] << 8) | b[i + 1]; }

struct Device {
    int pid = 0;
    QString location;
    std::function<bool(unsigned char* reply, int* n, QString* error)> transfer;   // the read command and its reply
};

// USB Thermo Logger's settings: {"7402@1-7.2": {"name": …, "temperature_offset": …, "humidity_offset": …}}
QJsonObject loggerSettings() {
    QString base;
#if defined(Q_OS_WIN)
    base = qEnvironmentVariable("APPDATA", QDir::homePath());
#elif defined(Q_OS_MACOS)
    base = QDir::homePath() + QStringLiteral("/Library/Application Support");
#else
    base = qEnvironmentVariable("XDG_CONFIG_HOME", QDir::homePath() + QStringLiteral("/.config"));
#endif
    QFile f(base + QStringLiteral("/usb-thermo-logger/sensors.json"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

#if defined(Q_OS_LINUX)
std::vector<Device> devices() {
    std::vector<Device> out;
    const QDir base(QStringLiteral("/sys/class/hidraw"));
    for (const QString& name : base.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString dev = base.filePath(name + QStringLiteral("/device"));
        QFile ue(dev + QStringLiteral("/uevent"));
        if (!ue.open(QIODevice::ReadOnly)) continue;
        int pid = -1;
        for (const QByteArray& line : ue.readAll().split('\n'))
            if (line.startsWith("HID_ID=")) {
                const QList<QByteArray> p = line.mid(7).split(':');
                if (p.size() == 3 && p[1].toInt(nullptr, 16) == kVid) pid = p[2].toInt(nullptr, 16);
            }
        if (pid < 0) continue;
        // …/1-7.2:1.1/0003:0C45:7402.002B: the USB interface is after the last '.' of "1-7.2:1.1"; 0 is the keyboard
        const QString usbif = QFileInfo(QFileInfo(dev).canonicalFilePath()).dir().dirName();
        if (!usbif.contains(QLatin1Char(':')) || usbif.section(QLatin1Char('.'), -1) != QLatin1String("1")) continue;
        Device d;
        d.pid = pid;
        d.location = usbif.section(QLatin1Char(':'), 0, 0);
        const QByteArray path = QStringLiteral("/dev/%1").arg(name).toLocal8Bit();
        d.transfer = [path](unsigned char* reply, int* n, QString* error) {
            const int fd = ::open(path.constData(), O_RDWR);
            if (fd < 0) {
                *error = errno == EACCES ? QStringLiteral("no permission for %1: install USB Thermo Logger's udev rule "
                                                          "(or its .deb) and replug the sensor").arg(QString::fromLocal8Bit(path))
                                         : QStringLiteral("cannot open %1").arg(QString::fromLocal8Bit(path));
                return false;
            }
            unsigned char buf[64];
            pollfd p{fd, POLLIN, 0};
            while (::poll(&p, 1, 0) > 0 && ::read(fd, buf, sizeof buf) > 0) {}   // stale replies
            unsigned char out[9] = {0};                                         // report ID 0, then the command
            std::copy(kRead, kRead + 8, out + 1);
            bool ok = ::write(fd, out, 9) == 9 && ::poll(&p, 1, 1500) > 0;
            const ssize_t got = ok ? ::read(fd, buf, sizeof buf) : -1;
            ::close(fd);
            if (got <= 0) {
                *error = QStringLiteral("no reply");
                return false;
            }
            *n = int(std::min<ssize_t>(got, 8));
            std::copy(buf, buf + *n, reply);
            return true;
        };
        out.push_back(d);
    }
    return out;
}
#elif defined(Q_OS_WIN)
std::vector<Device> devices() {
    std::vector<Device> out;
    GUID guid;
    HidD_GetHidGuid(&guid);
    HDEVINFO info = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (info == INVALID_HANDLE_VALUE) return out;
    SP_DEVICE_INTERFACE_DATA ifd;
    ifd.cbSize = sizeof ifd;
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(info, nullptr, &guid, i, &ifd); ++i) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(info, &ifd, nullptr, 0, &need, nullptr);
        std::vector<char> buf(need + 4);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(info, &ifd, detail, need, nullptr, nullptr)) continue;
        const QString path = QString::fromWCharArray(detail->DevicePath);
        if (!path.contains(QStringLiteral("mi_01"), Qt::CaseInsensitive)) continue;   // interface 0: the keyboard
        HANDLE h = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        HIDD_ATTRIBUTES attr;
        attr.Size = sizeof attr;
        const bool ok = HidD_GetAttributes(h, &attr);
        CloseHandle(h);
        if (!ok || attr.VendorID != kVid) continue;
        Device d;
        d.pid = attr.ProductID;
        d.location = path.count(QLatin1Char('#')) >= 2 ? path.section(QLatin1Char('#'), 2, 2) : path;
        const std::wstring wpath = path.toStdWString();
        d.transfer = [wpath](unsigned char* reply, int* n, QString* error) {
            HANDLE h = CreateFileW(wpath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (h == INVALID_HANDLE_VALUE) {
                *error = QStringLiteral("cannot open the sensor (error %1)").arg(GetLastError());
                return false;
            }
            USHORT inLen = 9, outLen = 9;
            PHIDP_PREPARSED_DATA pp = nullptr;
            if (HidD_GetPreparsedData(h, &pp)) {
                HIDP_CAPS caps;
                if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS) {
                    if (caps.InputReportByteLength) inLen = caps.InputReportByteLength;
                    if (caps.OutputReportByteLength) outLen = caps.OutputReportByteLength;
                }
                HidD_FreePreparsedData(pp);
            }
            auto io = [&](bool write, unsigned char* data, DWORD len, DWORD* done) {
                OVERLAPPED ov{};
                ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                BOOL r = write ? WriteFile(h, data, len, nullptr, &ov) : ReadFile(h, data, len, nullptr, &ov);
                bool ok = r || GetLastError() == ERROR_IO_PENDING;
                if (ok && !r && WaitForSingleObject(ov.hEvent, 1500) != WAIT_OBJECT_0) {
                    CancelIo(h);
                    ok = false;
                }
                ok = ok && GetOverlappedResult(h, &ov, done, TRUE);
                CloseHandle(ov.hEvent);
                return ok;
            };
            std::vector<unsigned char> out(std::max<USHORT>(outLen, 9), 0), in(std::max<USHORT>(inLen, 9), 0);
            std::copy(kRead, kRead + 8, out.begin() + 1);
            DWORD done = 0;
            bool ok = io(true, out.data(), outLen, &done) && io(false, in.data(), inLen, &done) && done > 1;
            CloseHandle(h);
            if (!ok) {
                *error = QStringLiteral("no reply");
                return false;
            }
            *n = int(std::min<DWORD>(done - 1, 8));   // without the report ID byte
            std::copy(in.begin() + 1, in.begin() + 1 + *n, reply);
            return true;
        };
        out.push_back(d);
    }
    SetupDiDestroyDeviceInfoList(info);
    return out;
}
#elif defined(Q_OS_MACOS)
int intProp(IOHIDDeviceRef d, CFStringRef key) {
    auto ref = static_cast<CFNumberRef>(IOHIDDeviceGetProperty(d, key));
    int v = -1;
    if (ref) CFNumberGetValue(ref, kCFNumberSInt32Type, &v);
    return v;
}
struct MacReply {
    unsigned char data[64];
    int n = 0;
};
void onReport(void* ctx, IOReturn, void*, IOHIDReportType, uint32_t, uint8_t* report, CFIndex len) {
    auto* r = static_cast<MacReply*>(ctx);
    r->n = int(std::min<CFIndex>(len, 64));
    std::copy(report, report + r->n, r->data);
}
std::vector<Device> devices() {
    std::vector<Device> out;
    IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    IOHIDManagerSetDeviceMatching(mgr, nullptr);
    CFSetRef set = IOHIDManagerCopyDevices(mgr);
    if (!set) {
        CFRelease(mgr);
        return out;
    }
    const CFIndex count = CFSetGetCount(set);
    std::vector<const void*> devs(static_cast<std::size_t>(count));
    CFSetGetValues(set, devs.data());
    for (const void* v : devs) {
        auto d = static_cast<IOHIDDeviceRef>(const_cast<void*>(v));
        if (intProp(d, CFSTR(kIOHIDVendorIDKey)) != kVid) continue;
        if (intProp(d, CFSTR(kIOHIDPrimaryUsagePageKey)) == 1 && intProp(d, CFSTR(kIOHIDPrimaryUsageKey)) == 6) continue;   // keyboard
        Device x;
        x.pid = intProp(d, CFSTR(kIOHIDProductIDKey));
        x.location = QStringLiteral("%1").arg(uint(intProp(d, CFSTR(kIOHIDLocationIDKey))), 8, 16, QLatin1Char('0'));
        CFRetain(d);
        x.transfer = [d](unsigned char* reply, int* n, QString* error) {
            if (IOHIDDeviceOpen(d, kIOHIDOptionsTypeNone) != kIOReturnSuccess) {
                *error = QStringLiteral("cannot open the sensor (System Settings ▸ Privacy & Security ▸ Input Monitoring)");
                return false;
            }
            MacReply r;
            unsigned char buf[64];
            IOHIDDeviceRegisterInputReportCallback(d, buf, sizeof buf, onReport, &r);
            IOHIDDeviceScheduleWithRunLoop(d, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
            bool ok = IOHIDDeviceSetReport(d, kIOHIDReportTypeOutput, 0, kRead, 8) == kIOReturnSuccess;
            const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
            while (ok && r.n == 0 && std::chrono::steady_clock::now() < end) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
            IOHIDDeviceUnscheduleFromRunLoop(d, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
            IOHIDDeviceRegisterInputReportCallback(d, buf, sizeof buf, nullptr, nullptr);   // (r and buf end here)
            IOHIDDeviceClose(d, kIOHIDOptionsTypeNone);
            if (!ok || r.n == 0) {
                *error = QStringLiteral("no reply");
                return false;
            }
            *n = std::min(r.n, 8);
            std::copy(r.data, r.data + *n, reply);
            return true;
        };
        out.push_back(x);
    }
    CFRelease(set);
    // (mgr is kept: the device references stay valid while the program runs)
    return out;
}
#else
std::vector<Device> devices() { return {}; }
#endif

}  // namespace

bool decodeThermo(int pid, const unsigned char* b, int n, double* t, double* rh) {
    *t = *rh = kNaN;
    if (n < 6) return false;
    if (pid == 0x7402) {   // TEMPERHUM1V1.0: 1/100 °C; humidity as Sensirion SHT1x 12-bit ticks, compensated
        *t = s16(b, 2) / 100.0;
        const double raw = u16(b, 4);
        const double lin = -2.0468 + 0.0367 * raw - 1.5955e-6 * raw * raw;
        *rh = std::clamp((*t - 25.0) * (0.01 + 0.00008 * raw) + lin, 0.0, 100.0);
        return true;
    }
    if (pid == 0x7401 || pid == 0x7403) {   // TEMPer1, TH1000iso (its internal temperature): 1/256 °C
        *t = s16(b, 2) / 256.0;
        return true;
    }
    return false;
}

std::vector<ThermoReading> readThermometers() {
    std::vector<ThermoReading> out;
    const QJsonObject settings = loggerSettings();
    for (const Device& d : devices()) {
        ThermoReading r;
        r.model = modelName(d.pid);
        r.location = d.location;
        const QJsonObject c = settings[QStringLiteral("%1@%2").arg(d.pid, 4, 16, QLatin1Char('0')).arg(d.location)].toObject();
        r.name = c[QStringLiteral("name")].toString();
        unsigned char reply[8] = {0};
        int n = 0;
        double t, h;
        if (d.transfer(reply, &n, &r.error) && decodeThermo(d.pid, reply, n, &t, &h)) {
            r.temperature = t + c[QStringLiteral("temperature_offset")].toDouble(0);
            if (std::isfinite(h)) r.humidity = std::clamp(h + c[QStringLiteral("humidity_offset")].toDouble(0), 0.0, 100.0);
        } else if (r.error.isEmpty()) {
            r.error = QStringLiteral("unexpected reply");
        }
        out.push_back(r);
    }
    return out;
}

ThermoReading roomConditions(const std::vector<ThermoReading>& r) {
    for (const ThermoReading& x : r)
        if (std::isfinite(x.humidity)) return x;
    for (const ThermoReading& x : r)
        if (std::isfinite(x.temperature)) return x;
    return {};
}

}  // namespace ld
