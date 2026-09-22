find_package(Qt6 6.8 REQUIRED COMPONENTS Core Test Sql Gui)
find_package(KF6Config 6.0 REQUIRED)

# Unit tests run against the core library (egoboard_core: Qt Core + Sql only),
# each on its own temporary database file.

function(egoboard_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE egoboard::core Qt6::Test Qt6::Sql)
    target_compile_options(${name} PRIVATE -Wall -Wextra)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

egoboard_add_test(tst_storage)
egoboard_add_test(tst_schema)
egoboard_add_test(tst_vacuum)
# Auto-paste MIME reconstruction is headless-safe with the offscreen platform
add_executable(tst_autopaster tst_autopaster.cpp ../src/app/AutoPaster.cpp ../src/app/ClipboardWatcher.cpp ../src/app/SettingsManager.cpp ../src/app/ColorSchemeIndex.cpp ../src/app/IconThemeIndex.cpp)
target_include_directories(tst_autopaster PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/app ${CMAKE_CURRENT_SOURCE_DIR}/../src/core ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(tst_autopaster PRIVATE egoboard::core Qt6::Gui Qt6::Widgets Qt6::Test KF6::ConfigCore KF6::Notifications)
target_compile_options(tst_autopaster PRIVATE -Wall -Wextra)
add_test(NAME tst_autopaster COMMAND tst_autopaster)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)
find_package(KF6ConfigWidgets 6.0 REQUIRED) # KColorScheme, used to build theme expectations
add_executable(tst_ui tst_ui.cpp ../src/app/ThemeManager.cpp ../src/app/TextAppearance.cpp ../src/app/ColorSchemeIndex.cpp ../src/app/IconThemeManager.cpp ../src/app/IconThemeIndex.cpp ../src/app/ui/CodePreviewHighlighter.cpp)
target_include_directories(tst_ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/app ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/ui ${CMAKE_CURRENT_SOURCE_DIR}/../src/core ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(tst_ui PRIVATE Qt6::Widgets Qt6::Gui Qt6::Test KF6::ConfigCore KF6::ConfigWidgets)
target_compile_options(tst_ui PRIVATE -Wall -Wextra)
add_test(NAME tst_ui COMMAND tst_ui)

# Clipboard classification and privacy-policy tests use the offscreen QClipboard
add_executable(tst_clipboardwatcher tst_clipboardwatcher.cpp ../src/app/ClipboardWatcher.cpp ../src/app/SettingsManager.cpp ../src/app/ColorSchemeIndex.cpp ../src/app/IconThemeIndex.cpp)
target_include_directories(tst_clipboardwatcher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/app ${CMAKE_CURRENT_SOURCE_DIR}/../src/core ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(tst_clipboardwatcher PRIVATE egoboard::core Qt6::Gui Qt6::Test KF6::ConfigCore)
target_compile_options(tst_clipboardwatcher PRIVATE -Wall -Wextra)
add_test(NAME tst_clipboardwatcher COMMAND tst_clipboardwatcher)

# OCR boundary tests; recognition assertions remain environment-independent
find_package(Qt6 6.8 REQUIRED COMPONENTS Concurrent)
add_executable(tst_ocrworker tst_ocrworker.cpp ../src/app/OcrWorker.cpp)
target_include_directories(tst_ocrworker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/app ${CMAKE_CURRENT_SOURCE_DIR}/../src/core)
target_link_libraries(tst_ocrworker PRIVATE egoboard::core Qt6::Core Qt6::Gui Qt6::Concurrent Qt6::Test)
target_compile_options(tst_ocrworker PRIVATE -Wall -Wextra)
add_test(NAME tst_ocrworker COMMAND tst_ocrworker)

egoboard_add_test(tst_sensitive)
egoboard_add_test(tst_expire)
target_sources(tst_expire PRIVATE ../src/app/ExpireScheduler.cpp ../src/app/SettingsManager.cpp ../src/app/ColorSchemeIndex.cpp ../src/app/IconThemeIndex.cpp)
target_include_directories(tst_expire PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/app ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(tst_expire PRIVATE KF6::ConfigCore Qt6::Gui)

egoboard_add_test(tst_bookmarks)
egoboard_add_test(tst_exportimport)

# Automatic backups: service + worker thread + export pipeline end to end
add_executable(tst_backupservice tst_backupservice.cpp ../src/app/BackupService.cpp ../src/app/SettingsManager.cpp ../src/app/ColorSchemeIndex.cpp ../src/app/IconThemeIndex.cpp)
target_include_directories(tst_backupservice PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/app ${CMAKE_CURRENT_SOURCE_DIR}/../src/core ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(tst_backupservice PRIVATE egoboard::core Qt6::Test Qt6::Gui KF6::ConfigCore)
target_compile_options(tst_backupservice PRIVATE -Wall -Wextra)
add_test(NAME tst_backupservice COMMAND tst_backupservice)
egoboard_add_test(tst_transforms)
egoboard_add_test(tst_snippets)
egoboard_add_test(tst_encryption)
target_sources(tst_encryption PRIVATE ../src/app/EncryptionManager.cpp)
target_include_directories(tst_encryption PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/app ${CMAKE_CURRENT_SOURCE_DIR}/../src)
egoboard_add_test(tst_searchengine)
egoboard_add_test(tst_listmodel)

add_executable(tst_grouptreemodel tst_grouptreemodel.cpp ../src/core/GroupTreeModel.cpp)
target_include_directories(tst_grouptreemodel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/core ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(tst_grouptreemodel PRIVATE egoboard::core Qt6::Test Qt6::Gui)
target_compile_options(tst_grouptreemodel PRIVATE -Wall -Wextra)
add_test(NAME tst_grouptreemodel COMMAND tst_grouptreemodel)

add_executable(tst_singleinstance tst_singleinstance.cpp ../src/app/SingleInstanceGuard.cpp)
target_include_directories(tst_singleinstance PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/app ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(tst_singleinstance PRIVATE Qt6::Core Qt6::Network Qt6::Test)
target_compile_options(tst_singleinstance PRIVATE -Wall -Wextra)
add_test(NAME tst_singleinstance COMMAND tst_singleinstance)

add_executable(tst_settings tst_settings.cpp ../src/app/SettingsManager.cpp ../src/app/ColorSchemeIndex.cpp ../src/app/IconThemeIndex.cpp ../src/app/SystemThemeWatcher.cpp)
target_include_directories(tst_settings PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/app ${CMAKE_CURRENT_SOURCE_DIR}/../src/core ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(tst_settings PRIVATE egoboard::core Qt6::Test Qt6::Gui KF6::ConfigCore)
target_compile_options(tst_settings PRIVATE -Wall -Wextra)
add_test(NAME tst_settings COMMAND tst_settings)

find_package(Qt6 6.8 REQUIRED COMPONENTS Qml)
add_executable(tst_scripts tst_scripts.cpp ../src/app/ScriptActionManager.cpp)
target_include_directories(tst_scripts PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/app ${CMAKE_CURRENT_SOURCE_DIR}/../src/core ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(tst_scripts PRIVATE egoboard::core Qt6::Test Qt6::Qml)
target_compile_options(tst_scripts PRIVATE -Wall -Wextra)
add_test(NAME tst_scripts COMMAND tst_scripts)

# Layer-shell helper: needs Gui + LayerShellHelper (app-level), but test is headless-safe (offscreen)
find_package(Qt6 6.8 REQUIRED COMPONENTS Gui)
add_executable(tst_layershell tst_layershell.cpp ../src/app/LayerShellHelper.cpp)
target_include_directories(tst_layershell PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/app ${CMAKE_CURRENT_SOURCE_DIR}/../src/core)
target_link_libraries(tst_layershell PRIVATE Qt6::Gui Qt6::Test)
if(TARGET LayerShellQt::Interface)
    target_link_libraries(tst_layershell PRIVATE LayerShellQt::Interface)
    target_compile_definitions(tst_layershell PRIVATE EGOBOARD_HAVE_LAYERSHELLQT=1)
endif()
target_compile_options(tst_layershell PRIVATE -Wall -Wextra)
add_test(NAME tst_layershell COMMAND tst_layershell)

# wlr-data-control helper: Gui + WaylandClient, headless-safe offscreen test
find_package(Qt6 6.8 REQUIRED COMPONENTS WaylandClient)
find_package(KF6Config 6.0 QUIET)
pkg_check_modules(WAYLAND_CLIENT QUIET IMPORTED_TARGET wayland-client)
add_executable(tst_datacontrol tst_datacontrol.cpp ../src/app/WlrDataControlHelper.cpp ../src/app/SettingsManager.cpp ../src/app/ColorSchemeIndex.cpp ../src/app/IconThemeIndex.cpp)
target_include_directories(tst_datacontrol PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_BINARY_DIR}/../build
    ${CMAKE_CURRENT_BINARY_DIR}/..
)
# generated qwayland protocol headers live in the top build dir
target_include_directories(tst_datacontrol PRIVATE ${CMAKE_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/../build)
if(KF6Config_FOUND)
    target_link_libraries(tst_datacontrol PRIVATE KF6::ConfigCore)
endif()
target_link_libraries(tst_datacontrol PRIVATE egoboard::core Qt6::Gui Qt6::WaylandClient Qt6::Test)
if(TARGET PkgConfig::WAYLAND_CLIENT)
    target_link_libraries(tst_datacontrol PRIVATE PkgConfig::WAYLAND_CLIENT)
endif()
qt_generate_wayland_protocol_client_sources(tst_datacontrol
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/protocols/wlr-data-control-unstable-v1.xml)
target_compile_options(tst_datacontrol PRIVATE -Wall -Wextra)
add_test(NAME tst_datacontrol COMMAND tst_datacontrol)

add_executable(tst_krunner tst_krunner.cpp ../src/app/EgoboardDbusAdaptor.cpp ../src/krunner/EntryRow.cpp ../src/krunner/RunnerActions.cpp)
target_include_directories(tst_krunner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/app ${CMAKE_CURRENT_SOURCE_DIR}/../src/krunner ${CMAKE_CURRENT_SOURCE_DIR}/../src/core ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(tst_krunner PRIVATE egoboard::core Qt6::Test Qt6::Sql Qt6::DBus)
target_compile_options(tst_krunner PRIVATE -Wall -Wextra)
add_test(NAME tst_krunner COMMAND tst_krunner)

# Snippet shortcut resolution: pure logic, no KGlobalAccel instance is created
find_package(KF6GlobalAccel 6.0 REQUIRED)
find_package(KF6XmlGui 6.0 REQUIRED) # KActionCollection
add_executable(tst_hotkeys tst_hotkeys.cpp ../src/app/HotkeyManager.cpp)
target_include_directories(tst_hotkeys PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/app ${CMAKE_CURRENT_SOURCE_DIR}/../src/core ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(tst_hotkeys PRIVATE egoboard::core Qt6::Test Qt6::Gui KF6::GlobalAccel KF6::XmlGui)
target_compile_options(tst_hotkeys PRIVATE -Wall -Wextra)
add_test(NAME tst_hotkeys COMMAND tst_hotkeys)

# The palette's '>' command language: parsing, aliases, completion — no widgets
add_executable(tst_palette tst_palette.cpp ../src/app/ui/PaletteCommands.cpp)
target_include_directories(tst_palette PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/app ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/ui ${CMAKE_CURRENT_SOURCE_DIR}/../src/core)
target_link_libraries(tst_palette PRIVATE Qt6::Core Qt6::Test)
target_compile_options(tst_palette PRIVATE -Wall -Wextra)
add_test(NAME tst_palette COMMAND tst_palette)

# The palette widget itself: mode switching, argument completion, signals.
# Runs offscreen and against a temporary database.
add_executable(tst_paletteui tst_paletteui.cpp
    ../src/app/ui/CommandPalette.cpp
    ../src/app/ui/PaletteCommands.cpp
    ../src/app/ScriptActionManager.cpp
    ../src/app/ui/UiHelpers.cpp
    ../src/app/TextAppearance.cpp
    ../src/app/ScriptActionManager.cpp)
target_include_directories(tst_paletteui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/app ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/ui ${CMAKE_CURRENT_SOURCE_DIR}/../src/core ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(tst_paletteui PRIVATE egoboard::core Qt6::Widgets Qt6::Test Qt6::Qml)
target_compile_options(tst_paletteui PRIVATE -Wall -Wextra)
add_test(NAME tst_paletteui COMMAND tst_paletteui)

# Design polish (Track M): the colors the delegate, palette, timeline and code
# highlighter derive must stay readable on every installed scheme, and the row /
# timeline geometry must not drift. Headless, offscreen, same pattern as tst_paletteui.
add_executable(tst_uidesign tst_uidesign.cpp
    ../src/app/ui/EntryDelegate.cpp
    ../src/app/ui/UiHelpers.cpp
    ../src/app/ui/ExportImportDialogs.cpp
    ../src/app/ui/CodePreviewHighlighter.cpp
    ../src/app/SettingsManager.cpp
    ../src/app/TextAppearance.cpp
    ../src/app/ColorSchemeIndex.cpp
    ../src/app/IconThemeIndex.cpp)
target_include_directories(tst_uidesign PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/app ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/ui ${CMAKE_CURRENT_SOURCE_DIR}/../src/core ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(tst_uidesign PRIVATE egoboard::core Qt6::Widgets Qt6::Sql Qt6::Concurrent Qt6::Test KF6::ConfigCore KF6::ConfigWidgets)
target_compile_options(tst_uidesign PRIVATE -Wall -Wextra)
add_test(NAME tst_uidesign COMMAND tst_uidesign)

# Wheel-over-tray-icon state machine (header-only, so no extra sources)
add_executable(tst_traycycle tst_traycycle.cpp)
target_include_directories(tst_traycycle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/app ${CMAKE_CURRENT_SOURCE_DIR}/../src/core)
target_link_libraries(tst_traycycle PRIVATE Qt6::Core Qt6::Test)
target_compile_options(tst_traycycle PRIVATE -Wall -Wextra)
add_test(NAME tst_traycycle COMMAND tst_traycycle)

# Crash/support reports: QtCore-only (schema, redaction, gdb/ELF parsing), no
# GUI or database, so the report path itself never depends on the app running.
add_executable(tst_crashreport tst_crashreport.cpp ../src/app/CrashReport.cpp)
target_include_directories(tst_crashreport PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/app)
target_link_libraries(tst_crashreport PRIVATE Qt6::Core Qt6::Test)
target_compile_options(tst_crashreport PRIVATE -Wall -Wextra)
add_test(NAME tst_crashreport COMMAND tst_crashreport)
