add_executable(HabitifyJournalParserTest
  HabitifyJournalParserTest.cpp
  ${REPO_ROOT}/lib/Habitify/HabitifyJournalParser.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(HabitifyJournalParserTest PRIVATE
  ${REPO_ROOT}/lib/Habitify
  ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(HabitifyJournalParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HabitifyJournalParserTest)
