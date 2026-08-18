cmake_minimum_required(VERSION 3.24)

project(egoboard
    VERSION 0.1.0
    DESCRIPTION "KDE Plasma clipboard history manager"
    LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# --- Dependencies -------------------------------------------------------------

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Sql DBus Concurrent Network WaylandClient)
find_package(KF6Config 6.0 REQUIRED)
find_package(KF6GlobalAccel 6.0 REQUIRED)
find_package(KF6Notifications 6.0 REQUIRED)
find_package(KF6StatusNotifierItem 6.0 REQUIRED)
find_package(KF6WindowSystem 6.0 REQUIRED)
find_package(KF6XmlGui 6.0 REQUIRED)

# ECM provides FindWaylandScanner (the Wayland::Scanner target required by
# qt_generate_wayland_protocol_client_sources).
find_package(ECM REQUIRED NO_MODULE PATHS ${ECM_PATH} /usr/share/ECM)
list(APPEND CMAKE_MODULE_PATH "${ECM_MODULE_DIR}" "${ECM_FIND_MODULE_DIR}" /usr/share/ECM/find-modules)
find_package(WaylandScanner REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT IMPORTED_TARGET wayland-client)

# Optional: X11 XTest extension for auto-paste simulation on X11.
find_package(X11 COMPONENTS Xtst QUIET)

configure_file(cmake/Version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h")

# --- Core library (Qt Core + Sql only, no GUI/KF6 => unit-testable) -----------

add_library(egoboard_core STATIC
    src/core/IClipboardStorage.h # Q_OBJECT interface; AUTOMOC needs it listed
    src/core/DatabaseSchema.cpp
    src/core/StorageManager.cpp
    src/core/VacuumWorker.cpp
    src/core/BookmarkManager.cpp
    src/core/ExportImportManager.cpp
    src/core/SensitiveDataDetector.cpp
    src/core/ClipboardListModel.cpp
)
add_library(egoboard::core ALIAS egoboard_core)
target_include_directories(egoboard_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(egoboard_core
    PUBLIC Qt6::Core Qt6::Sql)

if(X11_FOUND AND X11_Xtst_FOUND)
    set(EGOBOARD_HAVE_XTEST ON)
endif()

# --- Application --------------------------------------------------------------

add_executable(egoboard
    src/main.cpp
    src/app/ApplicationContext.cpp
    src/app/SingleInstanceGuard.cpp
    src/app/SettingsManager.cpp
    src/app/ActiveWindowTracker.h
    src/app/X11ActiveWindowTracker.cpp
    src/app/WaylandActiveWindowTracker.cpp
    src/app/ClipboardWatcher.cpp
    src/app/AutoPaster.cpp
    src/app/HotkeyManager.cpp
    src/app/TrayController.cpp
    src/core/GroupTreeModel.cpp # Gui-dependent (QIcon in DecorationRole)
    src/app/ui/MainWindow.cpp
    src/app/ui/EntryDelegate.cpp
    src/app/ui/PreviewPane.cpp
    src/app/ui/QuickPasteMenu.cpp
    src/app/ui/GroupsDock.cpp
    src/app/ui/SettingsDialog.cpp
    src/app/ui/ExportImportDialogs.cpp
    data/resources.qrc
)
target_include_directories(egoboard PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/ui
    ${CMAKE_CURRENT_BINARY_DIR}/generated)

qt_generate_wayland_protocol_client_sources(egoboard
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/protocols/wlr-foreign-toplevel-management-unstable-v1.xml)
target_include_directories(egoboard PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(egoboard PRIVATE
    egoboard::core
    Qt6::Gui Qt6::Widgets Qt6::DBus Qt6::Concurrent Qt6::Network Qt6::WaylandClient
    KF6::ConfigCore
    KF6::GlobalAccel
    KF6::Notifications
    KF6::StatusNotifierItem
    KF6::WindowSystem
    KF6::XmlGui
    PkgConfig::WAYLAND_CLIENT)

target_compile_definitions(egoboard PRIVATE
    EGOBOARD_VERSION="${PROJECT_VERSION}")

if(EGOBOARD_HAVE_XTEST)
    target_compile_definitions(egoboard PRIVATE EGOBOARD_HAVE_XTEST=1)
    target_include_directories(egoboard PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(egoboard PRIVATE ${X11_Xtst_LIB} ${X11_LIBRARIES})
endif()

# --- Warnings -----------------------------------------------------------------

foreach(target egoboard_core egoboard)
    target_compile_options(${target} PRIVATE -Wall -Wextra)
endforeach()

# --- Tests --------------------------------------------------------------------

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# --- Install ------------------------------------------------------------------

include(GNUInstallDirs)
install(TARGETS egoboard DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/org.egoboard.Egoboard.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/egoboard.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
