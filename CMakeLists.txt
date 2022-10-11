cmake_minimum_required(VERSION 3.8)
project(extract)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(deps/libminecraft-file)
add_subdirectory(deps/wingetopt)

set(source_files src/main.cpp)
add_executable(extract ${source_files})
target_link_libraries(extract PRIVATE mcfile wingetopt)
target_include_directories(extract PRIVATE deps/wingetopt/src)
source_group(TREE ${CMAKE_CURRENT_LIST_DIR} FILES ${source_files})
