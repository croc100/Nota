cmake_minimum_required(VERSION 3.20)
project(Nota LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(NOTA_BUILD_TESTS "Build NotaCore unit tests" ON)
option(NOTA_BUILD_TOOLS "Build CLI verification harnesses" ON)
option(NOTA_SANITIZE    "Enable Address+UB sanitizers" OFF)

# 코어는 플랫폼 중립이어야 한다 (docs/DESIGN.md 3.2). 경고를 엄격히 둔다.
add_library(nota_flags INTERFACE)
target_compile_options(nota_flags INTERFACE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>
)
if(NOTA_SANITIZE)
  target_compile_options(nota_flags INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(nota_flags    INTERFACE -fsanitize=address,undefined)
endif()

add_subdirectory(core)

if(NOTA_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

if(NOTA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(core/tests)
endif()
