cmake_minimum_required(VERSION 3.16)
project(p2p_demo)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(p2p
    src/main.cpp
    src/peer.cpp
    src/connection.cpp
)

# наши заголовки
target_include_directories(p2p PRIVATE
    src
)

# Boost.Asio и error_code как header-only,
# чтобы не тянуть libboost_system.so
target_compile_definitions(p2p PRIVATE
    BOOST_ALL_NO_LIB
    BOOST_ERROR_CODE_HEADER_ONLY
    BOOST_SYSTEM_NO_LIB
)
