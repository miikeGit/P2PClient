cmake_minimum_required(VERSION 3.16)
project(P2PClient VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    add_compile_definitions(WIN32_LEAN_AND_MEAN)
    set(PLATFORM_LIBS ws2_32 iphlpapi)
endif()

find_package(Qt6 QUIET COMPONENTS Widgets Network)
if(NOT Qt6_FOUND)
    find_package(Qt5 REQUIRED COMPONENTS Widgets Network)
endif()
set(QT Qt${QT_VERSION_MAJOR})

include(FetchContent)

set(ENABLE_TESTING         OFF CACHE INTERNAL "")
set(ENABLE_PROGRAMS        OFF CACHE INTERNAL "")
set(MBEDTLS_FATAL_WARNINGS OFF CACHE INTERNAL "")
FetchContent_Declare(
    mbedtls
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
    GIT_TAG        v3.6.0)

FetchContent_MakeAvailable(mbedtls)

set(MbedTLS_INCLUDE_DIR "${mbedtls_SOURCE_DIR}/include" CACHE PATH "" FORCE)
set(MbedTLS_LIBRARY     "${mbedtls_BINARY_DIR}/library/libmbedtls${CMAKE_STATIC_LIBRARY_SUFFIX}"   CACHE FILEPATH "" FORCE)
set(MbedCrypto_LIBRARY  "${mbedtls_BINARY_DIR}/library/libmbedcrypto${CMAKE_STATIC_LIBRARY_SUFFIX}" CACHE FILEPATH "" FORCE)
set(MbedX509_LIBRARY    "${mbedtls_BINARY_DIR}/library/libmbedx509${CMAKE_STATIC_LIBRARY_SUFFIX}"  CACHE FILEPATH "" FORCE)

FetchContent_Declare(
    qmqtt
    GIT_REPOSITORY https://github.com/emqx/qmqtt.git
    GIT_TAG        master)

set(NO_EXAMPLES        ON  CACHE INTERNAL "")
set(NO_TESTS           ON  CACHE INTERNAL "")
set(NO_WEBSOCKETS      ON  CACHE INTERNAL "")
set(USE_MBEDTLS        ON  CACHE INTERNAL "")
set(USE_SYSTEM_SRTP    OFF CACHE INTERNAL "")
set(WARNINGS_AS_ERRORS OFF CACHE INTERNAL "")
FetchContent_Declare(
    datachannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG        v0.21.0)

FetchContent_MakeAvailable(qmqtt datachannel)

set(MBEDTLS_WEBRTC_DEFS
    MBEDTLS_SSL_DTLS_SRTP
    MBEDTLS_PK_WRITE_C
    MBEDTLS_X509_CRT_WRITE_C
    MBEDTLS_X509_CREATE_C
    MBEDTLS_PEM_WRITE_C)
target_compile_definitions(mbedcrypto  PUBLIC ${MBEDTLS_WEBRTC_DEFS})
target_compile_definitions(datachannel PUBLIC MBEDTLS_SSL_DTLS_SRTP)

set(MBEDTLS_LIBS
    "${mbedtls_BINARY_DIR}/library/libmbedtls${CMAKE_STATIC_LIBRARY_SUFFIX}"
    "${mbedtls_BINARY_DIR}/library/libmbedx509${CMAKE_STATIC_LIBRARY_SUFFIX}"
    "${mbedtls_BINARY_DIR}/library/libmbedcrypto${CMAKE_STATIC_LIBRARY_SUFFIX}")
target_link_libraries(datachannel PRIVATE -Wl,--start-group ${MBEDTLS_LIBS} -Wl,--end-group)

set(PROJECT_SOURCES main.cpp mainwindow.cpp mainwindow.h mainwindow.ui)

if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_add_executable(P2PClient MANUAL_FINALIZATION ${PROJECT_SOURCES})
else()
    add_executable(P2PClient ${PROJECT_SOURCES}
        p2pclient.h p2pclient.cpp)
endif()

target_link_libraries(P2PClient PRIVATE
    ${QT}::Widgets
    ${QT}::Network
    qmqtt
    datachannel
    mbedtls
    mbedx509
    mbedcrypto
    ${PLATFORM_LIBS})

set_target_properties(P2PClient PROPERTIES
    WIN32_EXECUTABLE TRUE)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(P2PClient)
endif()