add_executable(nota_core_tests
  test_framework.cpp
  test_ring_buffer.cpp
  test_staff_detect.cpp
  test_page_analysis.cpp
  test_skew.cpp
  test_score_geometry.cpp
  test_scroll_controller.cpp
)

target_link_libraries(nota_core_tests PRIVATE nota_core nota_flags)
target_include_directories(nota_core_tests PRIVATE .)

add_test(NAME nota_core_tests COMMAND nota_core_tests)
