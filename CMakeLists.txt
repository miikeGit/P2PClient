cmake_minimum_required(VERSION 3.16)

project(P2PClient VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)

include(FetchContent)
FetchContent_Declare(
    qmqtt
    GIT_REPOSITORY https://github.com/emqx/qmqtt.git
    GIT_TAG master
)
FetchContent_MakeAvailable(qmqtt)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(P2PClient MANUAL_FINALIZATION ${PROJECT_SOURCES})
else()
    add_executable(P2PClient ${PROJECT_SOURCES})
endif()

target_link_libraries(P2PClient PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    qmqtt
)

set_target_properties(P2PClient PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(P2PClient)
endif()