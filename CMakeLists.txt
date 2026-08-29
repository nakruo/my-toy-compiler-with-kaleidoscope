cmake_minimum_required(VERSION 3.10)
project(Kaleidoscope)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(LLVM REQUIRED CONFIG)

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

llvm_map_components_to_libnames(llvm_libs Core Passes)

add_executable(kaleidoscope main.cpp)
target_link_libraries(kaleidoscope ${llvm_libs})