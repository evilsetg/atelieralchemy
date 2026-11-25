include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# --- spdlog (CommonLib does find_package(spdlog CONFIG REQUIRED)) ---------------
FetchContent_Declare(
	spdlog
	GIT_REPOSITORY https://github.com/gabime/spdlog.git
	GIT_TAG v1.16.0
	GIT_SHALLOW TRUE
	OVERRIDE_FIND_PACKAGE
)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL ON CACHE BOOL "" FORCE) # CommonLib exports its targets; spdlog must be in an export set too
set(SPDLOG_USE_STD_FORMAT OFF CACHE BOOL "" FORCE)
set(SPDLOG_WCHAR_FILENAMES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

# --- rapidcsv (header only; CommonLib does find_path) ---------------------------
FetchContent_Declare(
	rapidcsv
	GIT_REPOSITORY https://github.com/d99kris/rapidcsv.git
	GIT_TAG v8.90
	GIT_SHALLOW TRUE
	SOURCE_SUBDIR do-not-build
)
FetchContent_MakeAvailable(rapidcsv)
set(RAPIDCSV_INCLUDE_DIRS "${rapidcsv_SOURCE_DIR}/src" CACHE PATH "" FORCE)

# --- DirectXTK: CommonLib only needs SimpleMath, so we build just that file ----
# (the full library needs fxc.exe for its shaders, which doesn't exist on Linux)
FetchContent_Declare(
	directxtk
	GIT_REPOSITORY https://github.com/microsoft/DirectXTK.git
	GIT_TAG oct2025
	GIT_SHALLOW TRUE
	SOURCE_SUBDIR do-not-build
)
FetchContent_MakeAvailable(directxtk)
add_library(directxtk_simplemath STATIC "${directxtk_SOURCE_DIR}/Src/SimpleMath.cpp")
target_include_directories(directxtk_simplemath PUBLIC "${directxtk_SOURCE_DIR}/Inc")
target_compile_features(directxtk_simplemath PUBLIC cxx_std_17)
# IMPORTED so CommonLib's install(EXPORT) doesn't require it to be in an export set.
add_library(Microsoft::DirectXTK INTERFACE IMPORTED GLOBAL)
set_target_properties(Microsoft::DirectXTK PROPERTIES
	INTERFACE_INCLUDE_DIRECTORIES "${directxtk_SOURCE_DIR}/Inc"
	INTERFACE_LINK_LIBRARIES directxtk_simplemath
)
file(WRITE "${CMAKE_BINARY_DIR}/fake-packages/directxtk/directxtk-config.cmake" "# provided by Dependencies.cmake\n")
set(directxtk_DIR "${CMAKE_BINARY_DIR}/fake-packages/directxtk" CACHE PATH "" FORCE)

# --- CommonLibSSE-NG ------------------------------------------------------------
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SKSE_SUPPORT_XBYAK OFF CACHE BOOL "" FORCE)
set(COMMONLIB_PREBUILT OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
	commonlibsse
	GIT_REPOSITORY https://github.com/alandtse/CommonLibVR.git
	GIT_TAG v16.12.0
	GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(commonlibsse)
if(NOT COMMAND add_commonlibsse_plugin)
	include("${commonlibsse_SOURCE_DIR}/cmake/CommonLibSSE.cmake")
endif()

# --- Dear ImGui -----------------------------------------------------------------
FetchContent_Declare(
	imgui
	GIT_REPOSITORY https://github.com/ocornut/imgui.git
	GIT_TAG v1.91.9b
	GIT_SHALLOW TRUE
	SOURCE_SUBDIR do-not-build
)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
	"${imgui_SOURCE_DIR}/imgui.cpp"
	"${imgui_SOURCE_DIR}/imgui_draw.cpp"
	"${imgui_SOURCE_DIR}/imgui_tables.cpp"
	"${imgui_SOURCE_DIR}/imgui_widgets.cpp"
	"${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp"
)
target_include_directories(imgui PUBLIC "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(imgui PUBLIC d3d11.lib dxgi.lib d3dcompiler.lib)

# --- nlohmann/json --------------------------------------------------------------
FetchContent_Declare(
	nlohmann_json
	URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)
