cmake_minimum_required(VERSION 3.16)
project(nfc-games LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(NFC_APPIMAGE "Link system libusb without Homebrew RPATH (AppImage)" OFF)

list(APPEND CMAKE_PREFIX_PATH "/home/linuxbrew/.linuxbrew")
if(EXISTS "/home/linuxbrew/.linuxbrew/lib/pkgconfig")
  set(ENV{PKG_CONFIG_PATH} "/home/linuxbrew/.linuxbrew/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

add_executable(nfc
  cpp/main.cpp
  cpp/acr122.cpp
  cpp/tags.cpp
  cpp/steam.cpp
  cpp/i18n.cpp
)
target_include_directories(nfc PRIVATE cpp ${LIBUSB_INCLUDE_DIRS})
target_compile_options(nfc PRIVATE ${LIBUSB_CFLAGS_OTHER} -Wall -Wextra)

if(NFC_APPIMAGE)
  find_library(NFC_SYSTEM_LIBUSB NAMES usb-1.0 libusb-1.0.so.0
    PATHS /usr/lib64 /usr/lib
    NO_DEFAULT_PATH)
  if(NOT NFC_SYSTEM_LIBUSB AND EXISTS "/usr/lib64/libusb-1.0.so.0")
    set(NFC_SYSTEM_LIBUSB "/usr/lib64/libusb-1.0.so.0")
  endif()
  if(NOT NFC_SYSTEM_LIBUSB)
    message(FATAL_ERROR "NFC_APPIMAGE requires system libusb-1.0 in /usr/lib64")
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
install(FILES HELP.md HELP.en.md DESTINATION ${CMAKE_INSTALL_DATADIR}/nfc-games)
install(FILES udev/99-acr122u.rules DESTINATION ${CMAKE_INSTALL_DATADIR}/nfc-games/udev)
install(PROGRAMS packaging/install-udev.sh DESTINATION ${CMAKE_INSTALL_DATADIR}/nfc-games/udev)
install(FILES packaging/nfc-games.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/nfc-games.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
