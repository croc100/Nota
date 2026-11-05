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
  # MSVC는 소스를 시스템 코드 페이지로 읽는다. 한국어 Windows(CP949)에서는
  # UTF-8 주석이 전부 C4819 경고가 되어 빌드 로그가 수십 줄씩 오염된다.
  # /utf-8 은 소스와 실행 문자셋을 모두 UTF-8로 고정한다.
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8;/W4>
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
