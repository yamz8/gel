#pragma once

#include <QDateTime>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QRectF>
#include <QUrl>
#include <QVariant>

#include "analysis.h"
#include "gelimage.h"
#include "session.h"

class FilePicker;

// The window's state and every action it takes, exposed to QML as backend.
// The session is the document; the image is only ever read.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY imageChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY imageChanged)
    Q_PROPERTY(QString imageInfo READ imageInfo NOTIFY imageChanged)
    Q_PROPERTY(int frameWidth READ frameWidth NOTIFY sessionChanged)
    Q_PROPERTY(int frameHeight READ frameHeight NOTIFY sessionChanged)
    Q_PROPERTY(int imageRevision READ imageRevision NOTIFY displayChanged)
    Q_PROPERTY(QString annotationColor READ annotationColor NOTIFY displayChanged)
    Q_PROPERTY(QRectF crop READ crop NOTIFY sessionChanged)
    Q_PROPERTY(bool cropped READ cropped NOTIFY sessionChanged)
    Q_PROPERTY(bool invert READ invert NOTIFY sessionChanged)
    Q_PROPERTY(double brightness READ brightness NOTIFY sessionChanged)
    Q_PROPERTY(double contrast READ contrast NOTIFY sessionChanged)
    Q_PROPERTY(double angle READ angle NOTIFY sessionChanged)
    Q_PROPERTY(int quarterTurns READ quarterTurns NOTIFY sessionChanged)
    Q_PROPERTY(QVariantList lanes READ lanes NOTIFY sessionChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY sessionChanged)
    Q_PROPERTY(QVariantList ladderMarks READ ladderMarks NOTIFY sessionChanged)
    Q_PROPERTY(QVariantList annotations READ annotations NOTIFY sessionChanged)
    Q_PROPERTY(int ladderLane READ ladderLane NOTIFY sessionChanged)
    Q_PROPERTY(QString ladderPreset READ ladderPreset NOTIFY sessionChanged)
    Q_PROPERTY(QStringList ladderPresets READ ladderPresets CONSTANT)
    Q_PROPERTY(QString ladderSizes READ ladderSizes NOTIFY sessionChanged)
    Q_PROPERTY(QString ladderUnit READ ladderUnit NOTIFY sessionChanged)
    Q_PROPERTY(QString ladderFit READ ladderFit NOTIFY sessionChanged)
    Q_PROPERTY(int polarity READ polarity NOTIFY sessionChanged)
    Q_PROPERTY(bool darkBands READ darkBands NOTIFY sessionChanged)
    Q_PROPERTY(int background READ background NOTIFY sessionChanged)
    Q_PROPERTY(double ballRadius READ ballRadius NOTIFY sessionChanged)
    Q_PROPERTY(int normalization READ normalization NOTIFY sessionChanged)
    Q_PROPERTY(int referenceLane READ referenceLane NOTIFY sessionChanged)
    Q_PROPERTY(int referenceBand READ referenceBand NOTIFY sessionChanged)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY sessionChanged)
    Q_PROPERTY(int selectedLane READ selectedLane NOTIFY selectionChanged)
    Q_PROPERTY(int selectedBand READ selectedBand NOTIFY selectionChanged)
    Q_PROPERTY(int selectedAnnotation READ selectedAnnotation NOTIFY selectionChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool statusIsError READ statusIsError NOTIFY statusChanged)

public:
    explicit Backend(QObject *parent = nullptr);
    Backend(FilePicker *picker, QObject *parent = nullptr);

    bool hasImage() const { return !m_image.isNull(); }
    QString fileName() const;
    QString imageInfo() const;
    int frameWidth() const { return m_frame.width(); }
    int frameHeight() const { return m_frame.height(); }
    int imageRevision() const { return m_revision; }
    QString annotationColor() const { return m_annotationColor; }
    QRectF crop() const;
    bool cropped() const { return !m_session.view.crop.isEmpty(); }
    bool invert() const { return m_session.view.invert; }
    double brightness() const { return m_session.view.brightness; }
    double contrast() const { return m_session.view.contrast; }
    double angle() const { return m_session.view.angle; }
    int quarterTurns() const { return m_session.view.quarterTurns; }
    QVariantList lanes() const;
    QVariantList results() const;
    QVariantList ladderMarks() const;
    QVariantList annotations() const;
    int ladderLane() const { return m_session.ladder.lane; }
    QString ladderPreset() const { return m_session.ladder.preset; }
    QStringList ladderPresets() const;
    QString ladderSizes() const;
    QString ladderUnit() const { return m_session.ladder.unit; }
    QString ladderFit() const;
    int polarity() const { return int(m_session.quant.polarity); }
    bool darkBands() const { return m_results.dark; }
    int background() const { return int(m_session.quant.background); }
    double ballRadius() const { return m_session.quant.ballRadius; }
    int normalization() const { return int(m_session.quant.normalization); }
    int referenceLane() const { return m_session.quant.referenceLane; }
    int referenceBand() const { return m_session.quant.referenceBand; }
    QStringList warnings() const { return m_results.warnings; }
    int selectedLane() const { return m_selectedLane; }
    int selectedBand() const { return m_selectedBand; }
    int selectedAnnotation() const { return m_selectedAnnotation; }
    bool dirty() const;
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QString status() const { return m_status; }
    bool statusIsError() const { return m_statusIsError; }

    QImage displayImage() const;
    const Session &session() const { return m_session; }
    const Analysis::Results &analysis() const { return m_results; }

    Q_INVOKABLE void openDialog();
    Q_INVOKABLE bool openPath(const QString &path);
    Q_INVOKABLE bool openUrl(const QUrl &url) { return openPath(url.toLocalFile()); }
    Q_INVOKABLE bool save();
    Q_INVOKABLE void exportDialog(const QString &format, int dpi, double widthMm);
    Q_INVOKABLE bool exportTo(const QString &path, const QString &format, int dpi, double widthMm);

    Q_INVOKABLE void detect(bool lanesToo = false);
    Q_INVOKABLE int addLane(double x);
    Q_INVOKABLE void removeLane(int lane);
    // Nudges pass an undo key, so a run of them undoes as one; drags don't.
    Q_INVOKABLE void setLaneGeometry(int lane, double center, double width, double top, double bottom,
                                     const QString &undoKey = {});
    Q_INVOKABLE void setLaneLabel(int lane, const QString &label);
    Q_INVOKABLE void toggleLadder(int lane);
    Q_INVOKABLE int laneAt(double x) const;
    Q_INVOKABLE void toggleBandAt(int lane, double y);
    Q_INVOKABLE void useAsReference(int lane, int band);

    Q_INVOKABLE void select(int lane, int band = -1);
    Q_INVOKABLE void selectAnnotation(int index);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void nudge(double dx, double dy);
    Q_INVOKABLE void removeSelected();

    Q_INVOKABLE void addArrow(double x1, double y1, double x2, double y2);
    Q_INVOKABLE void addText(double x, double y, const QString &text);
    Q_INVOKABLE void setAnnotationText(int index, const QString &text);
    Q_INVOKABLE void moveAnnotation(int index, double dx, double dy);

    Q_INVOKABLE void setInvert(bool on);
    Q_INVOKABLE void setBrightness(double value);
    Q_INVOKABLE void setContrast(double value);
    Q_INVOKABLE void rotate(int quarterTurns);
    Q_INVOKABLE void setAngle(double degrees);
    Q_INVOKABLE void setCrop(double x, double y, double width, double height);
    Q_INVOKABLE void resetCrop();

    Q_INVOKABLE void setPolarity(int polarity);
    Q_INVOKABLE void setBackground(int method);
    Q_INVOKABLE void setBallRadius(double radius);
    Q_INVOKABLE void setNormalization(int mode);
    Q_INVOKABLE void setReferenceLane(int lane);
    Q_INVOKABLE void setLadderPreset(const QString &name);
    Q_INVOKABLE void setLadderSizes(const QString &text);

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    Q_INVOKABLE QVariantMap laneProfile(int lane) const;
    // Frame pixels per printed point at this figure width, for drawing
    // annotations at the size they will export.
    Q_INVOKABLE double pixelsPerPoint(double widthMm) const;
    // The raw value under a frame point, for the status bar.
    Q_INVOKABLE double rawAt(double x, double y) const;

signals:
    void imageChanged();
    void displayChanged();
    void sessionChanged();
    void selectionChanged();
    void dirtyChanged();
    void historyChanged();
    void statusChanged();

private:
    enum class Change { View, Frame, Data };
    // Every edit goes through here: the state before it goes on the undo
    // stack (edits with the same key in quick succession count as one, so a
    // dragged slider is one undo), then the session is changed and redrawn.
    template <typename Edit> void edit(const QString &key, Change change, Edit &&apply);
    void refresh(Change change);
    void rebuildFrame();
    void renderDisplay();
    void setStatus(const QString &message, bool error = false);
    void sortLanes();
    void clampSelection();
    void reset(const Session &session, const QString &sessionPath);
    QByteArray savedJson() const;
    bool sameUndoKey(const QString &key);

    FilePicker *m_picker = nullptr;
    GelImage m_image;
    Frame m_frame;
    std::vector<float> m_frameValues;
    DisplayLevels m_levels;
    Session m_session;
    QString m_sessionPath;
    QByteArray m_saved;
    Analysis::Results m_results;
    QList<Session> m_undo, m_redo;
    QString m_undoKey;
    QDateTime m_undoTime;
    int m_selectedLane = -1, m_selectedBand = -1, m_selectedAnnotation = -1;
    QString m_status;
    bool m_statusIsError = false;
    int m_revision = 0;
    QString m_annotationColor = "#ffffff";
    bool m_wasDirty = false;

    struct PendingExport { QString format; int dpi = 300; double widthMm = 85; };
    PendingExport m_pendingExport;

    mutable QMutex m_displayMutex;
    QImage m_display;
};

class GelImageProvider : public QQuickImageProvider {
public:
    explicit GelImageProvider(Backend *backend) : QQuickImageProvider(QQuickImageProvider::Image), m_backend(backend) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    Backend *m_backend;
};
