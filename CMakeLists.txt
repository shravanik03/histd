#CMake version
cmake_minimum_required(VERSION 3.16)

#Project
project(histd LANGUAGES CXX)

#Setting C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug")
endif()

string(APPEND CMAKE_CXX_FLAGS " -Wall -Wextra")
string(APPEND CMAKE_CXX_FLAGS_DEBUG " -g -fsanitize=address")
string(APPEND CMAKE_EXE_LINKER_FLAGS_DEBUG " -fsanitize=address")

add_library(histd_core STATIC
  src/record_store.cpp
  src/tokenizer.cpp
  src/inverted_index.cpp
  src/index_store.cpp
  src/query_engine.cpp
  src/failure_clusterer.cpp
)

target_include_directories(histd_core PUBLIC ${CMAKE_SOURCE_DIR}/src)

add_executable(histd daemon/main.cpp)
target_link_libraries(histd PRIVATE histd_core)

add_executable(hist cli/main.cpp)
target_link_libraries(hist PRIVATE histd_core)

add_subdirectory(tests)