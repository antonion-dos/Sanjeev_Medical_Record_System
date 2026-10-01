cmake_minimum_required(VERSION 3.16)
project(SanjeevBlockchain VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Dependencies
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

include_directories(include)

# Core Blockchain Library
add_library(sanjeev_core
    src/crypto.cpp
    src/transaction.cpp
    src/block.cpp
    src/consensus.cpp
    src/storage.cpp
    src/blockchain.cpp
    src/server.cpp
)

target_link_libraries(sanjeev_core
    PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
    SQLite::SQLite3
    Threads::Threads
    $<$<PLATFORM_ID:Windows>:ws2_32>
)

# Blockchain Node Executable
add_executable(sanjeev_node src/main.cpp)
target_link_libraries(sanjeev_node PRIVATE sanjeev_core)

# CLI Lineage & Transaction Trace Tool
add_executable(sanjeev_trace src/trace_tool.cpp)
target_link_libraries(sanjeev_trace PRIVATE sanjeev_core)

# Tests
enable_testing()

add_executable(test_core_types tests/test_core_types.cpp)
target_link_libraries(test_core_types PRIVATE sanjeev_core)
add_test(NAME CoreTypesTests COMMAND test_core_types)

add_executable(test_storage tests/test_storage.cpp)
target_link_libraries(test_storage PRIVATE sanjeev_core)
add_test(NAME StorageTests COMMAND test_storage)

add_executable(test_blockchain tests/test_blockchain.cpp)
target_link_libraries(test_blockchain PRIVATE sanjeev_core)
add_test(NAME BlockchainTests COMMAND test_blockchain)

add_executable(test_document_encryption tests/test_document_encryption.cpp)
target_link_libraries(test_document_encryption PRIVATE sanjeev_core)
add_test(NAME DocumentEncryptionTests COMMAND test_document_encryption)

add_executable(test_document_update tests/test_document_update.cpp)
target_link_libraries(test_document_update PRIVATE sanjeev_core)
add_test(NAME DocumentUpdateTests COMMAND test_document_update)

add_executable(test_edge_cases tests/test_edge_cases.cpp)
target_link_libraries(test_edge_cases PRIVATE sanjeev_core)
add_test(NAME EdgeCaseTests COMMAND test_edge_cases)
