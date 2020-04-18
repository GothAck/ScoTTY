project(ScoTTY CXX)

cmake_minimum_required(VERSION 3.13)

find_package(Qt5 COMPONENTS Core Network REQUIRED)

add_executable(
  scotty
  main.cpp
)

install(TARGETS scotty DESTINATION bin PERMISSIONS WORLD_EXECUTE)

target_link_libraries(scotty docopt Qt5::Core Qt5::Network)
