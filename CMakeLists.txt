cmake_minimum_required(VERSION 3.28.3)
project(Mini_LevelDB)

set(CMAKE_CXX_STANDARD 20)

add_executable(Mini_LevelDB main.cpp
        mem_table/mem_table.cpp
        mem_table/includes/mem_table.h
        db/mini_kv_store.cpp
        db/includes/mini_kv_store.h
        db/includes/db_format.h
        db/includes/coding.h
        server/includes/wire_protocol.h
)
