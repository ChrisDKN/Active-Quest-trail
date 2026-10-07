cmake_minimum_required(VERSION 3.25)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" AQT_VERSION LIMIT_COUNT 1)
project(ActiveQuestTrail VERSION ${AQT_VERSION} LANGUAGES CXX)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
configure_file(cmake/Version.h.in generated/Version.h @ONLY)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
set(FMT_MODULE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)

set(AQT_DEPS "${CMAKE_SOURCE_DIR}/.build-deps" CACHE PATH "Build dependency directory")
set(AQT_COMMONLIB "${AQT_DEPS}/commonlib-prebuilt/commonlibsse-ng-prebuilt-v11.0.0-all-msvc-cmake")
if(NOT EXISTS "${AQT_COMMONLIB}/lib/CommonLibSSE.lib")
    message(FATAL_ERROR "Run python scripts/bootstrap.py before configuring")
endif()

add_subdirectory("${AQT_DEPS}/fmt" "deps/fmt" EXCLUDE_FROM_ALL)
add_subdirectory("${AQT_DEPS}/spdlog" "deps/spdlog" EXCLUDE_FROM_ALL)
add_library(CommonLibSSE STATIC IMPORTED)
set_target_properties(CommonLibSSE PROPERTIES
    IMPORTED_LOCATION "${AQT_COMMONLIB}/lib/CommonLibSSE.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${AQT_COMMONLIB}/include;${AQT_COMMONLIB}/extern/openvr/headers;${AQT_DEPS}/directxmath/Inc;${AQT_DEPS}/directxtk/Inc"
    INTERFACE_COMPILE_DEFINITIONS "ENABLE_SKYRIM_SE=1;ENABLE_SKYRIM_AE=1;ENABLE_SKYRIM_VR=1;HAS_SKYRIM_MULTI_TARGETING=1;WINVER=0x0601;_WIN32_WINNT=0x0601"
)

function(add_trail_plugin target)
    add_library(${target} SHARED src/Plugin.cpp src/Keybind.cpp src/Settings.cpp src/Trail.cpp src/DestinationGlow.cpp src/ChickenGuide.cpp)
    target_include_directories(${target} PRIVATE src extern "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_precompile_headers(${target} PRIVATE src/PCH.h)
    target_compile_options(${target} PRIVATE /EHsc /Zi /W3 "$<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>")
    target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(${target} PRIVATE CommonLibSSE spdlog::spdlog fmt::fmt
        "${AQT_COMMONLIB}/lib/openvr_api.lib"
        advapi32 bcrypt d3d11 d3dcompiler dbghelp dxgi ole32 shell32 user32 version)
    target_link_options(${target} PRIVATE /DEBUG:FULL /OPT:REF /OPT:ICF)
    set_target_properties(${target} PROPERTIES PREFIX "")
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "ActiveQuestTrail")
endfunction()

add_trail_plugin(ActiveQuestTrail)
add_trail_plugin(ActiveQuestTrail1597)
target_compile_definitions(ActiveQuestTrail1597 PRIVATE AQT_RUNTIME_1597=1)
set_target_properties(ActiveQuestTrail1597 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/1.5.97"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/1.5.97"
    PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/1.5.97")

add_trail_plugin(ActiveQuestTrail17104)
target_compile_definitions(ActiveQuestTrail17104 PRIVATE AQT_RUNTIME_17104=1)
set_target_properties(ActiveQuestTrail17104 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/1.7.104"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/1.7.104"
    PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/1.7.104")

install(TARGETS ActiveQuestTrail RUNTIME DESTINATION SKSE/Plugins)
install(DIRECTORY package/ DESTINATION . PATTERN "ActiveQuestTrail.ini" EXCLUDE)
