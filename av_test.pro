QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

msvc: QMAKE_CXXFLAGS += /utf-8

# GTest header and static libs (source at D:/GTest/googletest, built to D:/GTest/build)
INCLUDEPATH += D:/GTest/googletest/googletest/include
LIBS += -LD:/GTest/build/lib/Debug -lgtest -lgtest_main

SOURCES += \
    $$PWD/rtpprotocol.cpp \
    $$PWD/h264parser.cpp \
    $$PWD/rtpprotocol_test.cpp \
    $$PWD/h264parser_test.cpp

HEADERS += \
    $$PWD/rtpprotocol.h \
    $$PWD/h264parser.h \
    $$PWD/hexdump.h
