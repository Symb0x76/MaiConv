#include "maiconv/core/ma2.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace maiconv;

TEST_CASE("ma2 parser basic tap") {
  const std::vector<std::string> lines = {
      "VERSION\t1.03.00",
      "BPM_DEF\t0\t0\t120",
      "MET_DEF\t0\t0\t4\t4",
      "NMTAP\t0\t0\t0",
      "NMSI_\t0\t96\t0\t96\t24\t1",
  };

  Ma2Parser parser;
  Chart chart = parser.parse(lines);
  REQUIRE(chart.notes().size() == 2);
  REQUIRE(chart.notes()[0].type == NoteType::Tap);
  REQUIRE(chart.notes()[1].type == NoteType::SlideStraight);
  REQUIRE(chart.notes()[1].wait_ticks == 96);
  REQUIRE(chart.notes()[1].last_ticks == 24);
}

TEST_CASE("ma2 compose roundtrip") {
  const std::vector<std::string> lines = {
      "BPM_DEF\t0\t0\t120",
      "MET_DEF\t0\t0\t4\t4",
      "NMTAP\t0\t0\t0",
  };
  Ma2Parser parser;
  Ma2Composer composer;
  Chart chart = parser.parse(lines);
  const std::string out = composer.compose(chart, ChartFormat::Ma2_103);
  REQUIRE(out.find("NMTAP") != std::string::npos);
}