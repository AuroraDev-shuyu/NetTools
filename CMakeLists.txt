# NetTools - Qt 网络调试助手
# CMake 构建（与 NetTools.pro 并存；CMake 为大厂通用工具链，qmake 保留兼容旧流程）
#
# 构建示例（MSVC + Ninja）：
#   cmake -S . -B build\cmake-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug ^
#         -DCMAKE_PREFIX_PATH=C:/Qt/6.5.3/msvc2019_64
#   cmake --build build\cmake-debug

cmake_minimum_required(VERSION 3.16)

project(NetTools VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt 自动处理：AUTOMOC(Q_OBJECT) / AUTOUIC(.ui) / AUTORCC(.qrc)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 网络模块：TCP/UDP、并发、HTTP服务器（WebSockets为HttpServer的依赖，显式声明）
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Concurrent HttpServer WebSockets)

qt_add_executable(NetTools WIN32
    dataconverter.cpp
    errorhandler.cpp
    formchilddataconversion.cpp
    formchilddatavalidation.cpp
    formdataprocessor.cpp
    formhttpclient.cpp
    formhttpserver.cpp
    formmqttclient.cpp
    formmqttserver.cpp
    formpop3client.cpp
    formquicclient.cpp
    formquicserver.cpp
    formtcpclient.cpp
    formtcpserver.cpp
    formudpclient.cpp
    formudpserver.cpp
    inputvalidator.cpp
    main.cpp
    mainwidget.cpp
    mqttprotocol.cpp
    network.cpp
    pop3protocol.cpp
    quicclient.cpp
    quicserver.cpp

    dataconverter.h
    errorhandler.h
    formchilddataconversion.h
    formchilddatavalidation.h
    formdataprocessor.h
    formhttpclient.h
    formhttpserver.h
    formmqttclient.h
    formmqttserver.h
    formpop3client.h
    formquicclient.h
    formquicserver.h
    formtcpclient.h
    formtcpserver.h
    formudpclient.h
    formudpserver.h
    inputvalidator.h
    mainwidget.h
    mqttprotocol.h
    network.h
    pop3protocol.h
    quiccommon.h
    quicclient.h
    quicserver.h

    formchilddataconversion.ui
    formchilddatavalidation.ui
    formdataprocessor.ui
    formhttpclient.ui
    formhttpserver.ui
    formmqttclient.ui
    formmqttserver.ui
    formpop3client.ui
    formquicclient.ui
    formquicserver.ui
    formtcpclient.ui
    formtcpserver.ui
    formudpclient.ui
    formudpserver.ui
    mainwidget.ui

    images.qrc
)

target_link_libraries(NetTools PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
    Qt6::HttpServer
    Qt6::WebSockets
)

# MSVC：/utf-8 保证中文注释与字符串字面量按 UTF-8 解析；/permissive- 对齐 qmake 一致性模式
if(MSVC)
    target_compile_options(NetTools PRIVATE /utf-8 /permissive-)
endif()

# MsQuic（QUIC模块依赖）：头文件、导入库与运行时DLL
target_include_directories(NetTools PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/msquic/include
)
target_link_libraries(NetTools PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/msquic/lib/x64/msquic.lib
    ws2_32
)

# 构建后把 msquic.dll 拷贝到 exe 同目录（$<TARGET_FILE_DIR> 对单配置/多配置生成器都正确）
add_custom_command(TARGET NetTools POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/msquic/bin/x64/msquic.dll"
        "$<TARGET_FILE_DIR:NetTools>"
    COMMENT "Copying msquic.dll next to NetTools.exe"
)
