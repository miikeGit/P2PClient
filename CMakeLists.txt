cmake_minimum_required(VERSION 3.16)
project(P2PClient VERSION 0.1 LANGUAGES CXX C)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    set(PLATFORM_LIBS ws2_32 iphlpapi)
endif()

find_package(Qt6 QUIET COMPONENTS Widgets Network Test)
if(NOT Qt6_FOUND)
    find_package(Qt5 REQUIRED COMPONENTS Widgets Network Test)
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

file(APPEND "${mbedtls_SOURCE_DIR}/include/mbedtls/mbedtls_config.h" "
#define MBEDTLS_SSL_DTLS_SRTP
#define MBEDTLS_PK_WRITE_C
#define MBEDTLS_X509_CRT_WRITE_C
#define MBEDTLS_X509_CREATE_C
#define MBEDTLS_PEM_WRITE_C
")

set(MbedTLS_INCLUDE_DIR "${mbedtls_SOURCE_DIR}/include" CACHE PATH "MbedTLS include dir" FORCE)

if(MSVC)
    set(MbedTLS_LIBRARY    "${mbedtls_BINARY_DIR}/library/${CMAKE_CFG_INTDIR}/mbedtls.lib"    CACHE FILEPATH "MbedTLS library" FORCE)
    set(MbedCrypto_LIBRARY "${mbedtls_BINARY_DIR}/library/${CMAKE_CFG_INTDIR}/mbedcrypto.lib" CACHE FILEPATH "MbedCrypto library" FORCE)
    set(MbedX509_LIBRARY   "${mbedtls_BINARY_DIR}/library/${CMAKE_CFG_INTDIR}/mbedx509.lib"   CACHE FILEPATH "MbedX509 library" FORCE)
else()
    set(MbedTLS_LIBRARY    "${mbedtls_BINARY_DIR}/library/libmbedtls.a"    CACHE FILEPATH "MbedTLS library" FORCE)
    set(MbedCrypto_LIBRARY "${mbedtls_BINARY_DIR}/library/libmbedcrypto.a" CACHE FILEPATH "MbedCrypto library" FORCE)
    set(MbedX509_LIBRARY   "${mbedtls_BINARY_DIR}/library/libmbedx509.a"   CACHE FILEPATH "MbedX509 library" FORCE)
endif()
set(MbedTLS_FOUND TRUE CACHE BOOL "MbedTLS found status" FORCE)

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

if(MSVC)
    target_link_libraries(datachannel PRIVATE mbedtls mbedx509 mbedcrypto)
else()
    target_link_libraries(datachannel PRIVATE -Wl,--start-group mbedtls mbedx509 mbedcrypto -Wl,--end-group)
endif()

set(COMMON_SOURCES
    p2pclient.h p2pclient.cpp
    filetransfermanager.h filetransfermanager.cpp
    appconfig.h appconfig.cpp
    xxhash.h xxhash.c
)

set(PROJECT_SOURCES main.cpp mainwindow.cpp mainwindow.h mainwindow.ui ${COMMON_SOURCES})

if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_add_executable(P2PClient MANUAL_FINALIZATION ${PROJECT_SOURCES})
else()
    add_executable(P2PClient ${PROJECT_SOURCES} config.json)
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

set_target_properties(P2PClient PROPERTIES WIN32_EXECUTABLE TRUE)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(P2PClient)
endif()

add_custom_command(TARGET P2PClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/config.json"
    "$<TARGET_FILE_DIR:P2PClient>/config.json"
)

enable_testing()

set(TEST_SOURCES test_main.cpp ${COMMON_SOURCES})

if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_add_executable(P2PTests MANUAL_FINALIZATION ${TEST_SOURCES})
else()
    add_executable(P2PTests ${TEST_SOURCES})
endif()

target_link_libraries(P2PTests PRIVATE
    ${QT}::Core
    ${QT}::Network
    ${QT}::Test
    qmqtt
    datachannel
    mbedtls
    mbedx509
    mbedcrypto
    ${PLATFORM_LIBS}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(P2PTests)
endif()

add_test(NAME P2PBasicTests COMMAND P2PTests)