# build service
set(SOURCE_FILES
        Executor.cpp
        )

add_library(Executor ${SOURCE_FILES})
target_link_libraries(Executor ${CMAKE_THREAD_LIBS_INIT})
