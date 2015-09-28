# Once done these will be defined:
#
#  LIBVPX_FOUND
#  LIBVPX_INCLUDE_DIRS
#  LIBVPX_LIBRARIES
#
# For use in OBS:
#
#  LIBVPX_INCLUDE_DIR

find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
	pkg_check_modules(_VPX QUIET vpx libvpx)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(_lib_suffix 64)
else()
	set(_lib_suffix 32)
endif()

find_path(VPX_INCLUDE_DIR
	NAMES vpx/vp8.h
	HINTS
		ENV curlPath${_lib_suffix}
		ENV curlPath
		ENV DepsPath${_lib_suffix}
		ENV DepsPath
		${curlPath${_lib_suffix}}
		${curlPath}
		${DepsPath${_lib_suffix}}
		${DepsPath}
		${_CURL_INCLUDE_DIRS}
	PATHS
		/usr/include /usr/local/include /opt/local/include /sw/include
	PATH_SUFFIXES
		include)

find_library(VPX_LIB
	NAMES ${_VPX_LIBRARIES} vpx libvpx
	HINTS
		ENV curlPath${_lib_suffix}
		ENV curlPath
		ENV DepsPath${_lib_suffix}
		ENV DepsPath
		${curlPath${_lib_suffix}}
		${curlPath}
		${DepsPath${_lib_suffix}}
		${DepsPath}
		${_CURL_LIBRARY_DIRS}
	PATHS
		/usr/lib /usr/local/lib /opt/local/lib /sw/lib
	PATH_SUFFIXES
		lib${_lib_suffix} lib
		libs${_lib_suffix} libs
		bin${_lib_suffix} bin
		../lib${_lib_suffix} ../lib
		../libs${_lib_suffix} ../libs
		../bin${_lib_suffix} ../bin)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Libvpx DEFAULT_MSG VPX_LIB VPX_INCLUDE_DIR)
mark_as_advanced(VPX_INCLUDE_DIR VPX_LIB)

if(LIBVPX_FOUND)
	set(LIBVPX_INCLUDE_DIRS ${CURL_INCLUDE_DIR})
	set(LIBVPX_LIBRARIES ${CURL_LIB})
endif()
