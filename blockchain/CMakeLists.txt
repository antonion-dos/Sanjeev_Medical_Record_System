cmake_minimum_required(VERSION 3.16)
project(SanjeevBlockchain VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Dependencies
find_package(OpenSSL REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include_directories(include)

# Core Blockchain Library
add_library(sanjeev_core
    src/crypto.cpp
    src/transaction.cpp
    src/block.cpp
    src/consensus.cpp
    src/blockchain.cpp
    src/server.cpp
)

target_link_libraries(sanjeev_core
    PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

# Blockchain Node Executable
add_executable(sanjeev_node src/main.cpp)
target_link_libraries(sanjeev_node PRIVATE sanjeev_core)

# Tests
enable_testing()
add_executable(test_crypto tests/test_crypto.cpp)
target_link_libraries(test_crypto PRIVATE sanjeev_core)
add_test(NAME CryptoTests COMMAND test_crypto)
