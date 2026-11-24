#include "project.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cmath>

namespace ld {

namespace {

QJsonValue num(double v) { return std::isfinite(v) ? QJsonValue(v) : QJsonValue(); }
double num(const QJsonValue& v) { return v.isDouble() ? v.toDouble() : kNaN; }

QJsonObject toJson(const Sample& s) {
    QJsonObject o{{"name", s.name}};
    auto put = [&](const char* k, const QString& v) { if (!v.isEmpty()) o[QLatin1String(k)] = v; };
    put("material", s.material), put("batch", s.batch), put("side", s.side), put("direction", s.direction);
    put("treatment", s.treatment), put("notes", s.notes);
    if (s.control) o[QStringLiteral("control")] = true;
    return o;
}
Sample sampleOf(const QJsonObject& o) {
    Sample s;
    s.name = o[QStringLiteral("name")].toString();
    s.material = o[QStringLiteral("material")].toString(), s.batch = o[QStringLiteral("batch")].toString();
    s.side = o[QStringLiteral("side")].toString(), s.direction = o[QStringLiteral("direction")].toString();
    s.treatment = o[QStringLiteral("treatment")].toString(), s.notes = o[QStringLiteral("notes")].toString();
    s.control = o[QStringLiteral("control")].toBool();
    return s;
}
QJsonObject toJson(const Recording& r) {
    QJsonObject o{{"file", r.file}, {"sample", r.sample}, {"liquid", r.liquid}, {"date", r.date}};
    if (std::isfinite(r.volumeUl)) o[QStringLiteral("volume_ul")] = r.volumeUl;
    if (!r.notes.isEmpty()) o[QStringLiteral("notes")] = r.notes;
    if (!r.excluded.isEmpty()) {
        QJsonArray a;
        for (int id : r.excluded) a.append(id);
        o[QStringLiteral("excluded")] = a;
    }
    if (r.glassReference) o[QStringLiteral("glass_reference")] = true;
    if (!r.started.isEmpty()) o[QStringLiteral("started")] = r.started;
    if (std::isfinite(r.temperature)) o[QStringLiteral("temperature_c")] = r.temperature;
    if (std::isfinite(r.humidity)) o[QStringLiteral("humidity_pct")] = r.humidity;
    return o;
}
Recording recordingOf(const QJsonObject& o) {
    Recording r;
    r.file = o[QStringLiteral("file")].toString(), r.sample = o[QStringLiteral("sample")].toString();
    r.liquid = o[QStringLiteral("liquid")].toString(), r.date = o[QStringLiteral("date")].toString();
    r.volumeUl = num(o[QStringLiteral("volume_ul")]), r.notes = o[QStringLiteral("notes")].toString();
    for (const QJsonValue& v : o[QStringLiteral("excluded")].toArray()) r.excluded << v.toInt();
    r.glassReference = o[QStringLiteral("glass_reference")].toBool();
    r.started = o[QStringLiteral("started")].toString();
    r.temperature = num(o[QStringLiteral("temperature_c")]), r.humidity = num(o[QStringLiteral("humidity_pct")]);
    return r;
}
QJsonObject toJson(const LiquidData& l) {
    return {{"name", l.name}, {"density", num(l.density)}, {"tension", num(l.tension)}, {"viscosity", num(l.viscosity)},
            {"dispersive", num(l.dispersive)}, {"polar", num(l.polar)}, {"temperature", l.temperature}, {"source", l.source}};
}
LiquidData liquidOf(const QJsonObject& o) {
    LiquidData l;
    l.name = o[QStringLiteral("name")].toString();
    l.density = num(o[QStringLiteral("density")]), l.tension = num(o[QStringLiteral("tension")]);
    l.viscosity = num(o[QStringLiteral("viscosity")]), l.dispersive = num(o[QStringLiteral("dispersive")]);
    l.polar = num(o[QStringLiteral("polar")]);
    l.temperature = o[QStringLiteral("temperature")].toDouble(25);
    l.source = o[QStringLiteral("source")].toString();
    return l;
}

// The starting liquid table of a new project, from literature (collected 2026-11-25; see each "source").
// Test-liquid splits (dispersive/polar) are Ström, Fredriksson & Stenius 1987 (J. Colloid Interface Sci. 119, 352),
// defined at 20 °C and checked only through citing papers. NaN: no sourced value found.
std::vector<LiquidData> defaultLiquids() {
    const double n = kNaN;
    return {
        {"water", 997.05, 71.97, 0.890, 21.8, 51.0, 25,
         "ρ, η: CRC/IAPWS; γ: IAPWS, Vargaftik 1983 doi:10.1063/1.555688; split 21.8/51.0: Ström 1987 (at 72.8, 20 °C), "
         "Owens–Wendt 1969 doi:10.1002/app.1969.070130815"},
        {"diiodomethane", 3325, 50.8, n, 50.8, 0.0, 20,
         "γ, split: Ström 1987 (20 °C); measured 50.0–50.5 at 23 °C (doi:10.1021/acs.langmuir.3c00910, "
         "doi:10.1016/j.jcis.2018.06.044); ρ: CRC. uncertain: other splits in use (49.0/1.8, 44.1/6.7)"},
        {"ethylene glycol", 1110, 47.99, 16.1, 30.9, 16.8, 25,
         "γ, η: CRC 91st (25 °C); ρ estimated for 25 °C from CRC 1113.5 (20 °C). uncertain: split 30.9/16.8 (Ström 1987, "
         "not checked; others 29/19, 32.8/16.0)"},
        {"formamide", 1133, 58.2, n, 39.0, 19.0, 20,
         "ρ: CRC (20 °C); γ: 58.2 (20 °C), 58.2 at 23 °C doi:10.1021/acs.langmuir.3c00910. uncertain: split 39.0/19.0 "
         "(Ström 1987, secondary only)"},
        {"glycerol", 1261, 64.0, 934, 34.0, 30.0, 20,
         "ρ: CRC (20 °C); γ: 64.0 (Ström 1987); η: 934 at 25 °C (CRC; 954 elsewhere), depends strongly on water "
         "content. uncertain: split 34/30 (secondary only)"},
        {"n-hexadecane", 770.1, 27.47, 3.03, 27.47, 0.0, 25,
         "ρ: CRC 95th; γ: 27.47 (ECHA), 27.6 at 23 °C doi:10.1021/acs.langmuir.3c00910; η: CRC 99th; purely dispersive"},
        {"DMC", 1063.4, 28.08, 0.59, n, n, 25,
         "ρ, γ: doi:10.1021/je800594e (25 °C); γ 28.55 at 23 °C doi:10.1016/j.jcis.2018.06.044; η: Xu 2004 "
         "doi:10.1021/cr030203g, Logan 2018 doi:10.1149/2.0271802jes. uncertain: η 0.59–0.66 in sources"},
        {"DEC", 969.2, 26.26, 0.75, n, n, 25,
         "ρ: doi:10.1016/j.jct.2008.12.010; γ: Zhao 2010 doi:10.1016/j.fluid.2010.03.040 (25.43 in j.jct.2008.12.010); "
         "η: Xu 2004 doi:10.1021/cr030203g"},
        {"EMC", 1006, n, 0.65, n, n, 25,
         "ρ, η: Xu 2004 doi:10.1021/cr030203g, Logan 2018. γ: no peer-reviewed value found"},
        {"PC", 1200, 41.3, 2.53, n, n, 25,
         "ρ, η: Xu 2004 doi:10.1021/cr030203g; γ: 41.28 at 23 °C doi:10.1016/j.jcis.2018.06.044"},
        {"EC", 1321, n, 1.90, n, n, 40,
         "40 °C (solid below 36.4 °C). ρ: CRC 79th (39 °C), Xu 2004; η: Xu 2004. γ: in Naejus 1997 J. Chem. Thermodyn. "
         "29, 1503 (not read)"},
        {"LP30", 1303, 29.4, n, n, n, 25,
         "1 M LiPF6 in EC:DMC 1:1. Hofmann 2015 doi:10.3390/ijms160920258 (0.787 mol/kg, i.e. ~0.9 M; pendant drop, "
         "glovebox); 30.3 in Dahbi 2012 doi:10.1021/ie203066x (secondary). η: 4.4 at 20 °C (Hofmann)"},
        {"LP57", 1200, n, 2.75, n, n, 25,
         "1 M LiPF6 in EC:EMC 3:7. uncertain: ρ quoted secondhand; η interpolated from Logan 2018 "
         "doi:10.1149/2.0271802jes (3.05 at 20 °C, 2.47 at 30 °C, 1.0 mol/kg). γ: not found"},
    };
}

}  // namespace

bool Project::create(const QString& dir, const QString& name, Project* out, QString* error) {
    QDir d(dir);
    if (QFileInfo::exists(d.filePath(QStringLiteral("project.json")))) {
        *error = QStringLiteral("%1 already holds a project").arg(dir);
        return false;
    }
    if (!d.mkpath(QStringLiteral("recordings")) || !d.mkpath(QStringLiteral("results"))) {
        *error = QStringLiteral("cannot create folders in %1").arg(dir);
        return false;
    }
    Project p;
    p.dir = d.absolutePath();
    p.name = name.isEmpty() ? d.dirName() : name;
    p.liquids = defaultLiquids();
    if (!p.save(error)) return false;
    *out = std::move(p);
    return true;
}

bool Project::load(const QString& dir, Project* out, QString* error) {
    QDir d(dir);
    QFile f(d.filePath(QStringLiteral("project.json")));
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("no project.json in %1").arg(dir);
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (!doc.isObject()) {
        *error = QStringLiteral("project.json: %1").arg(pe.errorString());
        return false;
    }
    const QJsonObject o = doc.object();
    Project p;
    p.dir = d.absolutePath();
    p.name = o[QStringLiteral("name")].toString(d.dirName());
    for (const QJsonValue& v : o[QStringLiteral("samples")].toArray()) p.samples.push_back(sampleOf(v.toObject()));
    for (const QJsonValue& v : o[QStringLiteral("recordings")].toArray()) p.recordings.push_back(recordingOf(v.toObject()));
    for (const QJsonValue& v : o[QStringLiteral("liquids")].toArray()) p.liquids.push_back(liquidOf(v.toObject()));
    for (const QJsonValue& v : o[QStringLiteral("evaporation")].toArray()) {
        const QJsonObject e = v.toObject();
        p.evaporation.push_back({e[QStringLiteral("liquid")].toString(), e[QStringLiteral("date")].toString(),
                                 num(e[QStringLiteral("rate_ul_s")]), e[QStringLiteral("recording")].toString(),
                                 num(e[QStringLiteral("temperature_c")]), num(e[QStringLiteral("humidity_pct")])});
    }
    for (const QJsonValue& v : o[QStringLiteral("calibrations")].toArray()) {
        const QJsonObject c = v.toObject();
        p.calibrations.push_back({c[QStringLiteral("frame_width")].toInt(), num(c[QStringLiteral("px_per_mm")]),
                                  num(c[QStringLiteral("needle_mm")]), c[QStringLiteral("date")].toString(),
                                  c[QStringLiteral("method")].toString()});
    }
    *out = std::move(p);
    return true;
}

bool Project::save(QString* error) const {
    QJsonArray s, r, l, e, c;
    for (const Sample& x : samples) s.append(toJson(x));
    for (const Recording& x : recordings) r.append(toJson(x));
    for (const LiquidData& x : liquids) l.append(toJson(x));
    for (const EvaporationRef& x : evaporation)
        e.append(QJsonObject{{"liquid", x.liquid}, {"date", x.date}, {"rate_ul_s", num(x.rate)}, {"recording", x.recording},
                             {"temperature_c", num(x.temperature)}, {"humidity_pct", num(x.humidity)}});
    for (const Calibration& x : calibrations)
        c.append(QJsonObject{{"frame_width", x.frameWidth}, {"px_per_mm", num(x.pxPerMm)}, {"needle_mm", num(x.needleMm)},
                             {"date", x.date}, {"method", x.method}});
    const QJsonObject o{{"libredrop_project", 1}, {"name", name}, {"samples", s}, {"recordings", r}, {"liquids", l},
                        {"evaporation", e}, {"calibrations", c}};
    QSaveFile f(QDir(dir).filePath(QStringLiteral("project.json")));   // written whole or not at all
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(o).toJson()) < 0 || !f.commit()) {
        *error = QStringLiteral("cannot write %1").arg(f.fileName());
        return false;
    }
    return true;
}

QString Project::absolute(const QString& rel) const { return QDir(dir).absoluteFilePath(rel); }
QString Project::relative(const QString& abs) const { return QDir(dir).relativeFilePath(abs); }
QString Project::resultPath(const Recording& r) const {
    return QDir(dir).filePath(QStringLiteral("results/") + QFileInfo(r.file).completeBaseName() + QStringLiteral(".csv"));
}

Sample* Project::sample(const QString& n) {
    for (Sample& s : samples)
        if (s.name == n) return &s;
    return nullptr;
}

const LiquidData* Project::liquid(const QString& n) const {
    for (const LiquidData& l : liquids)
        if (l.name.compare(n, Qt::CaseInsensitive) == 0) return &l;
    return nullptr;
}

EvaporationRef Project::evaporationFor(const QString& liquid, const QString& date) const {
    EvaporationRef best;
    for (const EvaporationRef& e : evaporation) {
        if (e.liquid.compare(liquid, Qt::CaseInsensitive) != 0 || !std::isfinite(e.rate)) continue;
        if (e.date == date) return e;
        if (e.date < date && (best.date.isEmpty() || e.date > best.date)) best = e;
    }
    return best;
}

Calibration Project::calibrationFor(int w) const {
    Calibration best;
    for (const Calibration& c : calibrations)
        if (c.frameWidth == w && std::isfinite(c.pxPerMm) && (best.date.isEmpty() || c.date >= best.date)) best = c;
    return best;
}

bool readCaptureJson(const QString& video, Recording* r, Sample* s) {
    const QFileInfo fi(video);
    QFile f(fi.dir().filePath(fi.completeBaseName() + QStringLiteral(".json")));
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.isEmpty()) return false;
    const QString started = o[QStringLiteral("started")].toString();
    if (started.size() >= 10) r->date = started.left(10);
    if (started.size() >= 19) r->started = started.left(19);
    const QJsonObject m = o[QStringLiteral("sample")].toObject();
    s->name = m[QStringLiteral("sample")].toString();
    s->batch = m[QStringLiteral("batch")].toString();
    s->side = m[QStringLiteral("side")].toString();
    s->direction = m[QStringLiteral("direction")].toString();
    s->control = m[QStringLiteral("control")].toBool();
    r->liquid = m[QStringLiteral("liquid")].toString();
    r->volumeUl = num(m[QStringLiteral("volume_ul")]);
    r->notes = o[QStringLiteral("note")].toString();
    // a sample is side and direction of a material: name it so unless given whole
    if (!s->name.isEmpty()) {
        QStringList parts{s->name};
        if (!s->side.isEmpty()) parts << s->side;
        if (!s->direction.isEmpty()) parts << s->direction;
        r->sample = parts.join(QLatin1Char(' '));
    }
    return true;
}

bool Project::addRecording(const QString& path, bool copy, Recording* added, QString* error) {
    const QFileInfo fi(path);
    if (!fi.exists()) {
        *error = QStringLiteral("%1 does not exist").arg(path);
        return false;
    }
    QString file = fi.absoluteFilePath();
    const bool inside = !relative(file).startsWith(QStringLiteral(".."));
    if (copy && !inside) {
        const QString dst = QDir(dir).filePath(QStringLiteral("recordings/") + fi.fileName());
        if (QFileInfo::exists(dst)) {
            *error = QStringLiteral("recordings/%1 already exists").arg(fi.fileName());
            return false;
        }
        if (!QFile::copy(file, dst)) {
            *error = QStringLiteral("cannot copy %1").arg(path);
            return false;
        }
        const QString json = fi.dir().filePath(fi.completeBaseName() + QStringLiteral(".json"));
        if (QFileInfo::exists(json)) QFile::copy(json, QDir(dir).filePath(QStringLiteral("recordings/") + fi.completeBaseName() + QStringLiteral(".json")));
        file = dst;
    }
    for (const Recording& x : recordings)
        if (absolute(x.file) == file) {
            *error = QStringLiteral("%1 is already in the project").arg(fi.fileName());
            return false;
        }
    Recording r;
    r.file = relative(file);
    Sample s;
    if (!readCaptureJson(file, &r, &s) || r.date.isEmpty()) r.date = QFileInfo(file).lastModified().toString(QStringLiteral("yyyy-MM-dd"));
    if (r.started.isEmpty()) r.started = QFileInfo(file).lastModified().toString(Qt::ISODate).left(19);   // (the end, roughly)
    fillConditions(r);
    if (!r.sample.isEmpty() && !sample(r.sample)) {
        s.name = r.sample;
        samples.push_back(s);
    }
    recordings.push_back(r);
    if (added) *added = r;
    return true;
}

void Project::logConditions(const QString& sensor, double t, double h) const {
    QFile f(QDir(dir).filePath(QStringLiteral("conditions.csv")));
    const bool fresh = !f.exists();
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    if (fresh) f.write("time,sensor,temperature_c,humidity_pct\n");
    f.write(QStringLiteral("%1,%2,%3,%4\n")
                .arg(QDateTime::currentDateTime().toString(Qt::ISODate).left(19), QString(sensor).replace(QLatin1Char(','), QLatin1Char(' ')),
                     std::isfinite(t) ? QString::number(t, 'f', 2) : QString(), std::isfinite(h) ? QString::number(h, 'f', 1) : QString())
                .toUtf8());
}

bool Project::conditionsAt(const QString& iso, double* t, double* h) const {
    const QDateTime at = QDateTime::fromString(iso.left(19), Qt::ISODate);
    QFile f(QDir(dir).filePath(QStringLiteral("conditions.csv")));
    if (!at.isValid() || !f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    qint64 best = 15 * 60 + 1;
    bool found = false;
    while (!f.atEnd()) {
        const QList<QByteArray> c = f.readLine().trimmed().split(',');
        if (c.size() < 4) continue;
        const QDateTime when = QDateTime::fromString(QString::fromUtf8(c[0]), Qt::ISODate);
        if (!when.isValid()) continue;
        const qint64 d = std::abs(when.secsTo(at));
        if (d < best) {
            bool okT = false, okH = false;
            const double tv = c[2].toDouble(&okT), hv = c[3].toDouble(&okH);
            if (!okT) continue;
            best = d, found = true;
            *t = tv, *h = okH ? hv : kNaN;
        }
    }
    return found;
}

bool Project::fillConditions(Recording& r) const {
    if (std::isfinite(r.temperature) || r.started.isEmpty()) return false;
    double t, h;
    if (!conditionsAt(r.started, &t, &h)) return false;
    r.temperature = t, r.humidity = h;
    return true;
}

}  // namespace ld
