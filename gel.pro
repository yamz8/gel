QT += core gui qml quick quickcontrols2 dbus svg

CONFIG += c++20 release
TARGET = gel
TEMPLATE = app

HEADERS += \
    src/analysis.h \
    src/backend.h \
    src/cli.h \
    src/figure.h \
    src/filepicker.h \
    src/gelimage.h \
    src/portalfilepicker.h \
    src/session.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/analysis.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/figure.cpp \
    src/gelimage.cpp \
    src/portalfilepicker.cpp \
    src/session.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
