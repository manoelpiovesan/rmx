cmake_minimum_required(VERSION 3.21)

project(XPadLink VERSION 0.2.0 LANGUAGES CXX)

option(XPAD_USE_ABLETON_LINK "Build with Ableton Link SDK support" OFF)
set(ABLETON_LINK_DIR "" CACHE PATH "Path to Ableton Link SDK root")

if(ABLETON_LINK_DIR STREQUAL "" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ableton-link/include/ableton/Link.hpp")
    set(ABLETON_LINK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ableton-link" CACHE PATH "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(FetchContent)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# --- Fetch nlohmann/json library (header-only) --------------------------------
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
)
FetchContent_GetProperties(nlohmann_json)
if(NOT nlohmann_json_POPULATED)
    FetchContent_Populate(nlohmann_json)
    # Create a dummy library just for the include path
    add_library(nlohmann_json INTERFACE)
    target_include_directories(nlohmann_json INTERFACE "${nlohmann_json_SOURCE_DIR}/single_include")
endif()
set(NLOHMANN_JSON_INCLUDE_DIR "${nlohmann_json_SOURCE_DIR}/single_include")

pkg_check_modules(RTMIDI     REQUIRED rtmidi)
pkg_check_modules(RUBBERBAND REQUIRED rubberband)
pkg_check_modules(GLFW3      REQUIRED glfw3)
find_package(OpenGL REQUIRED)

# --- Third-party paths -------------------------------------------------------
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
set(MINIAUDIO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# --- ImGui library -----------------------------------------------------------
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(imgui PUBLIC ${GLFW3_LIBRARIES} OpenGL::GL)
target_compile_options(imgui PRIVATE -w)  # suppress imgui warnings

# --- Core library ------------------------------------------------------------
add_library(xpad_core STATIC
    src/app/App.cpp
    src/audio/AudioEngine.cpp
    src/audio/AudioScheduler.cpp
    src/audio/TimeStretch.cpp
    src/config/Config.cpp
    src/gui/MainWindow.cpp
    src/link/LinkManager.cpp
    src/midi/MidiManager.cpp
    src/samples/SampleLoader.cpp
)

target_include_directories(xpad_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${MINIAUDIO_DIR}
    ${RTMIDI_INCLUDE_DIRS}
    ${RUBBERBAND_INCLUDE_DIRS}
    ${GLFW3_INCLUDE_DIRS}
)

target_link_libraries(xpad_core PUBLIC
    Threads::Threads
    imgui
    nlohmann_json
    ${RTMIDI_LIBRARIES}
    ${RUBBERBAND_LIBRARIES}
    ${GLFW3_LIBRARIES}
    OpenGL::GL
)

if(UNIX)
    target_link_libraries(xpad_core PUBLIC dl m)
    if(NOT APPLE)
        target_link_libraries(xpad_core PUBLIC asound)
    endif()
endif()

target_compile_features(xpad_core PUBLIC cxx_std_20)
target_compile_definitions(xpad_core PUBLIC
    $<$<BOOL:${XPAD_USE_ABLETON_LINK}>:XPAD_HAS_ABLETON_LINK=1>
)

if(MSVC)
    target_compile_options(xpad_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(xpad_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --- Ableton Link integration ------------------------------------------------
if(XPAD_USE_ABLETON_LINK)
    if(NOT EXISTS "${ABLETON_LINK_DIR}/include/ableton/Link.hpp")
        message(FATAL_ERROR "XPAD_USE_ABLETON_LINK=ON requires ABLETON_LINK_DIR pointing to the Ableton Link SDK root")
    endif()
    if(NOT EXISTS "${ABLETON_LINK_DIR}/modules/asio-standalone/asio/include/asio.hpp")
        message(FATAL_ERROR "Ableton Link SDK is missing the asio-standalone submodule.\nRun: git -C ${ABLETON_LINK_DIR} submodule update --init --recursive")
    endif()

    include("${ABLETON_LINK_DIR}/cmake_include/ConfigureAbletonLink.cmake")
    ConfigureAbletonLink("${ABLETON_LINK_DIR}")
    target_link_libraries(xpad_core PUBLIC Ableton::Link)

    # Windows socket libraries needed for Ableton Link networking
    if(WIN32)
        target_link_libraries(xpad_core PUBLIC ws2_32 iphlpapi wsock32)
    endif()
endif()

# --- Executables -------------------------------------------------------------
add_executable(xpad_link src/main.cpp)
target_link_libraries(xpad_link PRIVATE xpad_core)

add_executable(xpad_link_tests tests/LinkManagerTests.cpp)
target_link_libraries(xpad_link_tests PRIVATE xpad_core)

add_test(NAME xpad_link_tests COMMAND xpad_link_tests)
