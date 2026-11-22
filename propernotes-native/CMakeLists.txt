# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.24)
project(ProperNotesNative VERSION 0.3.5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(PDF4QT_SOURCE_DIR "" CACHE PATH "Existing pinned PDF4QT source checkout")
if(NOT EXISTS "${PDF4QT_SOURCE_DIR}/Pdf4QtLibCore/CMakeLists.txt")
    message(FATAL_ERROR "Supply the existing pinned PDF4QT checkout with PDF4QT_SOURCE_DIR. This entry does not download or install dependencies.")
endif()
set(PDF4QT_BUILD_ONLY_CORE_LIBRARY ON CACHE BOOL "" FORCE)
set(PDF4QT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PDF4QT_ENABLE_OCR OFF CACHE BOOL "" FORCE)
set(PDF4QT_OCR_REQUIRED OFF CACHE BOOL "" FORCE)
set(PDF4QT_ENABLE_COVERAGE OFF CACHE BOOL "" FORCE)
set(PDF4QT_INSTALL_DEPENDENCIES OFF CACHE BOOL "" FORCE)
set(PDF4QT_INSTALL_QT_DEPENDENCIES OFF CACHE BOOL "" FORCE)
set(PDF4QT_INSTALL_MSVC_REDISTRIBUTABLE OFF CACHE BOOL "" FORCE)
set(PDF4QT_INSTALL_PREPARE_WIX_INSTALLER OFF CACHE BOOL "" FORCE)
set(PDF4QT_INSTALL_INCLUDE OFF CACHE BOOL "" FORCE)
add_subdirectory("${PDF4QT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/pdf4qt")
# PDF4QT writes config.h into its own binary directory. Its standalone build
# finds that through the top-level binary include, which changes when embedded.
target_include_directories(Pdf4QtLibCore PRIVATE "${CMAKE_BINARY_DIR}/pdf4qt")

find_package(Qt6 6.11.3 EXACT REQUIRED COMPONENTS Core Gui Widgets Svg Xml Network)
qt_add_executable(ProperNotesNative WIN32
    src/main.cpp
    src/MainWindow.h src/MainWindow.cpp
    src/MainWindowUi.cpp src/MainWindowActions.cpp src/DocumentSession.h
    src/MainWindowReview.cpp
    src/MainWindowWorkspace.cpp src/MainWindowState.h
    src/WorkspaceStore.h src/WorkspaceStore.cpp
    src/FavoriteTools.h src/FavoriteTools.cpp src/FavoritesBar.h src/FavoritesBar.cpp
    src/InkCanvas.h src/InkCanvas.cpp
    src/InkData.h
    src/PdfDocument.h src/PdfDocument.cpp
    src/PdfDocumentText.cpp src/PdfTextLayer.h src/PdfTextLayer.cpp
    src/AppPreferences.h src/AppPreferences.cpp
    src/SettingsDialog.h src/SettingsDialog.cpp
    src/BackupStore.h src/BackupStore.cpp
    src/DocumentView.h src/DocumentView.cpp
    src/ThumbnailBrowser.h src/ThumbnailBrowser.cpp
    src/ToolPreferences.h
)
target_link_libraries(ProperNotesNative PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::Xml Qt6::Network Pdf4QtLibCore)
target_compile_definitions(ProperNotesNative PRIVATE QT_NO_EMIT NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(ProperNotesNative PRIVATE /utf-8 /W4)
    target_compile_options(Pdf4QtLibCore PRIVATE /MP3)
endif()
set_target_properties(ProperNotesNative PROPERTIES
    OUTPUT_NAME "ProperNotes.Native"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
install(TARGETS ProperNotesNative RUNTIME DESTINATION .)
