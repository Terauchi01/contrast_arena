add_library(contrast_core
  src/board.cpp
  src/rules.cpp
  src/game_state.cpp
  src/zobrist.cpp
)

target_include_directories(contrast_core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(contrast_core PUBLIC cxx_std_17)
