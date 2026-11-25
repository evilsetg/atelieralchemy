cmake_minimum_required(VERSION 3.24)

project(
	AtelierAlchemy
	VERSION 0.1.2
	LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

include(cmake/Dependencies.cmake)

file(GLOB_RECURSE PLUGIN_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

add_commonlibsse_plugin(
	${PROJECT_NAME}
	AUTHOR "Astrid Krein"
	SOURCES ${PLUGIN_SOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_precompile_headers(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/PCH.h")
target_link_libraries(${PROJECT_NAME} PRIVATE imgui nlohmann_json::nlohmann_json)
target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE /W3 /utf-8 /Zc:preprocessor)
	target_link_options(${PROJECT_NAME} PRIVATE /DEBUG:FULL)
endif()

# Stage the full mod (DLL + data) into dist/ for packaging. A custom target always runs,
# so edits to data/ are picked up even when the DLL is up to date.
set(DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist/AtelierAlchemy")
add_custom_target(
	stage ALL
	COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}/SKSE/Plugins"
	COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${PROJECT_NAME}>" "${DIST_DIR}/SKSE/Plugins/"
	COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/data" "${DIST_DIR}"
	DEPENDS ${PROJECT_NAME}
	VERBATIM
)
