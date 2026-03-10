cmake_minimum_required(VERSION 3.16)
project(iso_writer VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(iso_writer iso_writer.cpp)

target_compile_options(iso_writer PRIVATE
    -Wall -Wextra -O2
)

target_link_libraries(iso_writer PRIVATE pthread)

install(TARGETS iso_writer DESTINATION /usr/local/bin)
