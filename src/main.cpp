// cellcount — count cells or colonies on a microscope or plate image by clicking.

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>
#include <cstdio>

#include "backend.h"
#include "canvas.h"
#include "cli.h"
#include "markericon.h"
#include "portalfilepicker.h"
#include "spacekey.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    // cellcount themes itself; Qt's gtk3 platform theme only adds a
    // use-after-free inside GTK when the desktop theme changes, as Hype found.
    qputenv("QT_QPA_PLATFORMTHEME", "generic");
    // Commands draw no window, so they must not need a display, even where the
    // desktop exports QT_QPA_PLATFORM=wayland. Bare cellcount prints help, as a
    // command line tool should; the launcher says cellcount open.
    const bool command = argc == 1 || isCliCommand(argv[1]) || QByteArray(argv[1]) == "--version";
    if (command)
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QGuiApplication app(argc, argv);
    app.setApplicationName("cellcount");
    app.setApplicationVersion("0.1.0");

    // Matches the window to cellcount.desktop, so the compositor (Wayland
    // app_id) and the launcher pick up the installed icon.
    app.setDesktopFileName("cellcount");
    app.setWindowIcon(QIcon::fromTheme("cellcount"));

    if (app.arguments().value(1) == "--version") {
        fprintf(stdout, "cellcount %s\n", qPrintable(app.applicationVersion()));
        return 0;
    }
    if (command)
        return runCli(app.arguments());

    QCommandLineParser args;
    args.setApplicationDescription("Count cells or colonies on an image by clicking.\n\n" + cliSummary());
    args.addHelpOption();
    args.addPositionalArgument("file", "Image or .cellcount session to open", "[image|session]");
    QStringList arguments = app.arguments();
    if (arguments.value(1) == "open")
        arguments.removeAt(1);
    args.process(arguments);

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    PortalFilePicker picker;
    Backend backend(&theme, &picker);
    SpaceKey space;
    app.installEventFilter(&space);

    qmlRegisterType<CountCanvas>("Cellcount", 1, 0, "CountCanvas");
    qmlRegisterType<MarkerIcon>("Cellcount", 1, 0, "MarkerIcon");

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("spaceKey", &space);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    const QString file = args.positionalArguments().value(0);
    if (!file.isEmpty())
        backend.openPath(QUrl(file).isLocalFile() ? QUrl(file).toLocalFile() : file);

    return app.exec();
}
