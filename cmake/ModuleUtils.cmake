function(add_module_library MODULE_NAME)
    add_library(${MODULE_NAME})
    target_sources(${MODULE_NAME} 
        PUBLIC FILE_SET CXX_MODULES FILES 
        modules/${MODULE_NAME}.ixx
    )
    target_compile_features(${MODULE_NAME} PUBLIC cxx_std_23)
    
    if(MSVC)
        target_link_libraries(${MODULE_NAME} PUBLIC std)
    endif()

    # Optional dependencies
    if(ARGN)
        target_link_libraries(${MODULE_NAME} PUBLIC ${ARGN})
    endif()
endfunction()