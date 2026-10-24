// gel — annotate and quantify an agarose gel or Western blot, and export a
// figure ready for publication.

#include <QCommandLineParser>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include "backend.h"
#include "cli.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    // Commands and help draw no window, so they must not need a display, even
    // where the desktop exports QT_QPA_PLATFORM=wayland. Bare gel prints
    // help, as a command line tool should; the launcher runs gel open.
    const bool command = argc == 1 || isCliCommand(argv[1]);
    bool windowless = command;
    for (int i = 1; i < argc; ++i) {
        const QByteArray argument(argv[i]);
        windowless = windowless || argument == "--help" || argument == "-h" || argument == "--version" || argument == "-v";
    }
    if (windowless) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        // Nothing to show, so no desktop integration to load or complain about.
        qputenv("QT_QPA_PLATFORMTHEME", "generic");
        qputenv("NO_AT_BRIDGE", "1");
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("gel");
    app.setApplicationVersion("0.1.0");

    // Matches the window to gel.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("gel");
    app.setWindowIcon(QIcon::fromTheme("gel"));

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    if (command)
        return runCli(app.arguments());

    QCommandLineParser args;
    args.setApplicationDescription("Annotate and quantify a gel or blot, and export a figure.\n\n" + cliSummary());
    args.addHelpOption();
    args.addVersionOption();
    args.addPositionalArgument("open", "Open the window", "open");
    args.addPositionalArgument("file", "Image (PNG, JPEG, TIFF) or .gel session", "[file]");
    args.process(app);
    // gel open [file] is what the launcher runs; gel <file> works too.
    QStringList positional = args.positionalArguments();
    if (positional.value(0) == "open")
        positional.removeFirst();
    if (positional.size() > 1 || (positional.size() == 1 && !QFileInfo::exists(positional[0]))) {
        fprintf(stderr, "Unknown command or file %s. Run gel help.\n", qPrintable(positional.value(0)));
        return 1;
    }

    QQuickStyle::setStyle("Material");
    // The side panel holds many controls; the dense variant fits them.
    qputenv("QT_QUICK_CONTROLS_MATERIAL_VARIANT", "Dense");

    Theme theme;
    Backend backend;

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.addImageProvider("gel", new GelImageProvider(&backend));
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    if (positional.size() == 1)
        backend.openPath(QFileInfo(positional[0]).absoluteFilePath());

    return app.exec();
}
