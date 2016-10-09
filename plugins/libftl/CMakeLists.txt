project(libftl)

#include_directories(${OBS_JANSSON_INCLUDE_DIRS})

set(FTLSDK_SOURCES
	ftl-sdk/libftl/hmac/hmac.c
	ftl-sdk/libftl/hmac/sha2.c
	ftl-sdk/libftl/ftl-sdk.c
	ftl-sdk/libftl/handshake.c
	ftl-sdk/libftl/ftl_helpers.c
	ftl-sdk/libftl/media.c
	ftl-sdk/libftl/gettimeofday/gettimeofday.c
	ftl-sdk/libftl/logging.c)
	
if (WIN32)
  set(FTLSDK_PLATFORM_SOURCES 
		ftl-sdk/libftl/win32/socket.c
		ftl-sdk/libftl/gettimeofday/gettimeofday.c
		ftl-sdk/libftl/win32/threads.c
		ftl-sdk/libftl/win32/threads.h)

  include_directories(ftl-sdk/libftl/win32)
else()
  set(FTLSDK_PLATFORM_SOURCES ftl-sdk/libftl/posix/socket.c
  							  ftl-sdk/libftl/posix/threads.c
							  ftl-sdk/libftl/posix/threads.h)
  include_directories(ftl-sdk/libftl/posix)
endif()	

set(FTLSDK_HEADERS
	ftl-sdk/libftl/hmac/hmac.h
	ftl-sdk/libftl/hmac/sha2.h
	ftl-sdk/libftl/ftl.h
	ftl-sdk/libftl/ftl_private.h)
	
if (WIN32)
  set(FTLSDK_PLATFORM_HEADERS 
		ftl-sdk/libftl/gettimeofday/gettimeofday.h)
endif()		

add_library(libftl SHARED 
	${FTLSDK_SOURCES}
	${FTLSDK_HEADERS}
	${FTLSDK_PLATFORM_SOURCES}
	${FTLSDK_PLATFORM_HEADERS})
	
set_target_properties(libftl PROPERTIES VERSION "0.5.0")
set_target_properties(libftl PROPERTIES SOVERSION 0)	

if(WIN32)
  target_link_libraries(libftl ws2_32)
endif()
	
target_include_directories(libftl
	PUBLIC
		"$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/plugins/libftl>"
		"$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>")
		
file (COPY ${CMAKE_CURRENT_SOURCE_DIR}/ftl-sdk/libftl/ftl.h
           DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/include)

include_directories (${CMAKE_CURRENT_BINARY_DIR}/include)
include_directories (${CMAKE_CURRENT_SOURCE_DIR}/ftl-sdk/libftl/)
#set(LIBFTL_INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/include)

install_obs_plugin_with_data(libftl data)
#set(LIBFTL_LIBRARIES libftl)

#include_directories(${LIBFTL_INCLUDE_DIRS})

#install(TARGETS libftl DESTINATION lib)
#install(FILES ftl-sdk/libftl/ftl.h DESTINATION "include/libftl")
#target_include_directories(libftl
#	PUBLIC src "${CMAKE_CURRENT_BINARY_DIR}/include")

#target_include_directories(libobs
#	PUBLIC
#		"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
#		"$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/config>"
#		"$<INSTALL_INTERFACE:${OBS_INCLUDE_DESTINATION}>")

#target_link_libraries(libobs
#	PRIVATE
#		${libobs_PLATFORM_DEPS}
#		${libobs_image_loading_LIBRARIES}
#		${OBS_JANSSON_IMPORT}
#		${FFMPEG_LIBRARIES}
#		${ZLIB_LIBRARIES}
#	PUBLIC
#		${THREADS_LIBRARIES})

#install_obs_core(libobs EXPORT LibObs)
#install_obs_data(libobs data libobs)
#install_obs_headers(${libobs_HEADERS})

#obs_install_additional(libobs)
