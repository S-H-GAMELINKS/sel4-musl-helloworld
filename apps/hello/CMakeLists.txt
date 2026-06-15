cmake_minimum_required(VERSION 3.16.0)

project(hello C)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/components/Hello)

DeclareCAmkESRootserver(hello.camkes)
add_simulate_test([=[wait_for "Hello World"]=])
