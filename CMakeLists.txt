cmake_minimum_required(VERSION 3.16)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" NFC_VERSION_RAW)
string(STRIP "${NFC_VERSION_RAW}" NFC_VERSION)
project(nfc-games VERSION ${NFC_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(NFC_APPIMAGE "Link system libusb without extra RPATH (AppImage)" OFF)

if(DEFINED ENV{HOMEBREW_PREFIX} AND EXISTS "$ENV{HOMEBREW_PREFIX}/lib/pkgconfig")
  list(APPEND CMAKE_PREFIX_PATH "$ENV{HOMEBREW_PREFIX}")
  set(ENV{PKG_CONFIG_PATH} "$ENV{HOMEBREW_PREFIX}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
elseif(EXISTS "/home/linuxbrew/.linuxbrew/lib/pkgconfig")
  list(APPEND CMAKE_PREFIX_PATH "/home/linuxbrew/.linuxbrew")
  set(ENV{PKG_CONFIG_PATH} "/home/linuxbrew/.linuxbrew/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
endif()

if(WIN32)
  if(NOT LIBUSB_ROOT AND DEFINED ENV{LIBUSB_ROOT})
    set(LIBUSB_ROOT "$ENV{LIBUSB_ROOT}")
  endif()
  find_path(LIBUSB_INCLUDE_DIRS libusb.h
    NO_CMAKE_FIND_ROOT_PATH
    PATHS
      "${LIBUSB_ROOT}/include/libusb-1.0"
      "${LIBUSB_ROOT}/include"
  )
  find_library(LIBUSB_LIBRARIES NAMES usb-1.0 libusb-1.0
    NO_CMAKE_FIND_ROOT_PATH
    PATHS
      "${LIBUSB_ROOT}/lib"
      "${LIBUSB_ROOT}/MinGW64/static"
      "${LIBUSB_ROOT}/MinGW64/dll"
  )
  if(NOT LIBUSB_INCLUDE_DIRS OR NOT LIBUSB_LIBRARIES)
    message(FATAL_ERROR "libusb-1.0 not found. Set LIBUSB_ROOT to a MinGW libusb prefix.")
  endif()
else()
  find_package(PkgConfig)
  if(PkgConfig_FOUND)
    pkg_check_modules(LIBUSB libusb-1.0)
  endif()
  if(NOT LIBUSB_FOUND)
    find_path(LIBUSB_INCLUDE_DIRS libusb.h
      PATHS
        /usr/include/libusb-1.0
        /usr/local/include/libusb-1.0
        /home/linuxbrew/.linuxbrew/include/libusb-1.0
    )
    find_library(LIBUSB_LIBRARIES NAMES usb-1.0
      PATHS /usr/lib64 /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib
    )
  endif()
  if(NOT LIBUSB_INCLUDE_DIRS OR (NOT LIBUSB_FOUND AND NOT LIBUSB_LIBRARIES))
    message(FATAL_ERROR "libusb-1.0 not found. Install libusb1-devel or libusb-1.0-0-dev.")
  endif()
endif()

add_executable(nfc
  cpp/main.cpp
  cpp/app.cpp
  cpp/watch.cpp
  cpp/cmds.cpp
  cpp/menu.cpp
  cpp/acr122.cpp
  cpp/ndef.cpp
  cpp/tags.cpp
  cpp/steam.cpp
  cpp/lutris.cpp
  cpp/roms.cpp
  cpp/launch.cpp
  cpp/i18n.cpp
  cpp/plat.cpp
)
target_include_directories(nfc PRIVATE cpp ${LIBUSB_INCLUDE_DIRS})
if(MSVC)
  target_compile_options(nfc PRIVATE /W4)
else()
  target_compile_options(nfc PRIVATE ${LIBUSB_CFLAGS_OTHER} -Wall -Wextra)
endif()
target_compile_definitions(nfc PRIVATE NFC_VERSION="${NFC_VERSION}")

if(WIN32)
  target_link_libraries(nfc PRIVATE ${LIBUSB_LIBRARIES} setupapi ole32 advapi32 shell32)
  if(MINGW)
    target_link_options(nfc PRIVATE -static -static-libgcc -static-libstdc++
      -Wl,--major-subsystem-version=10,--minor-subsystem-version=0)
  endif()
  if(NFC_ZADIG_EXE)
    if(NOT EXISTS "${NFC_ZADIG_EXE}")
      message(FATAL_ERROR "NFC_ZADIG_EXE not found: ${NFC_ZADIG_EXE}")
    endif()
    enable_language(RC)
    file(TO_CMAKE_PATH "${NFC_ZADIG_EXE}" NFC_ZADIG_CMAKE)
    string(REPLACE "\\" "/" NFC_ZADIG_CMAKE "${NFC_ZADIG_CMAKE}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/zadig.rc" "ZADIG RCDATA \"${NFC_ZADIG_CMAKE}\"\n")
    target_sources(nfc PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/zadig.rc")
    target_compile_definitions(nfc PRIVATE NFC_EMBED_ZADIG=1)
  endif()
elseif(NFC_APPIMAGE)
  find_library(NFC_SYSTEM_LIBUSB NAMES usb-1.0
    PATHS /usr/lib64 /usr/lib /usr/lib/x86_64-linux-gnu /lib64 /lib
  )
  if(NOT NFC_SYSTEM_LIBUSB)
    foreach(_so
        /usr/lib64/libusb-1.0.so.0
        /usr/lib/x86_64-linux-gnu/libusb-1.0.so.0
        /usr/lib/libusb-1.0.so.0)
      if(EXISTS "${_so}")
        set(NFC_SYSTEM_LIBUSB "${_so}")
        break()
      endif()
    endforeach()
  endif()
  if(NOT NFC_SYSTEM_LIBUSB)
    message(FATAL_ERROR "NFC_APPIMAGE requires system libusb-1.0")
  endif()
  target_link_libraries(nfc PRIVATE ${NFC_SYSTEM_LIBUSB})
  set_target_properties(nfc PROPERTIES
    SKIP_BUILD_RPATH ON
    BUILD_WITH_INSTALL_RPATH OFF
    INSTALL_RPATH ""
  )
else()
  target_link_directories(nfc PRIVATE ${LIBUSB_LIBRARY_DIRS})
  target_link_libraries(nfc PRIVATE ${LIBUSB_LIBRARIES})
  if(LIBUSB_LIBRARY_DIRS)
    set_target_properties(nfc PROPERTIES
      BUILD_RPATH "${LIBUSB_LIBRARY_DIRS}"
      INSTALL_RPATH "${LIBUSB_LIBRARY_DIRS}"
    )
  endif()
endif()

include(GNUInstallDirs)
install(TARGETS nfc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES HELP.md HELP.en.md LICENSE README.md README.da.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/nfc-games)
install(DIRECTORY lang DESTINATION ${CMAKE_INSTALL_DATADIR}/nfc-games
  FILES_MATCHING PATTERN "*.txt")
if(NOT WIN32)
  install(FILES udev/99-acr122u.rules DESTINATION ${CMAKE_INSTALL_DATADIR}/nfc-games/udev)
  install(PROGRAMS packaging/install-udev.sh DESTINATION ${CMAKE_INSTALL_DATADIR}/nfc-games/udev)
  install(FILES packaging/nfc-games.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES packaging/nfc-games.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
endif()

if(NOT WIN32)
enable_testing()
add_executable(nfc_tests tests/test_nfc.cpp cpp/steam.cpp cpp/lutris.cpp cpp/roms.cpp cpp/tags.cpp cpp/i18n.cpp cpp/ndef.cpp cpp/launch.cpp cpp/plat.cpp)
target_include_directories(nfc_tests PRIVATE cpp)
target_compile_definitions(nfc_tests PRIVATE NFC_VERSION="${NFC_VERSION}")
add_test(NAME nfc_tests COMMAND nfc_tests)
set_tests_properties(nfc_tests PROPERTIES
  ENVIRONMENT "NFC_SHARE=${CMAKE_SOURCE_DIR}")
endif()
