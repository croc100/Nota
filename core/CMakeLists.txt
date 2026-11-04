# NotaCore — 플랫폼 중립 공유 코어.
#
# 규칙: 이 타깃에는 플랫폼 API가 들어가지 않는다. Apple/Android/OS 헤더 금지.
# 입력은 PCM 버퍼와 비트맵, 출력은 숫자다. 파일 I/O는 호출자가 담당한다.
# 이 규칙은 Linux 빌드가 통과하는 것으로 강제된다 (docs/DESIGN.md 9.3).

add_library(nota_core
  src/ring_buffer.cpp
  src/binarize.cpp
  src/staff_detect.cpp
  src/page_analysis.cpp
  src/skew.cpp
)

target_include_directories(nota_core PUBLIC include)
target_link_libraries(nota_core PRIVATE nota_flags)

set_target_properties(nota_core PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
)
