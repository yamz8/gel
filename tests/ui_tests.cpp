// Drives the real window offscreen with the mouse and keyboard, as a person
// would. Set GEL_UI_SHOTS to a directory to keep a screenshot of each test.
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include "backend.h"
#include "filepicker.h"
#include "synthetic.h"
#include "theme.h"
#include "translator.h"

namespace {
QStringList warnings;

void collect(QtMsgType type, const QMessageLogContext &, const QString &message) {
    if (type != QtDebugMsg && type != QtInfoMsg)
        warnings << message;
}

// Answers like the portal once a file is chosen; with no path, like a cancelled chooser.
class FakePicker : public FilePicker {
public:
    QString path;
    void openImage() override { if (!path.isEmpty()) emit openSelected(QUrl::fromLocalFile(path)); }
    void exportFile(const QUrl &, const QString &) override {
        if (!path.isEmpty())
            emit exportSelected(QUrl::fromLocalFile(path));
    }
};

void write(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

// One window, its backend, and a private theme and language.
struct Window {
    QTemporaryDir dir;
    FakePicker picker;
    std::unique_ptr<Translator> translator;
    std::unique_ptr<Theme> theme;
    std::unique_ptr<Backend> backend;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow *window = nullptr;
    QString image;

    explicit Window(const QString &language = "en", QSize size = QSize(1360, 860)) {
        write(dir.filePath("language"), language.toUtf8());
        write(dir.filePath("state/theme/colors.toml"), "accent = \"#FFD60A\"\n");
        translator = std::make_unique<Translator>(dir.filePath("language"));
        qApp->installTranslator(translator.get());
        theme = std::make_unique<Theme>(dir.filePath("state"), nullptr, "/bin/false");
        backend = std::make_unique<Backend>(&picker);
        engine = std::make_unique<QQmlApplicationEngine>();
        QObject::connect(translator.get(), &Translator::changed, engine.get(), &QQmlEngine::retranslate);
        engine->addImageProvider("gel", new GelImageProvider(backend.get()));
        engine->rootContext()->setContextProperty("translator", translator.get());
        engine->rootContext()->setContextProperty("theme", theme.get());
        engine->rootContext()->setContextProperty("backend", backend.get());
        engine->load(QUrl("qrc:/Main.qml"));
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().value(0));
        if (!window)
            return;
        window->resize(size);
        window->show();
        image = dir.filePath("demo.png");
        Synthetic::demo().qimage().save(image);
        settle();
    }
    ~Window() {
        engine.reset();
        qApp->removeTranslator(translator.get());
    }

    void settle() {
        for (int i = 0; i < 4; ++i) {
            QCoreApplication::processEvents();
            QTest::qWait(10);
        }
    }
    void key(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QTest::keyClick(window, key, modifiers);
        settle();
    }
    void key(char character) {
        QTest::keyClick(window, character);
        settle();
    }
    QQuickItem *item(const char *name) const { return window->findChild<QQuickItem *>(name); }
    // A frame point in window coordinates.
    QPoint at(double fx, double fy) const {
        QQuickItem *gel = item("gel");
        const QRectF crop = backend->crop();
        const double s = gel->width() / crop.width();
        return gel->mapToScene(QPointF((fx - crop.x()) * s, (fy - crop.y()) * s)).toPoint();
    }
    void hover(QPoint p) {
        QTest::mouseMove(window, p);
        settle();
    }
    void drag(QPoint from, QPoint to) {
        QTest::mousePress(window, Qt::LeftButton, {}, from);
        for (int i = 1; i <= 8; ++i)
            QTest::mouseMove(window, from + (to - from) * i / 8);
        QTest::mouseRelease(window, Qt::LeftButton, {}, to);
        settle();
    }
    void click(QPoint p) {
        QTest::mouseClick(window, Qt::LeftButton, {}, p);
        settle();
    }
    void shot(const QString &name) {
        const QString directory = qEnvironmentVariable("GEL_UI_SHOTS");
        if (!directory.isEmpty())
            window->grabWindow().save(QDir(directory).filePath(name + ".png"));
    }
    bool open() {
        const bool ok = backend->openPath(image);
        settle();
        return ok;
    }
};
} // namespace

class UiTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { qInstallMessageHandler(collect); }
    void init() { warnings.clear(); }
    void cleanup() { QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n'))); }

    void showsTheEmptyWindow() {
        Window w;
        QVERIFY(w.window);
        QVERIFY(!w.backend->hasImage());
        w.shot("empty");
    }

    void opensFromTheDialogAndDetectsWithSpace() {
        Window w;
        w.picker.path = w.image;
        w.key(Qt::Key_O, Qt::ControlModifier);
        QVERIFY(w.backend->hasImage());
        w.key(Qt::Key_Space);
        QCOMPARE(w.backend->session().lanes.size(), 8);
        QVERIFY(w.backend->results().size() >= 20);
        QVERIFY(w.backend->status().contains("8 lanes"));
        w.shot("detected");
    }

    void addsALaneUnderThePointer() {
        Window w;
        QVERIFY(w.open());
        w.hover(w.at(720, 200));
        QVERIFY(w.window->property("pointerInside").toBool());
        w.key('L');
        QCOMPARE(w.backend->session().lanes.size(), 1);
        QVERIFY(std::abs(w.backend->session().lanes[0].center - 720) < 3 ||
                w.backend->session().lanes[0].center + w.backend->session().lanes[0].width / 2 >= 759);
    }

    void picksAndRemovesBandsByClicking() {
        Window w;
        QVERIFY(w.open());
        w.key(Qt::Key_Space);
        const int before = w.backend->session().lanes[3].bands.size();
        w.key('B');
        QCOMPARE(w.window->property("mode").toString(), QString("bands"));
        const double x = w.backend->session().lanes[3].center;
        const Band band = w.backend->session().lanes[3].bands[0];
        w.click(w.at(x, band.position));
        QCOMPARE(w.backend->session().lanes[3].bands.size(), before - 1);
        w.click(w.at(x, band.position));
        QCOMPARE(w.backend->session().lanes[3].bands.size(), before);
        QVERIFY(w.backend->session().lanes[3].bands[0].manual);
        QVERIFY(std::abs(w.backend->session().lanes[3].bands[0].position - band.position) < 2);
    }

    void dragsALaneAndUndoes() {
        Window w;
        QVERIFY(w.open());
        w.key(Qt::Key_Space);
        QCOMPARE(w.backend->session().lanes.size(), 8);
        const double center = w.backend->session().lanes[2].center;
        w.drag(w.at(center, 200), w.at(center + 10, 200));
        QCOMPARE(w.backend->session().lanes.size(), 8);
        QVERIFY(std::abs(w.backend->session().lanes[2].center - (center + 10)) < 1.5);
        QCOMPARE(w.backend->selectedLane(), 2);
        w.key(Qt::Key_Right);
        QVERIFY(std::abs(w.backend->session().lanes[2].center - (center + 11)) < 1.5);
        w.key(Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(std::abs(w.backend->session().lanes[2].center - (center + 10)) < 1.5);
        w.key(Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(w.backend->session().lanes.size(), 8);
        QVERIFY(std::abs(w.backend->session().lanes[2].center - center) < .01);
        w.key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(std::abs(w.backend->session().lanes[2].center - (center + 10)) < 1.5);
    }

    void cropsArrowsAndLabels() {
        Window w;
        QVERIFY(w.open());
        w.key(Qt::Key_Space);
        w.key('C');
        w.drag(w.at(20, 10), w.at(700, 465));
        QVERIFY(w.backend->cropped());
        QVERIFY(std::abs(w.backend->crop().width() - 680) < 3);
        QCOMPARE(w.window->property("mode").toString(), QString("normal"));

        w.key('A');
        w.drag(w.at(300, 120), w.at(330, 160));
        QCOMPARE(w.backend->session().annotations.size(), 1);
        QCOMPARE(w.backend->session().annotations[0].kind, Annotation::Arrow);

        w.key('T');
        w.click(w.at(400, 60));
        QQuickItem *editor = w.item("editor");
        QVERIFY(editor->isVisible());
        for (const QChar c : QString("PCR product")) QTest::keyClick(w.window, c.toLatin1());
        w.key(Qt::Key_Return);
        QCOMPARE(w.backend->session().annotations.size(), 2);
        QCOMPARE(w.backend->session().annotations[1].text, QString("PCR product"));
        // Typing in the field didn't fire the one-letter shortcuts.
        QCOMPARE(w.backend->session().lanes.size(), 8);
        w.shot("annotated");
    }

    void marksTheLadderAndSizesBands() {
        Window w;
        QVERIFY(w.open());
        w.key(Qt::Key_Space);
        w.backend->toggleLadder(0);
        w.backend->select(3);
        w.settle();
        QVERIFY(w.backend->analysis().fit.ok);
        QCOMPARE(w.backend->ladderMarks().size(), 10);
        const auto lane = w.backend->analysis().forLane(3);
        QVERIFY(std::abs(lane.first().size - 2500) / 2500 < .05);
        w.shot("ladder");
    }

    void savesAndAsksBeforeQuitting() {
        Window w;
        QVERIFY(w.open());
        QVERIFY(!w.backend->dirty());
        w.key(Qt::Key_Space);
        QVERIFY(w.backend->dirty());
        w.key('Q');
        QObject *dialog = w.window->findChild<QObject *>("confirmDialog");
        QVERIFY(dialog && dialog->property("visible").toBool());
        QVERIFY(w.window->isVisible());
        w.key(Qt::Key_Escape);
        QMetaObject::invokeMethod(dialog, "close");
        w.settle();
        w.key(Qt::Key_S, Qt::ControlModifier);
        QVERIFY(!w.backend->dirty());
        QVERIFY(QFile::exists(w.dir.filePath("demo.gel")));

        // Opening the image again brings the session with it.
        Window again;
        QVERIFY(again.backend->openPath(w.image));
        QCOMPARE(again.backend->session().lanes.size(), 8);
    }

    void exportsThroughTheDialog() {
        Window w;
        QVERIFY(w.open());
        w.key(Qt::Key_Space);
        w.key(Qt::Key_E, Qt::ControlModifier);
        QObject *dialog = w.window->findChild<QObject *>("exportDialog");
        QVERIFY(dialog && dialog->property("visible").toBool());
        w.shot("export");
        w.picker.path = w.dir.filePath("figure.png");
        w.key(Qt::Key_Return);
        QVERIFY2(QFile::exists(w.picker.path), qPrintable(w.backend->status()));
        QImage figure(w.picker.path);
        QVERIFY(figure.width() > 900);
    }

    void speaksDanish() {
        Window w("da");
        QVERIFY(w.open());
        w.key(Qt::Key_Space);
        QVERIFY2(w.backend->status().contains("baner"), qPrintable(w.backend->status()));
        w.shot("danish");
    }

    // The README's screenshot: a ladder, labeled samples, a loading
    // reference, and an annotation, with the default accent.
    void composesTheReadmeScene() {
        Window w;
        QVERIFY(w.open());
        w.key(Qt::Key_Space);
        const QStringList labels{"1 kb", "0", "5", "10", "15", "20", "25", "30"};
        for (int i = 0; i < labels.size(); ++i)
            w.backend->setLaneLabel(i, labels[i]);
        w.backend->toggleLadder(0);
        w.backend->useAsReference(5, 0);
        w.backend->setCrop(20, 40, 680, 430);
        w.backend->addArrow(600, 150, 575, 172);
        w.backend->addText(470, 104, "2.5 kb product");
        w.backend->select(3, 0);
        w.settle();
        QVERIFY(w.backend->analysis().fit.ok);
        QVERIFY(w.backend->results().at(10).toMap().value("normalized").toString().size() > 0);
        w.shot("readme");
        const QString directory = qEnvironmentVariable("GEL_UI_SHOTS");
        if (!directory.isEmpty())
            QVERIFY(w.backend->exportTo(QDir(directory).filePath("readme-figure.png"), "png", 200, 85));
    }

    void turnsAndClearsLanesWithUndo() {
        Window w;
        QVERIFY(w.open());
        w.key(Qt::Key_Space);
        w.key('R');
        QCOMPARE(w.backend->frameWidth(), 470);
        QVERIFY(w.backend->session().lanes.isEmpty());
        w.key(Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(w.backend->frameWidth(), 760);
        QCOMPARE(w.backend->session().lanes.size(), 8);
    }
};

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Material");
    qputenv("QT_QUICK_CONTROLS_MATERIAL_VARIANT", "Dense");
    UiTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "ui_tests.moc"
