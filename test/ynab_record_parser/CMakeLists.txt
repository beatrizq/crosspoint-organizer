add_executable(YnabRecordParserTest
  YnabRecordParserTest.cpp
  ${REPO_ROOT}/lib/Ynab/YnabRecordParser.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(YnabRecordParserTest PRIVATE
  ${REPO_ROOT}/lib/Ynab
  ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(YnabRecordParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(YnabRecordParserTest)
