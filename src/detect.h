#pragma once

#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

// Reads an image for counting. A 16-bit grayscale image (common for microscope
// TIFFs) is stretched to 8 bits between its 0.1st and 99.9th percentiles, so
// faint samples aren't shown black. The file itself is only ever read.
QImage loadImage(const QString &path, QString *error);
QStringList imageSuffixes();

struct Blob {
    double x = 0;
    double y = 0;
    int area = 0;
};

struct DetectOptions {
    enum Polarity { Auto, Dark, Light };
    int threshold = -1; // 0–255; below 0 picks one with Otsu's method
    int minArea = 6;
    int maxArea = 0; // 0 means no upper limit
    Polarity polarity = Auto;
};

struct DetectResult {
    QList<Blob> blobs;
    int threshold = 0;
    bool dark = true; // whether the objects are darker than the background
};

int otsuThreshold(const QImage &gray);
// Thresholds the image and returns the center of every 8-connected blob within
// the area limits. These are only suggestions: nothing is counted until the
// user accepts them.
DetectResult detectBlobs(const QImage &image, const DetectOptions &options = {});
