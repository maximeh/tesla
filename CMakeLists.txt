cmake_minimum_required( VERSION 2.8 )
set( CMAKE_BUILD_TYPE Release)

set(CMAKE_VERBOSE_MAKEFILE ON)
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")

project( tesla )
include( FindPackageHandleStandardArgs )

set( TESLA_SOURCE_FILES src/tesla.c src/tesla.h)

find_package( PkgConfig REQUIRED )

pkg_search_module( LIBUSB REQUIRED libusb-1.0 )
if( LIBUSB_FOUND )
  message( STATUS "LIBUSB found at: ${LIBUSB_LIBRARIES}" )
else()
  message( SEND_ERROR "Could not find LIBUSB on your system. Please install it." )
endif()

add_definitions( -DUSE_RRD=${USE_RRD} )
pkg_search_module( LIBRRD REQUIRED librrd )
if( LIBRRD_FOUND )
        message( STATUS "LIBRRD found at: ${LIBRRD_LIBRARIES}" )
else()
        message( SEND_ERROR "Could not find LIBRRD on your system. Please install it." )
endif()
list( APPEND TESLA_LIBRARIES "${LIBRRD_LDFLAGS}" )
list( APPEND TESLA_SOURCE_FILES src/rrd_helpers.c src/rrd_helpers.h)

add_executable( tesla ${TESLA_SOURCE_FILES} )
target_link_libraries( tesla
  ${LIBUSB_LIBRARIES}
  ${TESLA_LIBRARIES}
)
install( TARGETS tesla DESTINATION bin )
