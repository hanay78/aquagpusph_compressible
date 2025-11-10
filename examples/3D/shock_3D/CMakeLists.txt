include(${AQUAGPUSPH_CMAKE_MODULE_PATH}/example.cmake)

add_library(r_rocks_sim SHARED chronosim.cpp)
target_include_directories(r_rocks_sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                                             ${CHRONO_INCLUDE_DIRS}
                                             ${CMAKE_BINARY_DIR}
                                             ${CMAKE_SOURCE_DIR}
                                             ${CMAKE_SOURCE_DIR}/aquagpusph/ext)
target_include_directories(r_rocks_sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CHRONO_INCLUDE_DIRS})
target_compile_options(r_rocks_sim PRIVATE ${CHRONO_CXX_FLAGS})
target_link_libraries(r_rocks_sim aquagpusphlib ${DEP_LIBS} ${CHRONO_LIBRARIES})
target_compile_definitions(r_rocks_sim PUBLIC AQUAgpusph_EXPORTS)
set_target_properties(r_rocks_sim PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
if(WIN32)
    set(EXTTOOL_LIB_NAME r_rocks_sim.dll)
else()
    set(EXTTOOL_LIB_NAME libr_rocks_sim.so)
endif()

if(WIN32)
    add_custom_target(copy_r_rocks_sim_dll_to_example ALL)
    add_custom_command(TARGET copy_r_rocks_sim_dll_to_example POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:rockr_sim> ${CMAKE_CURRENT_BINARY_DIR}/${EXTTOOL_LIB_NAME}
    )
    add_dependencies(copy_r_rocks_sim_dll_to_example rocks_sim)
endif()

install(TARGETS r_rocks_sim
  LIBRARY DESTINATION ${EXAMPLE_INSTALL_DIR}
)
