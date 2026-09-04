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
egoboard_add_test(tst_sensitive)
egoboard_add_test(tst_expire)
target_sources(tst_expire PRIVATE ../src/app/ExpireScheduler.cpp ../src/app/SettingsManager.cpp)
target_include_directories(tst_expire PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/app ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(tst_expire PRIVATE KF6::ConfigCore)

egoboard_add_test(tst_bookmarks)
egoboard_add_test(tst_exportimport)
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

add_executable(tst_settings tst_settings.cpp ../src/app/SettingsManager.cpp)
target_include_directories(tst_settings PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/app ${CMAKE_CURRENT_SOURCE_DIR}/../src/core ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(tst_settings PRIVATE egoboard::core Qt6::Test KF6::ConfigCore)
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
add_executable(tst_datacontrol tst_datacontrol.cpp ../src/app/WlrDataControlHelper.cpp ../src/app/SettingsManager.cpp)
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

add_executable(tst_krunner tst_krunner.cpp ../src/app/EgoboardDbusAdaptor.cpp)
target_include_directories(tst_krunner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/app ${CMAKE_CURRENT_SOURCE_DIR}/../src/core ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(tst_krunner PRIVATE egoboard::core Qt6::Test Qt6::Sql Qt6::DBus)
target_compile_options(tst_krunner PRIVATE -Wall -Wextra)
add_test(NAME tst_krunner COMMAND tst_krunner)
