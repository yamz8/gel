QT += core gui qml quick quickcontrols2 testlib dbus svg
CONFIG += c++20 testcase
TARGET = ui_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += \
    synthetic.h \
    ../src/analysis.h \
    ../src/backend.h \
    ../src/figure.h \
    ../src/filepicker.h \
    ../src/gelimage.h \
    ../src/portalfilepicker.h \
    ../src/session.h \
    ../src/theme.h \
    ../src/translator.h

SOURCES += \
    ui_tests.cpp \
    ../src/analysis.cpp \
    ../src/backend.cpp \
    ../src/figure.cpp \
    ../src/gelimage.cpp \
    ../src/portalfilepicker.cpp \
    ../src/session.cpp \
    ../src/theme.cpp \
    ../src/translator.cpp

RESOURCES += ../src/resources.qrc
