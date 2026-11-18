cmake_minimum_required(VERSION 3.22)
project(raye-store VERSION 0.5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(ECM 6.0 REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FindPkgConfig)

# /usr/libexec, where the polkit policy and the Windows runner look
set(RAYE_LIBEXECDIR libexec)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Widgets Network Qml Quick QuickControls2 Concurrent DBus Test)
# put the QML modules under qrc:/qt/qml, where the engine looks for them
qt_policy(SET QTP0001 NEW)
# a qmldir in every folder of a module, so src/qml/controls works too
qt_policy(SET QTP0004 NEW)
find_package(KF6 6.0 REQUIRED COMPONENTS CoreAddons DBusAddons I18n Kirigami Notifications WindowSystem)
find_package(KF6KirigamiAddons 1.0 REQUIRED)
find_package(AppStreamQt 1.0 REQUIRED)
find_package(packagekitqt6 1.0 REQUIRED)
pkg_check_modules(AppStream REQUIRED IMPORTED_TARGET appstream)
pkg_check_modules(Flatpak REQUIRED IMPORTED_TARGET flatpak)

# Every part of the Store is found by name, so adding a file needs no
# edit here:
#   src/*.cpp (and below)        the Store's code, main.cpp is the program
#   src/qml/**/*.qml, *.js       its interface; images next to them are resources
#   src/raye-store-*             helpers, installed to libexec
#   runner/*.cpp                 raye-store-runner, the Start menu search
#   data/krunner/*               its search plugin and D-Bus service
#   tests/test_*.cpp, test_*.py  one test each
#   windows/lib/*.cpp            the Windows programs library
#   windows/ui/*.cpp, qml/**     raye-windows-ui (main.cpp is the program)
#   windows/cli/*.cpp            raye-windows
# A QML file starting with "pragma Singleton" is registered as one.
function(raye_qml_files out dir)
    file(GLOB_RECURSE qml CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
        ${dir}/*.qml ${dir}/*.js ${dir}/*.mjs)
    list(SORT qml)
    foreach(f IN LISTS qml)
        file(STRINGS ${f} singleton REGEX "^[ \t]*pragma[ \t]+Singleton" LIMIT_COUNT 1)
        if(singleton)
            set_source_files_properties(${f} PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
        endif()
    endforeach()
    set(${out} ${qml} PARENT_SCOPE)
endfunction()

function(raye_resources out dir)
    file(GLOB_RECURSE res CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
        ${dir}/*.svg ${dir}/*.png ${dir}/*.webp ${dir}/*.json)
    list(SORT res)
    set(${out} ${res} PARENT_SCOPE)
endfunction()

# --- Raye Store ---------------------------------------------------------

file(GLOB_RECURSE store_sources CONFIGURE_DEPENDS src/*.cpp)
list(FILTER store_sources EXCLUDE REGEX "/src/main\\.cpp$")
list(SORT store_sources)
add_library(raye-store-core STATIC ${store_sources})
target_include_directories(raye-store-core PUBLIC src)
target_link_libraries(raye-store-core PUBLIC
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Concurrent
    KF6::CoreAddons KF6::I18n KF6::Notifications
    AppStreamQt PkgConfig::AppStream PkgConfig::Flatpak PK::packagekitqt6)

raye_qml_files(store_qml src/qml)
raye_resources(store_res src/qml)
add_executable(raye-store src/main.cpp)
qt_add_qml_module(raye-store
    URI org.raye.store
    VERSION 1.0
    QML_FILES ${store_qml}
    RESOURCES ${store_res}
)
target_link_libraries(raye-store PRIVATE raye-store-core
    Qt6::Widgets Qt6::Quick Qt6::QuickControls2 KF6::CoreAddons KF6::I18n
    KF6::DBusAddons KF6::Notifications KF6::WindowSystem)

# The version shown in About is the one of the package when there is
# one, the project's otherwise. Same as raye-settings: the
# changelog is the one place an app's own version is written. This is
# the app's version, not the release's -- that one is read from
# os-release, never compiled in.
set(RAYE_STORE_VERSION ${PROJECT_VERSION})
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/debian/changelog)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS debian/changelog)
    file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/debian/changelog changelog LIMIT_COUNT 1)
    if(changelog MATCHES "\\(([^)]+)\\)")
        set(RAYE_STORE_VERSION ${CMAKE_MATCH_1})
    endif()
endif()
target_compile_definitions(raye-store PRIVATE RAYE_STORE_VERSION="${RAYE_STORE_VERSION}")

install(TARGETS raye-store ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

# the Start menu asks the Store for apps to get; the runner answers on D-Bus
file(GLOB store_runner_sources CONFIGURE_DEPENDS runner/*.cpp)
if(store_runner_sources)
    add_executable(raye-store-runner ${store_runner_sources})
    target_link_libraries(raye-store-runner PRIVATE raye-store-core Qt6::DBus KF6::I18n)
    install(TARGETS raye-store-runner DESTINATION ${RAYE_LIBEXECDIR})
endif()
file(GLOB store_runner_plugin CONFIGURE_DEPENDS data/krunner/*.desktop)
install(FILES ${store_runner_plugin} DESTINATION ${KDE_INSTALL_DATADIR}/krunner/dbusplugins)
file(GLOB store_runner_service CONFIGURE_DEPENDS data/krunner/*.service)
install(FILES ${store_runner_service} DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR})

file(GLOB store_notifyrc CONFIGURE_DEPENDS data/*.notifyrc)
install(FILES ${store_notifyrc} DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})

file(GLOB store_helpers CONFIGURE_DEPENDS src/raye-store-*)
install(PROGRAMS ${store_helpers} DESTINATION ${RAYE_LIBEXECDIR})

# Discover's command: what still calls it opens the Store
install(PROGRAMS data/plasma-discover DESTINATION ${KDE_INSTALL_BINDIR})

file(GLOB store_desktop CONFIGURE_DEPENDS data/*.desktop data/placeholders/*.desktop)
install(FILES ${store_desktop} DESTINATION ${KDE_INSTALL_APPDIR})
# what opens a .deb: read after the desktop's own list, which still names
# Discover (not there: the Store replaces it)
install(FILES data/mimeapps.list DESTINATION ${KDE_INSTALL_APPDIR})

file(GLOB store_data CONFIGURE_DEPENDS
    data/*.json data/*.tsv data/*.txt data/*.flatpakrepo)
install(FILES ${store_data} DESTINATION ${KDE_INSTALL_DATADIR}/raye-store)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/data/icons)
    install(DIRECTORY data/icons/ DESTINATION ${KDE_INSTALL_DATADIR}/raye-store/icons)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/data/flatpak)
    install(DIRECTORY data/flatpak/ DESTINATION ${KDE_INSTALL_DATADIR}/raye-store/flatpak)
endif()

file(GLOB polkit_rules CONFIGURE_DEPENDS data/*.rules)
install(FILES ${polkit_rules} DESTINATION ${KDE_INSTALL_DATADIR}/polkit-1/rules.d)
file(GLOB polkit_actions CONFIGURE_DEPENDS data/*.policy)
install(FILES ${polkit_actions} DESTINATION ${KDE_INSTALL_DATADIR}/polkit-1/actions)

# units run for each user; the rest are system units
set(store_user_units raye-store-autoupdate.service raye-store-autoupdate.timer)
file(GLOB store_units CONFIGURE_DEPENDS data/*.service data/*.timer)
foreach(unit IN LISTS store_units)
    get_filename_component(unit_name ${unit} NAME)
    if(unit_name IN_LIST store_user_units)
        install(FILES ${unit} DESTINATION lib/systemd/user)
    else()
        install(FILES ${unit} DESTINATION lib/systemd/system)
    endif()
endforeach()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/po)
    ki18n_install(po)
endif()

# --- Windows programs (package raye-windows-ui) -------------------------

file(GLOB windows_lib_sources CONFIGURE_DEPENDS windows/lib/*.cpp)
if(windows_lib_sources)
    list(SORT windows_lib_sources)
    add_library(raye-windows-lib STATIC ${windows_lib_sources})
    target_include_directories(raye-windows-lib PUBLIC windows/lib)
    target_link_libraries(raye-windows-lib PUBLIC Qt6::Core Qt6::Gui)
endif()

file(GLOB windows_ui_sources CONFIGURE_DEPENDS windows/ui/*.cpp)
list(FILTER windows_ui_sources EXCLUDE REGEX "/windows/ui/main\\.cpp$")
if(windows_ui_sources)
    list(SORT windows_ui_sources)
    add_library(raye-windows-ui-core STATIC ${windows_ui_sources})
    target_include_directories(raye-windows-ui-core PUBLIC windows/ui)
    target_link_libraries(raye-windows-ui-core PUBLIC Qt6::Core Qt6::Gui raye-store-core)
    if(TARGET raye-windows-lib)
        target_link_libraries(raye-windows-ui-core PUBLIC raye-windows-lib)
    endif()
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/windows/ui/main.cpp)
    raye_qml_files(windows_qml windows/ui/qml)
    raye_resources(windows_res windows/ui/qml)
    add_executable(raye-windows-ui windows/ui/main.cpp)
    qt_add_qml_module(raye-windows-ui
        URI org.raye.windows
        VERSION 1.0
        QML_FILES ${windows_qml}
        RESOURCES ${windows_res}
    )
    target_link_libraries(raye-windows-ui PRIVATE raye-store-core
        Qt6::Widgets Qt6::Quick Qt6::QuickControls2 KF6::CoreAddons KF6::I18n)
    foreach(lib raye-windows-ui-core raye-windows-lib)
        if(TARGET ${lib})
            target_link_libraries(raye-windows-ui PRIVATE ${lib})
        endif()
    endforeach()
    install(TARGETS raye-windows-ui DESTINATION ${RAYE_LIBEXECDIR})
endif()

file(GLOB windows_cli_sources CONFIGURE_DEPENDS windows/cli/*.cpp)
if(windows_cli_sources)
    list(SORT windows_cli_sources)
    add_executable(raye-windows ${windows_cli_sources})
    target_link_libraries(raye-windows PRIVATE Qt6::Core)
    if(TARGET raye-windows-lib)
        target_link_libraries(raye-windows PRIVATE raye-windows-lib)
    endif()
    install(TARGETS raye-windows ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
endif()

# --- Tests --------------------------------------------------------------

if(BUILD_TESTING)
    enable_testing()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    # tests/test_foo.cpp -> test "foo", linked with every library above
    file(GLOB cpp_tests CONFIGURE_DEPENDS tests/test_*.cpp)
    list(SORT cpp_tests)
    foreach(src IN LISTS cpp_tests)
        get_filename_component(stem ${src} NAME_WE)
        string(REGEX REPLACE "^test_" "" name ${stem})
        string(REPLACE "_" "-" target ${stem})
        add_executable(${target} ${src})
        target_link_libraries(${target} PRIVATE raye-store-core Qt6::Test Qt6::Qml)
        # fixtures by absolute path: package builds compile with relative file names
        target_compile_definitions(${target} PRIVATE RAYE_TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")
        foreach(lib raye-windows-lib raye-windows-ui-core)
            if(TARGET ${lib})
                target_link_libraries(${target} PRIVATE ${lib})
            endif()
        endforeach()
        add_test(NAME ${name} COMMAND ${target})
        set_tests_properties(${name} PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
    endforeach()
    file(GLOB py_tests CONFIGURE_DEPENDS tests/test_*.py)
    list(SORT py_tests)
    foreach(src IN LISTS py_tests)
        get_filename_component(stem ${src} NAME_WE)
        string(REGEX REPLACE "^test_" "" name ${stem})
        # -B: no __pycache__ left in the source tree
        add_test(NAME py-${name} COMMAND ${Python3_EXECUTABLE} -I -B ${src})
    endforeach()
    # what Settings starts: the real program, its options, no window
    add_test(NAME cli COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_cli.py $<TARGET_FILE:raye-store>)
    set_tests_properties(cli PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
    # the interface itself: catches a QML module the engine cannot find
    add_test(NAME qml COMMAND raye-store --check-qml)
    set_tests_properties(qml PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
endif()
