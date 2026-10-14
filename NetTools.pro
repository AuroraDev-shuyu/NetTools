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
    network.cpp \
    pop3protocol.cpp \
    quicclient.cpp \
    quicserver.cpp

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
    network.h \
    pop3protocol.h \
    quiccommon.h \
    quicclient.h \
    quicserver.h

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

# MsQuic（QUIC模块依赖）：头文件、导入库与运行时DLL
INCLUDEPATH += $$PWD/third_party/msquic/include
LIBS += -L$$PWD/third_party/msquic/lib/x64 -lmsquic

# ws2_32：quicserver.cpp 使用 htonl/htons/ntohs 等 Winsock 字节序函数
LIBS += -lws2_32

# 输出目录按构建类型区分（与Qt Creator阴影构建默认一致），
# 构建后把 msquic.dll 拷贝到exe同目录
win32 {
    CONFIG(debug, debug|release) {
        DESTDIR = $$OUT_PWD/debug
    } else {
        DESTDIR = $$OUT_PWD/release
    }
    QMAKE_POST_LINK += $$quote(cmd /c copy /y $$shell_path($$PWD/third_party/msquic/bin/x64/msquic.dll) $$shell_path($$DESTDIR))
}

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    images.qrc
