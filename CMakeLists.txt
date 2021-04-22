cmake_minimum_required(VERSION 3.15)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS FALSE)

add_compile_options(
    -pedantic-errors
    -Wno-unused-parameter
    "$<$<CONFIG:DEBUG>:-Og>" "$<$<CONFIG:DEBUG>:-g3>"
    "$<$<CONFIG:RELEASE>:-O3>" "$<$<CONFIG:RELEASE>:-g3>"
    -fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free
    -pthread
)

set(DEPS_DEPLOY_DIR ${CMAKE_BINARY_DIR}/_deps)

include_directories ("${PROJECT_SOURCE_DIR}")

include("FetchContent")
FetchContent_Declare(
	spdlog
	GIT_REPOSITORY https://github.com/gabime/spdlog.git
	GIT_TAG v1.4.2
)
FetchContent_MakeAvailable(spdlog)

link_libraries(pthread)

file(GLOB_RECURSE mains RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/example/*.cpp")
foreach(mainfile IN LISTS mains)
	get_filename_component(mainname ${mainfile} NAME_WE)
	add_executable(${mainname} ${mainfile})
	target_include_directories(${mainname} PUBLIC ${DEPS_DEPLOY_DIR}/spdlog-src/include/)
	target_link_libraries(spdlog)
endforeach()