cmake_minimum_required(VERSION 3.21)
project(music_player LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Оптимизация по размеру: разные флаги для MSVC и GCC/Clang
if(MSVC)
  set(CMAKE_CXX_FLAGS_RELEASE "/O1 /DNDEBUG")
else()
  set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG")
endif()

# IPO включаем только если поддерживается
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
  message(WARNING "IPO not supported: ${ipo_error}")
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Network)

# TagLib: на Windows/vcpkg используем CONFIG-режим,
# на Linux можно оставить pkg-config как fallback.
find_package(TagLib CONFIG QUIET)
if(NOT TagLib_FOUND)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)
  set(TAGLIB_TARGET PkgConfig::TAGLIB)
else()
  set(TAGLIB_TARGET TagLib::tag)
endif()

# --- miniaudio ---
add_library(miniaudio STATIC third_party/miniaudio/miniaudio_impl.c)
target_include_directories(miniaudio PUBLIC third_party/miniaudio)

# ВАЖНО: явно указываем, что линкуется как C, и отключаем Qt-инструменты,
# которые ломают обработку чистого C-файла.
set_target_properties(miniaudio PROPERTIES
  LINKER_LANGUAGE C
  AUTOMOC OFF
  AUTOUIC OFF
  AUTORCC OFF
)

if(UNIX AND NOT APPLE)
  find_package(Threads REQUIRED)
  target_link_libraries(miniaudio PUBLIC Threads::Threads dl m)
endif()
# На Windows miniaudio сам подтянет WASAPI/DirectSound, доп. линковка не нужна.

# --- приложение ---
# WIN32 keeps the process off the console subsystem, so double-clicking
# the exe does not open a terminal window.
qt_add_executable(music_player WIN32
  src/sandplay.rc
  src/main.cpp
  src/db/Database.cpp
  src/audio/AudioEngine.cpp
  src/audio/PlaybackQueue.cpp
  src/library/LibraryManager.cpp
  src/library/CoverCache.cpp
  src/library/CoverFetcher.cpp
  src/ui/CoverThumb.cpp
  src/ui/MainWindow.cpp
  src/ui/PlayerPanel.cpp
  src/ui/SkinCanvas.cpp
  src/ui/DraggableItem.cpp
  src/ui/items/PlayPauseButtonItem.cpp
  src/ui/items/SeekBarItem.cpp
  src/ui/items/CoverArtItem.cpp
  src/ui/items/TrackLabelItem.cpp
  src/ui/items/TextButtonItem.cpp
  src/ui/items/VolumeBarItem.cpp
  src/models/TrackTableModel.cpp
  src/models/PlaylistModel.cpp
  src/models/QueueModel.cpp
)

if(WIN32)
    find_program(WINDEPLOYQT_EXE windeployqt
        HINTS "${Qt6_DIR}/../../../bin"
    )
    if(WINDEPLOYQT_EXE)
        add_custom_command(TARGET music_player POST_BUILD
            COMMAND "${WINDEPLOYQT_EXE}" "$<TARGET_FILE:music_player>"
            COMMENT "Running windeployqt..."
        )
    endif()
endif()

set_target_properties(music_player PROPERTIES OUTPUT_NAME SandPlay)

target_include_directories(music_player PRIVATE src)
target_link_libraries(music_player PRIVATE
  Qt6::Widgets Qt6::Sql Qt6::Network
  ${TAGLIB_TARGET}
  miniaudio
)