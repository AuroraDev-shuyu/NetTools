QT += core network
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

msvc: QMAKE_CXXFLAGS += /utf-8

TARGET = qt_bench

SOURCES += \
    $$PWD/qt_bench.cpp
