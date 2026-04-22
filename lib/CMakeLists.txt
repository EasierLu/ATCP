file(GLOB_RECURSE ATCP_SOURCES "src/*.c")

add_library(atcp ${ATCP_SOURCES})

target_include_directories(atcp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(atcp PRIVATE ATCP_BUILDING_DLL)
endif()

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
