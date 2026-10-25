cmake_minimum_required(VERSION 3.25)

project(PersonalProductivityApp
    VERSION 0.1.0
    DESCRIPTION "A personal productivity service for the Linux desktop"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

include(GNUInstallDirs)
set(CMAKE_INSTALL_LIBDIR "lib/personal-productivity-app")
set(CMAKE_INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
# Qt captures these deployment destinations while its CMake package loads.
# Keep them inside the app-owned directory so uninstalling cannot remove files
# shared with another Qt application.
set(QT_DEPLOY_LIB_DIR "${CMAKE_INSTALL_LIBDIR}")
set(QT_DEPLOY_PLUGINS_DIR "${CMAKE_INSTALL_LIBDIR}/plugins")

find_package(Qt6 6.12 REQUIRED COMPONENTS Widgets Sql DBus Network)
find_package(Qt6 6.12 QUIET COMPONENTS Svg)

# Keep the dependency direction explicit as the application grows:
# domain <- infrastructure <- services <- UI <- app entry point.
add_library(productivity_domain INTERFACE)
target_include_directories(productivity_domain INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/domain>"
)

add_library(productivity_infrastructure INTERFACE)
target_link_libraries(productivity_infrastructure INTERFACE productivity_domain)
target_include_directories(productivity_infrastructure INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/infrastructure>"
)

add_library(productivity_services STATIC
    src/services/DeadlineMonitor.cpp
    src/services/ApplicationSettings.cpp
    src/services/AutostartService.cpp
    src/services/NotesService.cpp
    src/services/OutlookAuthService.cpp
    src/services/OutlookAuthService.h
    src/services/TodoService.cpp
    src/services/TimerService.cpp
)
target_link_libraries(productivity_services PUBLIC productivity_infrastructure Qt6::Sql Qt6::DBus Qt6::Network)
target_include_directories(productivity_services PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/services>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
)

add_library(productivity_ui STATIC
    src/ui/NotesWidget.cpp
    src/ui/SettingsWidget.cpp
    src/ui/TodoOverviewWidget.cpp
    src/ui/TimerOverviewWidget.cpp
)
target_link_libraries(productivity_ui PUBLIC productivity_services Qt6::Widgets)
target_include_directories(productivity_ui PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/ui>"
)

add_executable(personal-productivity-app
    src/app/main.cpp
    src/infrastructure/logging/ApplicationLogger.cpp
    src/infrastructure/notifications/NotificationCoordinator.cpp
    src/infrastructure/persistence/ApplicationDatabase.cpp
)
qt_add_resources(personal-productivity-app app_icons
    PREFIX "/"
    BASE resources/icons
    FILES resources/icons/app.svg
)
target_link_libraries(personal-productivity-app
    PRIVATE Qt6::Widgets Qt6::Sql Qt6::DBus productivity_ui
)
if(TARGET Qt6::Svg)
    target_link_libraries(personal-productivity-app PRIVATE Qt6::Svg)
endif()

install(TARGETS personal-productivity-app
    RUNTIME DESTINATION bin
)

qt_generate_deploy_app_script(
    TARGET personal-productivity-app
    OUTPUT_SCRIPT deploy_script
    NO_TRANSLATIONS
)
# Deployment runs at install time, so provide this override in that scope.
install(CODE "set(QT_DEPLOY_PLUGINS_DIR \"${CMAKE_INSTALL_LIBDIR}/plugins\")")
install(SCRIPT "${deploy_script}")

install(FILES resources/icons/app.svg
    DESTINATION share/icons/hicolor/scalable/apps
    RENAME personal-productivity-app.svg
)
install(FILES resources/linux/personal-productivity-app.desktop
    DESTINATION share/applications
)
install(FILES resources/linux/personal-productivity-app-autostart.desktop
    DESTINATION share/personal-productivity-app
)
