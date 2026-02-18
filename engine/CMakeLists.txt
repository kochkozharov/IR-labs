cmake_minimum_required(VERSION 3.10)
project(search_engine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall")

find_package(Threads REQUIRED)

set(SOURCES
    src/main.cpp
    src/tokenizer.cpp
    src/stemmer.cpp
    src/inverted_index.cpp
    src/boolean_search.cpp
    src/zipf_analyzer.cpp
    src/json_reader.cpp
)

add_executable(engine ${SOURCES})

target_include_directories(engine PRIVATE src /usr/local/include)
target_link_libraries(engine Threads::Threads)
