cmake_minimum_required(VERSION 3.22)
project(Progetto_Sistemi_Op_2023_2 C)

set(CMAKE_C_STANDARD 23)

add_executable(Progetto_Sistemi_Op_2023 server.c client.c shared_memory.c ../inc/shared_memory.h ../inc/semaphore.h semaphore.c)
