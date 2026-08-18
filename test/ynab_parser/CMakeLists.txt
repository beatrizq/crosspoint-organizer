add_executable(YnabMonthParserTest
  YnabMonthParserTest.cpp
  ${REPO_ROOT}/lib/Ynab/YnabMonthParser.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(YnabMonthParserTest PRIVATE
  ${REPO_ROOT}/lib/Ynab
  ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(YnabMonthParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(YnabMonthParserTest)
