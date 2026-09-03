#include "maiconv/core/ma2.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace maiconv;

TEST_CASE("ma2 parser basic tap") {
  const std::vector<std::string> lines = {
      "VERSION\t1.03.00", "BPM_DEF\t0\t0\t120",         "MET_DEF\t0\t0\t4\t4",
      "NMTAP\t0\t0\t0",   "NMSI_\t0\t96\t0\t96\t24\t1",
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
  REQUIRE(out.find("TAP") != std::string::npos);
}

TEST_CASE("ma2 parser bpm and meter definitions") {
  const std::vector<std::string> lines = {
      "BPM_DEF\t0\t0\t200", // bar/tick form clears and replaces default 120
      "BPM\t1\t0\t250",     // inline form appends a change
      "MET_DEF\t0\t0\t4\t4",
      "MET\t1\t0\t2\t4",
  };

  Ma2Parser parser;
  Chart chart = parser.parse(lines);

  REQUIRE(chart.bpm_changes().size() == 2);
  REQUIRE(chart.bpm_at_tick(0) == Catch::Approx(200.0));
  REQUIRE(chart.bpm_at_tick(383) == Catch::Approx(200.0));
  REQUIRE(chart.bpm_at_tick(384) == Catch::Approx(250.0));

  REQUIRE(chart.measure_changes().size() == 2);
  REQUIRE(chart.measure_changes()[0].bar == 0);
  REQUIRE(chart.measure_changes()[0].quaver == 4);
  REQUIRE(chart.measure_changes()[1].bar == 1);
  REQUIRE(chart.measure_changes()[1].tick == 0);
  REQUIRE(chart.measure_changes()[1].quaver == 2);
  REQUIRE(chart.measure_changes()[1].beats == 4);
}

TEST_CASE("ma2 parser slide fields") {
  const std::vector<std::string> lines = {
      "NMSI_\t0\t0\t1\t96\t24\t3",
      "CNSI_\t0\t192\t1\t96\t24\t4",
  };

  Ma2Parser parser;
  Chart chart = parser.parse(lines);

  REQUIRE(chart.notes().size() == 2);
  REQUIRE(chart.notes()[0].type == NoteType::SlideStraight);
  REQUIRE(chart.notes()[0].key == 1);
  REQUIRE(chart.notes()[0].wait_ticks == 96);
  REQUIRE(chart.notes()[0].last_ticks == 24);
  REQUIRE(chart.notes()[0].end_key == 3);
  REQUIRE(chart.notes()[0].state == SpecialState::Normal);

  REQUIRE(chart.notes()[1].type == NoteType::SlideStraight);
  REQUIRE(chart.notes()[1].end_key == 4);
  REQUIRE(chart.notes()[1].state == SpecialState::ConnectingSlide);
}

TEST_CASE("ma2 parser hold") {
  const std::vector<std::string> lines = {"NMHLD\t0\t0\t2\t192"};

  Ma2Parser parser;
  Chart chart = parser.parse(lines);

  REQUIRE(chart.notes().size() == 1);
  REQUIRE(chart.notes()[0].type == NoteType::Hold);
  REQUIRE(chart.notes()[0].bar == 0);
  REQUIRE(chart.notes()[0].tick == 0);
  REQUIRE(chart.notes()[0].key == 2);
  REQUIRE(chart.notes()[0].last_ticks == 192);
  REQUIRE(chart.notes()[0].state == SpecialState::Normal);
}

TEST_CASE("ma2 parser touch tap") {
  const std::vector<std::string> lines = {"NMTTP\t0\t0\t1\tC2\t1\tM2"};

  Ma2Parser parser;
  Chart chart = parser.parse(lines);

  REQUIRE(chart.notes().size() == 1);
  REQUIRE(chart.notes()[0].type == NoteType::TouchTap);
  REQUIRE(chart.notes()[0].key == 1);
  REQUIRE(chart.notes()[0].is_touch);
  REQUIRE(chart.notes()[0].touch_group == "C2");
  REQUIRE(chart.notes()[0].special_effect);
  REQUIRE(chart.notes()[0].touch_size == "M2");
}

TEST_CASE("ma2 composer emits header and version") {
  const std::vector<std::string> lines = {"NMTAP\t0\t0\t0"};

  Ma2Parser parser;
  Ma2Composer composer;
  Chart chart = parser.parse(lines);

  const std::string out_103 = composer.compose(chart, ChartFormat::Ma2_103);
  REQUIRE(out_103.find("BPM_DEF") != std::string::npos);
  REQUIRE(out_103.find("MET_DEF") != std::string::npos);
  REQUIRE(out_103.find("RESOLUTION") != std::string::npos);
  REQUIRE(out_103.find("TAP\t0\t0\t0") != std::string::npos);
  REQUIRE(out_103.find("1.03.00") != std::string::npos);

  const std::string out_104 = composer.compose(chart, ChartFormat::Ma2_104);
  REQUIRE(out_104.find("1.04.00") != std::string::npos);
  REQUIRE(out_104.find("1.03.00") == std::string::npos);
}

TEST_CASE("ma2 composer parse roundtrip preserves notes") {
  const std::vector<std::string> lines = {
      "NMTAP\t0\t0\t0",
      "NMSI_\t0\t96\t1\t96\t24\t2",
  };

  Ma2Parser parser;
  Ma2Composer composer;
  Ma2Tokenizer tokenizer;

  Chart chart = parser.parse(lines);
  const std::string out = composer.compose(chart, ChartFormat::Ma2_104);
  Chart reparsed = parser.parse(tokenizer.tokenize_text(out));

  REQUIRE(chart.notes().size() == 2);
  REQUIRE(reparsed.notes().size() == chart.notes().size());
  REQUIRE(reparsed.notes()[0].type == chart.notes()[0].type);
  REQUIRE(reparsed.notes()[0].type == NoteType::Tap);
  REQUIRE(reparsed.notes()[1].type == chart.notes()[1].type);
  REQUIRE(reparsed.notes()[1].type == NoteType::SlideStraight);
}
