add_library(krunner_egoboard MODULE EgoboardRunner.cpp)
target_link_libraries(krunner_egoboard PRIVATE KF6::Runner Qt6::DBus Qt6::Gui)
install(TARGETS krunner_egoboard DESTINATION ${KDE_INSTALL_PLUGINDIR}/kf6/krunner)
