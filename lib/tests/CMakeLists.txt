# AudioThief 单元测试
enable_testing()

# 因为测试需要直接访问内部头文件，添加内部src目录到include路径
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)

# 定义测试列表
set(TEST_SOURCES
    test_fft
    test_qam
    test_ofdm
    test_rs
    test_crc
    test_frame_sync
    test_arq
    test_loopback
)

foreach(test_name ${TEST_SOURCES})
    add_executable(${test_name} ${test_name}.c)
    target_link_libraries(${test_name} PRIVATE atcp)
    target_include_directories(${test_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
    )
    if(UNIX)
        target_link_libraries(${test_name} PRIVATE m)
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
