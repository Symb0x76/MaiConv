#include "maiconv/core/chart.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace maiconv;

TEST_CASE("chart tick-second conversion") {
  Chart chart;
  chart.bpm_changes().push_back(BpmChange{0, 0, 120.0});
  chart.bpm_changes().push_back(BpmChange{1, 0, 240.0});
  chart.normalize();

  REQUIRE(chart.ticks_to_seconds(384) == Catch::Approx(2.0));
  REQUIRE(chart.bpm_at_tick(383) == Catch::Approx(120.0));
  REQUIRE(chart.bpm_at_tick(384) == Catch::Approx(240.0));
}

TEST_CASE("chart rotate and shift") {
  Chart chart;
  Note note;
  note.type = NoteType::Tap;
  note.bar = 0;
  note.tick = 0;
  note.key = 0;
  chart.notes().push_back(note);

  chart.rotate(FlipMethod::Clockwise90);
  REQUIRE(chart.notes()[0].key == 2);

  chart.shift_by_offset(96);
  REQUIRE(chart.notes()[0].bar == 0);
  REQUIRE(chart.notes()[0].tick == 96);
}
