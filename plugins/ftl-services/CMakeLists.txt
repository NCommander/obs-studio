project(ftl-services)

include_directories(${OBS_JANSSON_INCLUDE_DIRS})

set(ftl-services_SOURCES
	ftl-beam.c
	ftl-services-main.c)

set(ftl-services_HEADERS
	ftl-format-ver.h)

set(FTL_SERVICES_URL
	"https://github.com/WatchBeam/abandoned-ftl-studio"
	CACHE STRING "Default services package URL")

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/lookup-config.h.in"
	"${CMAKE_BINARY_DIR}/plugins/ftl-services/lookup-config.h")

set(ftl-services_config_HEADERS
	"${CMAKE_BINARY_DIR}/plugins/ftl-services/lookup-config.h")

add_library(ftl-services MODULE
	${ftl-services_SOURCES}
	${ftl-services_HEADERS}
	${ftl-services_config_HEADERS})
target_link_libraries(ftl-services
	libobs
	file-updater
	${OBS_JANSSON_IMPORT})

target_include_directories(ftl-services
	PUBLIC
		"$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/plugins/ftl-services>")

install_obs_plugin_with_data(ftl-services data)
