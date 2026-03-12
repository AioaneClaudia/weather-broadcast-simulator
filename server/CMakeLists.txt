cmake_minimum_required(VERSION 3.10)
project(WeatherServer)

set(CMAKE_CXX_STANDARD 17)

if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0601)
endif()

add_executable(WeatherServer main.cpp)

if(WIN32)
    target_link_libraries(WeatherServer ws2_32)
endif()
