cmake_minimum_required( VERSION 2.8 )
set( CMAKE_BUILD_TYPE Release )
option( USE_RRD "USE_RRD" OFF )
option( USE_KEEN "USE_KEEN" OFF )
project( tesla )
include( FindPackageHandleStandardArgs )

if( ${USE_RRD} AND ${USE_KEEN} )
  message( SEND_ERROR "You must either set -DUSE_RRD *OR* -DUSE_KEEN" )
elseif( NOT ${USE_RRD} AND NOT ${USE_KEEN} )
  message( SEND_ERROR "You must set -DUSE_RRD *OR* -DUSE_KEEN" )
endif()

set( TESLA_SOURCE_FILES src/tesla.c src/tesla.h)

find_package( PkgConfig REQUIRED )

pkg_search_module( LIBUSB REQUIRED libusb )
if( LIBUSB_FOUND )
  message( STATUS "LIBUSB found at: ${LIBUSB_LIBRARIES}" )
else()
  message( SEND_ERROR "Could not find LIBUSB on your system. Please install it." )
endif()

if( ${USE_KEEN} )
  add_definitions( -DUSE_KEEN=${USE_KEEN} )
  pkg_search_module( LIBCURL REQUIRED libcurl )
  if( LIBCURL_FOUND )
    message( STATUS "LIBCURL found at: ${LIBCURL_LIBRARIES}" )
  else()
    message( SEND_ERROR "Could not find LIBCURL on your system. Please install it." )
  endif()
  list( APPEND TESLA_LIBRARIES ${LIBCURL_LIBRARIES} )
  list( APPEND TESLA_SOURCE_FILES src/curl_helpers.c src/curl_helpers.h)
endif()

if( ${USE_RRD} )
  add_definitions( -DUSE_RRD=${USE_RRD} )
  pkg_search_module( LIBRRD REQUIRED librrd )
  if( LIBRRD_FOUND )
    message( STATUS "LIBRRD found at: ${LIBRRD_LIBRARIES}" )
  else()
    message( SEND_ERROR "Could not find LIBRRD on your system. Please install it." )
  endif()
  list( APPEND TESLA_LIBRARIES "${LIBRRD_LDFLAGS}" )
  list( APPEND TESLA_SOURCE_FILES src/rrd_helpers.c src/rrd_helpers.h)
endif()

add_executable( tesla ${TESLA_SOURCE_FILES} )
target_link_libraries( tesla
  ${LIBUSB_LIBRARIES}
  ${TESLA_LIBRARIES}
)
install( TARGETS tesla DESTINATION bin )
