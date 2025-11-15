cmake_minimum_required(VERSION 3.10)
project(CircularLinkedList_All C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(LIST_CFG_USE_MALLOC "Use malloc/free mode" OFF)
option(LIST_CFG_FAST_ALLOC "Enable fast allocation stack" ON)
option(LIST_CFG_SIZE_COUNTER "Enable size counter" OFF)
option(ZEROLIST_CFG_FALLBACK "Enable static fallback malloc" OFF)
option(ZEROLIST_CFG_DYNAMIC_EXPAND "Enable dynamic expand mode" ON)
set(LIST_CFG_ZEROLIST_TYPE "uint8_t" CACHE STRING "ZEROLIST_TYPE definition (e.g. uint16_t)")

if(LIST_CFG_USE_MALLOC AND LIST_CFG_FAST_ALLOC)
    message(FATAL_ERROR "LIST_CFG_FAST_ALLOC only works when LIST_CFG_USE_MALLOC=OFF")
endif()

if(LIST_CFG_USE_MALLOC AND LIST_CFG_FALLBACK)
    message(FATAL_ERROR "Static fallback malloc cannot be enabled in malloc mode")
endif()

if(LIST_CFG_USE_MALLOC AND ZEROLIST_CFG_DYNAMIC_EXPAND)
    message(FATAL_ERROR "Dynamic expand mode requires static allocation")
endif()

if(ZEROLIST_CFG_DYNAMIC_EXPAND AND LIST_CFG_FALLBACK)
    message(FATAL_ERROR "Fallback malloc and dynamic expand are mutually exclusive")
endif()

function(bool_to_int out_var value)
    if(${value})
        set(${out_var} 1 PARENT_SCOPE)
    else()
        set(${out_var} 0 PARENT_SCOPE)
    endif()
endfunction()

bool_to_int(LIST_CFG_USE_MALLOC_INT LIST_CFG_USE_MALLOC)
bool_to_int(LIST_CFG_FAST_ALLOC_INT LIST_CFG_FAST_ALLOC)
bool_to_int(LIST_CFG_SIZE_COUNTER_INT LIST_CFG_SIZE_COUNTER)
bool_to_int(LIST_CFG_FALLBACK_INT LIST_CFG_FALLBACK)
bool_to_int(LIST_CFG_DYNAMIC_EXPAND_INT ZEROLIST_CFG_DYNAMIC_EXPAND)

# 源文件
set(SRCS zerolist.c)

# 包含目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_executable(example_fallback example/example.c ${SRCS})
target_include_directories(example_fallback PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# target_compile_definitions(example_fallback
#     PRIVATE
#         ZEROLIST_USE_MALLOC=${LIST_CFG_USE_MALLOC_INT}
#         ZEROLIST_FAST_ALLOC=${LIST_CFG_FAST_ALLOC_INT}
#         ZEROLIST_SIZE_ENABLE=${LIST_CFG_SIZE_COUNTER_INT}
#         ZEROZEROLIST_STATIC_FALLBACK_MALLOC=${LIST_CFG_FALLBACK_INT}
#         ZEROZEROLIST_STATIC_DYNAMIC_EXPAND=${LIST_CFG_DYNAMIC_EXPAND_INT}
#         ZEROLIST_TYPE=${LIST_CFG_ZEROLIST_TYPE}
# )
