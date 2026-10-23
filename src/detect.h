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
    double contrast = 0; // gray levels against its surroundings
};

struct DetectOptions {
    enum Polarity { Auto, Dark, Light };
    // The least contrast with its surroundings, in gray levels 0–255, for a
    // spot to count; below 0 picks one from the image's noise.
    int threshold = -1;
    int minArea = 12;
    int maxArea = 0; // 0 means up to a tenth of the image's short side across
    Polarity polarity = Auto;
};

struct DetectResult {
    QList<Blob> blobs; // area counts the pixels that stand out by half the contrast
    int threshold = 0;
    bool dark = true; // whether the objects are darker than the background
};

// Finds round spots between the area limits that stand out from their own
// surroundings, and returns their centers. These are only suggestions:
// nothing is counted until the user accepts them.
DetectResult detectBlobs(const QImage &image, const DetectOptions &options = {});
