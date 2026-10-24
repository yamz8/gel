QT += core gui qml quick quickcontrols2 testlib dbus svg
CONFIG += c++20 testcase
TARGET = gel_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += \
    synthetic.h \
    ../src/analysis.h \
    ../src/figure.h \
    ../src/gelimage.h \
    ../src/session.h \
    ../src/theme.h \
    ../src/translator.h

SOURCES += \
    gel_tests.cpp \
    ../src/analysis.cpp \
    ../src/figure.cpp \
    ../src/gelimage.cpp \
    ../src/session.cpp \
    ../src/theme.cpp \
    ../src/translator.cpp

RESOURCES += ../src/resources.qrc
