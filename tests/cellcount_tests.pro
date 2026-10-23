QT += core gui qml quick quickcontrols2 testlib dbus concurrent
CONFIG += c++20 testcase
TARGET = cellcount_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += \
    synthetic.h \
    ../src/backend.h \
    ../src/detect.h \
    ../src/filepicker.h \
    ../src/painter.h \
    ../src/session.h \
    ../src/theme.h \
    ../src/translator.h

SOURCES += \
    cellcount_tests.cpp \
    ../src/backend.cpp \
    ../src/detect.cpp \
    ../src/painter.cpp \
    ../src/session.cpp \
    ../src/theme.cpp \
    ../src/translator.cpp

RESOURCES += ../src/resources.qrc
