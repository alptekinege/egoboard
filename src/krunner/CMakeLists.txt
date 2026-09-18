find_package(KF6I18n REQUIRED) # KRunner itself depends on KI18n
find_package(KF6Service QUIET)
find_package(KF6KIO QUIET)

add_library(krunner_egoboard MODULE EgoboardRunner.cpp EntryRow.cpp RunnerActions.cpp)
target_link_libraries(krunner_egoboard PRIVATE KF6::Runner KF6::I18n Qt6::DBus Qt6::Gui)
target_include_directories(krunner_egoboard PRIVATE ${CMAKE_SOURCE_DIR}/src/core)

# Launching through the desktop entry is what makes the "Start Egoboard"
# match work for AppImage installs, which are not on PATH.
if(KF6Service_FOUND AND KF6KIO_FOUND)
    # ApplicationLauncherJob lives in KIOGui.
    target_link_libraries(krunner_egoboard PRIVATE KF6::Service KF6::KIOGui)
    target_compile_definitions(krunner_egoboard PRIVATE EGOBOARD_HAVE_KSERVICE=1)
endif()

install(TARGETS krunner_egoboard DESTINATION ${KDE_INSTALL_PLUGINDIR}/kf6/krunner)
