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
    
    target_compile_features(${MODULE_NAME} PUBLIC cxx_std_23)
    
    if(MSVC)
        target_link_libraries(${MODULE_NAME} PUBLIC std)
    endif()

    if(ARGN)
        target_link_libraries(${MODULE_NAME} PUBLIC ${ARGN})
    endif()
endfunction()