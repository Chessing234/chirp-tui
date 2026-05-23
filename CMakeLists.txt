cmake_minimum_required(VERSION 3.16)
project(reminders VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(reminders main.cpp)
target_include_directories(reminders PRIVATE ${CMAKE_SOURCE_DIR})

if(MSVC)
  target_compile_options(reminders PRIVATE /W4 /permissive-)
  target_compile_definitions(reminders PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(reminders PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "" AND NOT MSVC)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
  target_compile_options(reminders PRIVATE $<$<CONFIG:Release>:/O2>)
else()
  target_compile_options(reminders PRIVATE $<$<CONFIG:Release>:-O2>)
  if(NOT APPLE)
    target_link_options(reminders PRIVATE $<$<CONFIG:Release>:-s>)
  endif()
endif()

if(WIN32)
  # MSVC links the C++ runtime / thread support automatically.
elseif(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(reminders PRIVATE Threads::Threads)
endif()

install(TARGETS reminders RUNTIME DESTINATION bin)
