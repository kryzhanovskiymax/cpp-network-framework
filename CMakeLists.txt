cmake_minimum_required(VERSION 3.11)

# Проект называется Hello и написан на C++
project(Hello CXX)
# Исходый код будет компилироваться с поддержкой стандарта С++ 20
set(CMAKE_CXX_STANDARD 20)
include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

find_package(Boost 1.78.0 REQUIRED)
if(Boost_FOUND)
  include_directories(${Boost_INCLUDE_DIRS})
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(app src/main.cpp)
add_compile_definitions(BOOST_BEAST_USE_STD_STRING_VIEW) 
target_link_libraries(app PRIVATE Threads::Threads) 