cmake_minimum_required(VERSION 3.16)
project(nfc-games LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_PREFIX_PATH "/home/linuxbrew/.linuxbrew")
if(EXISTS "/home/linuxbrew/.linuxbrew/lib/pkgconfig")
  set(ENV{PKG_CONFIG_PATH} "/home/linuxbrew/.linuxbrew/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

add_executable(nfc
  cpp/main.cpp
  cpp/acr122.cpp
)
target_include_directories(nfc PRIVATE cpp ${LIBUSB_INCLUDE_DIRS})
target_link_directories(nfc PRIVATE ${LIBUSB_LIBRARY_DIRS})
target_link_libraries(nfc PRIVATE ${LIBUSB_LIBRARIES})
target_compile_options(nfc PRIVATE ${LIBUSB_CFLAGS_OTHER} -Wall -Wextra)

if(LIBUSB_LIBRARY_DIRS)
  set_target_properties(nfc PROPERTIES
    BUILD_RPATH "${LIBUSB_LIBRARY_DIRS}"
    INSTALL_RPATH "${LIBUSB_LIBRARY_DIRS}"
  )
endif()
