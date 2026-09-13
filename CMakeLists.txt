cmake_minimum_required(VERSION 3.25)

project(
	SkyParkourVibrationAddon
	VERSION 1.0.0
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
	message(FATAL_ERROR "In-source builds are not allowed.")
endif()

# CommonLibSSE-NG: SE + AE only (SkyParkour itself does not support VR).
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SKSE_SUPPORT_PATCH_SAFETY OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/CommonLibSSE-NG CommonLibSSE EXCLUDE_FROM_ALL)
include(extern/CommonLibSSE-NG/cmake/CommonLibSSE.cmake)

set(PLUGIN_AUTHOR "MrShersh")
set(PLUGIN_DISPLAY_NAME "SkyParkour Vibration Addon")

# add_commonlibsse_plugin sets only the SKSE plugin version; this resource fills the DLL's Windows file properties,
# which crash logs show to identify the build.
configure_file(cmake/version.rc.in "${CMAKE_CURRENT_BINARY_DIR}/version.rc" @ONLY)

add_commonlibsse_plugin(
	${PROJECT_NAME}
	AUTHOR "${PLUGIN_AUTHOR}"
	SOURCES
		"${CMAKE_CURRENT_BINARY_DIR}/version.rc"
		src/Haptics.cpp
		src/Haptics.h
		src/main.cpp
		src/Menu.cpp
		src/Menu.h
		src/Settings.cpp
		src/Settings.h
		src/Vibration.cpp
		src/Vibration.h
)

target_include_directories(${PROJECT_NAME} PRIVATE src extern/SKSEMenuFramework)
# windows.h leaks in through dependencies; keep its min/max macros out of std::min/std::max.
target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

if(MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE /W4 /Zi)
	target_link_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:Release>:/DEBUG:FULL;/OPT:REF;/OPT:ICF>")
endif()

set(DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist" CACHE PATH "Mod folder the build output is copied to")
add_custom_command(
	TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/data" "${DIST_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"$<TARGET_FILE:${PROJECT_NAME}>"
		"$<TARGET_PDB_FILE:${PROJECT_NAME}>"
		"${DIST_DIR}/SKSE/Plugins/"
)
