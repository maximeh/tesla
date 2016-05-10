project( tesla C)

cmake_minimum_required( VERSION 3.3 )
set( CMAKE_BUILD_TYPE RELEASE )
set( CMAKE_VERBOSE_MAKEFILE ON )

option(ENABLE_INCLUDE_WHAT_YOU_USE
	"Enable include-what-you-use"
	ON)
if(ENABLE_INCLUDE_WHAT_YOU_USE)
	find_program(INCLUDE_WHAT_YOU_USE include-what-you-use)
	if(INCLUDE_WHAT_YOU_USE)
		message(STATUS "Enable Include What You Use")
		set(CMAKE_C_INCLUDE_WHAT_YOU_USE include-what-you-use)
	else()
		message(WARNING "Could not find include-what-you-use")
	endif()
endif()

set( TESLA_SOURCE_FILES src/tesla.c src/tesla.h
			src/rrd_helpers.c src/rrd_helpers.h )
add_executable( tesla ${TESLA_SOURCE_FILES} )
install( TARGETS tesla DESTINATION bin )

set( CC_OPTIONS
	"-Ofast"
	"-march=native"
	"-Wall"
	"-Wextra"
	"-Wpedantic"
	"-Wformat=2"
	"-Wno-unused-parameter"
	"-Wshadow"
	"-Wwrite-strings"
	"-Wstrict-prototypes"
	"-Wold-style-definition"
	"-Wredundant-decls"
	"-Wnested-externs"
	"-Wmissing-include-dirs"
	"-Wcast-align"
	"-Wcast-qual"
	"-Wdisabled-optimization"
	"-Winit-self"
	"-Wmissing-declarations"
	"-Wsign-conversion"
	"-Wstrict-overflow=5"
	"-Wswitch-default"
	"-Wundef"
	"-Werror"
	"-Wno-unused"
)

if ( CMAKE_BUILD_TYPE MATCHES DEBUG )
	set( CC_OPTIONS
		${CC_OPTIONS}
		"-fsanitize=address"
		"-fsanitize=leak"
		"-fsanitize=undefined"
		"-fsanitize=signed-integer-overflow"
		"-fsanitize=shift"
		"-fsanitize=integer-divide-by-zero"
		"-fsanitize=null"
	)
endif()


target_compile_options(
	tesla
	PRIVATE
	${CC_OPTIONS}
)

target_link_libraries(
	tesla
	${CC_OPTIONS}
)

# GCC warnings that Clang doesn't provide:
if ("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
	target_compile_options(
		tesla
		PRIVATE
		-Wjump-misses-init -Wlogical-op
	)
endif()

find_package( PkgConfig REQUIRED )
include( FindPackageHandleStandardArgs )

pkg_search_module( LIBUSB REQUIRED libusb-1.0 )
if( LIBUSB_FOUND )
	message( STATUS "LIBUSB found at: ${LIBUSB_LIBRARIES}" )
else()
	message( SEND_ERROR "LIBUSB not found on your system." )
endif()
target_link_libraries( tesla ${LIBUSB_LIBRARIES} )

pkg_search_module( LIBRRD REQUIRED librrd )
if( LIBRRD_FOUND )
	message( STATUS "LIBRRD found at: ${LIBRRD_LIBRARIES}" )
else()
	message( SEND_ERROR "LIBRRD not found on your system." )
endif()
target_link_libraries( tesla ${LIBRRD_LIBRARIES} )
