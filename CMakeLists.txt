cmake_minimum_required(VERSION 3.15)
project(Kasturi C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/utf-8 /W3)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# Outputs go to the source tree so the compiler finds lib/ and runtime/
# relative to its own location (bin/../lib).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} ${CMAKE_SOURCE_DIR}/bin)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${cfg} ${CMAKE_SOURCE_DIR}/lib)
endforeach()

file(GLOB KASTURI_SOURCES ${CMAKE_SOURCE_DIR}/src/*.c)
add_executable(kasturi ${KASTURI_SOURCES})

file(GLOB KASTURI_RT_SOURCES ${CMAKE_SOURCE_DIR}/runtime/*.c)
add_library(kasturi_rt STATIC ${KASTURI_RT_SOURCES})
if(NOT WIN32)
  target_link_libraries(kasturi_rt PUBLIC m)
endif()

install(TARGETS kasturi RUNTIME DESTINATION kasturi/bin)
install(TARGETS kasturi_rt ARCHIVE DESTINATION kasturi/lib)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/lib/ DESTINATION kasturi/lib FILES_MATCHING PATTERN "*.kst")
install(DIRECTORY ${CMAKE_SOURCE_DIR}/runtime/ DESTINATION kasturi/runtime)
