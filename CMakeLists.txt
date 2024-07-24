cmake_minimum_required(VERSION 3.10)

project(HelloWorld)

find_package(fmt CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(OpenAL CONFIG REQUIRED)
find_package(SndFile CONFIG REQUIRED)

include_directories(include SYSTEM "include/glad")

add_executable(HelloWorld 
	src/main.cpp
	src/glad.c)

target_link_libraries(HelloWorld PRIVATE fmt::fmt)
target_link_libraries(HelloWorld PRIVATE glfw OpenAL::OpenAL SndFile::sndfile)
