cmake_minimum_required(VERSION 3.18)

project(Vehicle
    VERSION 1.0.0
    DESCRIPTION "Vehicle front target detection and behavior analysis system"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VEHICLE_BUILD_TESTS "Build Vehicle unit tests" ON)
option(VEHICLE_BUILD_QT_TERMINAL "Build the embedded Qt MQTT/CAN dashboard" OFF)
set(ONNXRUNTIME_ROOT "" CACHE PATH "Path to ONNX Runtime installation root")
set(VEHICLE_BUNDLED_ONNXRUNTIME_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime-linux-x64-1.26.0")

set(VEHICLE_ONNXRUNTIME_INCLUDE_HINTS
    ${ONNXRUNTIME_ROOT}/include
    $ENV{ONNXRUNTIME_ROOT}/include)
set(VEHICLE_ONNXRUNTIME_LIBRARY_HINTS
    ${ONNXRUNTIME_ROOT}/lib
    ${ONNXRUNTIME_ROOT}/lib64
    $ENV{ONNXRUNTIME_ROOT}/lib
    $ENV{ONNXRUNTIME_ROOT}/lib64)
if(NOT CMAKE_CROSSCOMPILING)
    list(APPEND VEHICLE_ONNXRUNTIME_INCLUDE_HINTS
        ${VEHICLE_BUNDLED_ONNXRUNTIME_ROOT}/include)
    list(APPEND VEHICLE_ONNXRUNTIME_LIBRARY_HINTS
        ${VEHICLE_BUNDLED_ONNXRUNTIME_ROOT}/lib
        ${VEHICLE_BUNDLED_ONNXRUNTIME_ROOT}/lib64)
endif()

find_package(OpenCV QUIET COMPONENTS core imgproc imgcodecs dnn highgui videoio)
find_package(CURL QUIET)
find_package(Threads REQUIRED)
find_path(MOSQUITTO_INCLUDE_DIR NAMES mosquitto.h)
find_library(MOSQUITTO_LIBRARY NAMES mosquitto)
find_path(ONNXRUNTIME_INCLUDE_DIR
    NAMES onnxruntime_cxx_api.h
    HINTS ${VEHICLE_ONNXRUNTIME_INCLUDE_HINTS}
)
find_library(ONNXRUNTIME_LIBRARY
    NAMES onnxruntime
    HINTS ${VEHICLE_ONNXRUNTIME_LIBRARY_HINTS}
)

add_library(vehicle_core
    src/business/analysis_pipeline.cpp
    src/business/telemetry_publisher.cpp
    src/core/behavior_analyzer.cpp
    src/core/detector.cpp
    src/core/tracker.cpp
    src/core/vehicle_state.cpp
    src/core/visualizer.cpp
    src/core/video_source.cpp
    src/infra/can_bus.cpp
    src/infra/http_client.cpp
    src/infra/mqtt_client.cpp
    src/infra/mjpeg_streamer.cpp
    src/infra/logger.cpp
    src/infra/performance_monitor.cpp
)

target_include_directories(vehicle_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(vehicle_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

target_link_libraries(vehicle_core PUBLIC
    stdc++fs
    Threads::Threads
)

if(OpenCV_FOUND)
    target_compile_definitions(vehicle_core PUBLIC VEHICLE_WITH_OPENCV=1)
    target_link_libraries(vehicle_core PUBLIC ${OpenCV_LIBS})
    message(STATUS "OpenCV enabled: ${OpenCV_VERSION}")
else()
    message(STATUS "OpenCV not found; Vehicle will build with synthetic detector/video source")
endif()

if(CURL_FOUND)
    target_compile_definitions(vehicle_core PUBLIC VEHICLE_WITH_CURL=1)
    target_link_libraries(vehicle_core PUBLIC CURL::libcurl)
    message(STATUS "libcurl enabled: ${CURL_VERSION}")
else()
    message(STATUS "libcurl not found; HTTP client will return service-unavailable responses")
endif()

if(MOSQUITTO_INCLUDE_DIR AND MOSQUITTO_LIBRARY)
    target_compile_definitions(vehicle_core PUBLIC VEHICLE_WITH_MOSQUITTO=1)
    target_include_directories(vehicle_core PUBLIC ${MOSQUITTO_INCLUDE_DIR})
    target_link_libraries(vehicle_core PUBLIC ${MOSQUITTO_LIBRARY})
    message(STATUS "libmosquitto enabled: ${MOSQUITTO_LIBRARY}")
else()
    message(STATUS "libmosquitto not found; MQTT client will log unavailable at runtime")
endif()

if(OpenCV_FOUND AND ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
    target_compile_definitions(vehicle_core PUBLIC VEHICLE_WITH_ONNXRUNTIME=1)
    target_include_directories(vehicle_core PUBLIC ${ONNXRUNTIME_INCLUDE_DIR})
    target_link_libraries(vehicle_core PUBLIC ${ONNXRUNTIME_LIBRARY})
    message(STATUS "ONNX Runtime enabled: ${ONNXRUNTIME_LIBRARY}")
elseif(NOT OpenCV_FOUND)
    message(STATUS "ONNX Runtime detector disabled because OpenCV preprocessing is unavailable")
else()
    message(STATUS "ONNX Runtime not found; set -DONNXRUNTIME_ROOT=/path/to/onnxruntime to enable YOLO11 inference")
endif()

add_executable(vehicle main.cpp)
target_link_libraries(vehicle PRIVATE vehicle_core)

if(VEHICLE_BUILD_QT_TERMINAL)
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)
    if(NOT MOSQUITTO_INCLUDE_DIR OR NOT MOSQUITTO_LIBRARY)
        message(FATAL_ERROR "vehicle_terminal requires libmosquitto development files")
    endif()

    set(CMAKE_AUTOMOC ON)
    add_executable(vehicle_terminal
        src/qt_terminal/main.cpp
        src/qt_terminal/dashboard_window.cpp
        src/qt_terminal/mjpeg_view.cpp
        src/qt_terminal/mqtt_bridge.cpp
        src/qt_terminal/socketcan_reader.cpp
        src/qt_terminal/dashboard_window.hpp
        src/qt_terminal/mjpeg_view.hpp
        src/qt_terminal/mqtt_bridge.hpp
        src/qt_terminal/socketcan_reader.hpp
    )
    target_include_directories(vehicle_terminal PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/qt_terminal
        ${MOSQUITTO_INCLUDE_DIR}
    )
    target_compile_options(vehicle_terminal PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )
    target_link_libraries(vehicle_terminal PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Network
        ${MOSQUITTO_LIBRARY}
        Threads::Threads
    )
endif()

if(VEHICLE_BUILD_TESTS)
    enable_testing()

    add_executable(vehicle_tests tests/test_pipeline.cpp)
    target_link_libraries(vehicle_tests PRIVATE vehicle_core)

    add_test(NAME vehicle_tests COMMAND vehicle_tests)
endif()
