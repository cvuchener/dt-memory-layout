cmake_minimum_required(VERSION 3.5)
project(dt-memory-layout)

option(USE_EXTERNAL_DFS "Use dfs from external subdirectory" ON)
if (${USE_EXTERNAL_DFS})
	add_subdirectory(external/libdfs)
	add_library(dfs::dfs ALIAS dfs)
else()
	find_package(dfs REQUIRED)
endif()

add_executable(dt-memory-layout dt-memory-layout.cpp)
target_link_libraries(dt-memory-layout dfs::dfs)

