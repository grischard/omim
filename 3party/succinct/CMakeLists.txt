cmake_minimum_required(VERSION 3.2)

project(succinct C CXX)

add_compile_options(
  "-Wall"
  "-std=c++11"
)

add_definitions(-DLTC_NO_ROLC)

include_directories(src/headers)

set(
  SRC
  bit_vector.hpp
  bp_vector.cpp
  bp_vector.hpp
  broadword.hpp
  cartesian_tree.hpp
  darray.hpp
  darray64.hpp
  elias_fano_compressed_list.hpp
  elias_fano_list.hpp
  elias_fano.hpp
  forward_enumerator.hpp
  gamma_bit_vector.hpp
  gamma_vector.hpp
  intrinsics.hpp
  mappable_vector.hpp
  mapper.hpp
  nibble_vector.hpp
  rs_bit_vector.cpp
  rs_bit_vector.hpp
  tables.hpp
  topk_vector.hpp
  util.hpp
  vbyte.hpp
)

add_library(succinct ${SRC})
