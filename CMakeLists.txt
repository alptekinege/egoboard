cmake_minimum_required(VERSION 3.24)

project(egoboard
    VERSION 0.2.0
    DESCRIPTION "KDE Plasma clipboard history manager"
    LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Human-facing release label. CMake's project VERSION remains numeric for
# package tooling; the app and generated artifacts use this full label.
set(EGOBOARD_VERSION "0.2")

# --- Dependencies -------------------------------------------------------------

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Sql DBus Concurrent Network WaylandClient Qml)
find_package(KF6Config 6.0 REQUIRED)
find_package(KF6ConfigWidgets 6.0 REQUIRED) # KColorScheme: reads *.colors schemes
find_package(KF6WidgetsAddons 6.0 REQUIRED) # KColorButton: text color pickers
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

option(EGOBOARD_USE_SQLCIPHER "Enable SQLCipher at-rest encryption (opt-in, KWallet-held key)" OFF)

find_package(KF6Wallet QUIET)
if(EGOBOARD_USE_SQLCIPHER AND KF6Wallet_FOUND)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(SQLCIPHER QUIET sqlcipher)
        if(SQLCIPHER_FOUND)
            set(EGOBOARD_HAVE_SQLCIPHER ON)
        endif()
    endif()
endif()

# Optional: X11 XTest extension for auto-paste simulation on X11.
find_package(X11 COMPONENTS Xtst QUIET)

find_package(KF6Runner QUIET)
find_package(LayerShellQt QUIET)
if(LayerShellQt_FOUND)
    set(EGOBOARD_HAVE_LAYERSHELLQT ON)
endif()

configure_file(cmake/Version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h")

# --- Core library (Qt Core + Sql only, no GUI/KF6 => unit-testable) -----------

add_library(egoboard_core STATIC
    src/core/IClipboardStorage.h # Q_OBJECT interface; AUTOMOC needs it listed
    src/core/DatabaseSchema.cpp
    src/core/StorageManager.cpp
    src/core/SearchEngine.cpp
    src/core/TransformEngine.cpp
    src/core/SnippetManager.cpp
    src/core/VacuumWorker.cpp
    src/core/BookmarkManager.cpp
    src/core/ExportImportManager.cpp
    src/core/SensitiveDataDetector.cpp
    src/core/ExpirePolicy.cpp
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
    src/app/ColorSchemeIndex.cpp
    src/app/ThemeManager.cpp
    src/app/IconThemeIndex.cpp
    src/app/IconThemeManager.cpp
    src/app/TextAppearance.cpp
    src/app/SystemThemeWatcher.cpp
    src/app/LayerShellHelper.cpp
    src/app/KWinCursorTracker.cpp
    src/app/WlrDataControlHelper.cpp
    src/app/ActiveWindowTracker.h
    src/app/X11ActiveWindowTracker.cpp
    src/app/WaylandActiveWindowTracker.cpp
    src/app/ClipboardWatcher.cpp
    src/app/ExpireScheduler.cpp
    src/app/AutoPaster.cpp
    src/app/BackupService.cpp
    src/app/HotkeyManager.cpp
    src/app/TrayController.cpp
    src/app/ScriptActionManager.cpp
    src/app/EncryptionManager.cpp
    src/app/EgoboardDbusAdaptor.cpp
    src/krunner/EntryRow.cpp
    src/krunner/RunnerActions.cpp
    src/core/GroupTreeModel.cpp # Gui-dependent (QIcon in DecorationRole)
    src/app/ui/MainWindow.cpp
    src/app/ui/AppearancePreview.cpp
    src/app/CrashReport.cpp
    src/app/ui/DesignTokens.h
    src/app/ui/EntryDelegate.cpp
    src/app/ui/PreviewPane.cpp
    src/app/ui/QuickPasteMenu.cpp
    src/app/ui/GroupsDock.cpp
    src/app/ui/SettingsDialog.cpp
    src/app/ui/ExportImportDialogs.cpp
    src/app/ui/CommandPalette.cpp
    src/app/ui/PaletteCommands.cpp
    src/app/ui/ShortcutCheatsheet.cpp
    src/app/ui/TimelineStrip.cpp
    src/app/ui/SnippetDialog.cpp
    src/app/ui/TransformChainDialog.cpp
    src/app/ui/UiHelpers.cpp
    src/app/ui/UiHelpers.h
    src/app/OcrWorker.cpp
    src/app/ui/CodePreviewHighlighter.cpp
    data/resources.qrc
)
target_include_directories(egoboard PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/krunner
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/ui
    ${CMAKE_CURRENT_BINARY_DIR}/generated)

qt_generate_wayland_protocol_client_sources(egoboard
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/protocols/wlr-foreign-toplevel-management-unstable-v1.xml
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/protocols/wlr-data-control-unstable-v1.xml)
target_include_directories(egoboard PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(egoboard PRIVATE
    egoboard::core
    Qt6::Gui Qt6::Widgets Qt6::DBus Qt6::Concurrent Qt6::Network Qt6::WaylandClient Qt6::Qml
    KF6::ConfigCore
    KF6::ConfigWidgets
    KF6::WidgetsAddons
    KF6::GlobalAccel
    KF6::Notifications
    KF6::StatusNotifierItem
    KF6::WindowSystem
    KF6::XmlGui
    PkgConfig::WAYLAND_CLIENT)

target_compile_definitions(egoboard PRIVATE
    EGOBOARD_VERSION="${EGOBOARD_VERSION}")

if(EGOBOARD_HAVE_LAYERSHELLQT)
    target_link_libraries(egoboard PRIVATE LayerShellQt::Interface)
    target_compile_definitions(egoboard PRIVATE EGOBOARD_HAVE_LAYERSHELLQT=1)
endif()

if(EGOBOARD_HAVE_XTEST)
    target_compile_definitions(egoboard PRIVATE EGOBOARD_HAVE_XTEST=1)
    target_include_directories(egoboard PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(egoboard PRIVATE ${X11_Xtst_LIB} ${X11_LIBRARIES})
endif()

if(EGOBOARD_HAVE_SQLCIPHER)
    target_compile_definitions(egoboard PRIVATE EGOBOARD_HAVE_SQLCIPHER=1)
    target_link_libraries(egoboard PRIVATE KF6::Wallet)
    if(TARGET PkgConfig::SQLCIPHER)
        target_link_libraries(egoboard PRIVATE PkgConfig::SQLCIPHER)
    endif()
endif()

if(KF6Runner_FOUND)
    add_subdirectory(src/krunner)
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
