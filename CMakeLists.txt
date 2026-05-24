cmake_minimum_required(VERSION 3.16)
project(p2p_network VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

find_package(Boost REQUIRED)

add_library(p2p_network
    src/peer.cpp
    src/connection.cpp
    src/stun_client.cpp
    src/p2p_node.cpp
)

add_library(p2p::network ALIAS p2p_network)

target_include_directories(p2p_network
    PUBLIC
        include
    PRIVATE
        src
)

if(TARGET Boost::headers)
    target_link_libraries(p2p_network PUBLIC Boost::headers)
elseif(TARGET Boost::boost)
    target_link_libraries(p2p_network PUBLIC Boost::boost)
elseif(TARGET boost::boost)
    target_link_libraries(p2p_network PUBLIC boost::boost)
else()
    target_include_directories(p2p_network PUBLIC ${Boost_INCLUDE_DIRS})
endif()

target_compile_definitions(p2p_network PUBLIC
    BOOST_ALL_NO_LIB
    BOOST_ERROR_CODE_HEADER_ONLY
    BOOST_SYSTEM_NO_LIB
)

add_executable(p2p_diag
    tools/p2p_diag.cpp
)

target_link_libraries(p2p_diag PRIVATE p2p_network)

add_executable(p2p
    tools/p2p_diag.cpp
)

target_link_libraries(p2p PRIVATE p2p_network)

if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    add_test(
        NAME p2p_e2e
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/p2p_e2e.py
            --binary $<TARGET_FILE:p2p_diag>
    )
endif()
