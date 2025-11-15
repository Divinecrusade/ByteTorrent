function(add_module_library MODULE_NAME)
    add_library(${MODULE_NAME})
    
    file(GLOB MODULE_INTERFACES 
        modules/${MODULE_NAME}.ixx
        modules/${MODULE_NAME}-*.ixx
    )
    
    target_sources(${MODULE_NAME} 
        PUBLIC FILE_SET CXX_MODULES FILES ${MODULE_INTERFACES}
    )
    
    file(GLOB MODULE_SOURCES src/${MODULE_NAME}/*.cpp)
    
    if(MODULE_SOURCES)
        target_sources(${MODULE_NAME} PRIVATE ${MODULE_SOURCES})
    endif()
    
    if(ARGN)
        target_link_libraries(${MODULE_NAME} PUBLIC ${ARGN})
    endif()
endfunction()

function(add_module_test MODULE_NAME)
    set(TEST_NAME ${MODULE_NAME}_test)
    
    add_executable(${TEST_NAME} ${TEST_NAME}.cpp)
    
    target_link_libraries(${TEST_NAME}
        PRIVATE
            ${MODULE_NAME}
            GTest::gtest
            GTest::gtest_main
            ${ARGN}
    )
    
    gtest_discover_tests(${TEST_NAME})
endfunction()
