cmake_minimum_required(VERSION 3.16)
project(raspi_nam LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED NAM_CORE_DIR)
  set(NAM_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../NeuralAmpModelerCore" CACHE PATH
      "Path to a NeuralAmpModelerCore checkout")
endif()

if(NOT EXISTS "${NAM_CORE_DIR}/NAM/get_dsp.h")
  message(FATAL_ERROR
    "NeuralAmpModelerCore was not found at '${NAM_CORE_DIR}'. "
    "Clone it next to this repo or pass -DNAM_CORE_DIR=/path/to/NeuralAmpModelerCore")
endif()

if(NOT EXISTS "${NAM_CORE_DIR}/Dependencies/eigen/Eigen/Core")
  message(FATAL_ERROR
    "Eigen submodule not found under '${NAM_CORE_DIR}/Dependencies/eigen'. "
    "Run 'git -C ${NAM_CORE_DIR} submodule update --init --recursive'")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(PORTAUDIO REQUIRED IMPORTED_TARGET portaudio-2.0)

find_path(NLOHMANN_JSON_INCLUDE_DIR
  json.hpp
  PATH_SUFFIXES nlohmann
  REQUIRED
)

file(GLOB_RECURSE NAM_SOURCES CONFIGURE_DEPENDS
  "${NAM_CORE_DIR}/NAM/*.cpp"
  "${NAM_CORE_DIR}/NAM/*.c"
)

add_executable(nam_pisound_host
  main.cpp
  ${NAM_SOURCES}
)

target_include_directories(nam_pisound_host PRIVATE
  "${NAM_CORE_DIR}"
  "${NAM_CORE_DIR}/Dependencies/eigen"
  "${NLOHMANN_JSON_INCLUDE_DIR}"
)

target_compile_definitions(nam_pisound_host PRIVATE
  NAM_SAMPLE_FLOAT
)

target_link_libraries(nam_pisound_host PRIVATE
  PkgConfig::PORTAUDIO
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(nam_pisound_host PRIVATE
    -O3
    -Wall
    -Wextra
    -Wpedantic
  )
endif()
