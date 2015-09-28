project(obs-vpx)

find_package(LibVPX REQUIRED)
include_directories(${LIBVPX_INCLUDE_DIRS})

set(obs-vpx_SOURCES
	obs-vpx.c
	obs-vpx-plugin-main.c)

add_library(obs-vpx MODULE
	${obs-vpx_SOURCES})
target_link_libraries(obs-vpx
	libobs
	${LIBVPX_LIBRARIES})

install_obs_plugin_with_data(obs-vpx data)
