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
    return o;
}
Recording recordingOf(const QJsonObject& o) {
    Recording r;
    r.file = o[QStringLiteral("file")].toString(), r.sample = o[QStringLiteral("sample")].toString();
    r.liquid = o[QStringLiteral("liquid")].toString(), r.date = o[QStringLiteral("date")].toString();
    r.volumeUl = num(o[QStringLiteral("volume_ul")]), r.notes = o[QStringLiteral("notes")].toString();
    for (const QJsonValue& v : o[QStringLiteral("excluded")].toArray()) r.excluded << v.toInt();
    r.glassReference = o[QStringLiteral("glass_reference")].toBool();
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

// The starting liquid table of a new project. Water: standard values (IAPWS; Owens–Wendt split 21.8/51.0).
// The carbonates' values are approximate and still to be checked against literature (marked in "source").
std::vector<LiquidData> defaultLiquids() {
    return {
        {"water", 997.0, 72.0, 0.89, 21.8, 51.0, 25, "IAPWS (ρ, γ, η); Owens–Wendt/Fowkes split 21.8/51.0"},
        {"DMC", 1069, 28.5, kNaN, kNaN, kNaN, 25, "approximate, to be checked"},
        {"DEC", 975, 26.7, kNaN, kNaN, kNaN, 25, "approximate, to be checked"},
        {"PC", 1205, 41.1, kNaN, kNaN, kNaN, 25, "approximate, to be checked"},
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
                                 num(e[QStringLiteral("rate_ul_s")]), e[QStringLiteral("recording")].toString()});
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
        e.append(QJsonObject{{"liquid", x.liquid}, {"date", x.date}, {"rate_ul_s", num(x.rate)}, {"recording", x.recording}});
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
    if (!readCaptureJson(file, &r, &s) || r.date.isEmpty()) r.date = fi.lastModified().toString(QStringLiteral("yyyy-MM-dd"));
    if (!r.sample.isEmpty() && !sample(r.sample)) {
        s.name = r.sample;
        samples.push_back(s);
    }
    recordings.push_back(r);
    if (added) *added = r;
    return true;
}

}  // namespace ld
