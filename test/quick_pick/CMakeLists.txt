add_executable(QuickPickTest
  QuickPickTest.cpp
  ${REPO_ROOT}/lib/QuickPick/QuickPick.cpp
)

target_include_directories(QuickPickTest PRIVATE
  ${REPO_ROOT}/lib/QuickPick
)

target_link_libraries(QuickPickTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(QuickPickTest)
