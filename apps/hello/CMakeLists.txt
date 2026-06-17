cmake_minimum_required(VERSION 3.16.0)

project(hello C)

set(CAmkESCPP
    ON
    CACHE BOOL "" FORCE
)
includeGlobalComponents()

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/components/Hello)

DeclareCAmkESRootserver(hello.camkes)
add_simulate_test(
    [=[
    wait_for "Entering Ruby shell loop"
    send "help\\r"
    wait_for "commands: help, echo, version"
    ]=]
)
