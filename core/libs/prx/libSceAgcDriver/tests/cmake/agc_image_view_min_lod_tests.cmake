add_test_executable(agc_image_view_min_lod_tests EXCLUDE_FROM_ALL tests/ImageViewMinLod.cpp)
target_include_directories(agc_image_view_min_lod_tests PRIVATE ${LIBS_INCLUDE_DIR} ${ANYPS5_VULKAN_HEADERS_INCLUDE_DIR})
set_target_properties(agc_image_view_min_lod_tests PROPERTIES CXX_EXTENSIONS OFF)
configure_windows_unwind(agc_image_view_min_lod_tests)

if(BUILD_TESTING)
    set_target_properties(agc_image_view_min_lod_tests PROPERTIES EXCLUDE_FROM_ALL OFF)
    add_test(NAME agc_image_view_min_lod COMMAND agc_image_view_min_lod_tests)
endif()
