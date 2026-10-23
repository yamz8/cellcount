#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

#include "backend.h"
#include "detect.h"
#include "painter.h"
#include "session.h"
#include "synthetic.h"
#include "theme.h"
#include "translator.h"

class CellcountTests : public QObject {
    Q_OBJECT

private slots:
    void countsAndPercentages()
    {
        Session session = Session::withDefaults({"Live", "Dead", "Colony"});
        session.imageSize = QSize(100, 100);
        for (int i = 0; i < 6; ++i)
            session.markers << Marker{double(i), 1, 0};
        for (int i = 0; i < 2; ++i)
            session.markers << Marker{double(i), 2, 1};
        QCOMPARE(session.classCounts(), QList<int>({6, 2, 0}));
        QCOMPARE(session.total(), 8);
        QCOMPARE(session.percent(0), 75.0);
        QCOMPARE(session.percent(1), 25.0);
        QCOMPARE(session.percent(2), 0.0);
        QCOMPARE(session.viability(), 75.0);
    }

    void emptyCountsHaveNoPercentages()
    {
        Session session = Session::withDefaults({"Live", "Dead"});
        QVERIFY(std::isnan(session.percent(0)));
        QVERIFY(std::isnan(session.viability()));
        session.liveClass = session.deadClass = -1;
        session.markers << Marker{1, 1, 0};
        QVERIFY(std::isnan(session.viability()));
    }

    void rectangularRoiCountsOnlyInside()
    {
        Session session = Session::withDefaults({"Live"});
        session.imageSize = QSize(200, 200);
        session.roi = Roi{"rect", QRectF(10, 10, 50, 40)};
        session.markers << Marker{10, 10, 0} << Marker{60, 50, 0} << Marker{35, 30, 0} << Marker{61, 30, 0}
                        << Marker{5, 5, 0};
        QCOMPARE(session.total(), 3);
        QCOMPARE(session.areaPx(), 2000.0);
    }

    void circularRoiCountsOnlyInside()
    {
        Session session = Session::withDefaults({"Live"});
        session.imageSize = QSize(200, 200);
        session.roi = Roi{"circle", QRectF(0, 0, 100, 100)}; // center (50, 50), radius 50
        session.markers << Marker{50, 50, 0} << Marker{50, 100, 0} << Marker{99, 50, 0} << Marker{90, 90, 0}
                        << Marker{2, 2, 0};
        QCOMPARE(session.total(), 3);
        QVERIFY(qAbs(session.areaPx() - M_PI * 2500) < 1e-9);
    }

    void areaFollowsTheScale()
    {
        Session session = Session::withDefaults({"Live"});
        session.imageSize = QSize(1000, 500);
        QVERIFY(std::isnan(session.areaUm2()));
        session.pixelsPerUm = 2;
        QCOMPARE(session.areaPx(), 500000.0);
        QCOMPARE(session.areaUm2(), 125000.0);
    }

    void hemocytometerConcentration()
    {
        Session session = Session::withDefaults({"Live", "Dead"});
        QVERIFY(std::isnan(session.concentration(100)));
        // 200 cells over 4 large squares at a 1:2 dilution: 50 × 2 × 10⁴ = 10⁶ cells/mL.
        session.squares = 4;
        session.dilution = 2;
        QCOMPARE(session.concentration(200), 1e6);
        session.squares = 1;
        session.dilution = 1;
        QCOMPARE(session.concentration(37), 370000.0);
    }

    void jsonRoundTrip()
    {
        QTemporaryDir dir;
        Session session = Session::withDefaults({"Live", "Dead, stained", "Colony"});
        session.imagePath = dir.filePath("plate.png");
        session.imageSize = QSize(640, 480);
        session.pixelsPerUm = 1.25;
        session.dilution = 2;
        session.squares = 4;
        session.roi = Roi{"circle", QRectF(10.5, 20, 300, 300)};
        session.markers << Marker{1.5, 2.25, 0} << Marker{100, 200, 2} << Marker{639, 479, 1};
        const QString path = Session::sessionPathFor(session.imagePath);
        QString error;
        QVERIFY2(session.save(path, &error), qPrintable(error));

        Session loaded;
        QList<Problem> problems;
        QVERIFY(Session::load(path, &loaded, &problems));
        QVERIFY(problems.isEmpty());
        QCOMPARE(loaded, session);

        // The image is stored relative to the session, so the pair can move.
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(file.readAll()).object().value("image").toString(), QString("plate.png"));
    }

    void invalidSessionsAreReported()
    {
        QJsonObject json{{"cellcount", 1},
                         {"image", "x.png"},
                         {"classes", QJsonArray{QJsonObject{{"name", "Live"}, {"shape", "circle"}}}},
                         {"markers", QJsonArray{QJsonObject{{"x", 1}, {"y", 1}, {"class", 3}}}},
                         {"roi", QJsonObject{{"shape", "circle"}, {"x", 0}, {"y", 0}, {"width", 10}, {"height", 20}}}};
        QList<Problem> problems;
        QVERIFY(!Session::fromJson(json, "/tmp", nullptr, &problems));
        QStringList messages;
        for (const auto &problem : problems)
            messages << problem.message;
        QVERIFY2(messages.join("|").contains("class 4") || messages.join("|").contains("class 3"), qPrintable(messages.join("|")));
        QVERIFY(messages.join("|").contains("circular ROI"));
    }

    void csvExports()
    {
        Session session = Session::withDefaults({"Live", "Dead, stained"});
        session.imageSize = QSize(100, 100);
        session.roi = Roi{"rect", QRectF(0, 0, 50, 50)};
        session.markers << Marker{10, 10, 0} << Marker{80, 80, 1};
        const QStringList markers = session.markersCsv().trimmed().split('\n');
        QCOMPARE(markers[0], QString("x,y,class,class_name,counted"));
        QCOMPARE(markers[1], QString("10,10,1,Live,yes"));
        QCOMPARE(markers[2], QString("80,80,2,\"Dead, stained\",no"));
        const QString summary = session.summaryCsv();
        QVERIFY(summary.contains("count,Live,1,\n"));
        QVERIFY(summary.contains("count,Total,1,\n"));
        QVERIFY(summary.contains("viability,,100,%\n"));
        QVERIFY(summary.contains("roi_area,,2500,px2\n"));
    }

    void detectsDarkCellsOnBrightField()
    {
        QList<SyntheticCell> cells;
        const QImage image = syntheticField(480, 360, &cells);
        const DetectResult result = detectBlobs(image, {});
        QVERIFY(result.dark);
        // Every cell, and hardly anything else: the bright halos around live
        // cells may give a faint extra or two.
        QVERIFY(result.blobs.size() >= cells.size());
        QVERIFY(result.blobs.size() <= cells.size() * 105 / 100);
        for (const auto &cell : cells) {
            double nearest = 1e9;
            for (const auto &blob : result.blobs)
                nearest = std::min(nearest, std::hypot(blob.x - cell.center.x(), blob.y - cell.center.y()));
            QVERIFY2(nearest < 2.0, qPrintable(QString("cell at %1,%2 missed by %3").arg(cell.center.x()).arg(cell.center.y()).arg(nearest)));
        }
    }

    void detectsBrightCellsOnDarkField()
    {
        QList<SyntheticCell> cells;
        const QImage image = syntheticField(400, 300, &cells, true, 11);
        const DetectResult result = detectBlobs(image, {});
        QVERIFY(!result.dark);
        QCOMPARE(result.blobs.size(), cells.size());
    }

    void detectionRespectsMinimumArea()
    {
        QImage image(100, 100, QImage::Format_Grayscale8);
        image.fill(230);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        painter.drawEllipse(QPointF(20, 20), 1.5, 1.5); // dust, about 7 px
        painter.drawEllipse(QPointF(60, 60), 8, 8);     // a cell, about 200 px
        painter.end();
        DetectOptions options;
        options.minArea = 60;
        const DetectResult result = detectBlobs(image, options);
        QCOMPARE(result.blobs.size(), 1);
        QVERIFY(std::hypot(result.blobs[0].x - 60, result.blobs[0].y - 60) < 1);
        QVERIFY(result.blobs[0].contrast > 150);
        options.minArea = 4;
        QCOMPARE(detectBlobs(image, options).blobs.size(), 2);
    }

    // Each kind of counting job, scored against the true cells: how many are
    // found, and how many suggestions are not cells.
    void detectsEachKindOfImage_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<int>("polarity");
        QTest::addColumn<int>("minArea");
        QTest::addColumn<double>("leastFound");
        QTest::addColumn<double>("mostFalse");
        QTest::newRow("colonies on a plate") << "plate" << int(DetectOptions::Auto) << 12 << 1.0 << 0.03;
        QTest::newRow("fluorescent nuclei, 16-bit") << "nuclei" << int(DetectOptions::Auto) << 12 << 0.98 << 0.03;
        QTest::newRow("uneven lighting") << "vignetted" << int(DetectOptions::Auto) << 12 << 1.0 << 0.03;
        QTest::newRow("touching cells") << "crowded" << int(DetectOptions::Auto) << 12 << 0.98 << 0.03;
        QTest::newRow("hemocytometer, live cells") << "hemocytometer-live" << int(DetectOptions::Light) << 120 << 0.97 << 0.25;
        QTest::newRow("hemocytometer, dead cells") << "hemocytometer-dead" << int(DetectOptions::Dark) << 120 << 1.0 << -1.0;
    }

    void detectsEachKindOfImage()
    {
        QFETCH(QString, kind);
        QFETCH(int, polarity);
        QFETCH(int, minArea);
        QFETCH(double, leastFound);
        QFETCH(double, mostFalse);
        QList<SyntheticCell> cells;
        QImage image;
        if (kind == "plate")
            image = synthetic::plate(&cells);
        else if (kind == "nuclei")
            image = loadedBack(synthetic::nuclei16(&cells), "TIFF");
        else if (kind == "vignetted")
            image = synthetic::vignetted(&cells);
        else if (kind == "crowded")
            image = synthetic::crowded(&cells);
        else
            image = synthetic::hemocytometer(&cells);

        DetectOptions options;
        options.polarity = DetectOptions::Polarity(polarity);
        options.minArea = minArea;
        const DetectResult result = detectBlobs(image, options);

        // The cells this run is after: on a hemocytometer, live or dead.
        QList<SyntheticCell> wanted, others;
        for (const auto &cell : cells)
            (kind == "hemocytometer-dead" ? cell.dead : kind == "hemocytometer-live" ? !cell.dead : true)
                ? wanted << cell : others << cell;
        int found = 0, falseCount = 0;
        for (const auto &cell : wanted) {
            bool hit = false;
            for (const auto &blob : result.blobs)
                hit = hit || std::hypot(blob.x - cell.center.x(), blob.y - cell.center.y()) <= cell.radius;
            found += hit;
        }
        for (const auto &blob : result.blobs) {
            bool onCell = false;
            for (const auto &cell : wanted)
                onCell = onCell || std::hypot(blob.x - cell.center.x(), blob.y - cell.center.y()) <= cell.radius;
            falseCount += !onCell;
        }
        const double foundShare = double(found) / wanted.size(), falseShare = double(falseCount) / result.blobs.size();
        qInfo("%s: found %d of %d, %d of %d suggestions not on one", qPrintable(kind), found, int(wanted.size()),
              falseCount, int(result.blobs.size()));
        QVERIFY2(foundShare >= leastFound, qPrintable(QString("found %1%").arg(foundShare * 100)));
        if (mostFalse >= 0)
            QVERIFY2(falseShare <= mostFalse, qPrintable(QString("%1% false").arg(falseShare * 100)));
        // Live and dead are told apart: the live run suggests no dead cell.
        if (kind == "hemocytometer-live")
            for (const auto &cell : others)
                for (const auto &blob : result.blobs)
                    QVERIFY(std::hypot(blob.x - cell.center.x(), blob.y - cell.center.y()) > cell.radius);
    }

    void largeImagesFinishPromptly()
    {
        QList<SyntheticCell> cells;
        const QImage image = synthetic::vignetted(&cells).scaled(3600, 2800);
        DetectOptions options;
        options.minArea = 400;
        QElapsedTimer timer;
        timer.start();
        const DetectResult result = detectBlobs(image, options);
        qInfo("10 MP in %lld ms", timer.elapsed());
        QCOMPARE(result.blobs.size(), cells.size());
        QVERIFY(timer.elapsed() < 10000);
    }

    void sixteenBitTiffIsStretched()
    {
        QTemporaryDir dir;
        QImage image(64, 64, QImage::Format_Grayscale16);
        for (int y = 0; y < 64; ++y) {
            auto *line = reinterpret_cast<quint16 *>(image.scanLine(y));
            for (int x = 0; x < 64; ++x)
                line[x] = quint16(1000 + x * 10); // a faint ramp, near black at 8 bits
        }
        const QString path = dir.filePath("faint.tif");
        QVERIFY(image.save(path, "TIFF"));
        QString error;
        const QImage loaded = loadImage(path, &error);
        QVERIFY2(!loaded.isNull(), qPrintable(error));
        QCOMPARE(loaded.size(), QSize(64, 64));
        QCOMPARE(loaded.format(), QImage::Format_Grayscale8);
        QCOMPARE(qGray(loaded.pixel(0, 0)), 0);
        QCOMPARE(qGray(loaded.pixel(63, 0)), 255);
    }

    void backendEditsUndoAndRedo()
    {
        QTemporaryDir dir;
        const QString image = writeField(dir);
        Backend backend;
        QVERIFY(backend.openPath(image));
        QVERIFY(!backend.dirty());
        backend.addMarker(10, 10);
        backend.setCurrentClass(1);
        backend.addMarker(20, 20);
        QCOMPARE(backend.total(), 2);
        QVERIFY(backend.dirty());

        // A drag is one undo step.
        backend.beginMove(0);
        backend.moveMarker(0, 30, 30);
        backend.moveMarker(0, 40, 40);
        backend.endMove();
        QCOMPARE(backend.session().markers[0].x, 40.0);
        backend.undo();
        QCOMPARE(backend.session().markers[0].x, 10.0);
        backend.redo();
        QCOMPARE(backend.session().markers[0].x, 40.0);

        QVERIFY(backend.removeNearest(21, 21, 3));
        QVERIFY(!backend.removeNearest(200, 200, 3));
        QCOMPARE(backend.total(), 1);
        backend.undo();
        QCOMPARE(backend.total(), 2);
    }

    void autoSuggestNeverCountsSilently()
    {
        QTemporaryDir dir;
        QList<SyntheticCell> cells;
        const QString image = writeField(dir, &cells);
        Backend backend;
        QVERIFY(backend.openPath(image));
        QSignalSpy spy(&backend, &Backend::suggestionsChanged);
        backend.setAutoSuggest(true);
        QTRY_VERIFY(backend.suggestionCount() > 0 && !backend.detecting());
        const int offered = backend.suggestionCount();
        QVERIFY(offered >= cells.size());
        QCOMPARE(backend.total(), 0);
        backend.rejectSuggestion(0);
        QCOMPARE(backend.suggestionCount(), offered - 1);
        backend.acceptSuggestion(0);
        QCOMPARE(backend.total(), 1);
        QCOMPARE(backend.suggestionCount(), offered - 2);
        backend.acceptAllSuggestions();
        QCOMPARE(backend.total(), offered - 1);
        QCOMPARE(backend.suggestionCount(), 0);
    }

    void originalImageIsNeverModified()
    {
        QTemporaryDir dir;
        const QString image = writeField(dir);
        const QByteArray before = hash(image);

        Backend backend;
        QVERIFY(backend.openPath(image));
        backend.addMarker(50, 50);
        backend.setRoi("rect", 0, 0, 200, 200);
        QVERIFY(backend.save());
        QVERIFY(backend.exportTo(dir.filePath("out.csv"), "all"));
        QVERIFY(QFileInfo::exists(dir.filePath("out-markers.csv")));
        QVERIFY(QFileInfo::exists(dir.filePath("out-summary.csv")));
        QVERIFY(QFileInfo::exists(dir.filePath("out.png")));
        // Asked to export over the original, it refuses.
        QVERIFY(!backend.exportTo(image, "png"));
        QVERIFY(!backend.error().isEmpty());

        QCOMPARE(hash(image), before);

        // The saved session comes back when the image is opened again.
        Backend reopened;
        QVERIFY(reopened.openPath(image));
        QCOMPARE(reopened.total(), 1);
        QCOMPARE(reopened.roiShape(), QString("rect"));
    }

    void annotatedExportMatchesTheImage()
    {
        QList<SyntheticCell> cells;
        const QImage image = syntheticField(200, 150, &cells);
        Session session = Session::withDefaults({"Live"});
        session.imageSize = image.size();
        session.markers << Marker{cells[0].center.x(), cells[0].center.y(), 0};
        const QImage out = renderAnnotated(image, session, QColor("#FFD60A"));
        QCOMPARE(out.size(), image.size());
        QVERIFY(out != image.convertToFormat(QImage::Format_RGB32));
    }

    void classColorsStartAtTheAccent()
    {
        const QColor accent("#7aa2f7");
        QCOMPARE(classColor(accent, 0), accent);
        QSet<QRgb> colors;
        for (int i = 0; i < Session::maxClasses; ++i)
            colors.insert(classColor(accent, i).rgb());
        QCOMPARE(colors.size(), Session::maxClasses);
        const QStringList shapes = Session::shapes();
        QCOMPARE(QSet<QString>(shapes.begin(), shapes.end()).size(), Session::maxClasses);
    }

    void unreadableSessionBesideTheImageIsLeftAlone()
    {
        QTemporaryDir dir;
        const QString image = writeField(dir);
        const QString sidecar = Session::sessionPathFor(image);
        // A hand edit gave a marker a class that doesn't exist.
        write(sidecar, R"({"cellcount": 1, "image": "field.png", "classes": [{"name": "Live", "shape": "circle"}],
                           "markers": [{"x": 5, "y": 5, "class": 0}, {"x": 9, "y": 9, "class": 5}]})");
        const QByteArray before = hash(sidecar);
        Backend backend;
        QVERIFY(!backend.openPath(image));
        QVERIFY(backend.error().contains("field.png.cellcount"));
        QVERIFY(!backend.hasImage());
        QVERIFY(!backend.save());
        QCOMPARE(hash(sidecar), before);

        // One that names another image is left alone too.
        write(sidecar, R"({"cellcount": 1, "image": "other.png", "classes": [{"name": "Live", "shape": "circle"}],
                           "markers": []})");
        QVERIFY(!backend.openPath(image));
        QVERIFY(backend.error().contains("other.png"));
    }

    void refusedExportWritesNothing()
    {
        QTemporaryDir dir;
        const QString image = writeField(dir);
        Backend backend;
        QVERIFY(backend.openPath(image));
        backend.addMarker(10, 10);
        // "field.csv" with everything would put the annotated PNG on field.png.
        QVERIFY(!backend.exportTo(dir.filePath("field.csv"), "all"));
        QVERIFY(!QFileInfo::exists(dir.filePath("field-markers.csv")));
        QVERIFY(!QFileInfo::exists(dir.filePath("field-summary.csv")));
    }

    void singleExportsTakeTheirKindsExtension()
    {
        QTemporaryDir dir;
        Backend backend;
        QVERIFY(backend.openPath(writeField(dir)));
        backend.addMarker(10, 10);
        QVERIFY(backend.exportTo(dir.filePath("counts.csv"), "png"));
        QVERIFY(!QFileInfo::exists(dir.filePath("counts.csv")));
        QCOMPARE(QImage(dir.filePath("counts.png")).size(), QSize(320, 240));
        QVERIFY(backend.exportTo(dir.filePath("table.png"), "markers"));
        QVERIFY(QFileInfo::exists(dir.filePath("table.csv")));
        QVERIFY(!QFileInfo::exists(dir.filePath("table.png")));
    }

    void removingAnEarlierClassKeepsTheSelection()
    {
        QTemporaryDir dir;
        Backend backend;
        QVERIFY(backend.openPath(writeField(dir)));
        backend.addClass(); // "Class 4", now selected
        backend.setCurrentClass(2); // Colony
        backend.removeClass(0);
        QCOMPARE(backend.classes()[backend.currentClass()].toMap().value("name").toString(), QString("Colony"));
        backend.addMarker(5, 5);
        QCOMPARE(backend.classes()[backend.currentClass()].toMap().value("count").toInt(), 1);
        QCOMPARE(backend.classes()[backend.currentClass()].toMap().value("percent").toDouble(), 100.0);
    }

    void roiAreaIsClippedToTheImage()
    {
        Session session = Session::withDefaults({"Live"});
        session.imageSize = QSize(200, 100);
        session.roi = Roi{"rect", QRectF(150, 50, 100, 100)};
        QCOMPARE(session.areaPx(), 2500.0);
        // A circle centred on the corner keeps a quarter of its area.
        session.roi = Roi{"circle", QRectF(-50, -50, 100, 100)};
        QVERIFY(qAbs(session.areaPx() - M_PI * 2500 / 4) < M_PI * 2500 / 4 * 1e-3);

        QTemporaryDir dir;
        Backend backend;
        QVERIFY(backend.openPath(writeField(dir)));
        backend.setRoi("rect", -40, -40, 100, 100);
        QCOMPARE(backend.session().roi.rect, QRectF(0, 0, 60, 60));
    }

    void missingImageSizeIsReadFromTheImage()
    {
        QTemporaryDir dir;
        writeField(dir);
        const QString path = dir.filePath("hand.cellcount");
        write(path, R"({"cellcount": 1, "image": "field.png", "classes": [{"name": "Live", "shape": "circle"}],
                        "markers": [{"x": 300, "y": 200, "class": 0}]})");
        Session session;
        QList<Problem> problems;
        QVERIFY(Session::load(path, &session, &problems));
        QVERIFY(problems.isEmpty());
        QCOMPARE(session.imageSize, QSize(320, 240));
        QCOMPARE(session.areaPx(), 320.0 * 240);
    }

    void turningAutoSuggestOffDropsARunningDetection()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("big.png");
        syntheticField(2400, 1800, nullptr).save(path);
        Backend backend;
        QVERIFY(backend.openPath(path));
        backend.setAutoSuggest(true);
        backend.setAutoSuggest(false);
        QTest::qWait(1500);
        QCOMPARE(backend.suggestionCount(), 0);

        // Nor does a run started on one image land on the next.
        backend.setAutoSuggest(true);
        QVERIFY(backend.openPath(writeField(dir)));
        backend.setAutoSuggest(false);
        QTest::qWait(1500);
        QCOMPARE(backend.suggestionCount(), 0);
    }

    void undoDuringADragEndsTheDrag()
    {
        QTemporaryDir dir;
        Backend backend;
        QVERIFY(backend.openPath(writeField(dir)));
        backend.addMarker(10, 10);
        backend.addMarker(100, 100);
        backend.beginMove(1);
        backend.moveMarker(1, 150, 150);
        backend.undo(); // undoes the drag so far
        QCOMPARE(backend.session().markers[1].x, 100.0);
        backend.moveMarker(1, 200, 200); // the rest of the drag is ignored
        backend.endMove();
        QCOMPARE(backend.session().markers[1].x, 100.0);
        QCOMPARE(backend.markerCount(), 2);
        QVERIFY(backend.canRedo());
        backend.redo();
        QCOMPARE(backend.session().markers[1].x, 150.0);
    }

    void statusIsReportedOnce()
    {
        QTemporaryDir dir;
        Backend backend;
        QVERIFY(backend.openPath(writeField(dir)));
        QSignalSpy spy(&backend, &Backend::statusReported);
        backend.addMarker(10, 10);
        QVERIFY(backend.save());
        QCOMPARE(spy.count(), 1);
        QVERIFY(!backend.exportTo(dir.filePath("x.csv"), "nonsense"));
        backend.clearError();
        QCOMPARE(spy.count(), 1);
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        write(response, "{\"int\":0}");

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        write(response, "{\"int\":6}");
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
        write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

    // Every shipped catalog parses, and translates only strings English has.
    void catalogsAreValid()
    {
        QFile english(":/i18n/en.json");
        QVERIFY(english.open(QIODevice::ReadOnly));
        const QJsonObject plurals = QJsonDocument::fromJson(english.readAll()).object();
        QDirIterator it(":/i18n");
        int catalogs = 0;
        while (it.hasNext()) {
            QFile file(it.next());
            QVERIFY(file.open(QIODevice::ReadOnly));
            QJsonParseError error;
            const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
            QVERIFY2(document.isObject(), qPrintable(file.fileName() + ": " + error.errorString()));
            const QJsonObject catalog = document.object();
            for (auto entry = catalog.begin(); entry != catalog.end(); ++entry)
                if (entry.value().isObject())
                    QVERIFY2(plurals.contains(entry.key()), qPrintable(file.fileName() + ": " + entry.key()));
            ++catalogs;
        }
        QVERIFY(catalogs >= 3);
    }

private:
    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }

    static QString writeField(QTemporaryDir &dir, QList<SyntheticCell> *cells = nullptr)
    {
        const QString path = dir.filePath("field.png");
        syntheticField(320, 240, cells).save(path, "PNG");
        return path;
    }

    // The image as it comes back from a file, through the app's own loader.
    static QImage loadedBack(const QImage &image, const char *format)
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QString("image.") + (QByteArray(format) == "TIFF" ? "tif" : "png"));
        image.save(path, format);
        return loadImage(path, nullptr);
    }

    static QByteArray hash(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
    }
};

QTEST_MAIN(CellcountTests)
#include "cellcount_tests.moc"
