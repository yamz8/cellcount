QT += core gui qml quick quickcontrols2 dbus concurrent

CONFIG += c++20 release
TARGET = cellcount
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/canvas.h \
    src/cli.h \
    src/detect.h \
    src/filepicker.h \
    src/markericon.h \
    src/painter.h \
    src/portalfilepicker.h \
    src/session.h \
    src/spacekey.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/canvas.cpp \
    src/cli.cpp \
    src/detect.cpp \
    src/markericon.cpp \
    src/painter.cpp \
    src/portalfilepicker.cpp \
    src/session.cpp \
    src/spacekey.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
