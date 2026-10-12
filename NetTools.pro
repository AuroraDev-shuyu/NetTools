QT += widgets

#网络模块：提供TCP/UDP套接字功能
QT += network
QT += concurrent

QT +=core gui widgets network httpserver

greaterThan(QT_MAJOR_VERSION,4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    dataconverter.cpp \
    errorhandler.cpp \
    formchilddataconversion.cpp \
    formchilddatavalidation.cpp \
    formdataprocessor.cpp \
    formhttpclient.cpp \
    formhttpserver.cpp \
    formmqttclient.cpp \
    formmqttserver.cpp \
    formpop3client.cpp \
    formquicclient.cpp \
    formquicserver.cpp \
    formtcpclient.cpp \
    formtcpserver.cpp \
    formudpclient.cpp \
    formudpserver.cpp \
    inputvalidator.cpp \
    main.cpp \
    mainwidget.cpp \
    mqttprotocol.cpp \
    network.cpp

HEADERS += \
    dataconverter.h \
    errorhandler.h \
    formchilddataconversion.h \
    formchilddataconversion.h \
    formchilddatavalidation.h \
    formdataprocessor.h \
    formhttpclient.h \
    formhttpserver.h \
    formmqttclient.h \
    formmqttserver.h \
    formpop3client.h \
    formquicclient.h \
    formquicserver.h \
    formtcpclient.h \
    formtcpserver.h \
    formudpclient.h \
    formudpserver.h \
    inputvalidator.h \
    mainwidget.h \
    mqttprotocol.h \
    network.h

FORMS += \
    formchilddataconversion.ui \
    formchilddatavalidation.ui \
    formdataprocessor.ui \
    formhttpclient.ui \
    formhttpserver.ui \
    formmqttclient.ui \
    formmqttserver.ui \
    formpop3client.ui \
    formquicclient.ui \
    formquicserver.ui \
    formtcpclient.ui \
    formtcpserver.ui \
    formudpclient.ui \
    formudpserver.ui \
    mainwidget.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    images.qrc
