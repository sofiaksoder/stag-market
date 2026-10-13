cmake_minimum_required(VERSION 3.20)
project(stag_trader CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(engine INTERFACE)
target_include_directories(engine INTERFACE ${CMAKE_SOURCE_DIR}/engine)
target_compile_options(engine INTERFACE -Wall -Wextra -Wpedantic)

add_library(gateway_lib INTERFACE)
target_include_directories(gateway_lib INTERFACE ${CMAKE_SOURCE_DIR}/gateway)
target_link_libraries(gateway_lib INTERFACE engine)

option(STAG_SANITIZE "Build with ASan+UBSan" OFF)
if(STAG_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

enable_testing()
add_executable(order_book_test tests/order_book_test.cpp)
target_link_libraries(order_book_test PRIVATE engine)
add_test(NAME order_book_test COMMAND order_book_test)

add_executable(gateway_test tests/gateway_test.cpp)
target_link_libraries(gateway_test PRIVATE gateway_lib)
add_test(NAME gateway_test COMMAND gateway_test)

add_executable(stag_gateway gateway/main.cpp)
target_link_libraries(stag_gateway PRIVATE gateway_lib)

add_executable(stag_bench bench/perf.cpp)
target_include_directories(stag_bench PRIVATE bench)
target_link_libraries(stag_bench PRIVATE gateway_lib)

add_executable(ws_test tests/ws_test.cpp)
target_link_libraries(ws_test PRIVATE gateway_lib)
add_test(NAME ws_test COMMAND ws_test)

add_executable(stag_sim tools/sim.cpp)
target_include_directories(stag_sim PRIVATE tools)
target_link_libraries(stag_sim PRIVATE gateway_lib)

add_executable(stag_replay tools/replay.cpp)
target_link_libraries(stag_replay PRIVATE gateway_lib)

add_executable(journal_test tests/journal_test.cpp)
target_include_directories(journal_test PRIVATE tools)
target_link_libraries(journal_test PRIVATE gateway_lib)
add_test(NAME journal_test COMMAND journal_test)

# The browser build's C API, compiled natively for testing
add_library(stag_wasm_api STATIC wasm/stag_wasm.cpp)
target_include_directories(stag_wasm_api PUBLIC tools)
target_link_libraries(stag_wasm_api PUBLIC gateway_lib)

add_executable(wasm_api_test tests/wasm_api_test.cpp)
target_link_libraries(wasm_api_test PRIVATE stag_wasm_api)
add_test(NAME wasm_api_test COMMAND wasm_api_test)
