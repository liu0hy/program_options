cmake_minimum_required(VERSION 2.8.7)

project(program_options)

set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} --std=c++14)

set(SOURCE_FILES sample.cc)

add_executable(sample ${SOURCE_FILES})
