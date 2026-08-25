find_package(Qt6 6.8 REQUIRED COMPONENTS Core Test Sql)

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
egoboard_add_test(tst_bookmarks)
egoboard_add_test(tst_exportimport)
egoboard_add_test(tst_transforms)
egoboard_add_test(tst_snippets)

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
