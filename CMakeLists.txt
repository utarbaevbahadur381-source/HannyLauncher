cmake_minimum_required(VERSION 3.16)
project(HannyLauncher VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

set(SOURCES src/main.cpp src/Installer.cpp src/Installer.h resources.qrc)
if(WIN32)
    list(APPEND SOURCES hanny.rc)   # иконка .exe
endif()

add_executable(HannyLauncher WIN32 ${SOURCES})
target_link_libraries(HannyLauncher PRIVATE Qt6::Widgets Qt6::Network)
