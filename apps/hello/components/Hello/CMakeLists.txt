cmake_minimum_required(VERSION 3.16.0)

project(ComponentHello C)

DeclareCAmkESComponent(Hello SOURCES src/hello.c)
