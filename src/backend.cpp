#include "backend.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocale>
#include <QRegularExpression>
#include <cmath>

#include "figure.h"
#include "filepicker.h"
#include "portalfilepicker.h"

namespace {
// Positions turn with the frame: a clockwise quarter turn of a frame of
// height h moves (x, y) to (h - 1 - y, x).
QPointF turn(QPointF p, int direction, int width, int height) {
    return direction > 0 ? QPointF(height - 1 - p.y(), p.x()) : QPointF(p.y(), width - 1 - p.x());
}

QString formatNumber(double value, int decimals) {
    return std::isfinite(value) ? QLocale().toString(value, 'f', decimals) : QString();
}
} // namespace

Backend::Backend(QObject *parent) : Backend(new PortalFilePicker(), parent) { m_picker->setParent(this); }

// A picker passed in stays its owner's; tests keep theirs on the stack.
Backend::Backend(FilePicker *picker, QObject *parent) : QObject(parent), m_picker(picker) {
    connect(m_picker, &FilePicker::openSelected, this, [this](const QUrl &url) { openPath(url.toLocalFile()); });
    connect(m_picker, &FilePicker::exportSelected, this, [this](const QUrl &url) {
        exportTo(url.toLocalFile(), m_pendingExport.format, m_pendingExport.dpi, m_pendingExport.widthMm);
    });
    connect(m_picker, &FilePicker::failed, this, [this](const QString &message) { setStatus(message, true); });
}

QString Backend::fileName() const { return hasImage() ? QFileInfo(m_image.path()).fileName() : QString(); }

QString Backend::imageInfo() const {
    if (!hasImage())
        return {};
    return tr("%1 × %2 px, %3-bit").arg(m_image.width()).arg(m_image.height()).arg(m_image.bitDepth());
}

QRectF Backend::crop() const { return hasImage() ? Figure::effectiveCrop(m_session, m_frame) : QRectF(); }

QImage Backend::displayImage() const {
    QMutexLocker lock(&m_displayMutex);
    return m_display;
}

bool Backend::dirty() const { return hasImage() && savedJson() != m_saved; }

QByteArray Backend::savedJson() const { return QJsonDocument(m_session.toJson()).toJson(QJsonDocument::Compact); }

void Backend::setStatus(const QString &message, bool error) {
    m_status = message;
    m_statusIsError = error;
    emit statusChanged();
}

// ---- Files ----------------------------------------------------------------

void Backend::openDialog() { m_picker->openImage(); }

bool Backend::openPath(const QString &path) {
    if (path.isEmpty())
        return false;
    QString error;
    Session session;
    QString sessionPath;
    const bool isSession = QFileInfo(path).suffix().toLower() == "gel";
    const QString beside = Session::sessionPathFor(path);
    if (isSession || QFileInfo::exists(beside)) {
        sessionPath = QFileInfo(isSession ? path : beside).absoluteFilePath();
        session = Session::load(sessionPath, &error);
        if (!error.isEmpty()) {
            setStatus(error, true);
            return false;
        }
        if (!isSession)
            session.imagePath = QFileInfo(path).absoluteFilePath();
    } else {
        session.imagePath = QFileInfo(path).absoluteFilePath();
        const LadderPreset preset = ::ladderPresets().first();
        session.ladder = {-1, preset.name, preset.sizes, preset.unit};
    }

    GelImage image;
    if (!image.load(session.imagePath, &error)) {
        setStatus(error, true);
        return false;
    }
    const bool changed = !session.imageSha256.isEmpty() && session.imageSha256 != image.sha256();
    session.imageSha256 = image.sha256();
    m_image = std::move(image);
    reset(session, sessionPath);
    if (changed)
        setStatus(tr("The image has changed since this session was saved."), true);
    else if (!sessionPath.isEmpty())
        setStatus(tr("Opened %1").arg(QFileInfo(sessionPath).fileName()));
    else
        setStatus(tr("Press Space to find lanes and bands."));
    return true;
}

void Backend::reset(const Session &session, const QString &sessionPath) {
    m_session = session;
    m_sessionPath = sessionPath;
    m_undo.clear();
    m_redo.clear();
    m_undoKey.clear();
    m_selectedLane = m_selectedBand = m_selectedAnnotation = -1;
    m_levels = autoLevels(m_image);
    // Nothing to save until the first edit, whether or not a session exists.
    m_saved = savedJson();
    m_wasDirty = false;
    rebuildFrame();
    refresh(Change::Data);
    emit imageChanged();
    emit selectionChanged();
    emit historyChanged();
}

bool Backend::save() {
    if (!hasImage())
        return false;
    const QString path = m_sessionPath.isEmpty() ? Session::sessionPathFor(m_image.path()) : m_sessionPath;
    QString error;
    if (!m_session.save(path, &error)) {
        setStatus(error, true);
        return false;
    }
    m_sessionPath = path;
    m_saved = savedJson();
    m_wasDirty = false;
    emit dirtyChanged();
    setStatus(tr("Saved %1").arg(QFileInfo(path).fileName()));
    return true;
}

void Backend::exportDialog(const QString &format, int dpi, double widthMm) {
    if (!hasImage())
        return;
    m_pendingExport = {format, dpi, widthMm};
    const QFileInfo info(m_image.path());
    const QString suffix = format == "csv" ? "-results.csv" : "-figure." + format;
    m_picker->exportFile(QUrl::fromLocalFile(info.absoluteDir().filePath(info.completeBaseName() + suffix)), format);
}

bool Backend::exportTo(const QString &path, const QString &format, int dpi, double widthMm) {
    if (!hasImage() || path.isEmpty())
        return false;
    QString target = path;
    if (QFileInfo(target).suffix().isEmpty())
        target += "." + format;
    QString error;
    if (!Figure::exportTo(target, format, m_image, m_session, {widthMm, dpi}, &error)) {
        setStatus(error, true);
        return false;
    }
    setStatus(tr("Exported %1").arg(QFileInfo(target).fileName()));
    return true;
}

// ---- Editing ----------------------------------------------------------------

bool Backend::sameUndoKey(const QString &key) {
    const QDateTime now = QDateTime::currentDateTime();
    const bool same = !key.isEmpty() && key == m_undoKey && m_undoTime.msecsTo(now) < 1500;
    m_undoKey = key;
    m_undoTime = now;
    return same;
}

template <typename Edit> void Backend::edit(const QString &key, Change change, Edit &&apply) {
    if (!hasImage())
        return;
    const Session before = m_session;
    apply();
    if (m_session.toJson() == before.toJson())
        return;
    if (!sameUndoKey(key) || m_undo.isEmpty())
        m_undo << before;
    if (m_undo.size() > 200)
        m_undo.removeFirst();
    m_redo.clear();
    emit historyChanged();
    refresh(change);
}

void Backend::refresh(Change change) {
    if (change == Change::Frame)
        rebuildFrame();
    // Measure before anything is announced, so whatever QML asks for next
    // already matches the lanes.
    if (change != Change::View) {
        m_results = hasImage() ? Analysis::quantify(m_frame, m_session) : Analysis::Results{};
        clampSelection();
    }
    renderDisplay();
    emit sessionChanged();
    if (dirty() != m_wasDirty) {
        m_wasDirty = dirty();
        emit dirtyChanged();
    }
}

void Backend::rebuildFrame() {
    m_frame = Frame(&m_image, m_session.view.quarterTurns, m_session.view.angle);
    m_frameValues = m_frame.render();
}

void Backend::renderDisplay() {
    if (!hasImage())
        return;
    const auto &v = m_session.view;
    QImage display = ::renderDisplay(m_frameValues, m_frame.width(), m_frame.height(), m_levels, v.invert,
                                     v.brightness, v.contrast);
    m_annotationColor = Figure::annotationColor(display, crop()) == QColor(Qt::white) ? "#ffffff" : "#000000";
    {
        QMutexLocker lock(&m_displayMutex);
        m_display = display;
    }
    ++m_revision;
    emit displayChanged();
}

void Backend::clampSelection() {
    const int lanes = int(m_session.lanes.size());
    if (m_selectedLane >= lanes)
        m_selectedLane = -1;
    if (m_selectedLane < 0 || m_selectedBand >= m_session.lanes[m_selectedLane].bands.size())
        m_selectedBand = -1;
    if (m_selectedAnnotation >= m_session.annotations.size())
        m_selectedAnnotation = -1;
    emit selectionChanged();
}

// Lanes stay in order across the gel, and everything that points at a lane
// by its index follows it.
void Backend::sortLanes() {
    QList<int> order;
    for (int i = 0; i < m_session.lanes.size(); ++i)
        order << i;
    std::stable_sort(order.begin(), order.end(),
                     [this](int a, int b) { return m_session.lanes[a].center < m_session.lanes[b].center; });
    QList<Lane> sorted;
    QList<int> newIndex(order.size());
    for (int i = 0; i < order.size(); ++i) {
        sorted << m_session.lanes[order[i]];
        newIndex[order[i]] = i;
    }
    auto remap = [&](int &index) {
        if (index >= 0 && index < newIndex.size())
            index = newIndex[index];
    };
    remap(m_session.ladder.lane);
    remap(m_session.quant.referenceLane);
    remap(m_selectedLane);
    m_session.lanes = sorted;
}

void Backend::detect(bool lanesToo) {
    if (!hasImage())
        return;
    const bool dark = Analysis::resolveDark(m_image, m_session.quant.polarity);
    edit({}, Change::Data, [&] {
        if (lanesToo || m_session.lanes.isEmpty()) {
            m_session.lanes = Analysis::detectLanes(m_frame, dark, m_session.view.crop);
            if (m_session.ladder.lane >= m_session.lanes.size())
                m_session.ladder.lane = -1;
        }
        // Bands picked by hand stay; detected ones are found again around them.
        for (auto &lane : m_session.lanes) {
            QList<Band> bands;
            for (const auto &band : lane.bands)
                if (band.manual)
                    bands << band;
            const auto profile = Analysis::laneProfile(m_frame, lane, dark, m_session.quant);
            for (const auto &found : Analysis::detectBands(profile)) {
                const bool overlaps = std::any_of(bands.begin(), bands.end(), [&](const Band &b) {
                    return found.position >= b.lower && found.position <= b.upper;
                });
                if (!overlaps)
                    bands << found;
            }
            std::sort(bands.begin(), bands.end(), [](const Band &a, const Band &b) { return a.position < b.position; });
            lane.bands = bands;
        }
    });
    int bands = 0;
    for (const auto &lane : m_session.lanes)
        bands += lane.bands.size();
    if (m_session.lanes.isEmpty())
        setStatus(tr("No lanes found. Press L to add one under the pointer."), true);
    else
        setStatus(tr("Found %1 and %2.").arg(tr("%n lanes", "", int(m_session.lanes.size())), tr("%n bands", "", bands)));
}

int Backend::addLane(double x) {
    if (!hasImage())
        return -1;
    const QRectF area = crop();
    double width = std::max(8.0, area.width() / 20);
    double top = area.top(), bottom = area.bottom() - 1;
    if (!m_session.lanes.isEmpty()) {
        std::vector<double> widths;
        for (const auto &lane : m_session.lanes)
            widths.push_back(lane.width);
        std::nth_element(widths.begin(), widths.begin() + widths.size() / 2, widths.end());
        width = widths[widths.size() / 2];
        top = m_session.lanes.first().top;
        bottom = m_session.lanes.first().bottom;
    }
    x = std::clamp(x, width / 2, m_frame.width() - width / 2);
    edit({}, Change::Data, [&] {
        Lane lane;
        lane.center = x;
        lane.width = width;
        lane.top = top;
        lane.bottom = bottom;
        lane.label = QString::number(m_session.lanes.size() + 1);
        m_session.lanes << lane;
        m_selectedLane = int(m_session.lanes.size()) - 1;
        m_selectedBand = -1;
        m_selectedAnnotation = -1;
        sortLanes();
    });
    return m_selectedLane;
}

void Backend::removeLane(int lane) {
    if (lane < 0 || lane >= m_session.lanes.size())
        return;
    edit({}, Change::Data, [&] {
        m_session.lanes.removeAt(lane);
        auto shift = [lane](int &index) {
            if (index == lane)
                index = -1;
            else if (index > lane)
                --index;
        };
        shift(m_session.ladder.lane);
        shift(m_session.quant.referenceLane);
        m_selectedLane = -1;
        m_selectedBand = -1;
    });
}

void Backend::setLaneGeometry(int index, double center, double width, double top, double bottom,
                              const QString &undoKey) {
    if (index < 0 || index >= m_session.lanes.size())
        return;
    edit(undoKey, Change::Data, [&] {
        Lane &lane = m_session.lanes[index];
        lane.width = std::clamp(width, 2.0, double(m_frame.width()));
        lane.center = std::clamp(center, lane.width / 2 - .5, m_frame.width() - .5 - lane.width / 2);
        lane.top = std::clamp(std::round(std::min(top, bottom)), 0.0, double(m_frame.height() - 2));
        lane.bottom = std::clamp(std::round(std::max(top, bottom)), lane.top + 1, double(m_frame.height() - 1));
        // Bands that no longer sit in the lane go; the rest are clipped to it.
        QList<Band> kept;
        for (Band band : lane.bands) {
            if (band.position < lane.top || band.position > lane.bottom)
                continue;
            band.lower = std::max(band.lower, lane.top);
            band.upper = std::min(band.upper, lane.bottom);
            kept << band;
        }
        lane.bands = kept;
        m_selectedLane = index;
        sortLanes();
    });
}

void Backend::setLaneLabel(int lane, const QString &label) {
    if (lane < 0 || lane >= m_session.lanes.size())
        return;
    edit(QString("label%1").arg(lane), Change::Data, [&] { m_session.lanes[lane].label = label.trimmed(); });
}

void Backend::toggleLadder(int lane) {
    if (lane < 0 || lane >= m_session.lanes.size())
        return;
    edit({}, Change::Data, [&] {
        m_session.ladder.lane = m_session.ladder.lane == lane ? -1 : lane;
        if (m_session.ladder.sizes.isEmpty()) {
            const LadderPreset preset = ::ladderPresets().first();
            m_session.ladder = {m_session.ladder.lane, preset.name, preset.sizes, preset.unit};
        }
    });
}

int Backend::laneAt(double x) const {
    for (int i = 0; i < m_session.lanes.size(); ++i) {
        const auto &lane = m_session.lanes[i];
        if (std::abs(x - lane.center) <= lane.width / 2)
            return i;
    }
    return -1;
}

void Backend::toggleBandAt(int laneIndex, double y) {
    if (laneIndex < 0 || laneIndex >= m_session.lanes.size() || laneIndex >= m_results.profiles.size())
        return;
    const Lane &lane = m_session.lanes[laneIndex];
    if (y < lane.top || y > lane.bottom)
        return;
    for (int b = 0; b < lane.bands.size(); ++b)
        if (y >= lane.bands[b].lower - .5 && y <= lane.bands[b].upper + .5) {
            edit({}, Change::Data, [&] {
                m_session.lanes[laneIndex].bands.removeAt(b);
                m_selectedLane = laneIndex;
                m_selectedBand = -1;
            });
            setStatus(tr("Removed a band from lane %1.").arg(lane.label));
            return;
        }
    Band band = Analysis::bandAt(m_results.profiles[laneIndex], y);
    edit({}, Change::Data, [&] {
        QList<Band> &bands = m_session.lanes[laneIndex].bands;
        // A band picked next to others gives way at their edges.
        for (const auto &other : bands) {
            if (other.upper < band.position)
                band.lower = std::max(band.lower, other.upper + 1);
            if (other.lower > band.position)
                band.upper = std::min(band.upper, other.lower - 1);
        }
        band.position = std::clamp(band.position, band.lower, band.upper);
        bands << band;
        std::sort(bands.begin(), bands.end(), [](const Band &a, const Band &b) { return a.position < b.position; });
        m_selectedLane = laneIndex;
        m_selectedBand = int(std::find_if(bands.begin(), bands.end(), [&](const Band &b) {
                                 return b.position == band.position;
                             }) - bands.begin());
        m_selectedAnnotation = -1;
    });
    setStatus(tr("Added a band to lane %1.").arg(lane.label));
}

void Backend::useAsReference(int lane, int band) {
    if (lane < 0 || lane >= m_session.lanes.size())
        return;
    edit({}, Change::Data, [&] {
        m_session.quant.referenceLane = lane;
        m_session.quant.referenceBand = band;
        m_session.quant.normalization = band >= 0 ? Normalization::Band : Normalization::Lane;
    });
}

void Backend::select(int lane, int band) {
    m_selectedLane = lane >= 0 && lane < m_session.lanes.size() ? lane : -1;
    m_selectedBand = m_selectedLane >= 0 && band >= 0 && band < m_session.lanes[m_selectedLane].bands.size() ? band : -1;
    m_selectedAnnotation = -1;
    emit selectionChanged();
}

void Backend::selectAnnotation(int index) {
    m_selectedAnnotation = index >= 0 && index < m_session.annotations.size() ? index : -1;
    m_selectedLane = m_selectedBand = -1;
    emit selectionChanged();
}

void Backend::clearSelection() {
    m_selectedLane = m_selectedBand = m_selectedAnnotation = -1;
    emit selectionChanged();
}

void Backend::nudge(double dx, double dy) {
    if (m_selectedAnnotation >= 0) {
        moveAnnotation(m_selectedAnnotation, dx, dy);
    } else if (m_selectedLane >= 0 && m_selectedBand >= 0) {
        edit(QString("nudgeband"), Change::Data, [&] {
            Lane &lane = m_session.lanes[m_selectedLane];
            Band &band = lane.bands[m_selectedBand];
            const double shift = std::clamp(dy, lane.top - band.lower, lane.bottom - band.upper);
            band.position += shift;
            band.lower += shift;
            band.upper += shift;
            band.manual = true;
        });
    } else if (m_selectedLane >= 0) {
        const Lane &lane = m_session.lanes[m_selectedLane];
        const double shift = std::clamp(dy, -lane.top, m_frame.height() - 1 - lane.bottom);
        setLaneGeometry(m_selectedLane, lane.center + dx, lane.width, lane.top + shift, lane.bottom + shift,
                        QString("nudge%1").arg(m_selectedLane));
    }
}

void Backend::removeSelected() {
    if (m_selectedAnnotation >= 0) {
        edit({}, Change::Data, [&] {
            m_session.annotations.removeAt(m_selectedAnnotation);
            m_selectedAnnotation = -1;
        });
    } else if (m_selectedLane >= 0 && m_selectedBand >= 0) {
        edit({}, Change::Data, [&] {
            m_session.lanes[m_selectedLane].bands.removeAt(m_selectedBand);
            m_selectedBand = -1;
        });
    } else if (m_selectedLane >= 0) {
        removeLane(m_selectedLane);
    }
}

void Backend::addArrow(double x1, double y1, double x2, double y2) {
    if (QLineF(x1, y1, x2, y2).length() < 3)
        return;
    edit({}, Change::Data, [&] {
        m_session.annotations << Annotation{Annotation::Arrow, {x1, y1}, {x2, y2}, {}};
        m_selectedAnnotation = int(m_session.annotations.size()) - 1;
        m_selectedLane = m_selectedBand = -1;
    });
}

void Backend::addText(double x, double y, const QString &text) {
    if (text.trimmed().isEmpty())
        return;
    edit({}, Change::Data, [&] {
        m_session.annotations << Annotation{Annotation::Text, {x, y}, {}, text.trimmed()};
        m_selectedAnnotation = int(m_session.annotations.size()) - 1;
        m_selectedLane = m_selectedBand = -1;
    });
}

void Backend::setAnnotationText(int index, const QString &text) {
    if (index < 0 || index >= m_session.annotations.size())
        return;
    if (text.trimmed().isEmpty()) {
        selectAnnotation(index);
        removeSelected();
        return;
    }
    edit(QString("text%1").arg(index), Change::Data, [&] { m_session.annotations[index].text = text.trimmed(); });
}

void Backend::moveAnnotation(int index, double dx, double dy) {
    if (index < 0 || index >= m_session.annotations.size())
        return;
    edit(QString("move%1").arg(index), Change::Data, [&] {
        auto &a = m_session.annotations[index];
        a.from += QPointF(dx, dy);
        a.to += QPointF(dx, dy);
    });
}

// ---- View -------------------------------------------------------------------

void Backend::setInvert(bool on) {
    edit({}, Change::View, [&] { m_session.view.invert = on; });
}

void Backend::setBrightness(double value) {
    edit("brightness", Change::View, [&] { m_session.view.brightness = std::clamp(value, -1.0, 1.0); });
}

void Backend::setContrast(double value) {
    edit("contrast", Change::View, [&] { m_session.view.contrast = std::clamp(value, .1, 5.0); });
}

void Backend::rotate(int direction) {
    if (!hasImage() || direction == 0)
        return;
    direction = direction > 0 ? 1 : -1;
    const bool hadLanes = !m_session.lanes.isEmpty();
    edit({}, Change::Frame, [&] {
        const int w = m_frame.width(), h = m_frame.height();
        auto &v = m_session.view;
        v.quarterTurns = (((v.quarterTurns + direction) % 4) + 4) % 4;
        if (!v.crop.isEmpty()) {
            const QPointF a = turn(v.crop.topLeft(), direction, w, h), b = turn(v.crop.bottomRight(), direction, w, h);
            v.crop = QRectF(a, b).normalized();
        }
        for (auto &annotation : m_session.annotations) {
            annotation.from = turn(annotation.from, direction, w, h);
            annotation.to = turn(annotation.to, direction, w, h);
        }
        // Lanes run down the gel, so after a quarter turn they would run
        // across it; they go, and undo brings them back.
        m_session.lanes.clear();
        m_session.ladder.lane = -1;
        m_session.quant.referenceLane = -1;
        m_selectedLane = m_selectedBand = -1;
    });
    if (hadLanes)
        setStatus(tr("Turning the gel cleared its lanes. Press Ctrl+Z to bring them back."));
}

void Backend::setAngle(double degrees) {
    edit("angle", Change::Frame, [&] { m_session.view.angle = std::clamp(degrees, -45.0, 45.0); });
}

void Backend::setCrop(double x, double y, double width, double height) {
    const QRectF rect = QRectF(x, y, width, height).normalized().intersected(m_frame.rect());
    if (rect.width() < 8 || rect.height() < 8)
        return;
    edit({}, Change::View, [&] {
        m_session.view.crop = QRectF(std::round(rect.x()), std::round(rect.y()), std::round(rect.width()),
                                     std::round(rect.height()));
    });
}

void Backend::resetCrop() {
    edit({}, Change::View, [&] { m_session.view.crop = QRectF(); });
}

// ---- Quantification -----------------------------------------------------------

void Backend::setPolarity(int polarity) {
    edit({}, Change::Data, [&] { m_session.quant.polarity = Polarity(std::clamp(polarity, 0, 2)); });
}

void Backend::setBackground(int method) {
    edit({}, Change::Data, [&] { m_session.quant.background = BackgroundMethod(std::clamp(method, 0, 2)); });
}

void Backend::setBallRadius(double radius) {
    edit("radius", Change::Data, [&] { m_session.quant.ballRadius = std::clamp(radius, 1.0, 1000.0); });
}

void Backend::setNormalization(int mode) {
    edit({}, Change::Data, [&] {
        m_session.quant.normalization = Normalization(std::clamp(mode, 0, 2));
        // Pick a sensible reference so the column fills in at once.
        if (m_session.quant.normalization != Normalization::None && m_session.quant.referenceLane < 0 &&
            !m_session.lanes.isEmpty()) {
            m_session.quant.referenceLane = m_session.ladder.lane == 0 && m_session.lanes.size() > 1 ? 1 : 0;
            m_session.quant.referenceBand = 0;
        }
    });
}

void Backend::setReferenceLane(int lane) {
    edit({}, Change::Data, [&] {
        m_session.quant.referenceLane = lane;
        m_session.quant.referenceBand = std::max(0, m_session.quant.referenceBand);
    });
}

QStringList Backend::ladderPresets() const {
    QStringList names;
    for (const auto &preset : ::ladderPresets())
        names << preset.name;
    return names;
}

QString Backend::ladderSizes() const {
    QStringList parts;
    for (double size : m_session.ladder.sizes)
        parts << QLocale().toString(size, 'g', 6);
    return parts.join("; ");
}

void Backend::setLadderPreset(const QString &name) {
    const LadderPreset preset = ::ladderPreset(name);
    if (preset.name.isEmpty())
        return;
    edit({}, Change::Data, [&] {
        m_session.ladder.preset = preset.name;
        m_session.ladder.sizes = preset.sizes;
        m_session.ladder.unit = preset.unit;
    });
}

void Backend::setLadderSizes(const QString &text) {
    QList<double> sizes;
    for (QString part : text.split(QRegularExpression("[;\\s]+"), Qt::SkipEmptyParts)) {
        bool ok = false;
        double value = QLocale().toDouble(part, &ok);
        if (!ok)
            value = part.toDouble(&ok);
        if (!ok || value <= 0) {
            setStatus(tr("Ladder sizes are positive numbers separated by semicolons."), true);
            return;
        }
        sizes << value;
    }
    std::sort(sizes.begin(), sizes.end(), std::greater<>());
    edit({}, Change::Data, [&] {
        m_session.ladder.sizes = sizes;
        // Edited sizes are no longer the preset's.
        const LadderPreset preset = ::ladderPreset(m_session.ladder.preset);
        if (preset.sizes != sizes)
            m_session.ladder.preset = tr("Custom");
    });
}

QString Backend::ladderFit() const {
    const auto &fit = m_results.fit;
    if (m_session.ladder.lane < 0)
        return tr("Mark a lane as the ladder to size bands.");
    if (!fit.ok)
        return fit.message;
    const QString text = tr("Fitted %1 points, degree %2, R² %3")
                             .arg(fit.positions.size())
                             .arg(fit.degree)
                             .arg(QLocale().toString(fit.r2, 'f', 4));
    return fit.message.isEmpty() ? text : text + "\n" + fit.message;
}

// ---- History ------------------------------------------------------------------

void Backend::undo() {
    if (m_undo.isEmpty())
        return;
    const bool turned = m_undo.last().view.quarterTurns != m_session.view.quarterTurns ||
                        m_undo.last().view.angle != m_session.view.angle;
    m_redo << m_session;
    m_session = m_undo.takeLast();
    m_undoKey.clear();
    emit historyChanged();
    refresh(turned ? Change::Frame : Change::Data);
}

void Backend::redo() {
    if (m_redo.isEmpty())
        return;
    const bool turned = m_redo.last().view.quarterTurns != m_session.view.quarterTurns ||
                        m_redo.last().view.angle != m_session.view.angle;
    m_undo << m_session;
    m_session = m_redo.takeLast();
    m_undoKey.clear();
    emit historyChanged();
    refresh(turned ? Change::Frame : Change::Data);
}

// ---- What QML shows -------------------------------------------------------------

QVariantList Backend::lanes() const {
    QVariantList out;
    for (int l = 0; l < m_session.lanes.size(); ++l) {
        const auto &lane = m_session.lanes[l];
        const auto measured = m_results.forLane(l);
        QVariantList bands;
        for (int b = 0; b < lane.bands.size(); ++b) {
            const auto &band = lane.bands[b];
            const auto r = b < measured.size() ? measured[b] : Analysis::BandResult{};
            bands << QVariantMap{{"position", band.position}, {"lower", band.lower}, {"upper", band.upper},
                                 {"manual", band.manual},
                                 {"size", (r.extrapolated ? "~" : "") + Analysis::formatSize(r.size, m_session.ladder.unit)},
                                 {"saturated", r.saturatedPixels > 0}};
        }
        out << QVariantMap{{"label", lane.label}, {"center", lane.center}, {"width", lane.width},
                           {"top", lane.top}, {"bottom", lane.bottom}, {"ladder", l == m_session.ladder.lane},
                           {"reference", l == m_session.quant.referenceLane &&
                                             m_session.quant.normalization == Normalization::Lane},
                           {"bands", bands}};
    }
    return out;
}

QVariantList Backend::results() const {
    QVariantList out;
    const auto &q = m_session.quant;
    for (const auto &r : m_results.bands) {
        const bool reference = (q.normalization == Normalization::Band && r.lane == q.referenceLane && r.band == q.referenceBand) ||
                               (q.normalization == Normalization::Lane && r.lane == q.referenceLane);
        QString size = r.ladder && std::isfinite(r.ladderSize) ? Analysis::formatSize(r.ladderSize, m_session.ladder.unit)
                                                               : Analysis::formatSize(r.size, m_session.ladder.unit);
        if (!r.ladder && r.extrapolated && !size.isEmpty())
            size = "~" + size;
        out << QVariantMap{{"lane", r.lane}, {"band", r.band}, {"label", r.laneLabel}, {"size", size},
                           {"volume", formatNumber(r.volume, 0)}, {"normalized", formatNumber(r.normalized, 3)},
                           {"saturated", r.saturatedPixels > 0}, {"ladder", r.ladder}, {"reference", reference}};
    }
    return out;
}

QVariantList Backend::ladderMarks() const {
    QVariantList out;
    for (const auto &r : m_results.bands)
        if (r.ladder && std::isfinite(r.ladderSize))
            out << QVariantMap{{"y", r.position}, {"text", Analysis::formatSize(r.ladderSize, m_session.ladder.unit)}};
    return out;
}

QVariantList Backend::annotations() const {
    QVariantList out;
    for (const auto &a : m_session.annotations)
        out << QVariantMap{{"arrow", a.kind == Annotation::Arrow}, {"x", a.from.x()}, {"y", a.from.y()},
                           {"toX", a.to.x()}, {"toY", a.to.y()}, {"text", a.text}};
    return out;
}

QVariantMap Backend::laneProfile(int lane) const {
    if (lane < 0 || lane >= m_results.profiles.size() || lane >= m_session.lanes.size())
        return {};
    const auto &p = m_results.profiles[lane];
    QVariantList raw, background;
    for (size_t i = 0; i < p.raw.size(); ++i) {
        raw << p.raw[i];
        background << p.background[i];
    }
    QVariantList bands;
    for (const auto &band : m_session.lanes[lane].bands)
        bands << QVariantMap{{"position", band.position}, {"lower", band.lower}, {"upper", band.upper}};
    return {{"top", p.top}, {"raw", raw}, {"background", background}, {"bands", bands},
            {"label", m_session.lanes[lane].label}};
}

double Backend::pixelsPerPoint(double widthMm) const {
    if (!hasImage())
        return 1;
    Figure::Options options;
    options.widthMm = widthMm;
    return 1 / Figure::pointsPerPixel(m_session, m_frame, m_results, options);
}

double Backend::rawAt(double x, double y) const {
    if (!hasImage() || !m_frame.rect().adjusted(-.5, -.5, .5, .5).contains(QPointF(x, y)))
        return qQNaN();
    return m_frame.sample(x, y);
}

QImage GelImageProvider::requestImage(const QString &, QSize *size, const QSize &) {
    const QImage image = m_backend->displayImage();
    if (size)
        *size = image.size();
    return image;
}
