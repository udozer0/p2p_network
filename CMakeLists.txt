cmake_minimum_required(VERSION 3.16)
project(p2p_demo)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED)

add_executable(p2p
    src/main.cpp
    src/peer.cpp
    src/connection.cpp
    src/stun_client.cpp
)

target_include_directories(p2p PRIVATE
    src
)

if(TARGET Boost::headers)
    target_link_libraries(p2p PRIVATE Boost::headers)
elseif(TARGET Boost::boost)
    target_link_libraries(p2p PRIVATE Boost::boost)
elseif(TARGET boost::boost)
    target_link_libraries(p2p PRIVATE boost::boost)
else()
    target_include_directories(p2p PRIVATE ${Boost_INCLUDE_DIRS})
endif()

target_compile_definitions(p2p PRIVATE
    BOOST_ALL_NO_LIB
    BOOST_ERROR_CODE_HEADER_ONLY
    BOOST_SYSTEM_NO_LIB
)
