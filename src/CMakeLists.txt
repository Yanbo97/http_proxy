cmake_minimum_required(VERSION 3.22)
project(HTTP)

set(CMAKE_CXX_STANDARD 20)
find_package(Boost REQUIRED COMPONENTS system filesystem log log_setup)
include_directories(${Boost_INCLUDE_DIRS})
add_executable(HTTP main.cpp
        Session.cpp
        Session.h
        Server.cpp
        Server.h
        Cache.cpp
        Cache.h)
target_link_libraries(HTTP ${Boost_LIBRARIES})
