include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(linden STATIC
    ${CMAKE_SOURCE_DIR}/lib/spraylist2/linden_common.c
    ${CMAKE_SOURCE_DIR}/lib/spraylist2/linden.c
    ${CMAKE_SOURCE_DIR}/lib/spraylist2/gc/gc.c
    ${CMAKE_SOURCE_DIR}/lib/spraylist2/gc/ptst.c
    linden.cpp
)
execute_process(COMMAND getconf LEVEL1_DCACHE_LINESIZE OUTPUT_VARIABLE CACHE_LINE_SIZE)
set_target_properties(linden PROPERTIES COMPILE_FLAGS
    "-w -std=gnu11 -DINTEL -DCACHE_LINE_SIZE=${CACHE_LINE_SIZE}"
)
target_include_directories(linden PUBLIC
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/lib/spraylist2/include
)


add_library(sequence_heap STATIC
    ${CMAKE_SOURCE_DIR}/lib/sequence_heap/knheap.C
    sequence_heap.cpp
)
set_target_properties(sequence_heap PROPERTIES COMPILE_FLAGS
    "-w"
)
target_include_directories(sequence_heap PUBLIC
    ${CMAKE_SOURCE_DIR}/lib/sequence_heap
)

add_library(skip_queue STATIC
    skip_queue.cpp
)
target_include_directories(skip_queue PUBLIC
    ${CMAKE_SOURCE_DIR}/lib/skip_list
)

add_library(spraylist STATIC
    ${CMAKE_SOURCE_DIR}/lib/spraylist2/fraser.c
    ${CMAKE_SOURCE_DIR}/lib/spraylist2/intset.c
    ${CMAKE_SOURCE_DIR}/lib/spraylist2/linden_common.c
    ${CMAKE_SOURCE_DIR}/lib/spraylist2/linden.c
    ${CMAKE_SOURCE_DIR}/lib/spraylist2/measurements.c
    ${CMAKE_SOURCE_DIR}/lib/spraylist2/pqueue.c
    ${CMAKE_SOURCE_DIR}/lib/spraylist2/skiplist.c
    ${CMAKE_SOURCE_DIR}/lib/spraylist2/ssalloc.c
    spraylist.cpp
)
set_target_properties(spraylist PROPERTIES COMPILE_FLAGS
    "-w -std=gnu11 -DINTEL -DLOCKFREE -DCACHE_LINE_SIZE=${CACHE_LINE_SIZE}"
)
target_include_directories(spraylist PUBLIC
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/lib/spraylist2/atomic_ops
    ${CMAKE_SOURCE_DIR}/lib/spraylist2/include
)


add_library(qdcatree STATIC
    ${CMAKE_SOURCE_DIR}/lib/qdcatree/skiplist_qdcatree_set.c
    ${CMAKE_SOURCE_DIR}/lib/qdcatree/skiplist_fpaqdcatree_set.c
    ${CMAKE_SOURCE_DIR}/lib/qdcatree/skiplist_cachedqdcatree_set.c
    ${CMAKE_SOURCE_DIR}/lib/qdcatree/skiplist_adaptivecachedqdcatree_set.c
    ${CMAKE_SOURCE_DIR}/lib/qdcatree/skiplist_fpaqdcatree_set.c
    ${CMAKE_SOURCE_DIR}/lib/qdcatree/feseq_skiplist.c
    ${CMAKE_SOURCE_DIR}/lib/qdcatree/catreesrc/locks/qd_lock.c
    ${CMAKE_SOURCE_DIR}/lib/qdcatree/catreesrc/locks/tatas_lock.c
    ${CMAKE_SOURCE_DIR}/lib/spraylist2/gc/gc.c
    ${CMAKE_SOURCE_DIR}/lib/spraylist2/gc/ptst.c
    qdcatree.cpp
    cachedqdcatree.cpp
    adaptivecachedqdcatree.cpp
)
set_target_properties(qdcatree PROPERTIES COMPILE_FLAGS
    "-w -std=gnu11 -DINTEL -DCACHE_LINE_SIZE=${CACHE_LINE_SIZE}"
)
target_include_directories(qdcatree PUBLIC
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/lib/qdcatree
    ${CMAKE_SOURCE_DIR}/lib/qdcatree/catreesrc
    ${CMAKE_SOURCE_DIR}/lib/spraylist2
)
