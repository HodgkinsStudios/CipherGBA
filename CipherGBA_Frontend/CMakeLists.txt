# CipherGBA v0.1 -- preserve the original Linux X11 frontend and add SDL2.
# Generate one embedded icon source for the Linux WM icon, SDL2 window icon
# and every platform's About view. Never use artwork as a startup splash.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(CIPHERGBA_GENERATED_ARTWORK "${CMAKE_CURRENT_BINARY_DIR}/BrandArtwork.hpp")
execute_process(
    COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/packaging/branding/generate_embedded_artwork.py"
        "${PROJECT_SOURCE_DIR}/assets/branding/ciphergba_icon.png"
        "${CIPHERGBA_GENERATED_ARTWORK}"
    RESULT_VARIABLE CIPHERGBA_EMBED_STATUS)
if(NOT CIPHERGBA_EMBED_STATUS EQUAL 0)
    message(FATAL_ERROR "Failed to generate embedded CipherGBA artwork")
endif()
if(CIPHERGBA_SELECTED_FRONTEND STREQUAL "X11")
    add_library(ciphergba_frontend_support STATIC src/Paths.cpp src/Bindings.cpp src/CheatCodes.cpp)
    target_include_directories(ciphergba_frontend_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(ciphergba_frontend_support PUBLIC CipherGBA_Core)
    target_compile_options(ciphergba_frontend_support PRIVATE -Wall -Wextra -Wpedantic)
    find_package(X11 REQUIRED)
    add_executable(CipherGBA src/AppX11.cpp)
    target_include_directories(CipherGBA PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(CipherGBA PRIVATE ciphergba_frontend_support ${X11_LIBRARIES} ${CMAKE_DL_LIBS} ZLIB::ZLIB)
    target_compile_options(CipherGBA PRIVATE -Wall -Wextra -Wpedantic -O2)
elseif(CIPHERGBA_SELECTED_FRONTEND STREQUAL "SDL2")
    find_package(SDL2 CONFIG REQUIRED)
    set(CIPHERGBA_SDL_SOURCES src/AppSDL2.cpp src/CheatCodes.cpp)
    if(APPLE OR WIN32)
        # Reuse the approved project logo as native OS application icon.
        # Generated formats use Python stdlib only, no image tooling dependency.
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        if(APPLE)
            set(CIPHERGBA_OS_ICON "${CMAKE_CURRENT_BINARY_DIR}/CipherGBA.icns")
            set(CIPHERGBA_ICON_KIND "icns")
        else()
            set(CIPHERGBA_OS_ICON "${CMAKE_CURRENT_BINARY_DIR}/CipherGBA.ico")
            set(CIPHERGBA_ICON_KIND "ico")
        endif()
        execute_process(
            COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/packaging/icons/generate_native_icon.py"
                "${PROJECT_SOURCE_DIR}/assets/branding/ciphergba_icon.png"
                "${CIPHERGBA_OS_ICON}"
                "${CIPHERGBA_ICON_KIND}"
            RESULT_VARIABLE CIPHERGBA_ICON_STATUS)
        if(NOT CIPHERGBA_ICON_STATUS EQUAL 0)
            message(FATAL_ERROR "Unable to create CipherGBA native app icon")
        endif()
        if(APPLE)
            set_source_files_properties("${CIPHERGBA_OS_ICON}"
                PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
            list(APPEND CIPHERGBA_SDL_SOURCES "${CIPHERGBA_OS_ICON}")
        else()
            enable_language(RC)
            file(TO_CMAKE_PATH "${CIPHERGBA_OS_ICON}" CIPHERGBA_OS_ICON_RC)
            configure_file("${PROJECT_SOURCE_DIR}/packaging/windows/CipherGBA.rc.in"
                "${CMAKE_CURRENT_BINARY_DIR}/CipherGBA.rc" @ONLY)
            list(APPEND CIPHERGBA_SDL_SOURCES
                "${CMAKE_CURRENT_BINARY_DIR}/CipherGBA.rc")
        endif()
    endif()
    if(APPLE)
        enable_language(OBJCXX)
        list(APPEND CIPHERGBA_SDL_SOURCES src/OpenFileDialogMac.mm)
    elseif(WIN32)
        list(APPEND CIPHERGBA_SDL_SOURCES src/OpenFileDialogWindows.cpp)
    else()
        list(APPEND CIPHERGBA_SDL_SOURCES src/OpenFileDialogLinux.cpp)
    endif()
    add_executable(CipherGBA ${CIPHERGBA_SDL_SOURCES})
    if(APPLE)
        target_link_libraries(CipherGBA PRIVATE "-framework Cocoa")
        set_target_properties(CipherGBA PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.hodgkinsstudios.CipherGBA"
            MACOSX_BUNDLE_BUNDLE_NAME "CipherGBA"
            MACOSX_BUNDLE_ICON_FILE "CipherGBA.icns"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
            MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0")
    elseif(WIN32)
        target_link_libraries(CipherGBA PRIVATE comdlg32)
    endif()
    target_include_directories(CipherGBA PRIVATE "${PROJECT_SOURCE_DIR}/CipherGBA_Core/include")
    target_link_libraries(CipherGBA PRIVATE CipherGBA_Core_Static SDL2::SDL2 ZLIB::ZLIB)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(CIPHERGBA_DBUS QUIET IMPORTED_TARGET dbus-1)
            if(CIPHERGBA_DBUS_FOUND)
                target_compile_definitions(CipherGBA PRIVATE CIPHERGBA_HAVE_DBUS=1)
                target_link_libraries(CipherGBA PRIVATE PkgConfig::CIPHERGBA_DBUS)
                message(STATUS "CipherGBA: native FileChooser portal via libdbus-1")
            else()
                message(STATUS "CipherGBA: D-Bus unavailable; Zenity fallback only")
            endif()
        endif()
    endif()
    if(MSVC)
        target_compile_options(CipherGBA PRIVATE /W4)
    else()
        target_compile_options(CipherGBA PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()
if(TARGET CipherGBA)
    target_include_directories(CipherGBA PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/packaging/branding")
    target_compile_features(CipherGBA PRIVATE cxx_std_17)
    set_target_properties(CipherGBA PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin"
        BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
    install(TARGETS CipherGBA RUNTIME DESTINATION bin BUNDLE DESTINATION .)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/io.github.hodgkinsstudios.CipherGBA.desktop"
            DESTINATION share/applications)
        option(CIPHERGBA_RESIZE_INSTALL_ICON
            "Generate a real 256x256 application icon during package builds" OFF)
        set(CIPHERGBA_ICON_FILE "${PROJECT_SOURCE_DIR}/assets/branding/ciphergba_icon.png")
        if(CIPHERGBA_RESIZE_INSTALL_ICON)
            find_package(Python3 COMPONENTS Interpreter REQUIRED)
            set(CIPHERGBA_ICON_FILE "${CMAKE_CURRENT_BINARY_DIR}/ciphergba_install_256.png")
            execute_process(
                COMMAND "${Python3_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/packaging/linux/resize_png.py"
                    "${PROJECT_SOURCE_DIR}/assets/branding/ciphergba_icon.png"
                    "${CIPHERGBA_ICON_FILE}"
                RESULT_VARIABLE CIPHERGBA_ICON_RESULT)
            if(NOT CIPHERGBA_ICON_RESULT EQUAL 0)
                message(FATAL_ERROR "Could not prepare the 256x256 Linux desktop icon")
            endif()
        endif()
        install(FILES "${CIPHERGBA_ICON_FILE}"
            DESTINATION share/icons/hicolor/256x256/apps
            RENAME io.github.hodgkinsstudios.CipherGBA.png)
    endif()
endif()
