add_executable(frame-policy-test frame-policy-test.cpp)
add_test(NAME frame-policy COMMAND frame-policy-test)

if(WIN32 AND ENABLE_GPU_SMOKE_TEST)
  add_executable(obs-performance-smoke obs-performance-smoke.cpp)
  target_link_libraries(obs-performance-smoke PRIVATE OBS::libobs)
endif()
