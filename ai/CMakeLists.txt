cmake_minimum_required(VERSION 3.15)
project(test_rulebased LANGUAGES CXX)

set(CORE_DIR ${CMAKE_CURRENT_LIST_DIR}/../core)
set(CORE_SOURCES
    ${CORE_DIR}/src/board.cpp
    ${CORE_DIR}/src/game_state.cpp
    ${CORE_DIR}/src/rules.cpp
    ${CORE_DIR}/src/zobrist.cpp
)

add_executable(test_rulebased
    main.cpp
    src/contrast_ai/rule_based_policy.cpp
    src/contrast_ai/random_policy.cpp
    ${CORE_SOURCES}
)

target_include_directories(test_rulebased PRIVATE
    ${CORE_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_compile_features(test_rulebased PRIVATE cxx_std_20)
