cmake_minimum_required(VERSION 3.26)
project(Support C)

set(CMAKE_C_STANDARD 11)

include_directories(.)
include_directories(queue)
include_directories(YAUThreads)

add_executable(Support
        queue.h
        #queue/queue_example.c
        sut.c
        sut.h
#        test1.c
#        test2.c
#        test3.c
        test4.c
#        test5.c
)
