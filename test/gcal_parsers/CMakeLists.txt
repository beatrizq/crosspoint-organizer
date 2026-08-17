add_executable(GCalParsersTest
  GCalParsersTest.cpp
  ${REPO_ROOT}/lib/GCal/GCalEventsParser.cpp
  ${REPO_ROOT}/lib/GCal/GCalCalendarsParser.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(GCalParsersTest PRIVATE
  ${REPO_ROOT}/lib/GCal
  ${REPO_ROOT}/lib/CivilTime
  ${REPO_ROOT}/lib/JsonParser
)

target_link_libraries(GCalParsersTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GCalParsersTest)
