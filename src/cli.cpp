#include "cli.h"

#include <QBuffer>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cmath>
#include <cstdio>

#include "detect.h"
#include "painter.h"
#include "session.h"
#include "theme.h"

namespace {
void print(FILE *stream, const QString &text) { fprintf(stream, "%s\n", qPrintable(text)); }
void print(const QJsonObject &object) { fprintf(stdout, "%s", QJsonDocument(object).toJson().constData()); }
int fail(const QString &message) {
    print(stderr, message);
    return 1;
}

QString formatted(double value, int decimals) {
    return std::isnan(value) ? QString("-") : QString::number(value, 'f', decimals);
}

struct Command {
    QCommandLineParser parser;
    Command(const QString &name, const QString &description, const QString &input = {}, const QString &inputHelp = {}) {
        parser.setApplicationDescription(description);
        parser.addHelpOption();
        parser.addPositionalArgument(name, description, name);
        if (!input.isEmpty())
            parser.addPositionalArgument(input, inputHelp, "<" + input + ">");
    }
    void json() { parser.addOption({"json", "Print the result as JSON"}); }
    bool isJson() const { return parser.isSet("json"); }
    QString argument(int index) const { return parser.positionalArguments().value(index); }
    bool load(Session &session, QList<Problem> *problems = nullptr) const {
        if (argument(1).isEmpty()) {
            print(stderr, "Name a .cellcount session.");
            return false;
        }
        QList<Problem> found;
        const bool ok = Session::load(argument(1), &session, &found);
        if (problems)
            *problems = found;
        if (!ok && !problems)
            for (const auto &problem : found)
                if (problem.error)
                    print(stderr, argument(1) + ": " + problem.message);
        return ok;
    }
};

// The accent the desktop uses now, so exported markers match the window.
QColor currentAccent() { return QColor(Theme::readAccent(QDir::homePath() + "/.local/state/omarchy/current/theme/colors.toml")); }

int check(const QStringList &arguments) {
    Command command("check", "Validate a session file: its JSON, classes, markers, ROI and image.", "session",
                    ".cellcount session");
    command.json();
    command.parser.process(arguments);
    Session session;
    QList<Problem> problems;
    if (command.argument(1).isEmpty())
        return fail("Name a .cellcount session.");
    command.load(session, &problems);
    if (!session.imagePath.isEmpty()) {
        const QFileInfo image(session.imagePath);
        if (!image.exists()) {
            problems << Problem{true, "The image " + session.imagePath + " doesn't exist"};
        } else {
            QString error;
            const QImage loaded = loadImage(session.imagePath, &error);
            if (loaded.isNull())
                problems << Problem{true, error};
            else if (!session.imageSize.isEmpty() && loaded.size() != session.imageSize)
                problems << Problem{false, QString("The image is %1×%2, but the session was made on %3×%4")
                                               .arg(loaded.width()).arg(loaded.height())
                                               .arg(session.imageSize.width()).arg(session.imageSize.height())};
        }
    }
    int errors = 0;
    for (const auto &problem : problems)
        errors += problem.error;
    if (command.isJson()) {
        QJsonArray list;
        for (const auto &problem : problems)
            list << QJsonObject{{"severity", problem.error ? "error" : "warning"}, {"message", problem.message}};
        print({{"ok", errors == 0}, {"markers", session.markers.size()}, {"classes", session.classes.size()},
               {"problems", list}});
        return errors ? 1 : 0;
    }
    for (const auto &problem : problems)
        print(problem.error ? stderr : stdout,
              QString("%1: %2: %3").arg(command.argument(1), problem.error ? "error" : "warning", problem.message));
    print(stdout, QString("%1 markers, %2 classes, %3 errors, %4 warnings")
                      .arg(session.markers.size()).arg(session.classes.size()).arg(errors).arg(problems.size() - errors));
    return errors ? 1 : 0;
}

int summary(const QStringList &arguments) {
    Command command("summary", "Print the counts per class, the area and the concentration.", "session",
                    ".cellcount session");
    command.json();
    command.parser.process(arguments);
    Session session;
    if (!command.load(session))
        return 1;
    if (command.isJson()) {
        print(session.summaryJson());
        return 0;
    }
    const auto counts = session.classCounts();
    for (int i = 0; i < session.classes.size(); ++i)
        print(stdout, QString("%1  %2  %3%").arg(session.classes[i].name, -16).arg(counts[i], 6)
                          .arg(formatted(session.percent(i), 1), 6));
    print(stdout, QString("%1  %2").arg("Total", -16).arg(session.total(), 6));
    if (!std::isnan(session.viability()))
        print(stdout, QString("Viability: %1%").arg(formatted(session.viability(), 1)));
    print(stdout, QString("%1 area: %2 px²%3").arg(session.roi.isSet() ? "ROI" : "Image")
                      .arg(formatted(session.areaPx(), 0))
                      .arg(session.pixelsPerUm > 0 ? QString(" (%1 µm²)").arg(formatted(session.areaUm2(), 1)) : QString()));
    if (session.squares > 0)
        print(stdout, QString("Concentration: %1 cells/mL").arg(formatted(session.concentration(session.total()), 0)));
    return 0;
}

int detect(const QStringList &arguments) {
    Command command("detect", "Find candidate cells or colonies: round spots that stand out from their surroundings. "
                              "Counts nothing.",
                    "image", "PNG, JPEG or TIFF image");
    command.parser.addOption({"threshold", "Least contrast with the surroundings, in gray levels 0–255 "
                                           "(default: from the image's noise)", "N"});
    command.parser.addOption({"min-area", "Smallest cell in pixels (default: 12)", "px", "12"});
    command.parser.addOption({"max-area", "Largest cell in pixels (default: a tenth of the short side across)", "px", "0"});
    command.parser.addOption({"polarity", "auto, dark (dark objects) or light (bright objects)", "mode", "auto"});
    command.json();
    command.parser.process(arguments);
    if (command.argument(1).isEmpty())
        return fail("Name an image.");
    QString error;
    const QImage image = loadImage(command.argument(1), &error);
    if (image.isNull())
        return fail(error);
    DetectOptions options;
    bool ok = true;
    if (command.parser.isSet("threshold")) {
        options.threshold = command.parser.value("threshold").toInt(&ok);
        if (!ok || options.threshold < 0 || options.threshold > 255)
            return fail("The threshold must be a whole number from 0 to 255.");
    }
    options.minArea = command.parser.value("min-area").toInt(&ok);
    if (!ok || options.minArea < 1)
        return fail("--min-area must be a whole number of at least 1.");
    options.maxArea = command.parser.value("max-area").toInt(&ok);
    if (!ok || options.maxArea < 0)
        return fail("--max-area must be a whole number, or 0 for no limit.");
    const QString polarity = command.parser.value("polarity");
    if (polarity == "dark")
        options.polarity = DetectOptions::Dark;
    else if (polarity == "light")
        options.polarity = DetectOptions::Light;
    else if (polarity != "auto")
        return fail("--polarity must be auto, dark or light.");

    const DetectResult result = detectBlobs(image, options);
    if (command.isJson()) {
        QJsonArray list;
        for (const auto &blob : result.blobs)
            list << QJsonObject{{"x", std::round(blob.x * 100) / 100}, {"y", std::round(blob.y * 100) / 100},
                                {"area", blob.area}, {"contrast", std::round(blob.contrast * 10) / 10}};
        print({{"image", QFileInfo(command.argument(1)).absoluteFilePath()},
               {"width", image.width()},
               {"height", image.height()},
               {"threshold", result.threshold},
               {"polarity", result.dark ? "dark" : "light"},
               {"count", result.blobs.size()},
               {"candidates", list}});
        return 0;
    }
    for (const auto &blob : result.blobs)
        print(stdout, QString("%1,%2,%3").arg(blob.x, 0, 'f', 1).arg(blob.y, 0, 'f', 1).arg(blob.area));
    print(stderr, QString("%1 candidates at threshold %2 (%3 objects)")
                      .arg(result.blobs.size()).arg(result.threshold).arg(result.dark ? "dark" : "light"));
    return 0;
}

int exportSession(const QStringList &arguments) {
    Command command("export", "Export markers as CSV, the summary as CSV, or the image with its markers as PNG.",
                    "session", ".cellcount session");
    command.parser.addOption({"csv", "Write the markers (x, y, class) as CSV", "out"});
    command.parser.addOption({"summary-csv", "Write the counts and measurements as CSV", "out"});
    command.parser.addOption({"png", "Write the annotated image as PNG", "out"});
    command.json();
    command.parser.process(arguments);
    if (!command.parser.isSet("csv") && !command.parser.isSet("summary-csv") && !command.parser.isSet("png"))
        return fail("Choose at least one of --csv, --summary-csv or --png.");
    Session session;
    if (!command.load(session))
        return 1;

    QJsonObject written;
    auto write = [&](const QString &option, const QByteArray &data) {
        const QString out = command.parser.value(option);
        if (QFileInfo(out) == QFileInfo(session.imagePath))
            return fail("Refusing to overwrite the original image " + out);
        QDir().mkpath(QFileInfo(out).absolutePath());
        QString error;
        if (!writeAtomically(out, data, &error))
            return fail("Could not write " + out + ": " + error);
        written.insert(option, QFileInfo(out).absoluteFilePath());
        if (!command.isJson())
            print(stdout, "Wrote " + out);
        return 0;
    };
    if (command.parser.isSet("csv") && write("csv", session.markersCsv().toUtf8()))
        return 1;
    if (command.parser.isSet("summary-csv") && write("summary-csv", session.summaryCsv().toUtf8()))
        return 1;
    if (command.parser.isSet("png")) {
        QString error;
        const QImage image = loadImage(session.imagePath, &error);
        if (image.isNull())
            return fail(error);
        session.imageSize = image.size();
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        renderAnnotated(image, session, currentAccent()).save(&buffer, "PNG");
        if (write("png", png))
            return 1;
    }
    if (command.isJson())
        print({{"written", written}, {"total", session.total()}});
    return 0;
}

// As Hype does: one shared copy, linked into Claude Code, found directly by other agents.
int skill(const QStringList &arguments) {
    Command command("skill", "Print the agent skill, or install it for your coding agents with: skill install.");
    command.parser.addPositionalArgument("install", "Copy to ~/.agents/skills/cellcount and link into ~/.claude/skills",
                                         "[install]");
    command.parser.process(arguments);
    QFile bundled(":/skill.md");
    if (!bundled.open(QIODevice::ReadOnly))
        return 1;
    const QByteArray text = bundled.readAll();
    if (command.argument(1).isEmpty()) {
        fputs(text.constData(), stdout);
        return 0;
    }
    if (command.argument(1) != "install")
        return fail("Use cellcount skill to print the skill, or cellcount skill install.");
    const QDir home = QDir::home();
    const QString directory = home.filePath(".agents/skills/cellcount");
    QSaveFile file(directory + "/SKILL.md");
    if (!QDir().mkpath(directory) || !file.open(QIODevice::WriteOnly) || file.write(text) < 0 || !file.commit())
        return fail("Could not write " + file.fileName());
    print(stdout, "Installed " + file.fileName());
    if (!home.exists(".claude"))
        return 0;
    const QString link = home.filePath(".claude/skills/cellcount");
    const QFileInfo existing(link);
    if (existing.isSymLink())
        QFile::remove(link);
    else if (existing.exists())
        return fail(link + " exists and is not a link; leaving it alone.");
    if (!home.mkpath(".claude/skills") || !QFile::link("../../.agents/skills/cellcount", link))
        return fail("Could not link " + link);
    print(stdout, "Linked " + link);
    return 0;
}

int help(const QStringList &arguments) {
    const QString topic = arguments.value(2);
    if (isCliCommand(topic) && topic != "help")
        return runCli({arguments[0], topic, "--help"});
    print(stdout, "Usage: cellcount <command> [options]\n\n" + cliSummary());
    return 0;
}
} // namespace

QString cliSummary() {
    return "Window:\n"
           "  open [image|session]            Count on an image, or continue a saved session\n\n"
           "Commands that need no display (cellcount help <command> for options):\n"
           "  check <session>                 Validate a .cellcount session\n"
           "  summary <session>               Counts per class, area and concentration\n"
           "  detect <image>                  Candidate cells or colonies from blob detection\n"
           "  export <session>                Markers CSV, summary CSV or annotated PNG\n"
           "  skill [install]                 Print the skill for coding agents, or install it\n\n"
           "Every command takes --json, exits 0 on success and 1 on failure, with errors on stderr.";
}

bool isCliCommand(const QString &word) {
    return QStringList{"check", "summary", "detect", "export", "skill", "help", "--help", "-h"}.contains(word);
}

int runCli(const QStringList &arguments) {
    const QString command = arguments.value(1);
    return command == "check"     ? check(arguments)
           : command == "summary" ? summary(arguments)
           : command == "detect"  ? detect(arguments)
           : command == "export"  ? exportSession(arguments)
           : command == "skill"   ? skill(arguments)
                                  : help(arguments);
}
