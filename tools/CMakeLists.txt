# 검증 하네스. docs/DESIGN.md 9.3절.
# 프로덕션 코어를 그대로 호출한다. 별도 구현이 아니므로 검증한 것과 배포하는
# 것이 같은 코드다.

add_library(nota_tools_io STATIC image_io.cpp)
target_include_directories(nota_tools_io PUBLIC .)
target_link_libraries(nota_tools_io PUBLIC nota_core PRIVATE nota_flags)

add_executable(nota-analyze nota_analyze.cpp)
target_link_libraries(nota-analyze PRIVATE nota_core nota_tools_io nota_flags)

add_executable(nota-geometry nota_geometry.cpp)
target_link_libraries(nota-geometry PRIVATE nota_core nota_tools_io nota_flags)

add_executable(nota-scroll nota_scroll.cpp)
target_link_libraries(nota-scroll PRIVATE nota_core nota_tools_io nota_flags)
