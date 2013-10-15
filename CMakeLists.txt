cmake_minimum_required(VERSION 2.8)
include(FindPackageHandleStandardArgs)
set(CMAKE_BUILD_TYPE Release)

project(tesla)

file(
  GLOB_RECURSE
  source_files
  src/*
)

add_executable(
  tesla
  ${source_files}
)

find_package(PkgConfig REQUIRED)

pkg_search_module(LIBUSB REQUIRED libusb)
IF(LIBUSB_FOUND)
  MESSAGE(STATUS "LIBUSB found at: ${LIBUSB_LIBRARIES}")
else()
  MESSAGE(FATAL_ERROR "Could not find LIBUSB on your system. Please install it.")
ENDIF(LIBUSB_FOUND)
set(LIBUSB_LIBRARIES ${LIBUSB_LIBRARY})

pkg_search_module(LIBRRD REQUIRED librrd)
IF(LIBRRD_FOUND)
  MESSAGE(STATUS "LIBRRD found at: ${LIBRRD_LIBRARIES}")
else()
  MESSAGE(FATAL_ERROR "Could not find LIBRRD on your system. Please install it.")
ENDIF(LIBRRD_FOUND)
set(LIBRRD_LIBRARIES "${LIBRRD_LDFLAGS}")

target_link_libraries(tesla ${LIBUSB_LIBRARIES} ${LIBRRD_LIBRARIES})
install(TARGETS tesla DESTINATION bin)

