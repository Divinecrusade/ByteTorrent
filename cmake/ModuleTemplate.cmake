# Common function to create a module library
function(add_module MODULE_NAME)
    cmake_parse_arguments(
        MODULE
        ""                          # Options
        ""                          # Single value args
        "SOURCES;DEPENDENCIES"      # Multi-value args
        ${ARGN}
    )
    
    add_library(${MODULE_NAME} ${MODULE_SOURCES})
    
    target_include_directories(${MODULE_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
    )
    
    target_compile_features(${MODULE_NAME} PUBLIC cxx_std_23)
    
    # Common compile options
    if(MSVC)
        target_compile_options(${MODULE_NAME} PRIVATE
            /W4 /WX  # Warnings
            $<$<CONFIG:Release>:/O2>
            $<$<CONFIG:Debug>:/Od /Zi>
        )
    else()
        target_compile_options(${MODULE_NAME} PRIVATE
            -Wall -Wextra -Werror
            $<$<CONFIG:Release>:-O3>
            $<$<CONFIG:Debug>:-O0 -g>
        )
    endif()
    
    # Link dependencies if provided
    if(MODULE_DEPENDENCIES)
        target_link_libraries(${MODULE_NAME} PUBLIC ${MODULE_DEPENDENCIES})
    endif()
endfunction()

# Common function to create an application
function(add_app APP_NAME)
    cmake_parse_arguments(
        APP
        ""
        ""
        "SOURCES;DEPENDENCIES"
        ${ARGN}
    )
    
    add_executable(${APP_NAME} ${APP_SOURCES})
    
    target_include_directories(${APP_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
    )
    
    target_compile_features(${APP_NAME} PRIVATE cxx_std_23)
    
    # Same compile options as modules
    if(MSVC)
        target_compile_options(${APP_NAME} PRIVATE
            /W4 /WX
            $<$<CONFIG:Release>:/O2>
            $<$<CONFIG:Debug>:/Od /Zi>
        )
    else()
        target_compile_options(${APP_NAME} PRIVATE
            -Wall -Wextra -Werror
            $<$<CONFIG:Release>:-O3>
            $<$<CONFIG:Debug>:-O0 -g>
        )
    endif()
    
    if(APP_DEPENDENCIES)
        target_link_libraries(${APP_NAME} PRIVATE ${APP_DEPENDENCIES})
    endif()
endfunction()