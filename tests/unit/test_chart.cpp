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

TEST_CASE("chart normalize fills defaults and sorts changes") {
  Chart empty;
  empty.normalize();
  REQUIRE(empty.bpm_changes().size() == 1);
  REQUIRE(empty.bpm_changes()[0].bar == 0);
  REQUIRE(empty.bpm_changes()[0].tick == 0);
  REQUIRE(empty.bpm_changes()[0].bpm == Catch::Approx(120.0));
  REQUIRE(empty.measure_changes().size() == 1);
  REQUIRE(empty.measure_changes()[0].bar == 0);
  REQUIRE(empty.measure_changes()[0].tick == 0);
  REQUIRE(empty.measure_changes()[0].quaver == 4);
  REQUIRE(empty.measure_changes()[0].beats == 4);

  Chart chart;
  chart.bpm_changes().push_back(BpmChange{2, 0, 240.0});
  chart.bpm_changes().push_back(BpmChange{0, 0, 120.0});
  chart.bpm_changes().push_back(BpmChange{1, 0, 180.0});
  chart.normalize();

  REQUIRE(chart.bpm_changes().size() == 3);
  REQUIRE(chart.bpm_changes()[0].bar == 0);
  REQUIRE(chart.bpm_changes()[0].bpm == Catch::Approx(120.0));
  REQUIRE(chart.bpm_changes()[1].bar == 1);
  REQUIRE(chart.bpm_changes()[1].bpm == Catch::Approx(180.0));
  REQUIRE(chart.bpm_changes()[2].bar == 2);
  REQUIRE(chart.bpm_changes()[2].bpm == Catch::Approx(240.0));
}

TEST_CASE("chart bpm_at_tick selects active change") {
  Chart empty;
  REQUIRE(empty.bpm_at_tick(0) == Catch::Approx(120.0));
  REQUIRE(empty.bpm_at_tick(12345) == Catch::Approx(120.0));

  Chart chart;
  chart.bpm_changes().push_back(BpmChange{0, 0, 120.0});
  chart.bpm_changes().push_back(BpmChange{3, 0, 240.0});
  chart.normalize();

  REQUIRE(chart.bpm_at_tick(0) == Catch::Approx(120.0));
  REQUIRE(chart.bpm_at_tick(1151) == Catch::Approx(120.0));
  REQUIRE(chart.bpm_at_tick(1152) == Catch::Approx(240.0));
  REQUIRE(chart.bpm_at_tick(5000) == Catch::Approx(240.0));
}

TEST_CASE("chart ticks to seconds across a bpm change") {
  Chart chart;
  chart.bpm_changes().push_back(BpmChange{0, 0, 120.0});
  chart.bpm_changes().push_back(BpmChange{1, 0, 240.0});
  chart.normalize();

  // 2 seconds per bar at 120 BPM, 1 second per bar at 240 BPM.
  REQUIRE(chart.ticks_to_seconds(384) == Catch::Approx(2.0));
  REQUIRE(chart.ticks_to_seconds(768) == Catch::Approx(3.0));
}

TEST_CASE("chart seconds to ticks inverse and from offset") {
  Chart chart;
  chart.bpm_changes().push_back(BpmChange{0, 0, 120.0});
  chart.bpm_changes().push_back(BpmChange{1, 0, 240.0});
  chart.normalize();

  REQUIRE(chart.seconds_to_ticks(2.0) == 384);
  REQUIRE(chart.seconds_to_ticks(3.0) == 768);

  // Start inside the 240 BPM segment that begins at tick 384.
  REQUIRE(chart.seconds_to_ticks_at(1.0, 384) == 384);
  REQUIRE(chart.seconds_to_ticks_at(2.0, 384) == 768);
  // Start in the 120 BPM segment at bar 0.
  REQUIRE(chart.seconds_to_ticks_at(1.0, 0) == 192);
}

TEST_CASE("chart shift_by_offset moves changes, clamps and trims") {
  {
    Chart chart;
    Note note;
    note.type = NoteType::Tap;
    chart.notes().push_back(note);
    chart.bpm_changes().push_back(BpmChange{1, 0, 240.0});
    chart.measure_changes().push_back(MeasureChange{1, 0, 3, 4});

    chart.shift_by_offset(96);

    REQUIRE(chart.notes().size() == 1);
    REQUIRE(chart.notes()[0].bar == 0);
    REQUIRE(chart.notes()[0].tick == 96);
    REQUIRE(chart.bpm_changes().size() == 2);
    REQUIRE(chart.bpm_changes()[1].bar == 1);
    REQUIRE(chart.bpm_changes()[1].tick == 96);
    REQUIRE(chart.bpm_changes()[1].bpm == Catch::Approx(240.0));
    REQUIRE(chart.measure_changes().size() == 2);
    REQUIRE(chart.measure_changes()[1].bar == 1);
    REQUIRE(chart.measure_changes()[1].tick == 96);
    REQUIRE(chart.measure_changes()[1].quaver == 3);
  }

  {
    Chart chart;
    Note dropped_note; // At bar 0 tick 0; shifting left pushes it below 0.
    dropped_note.type = NoteType::Tap;
    chart.notes().push_back(dropped_note);
    Note kept_note;
    kept_note.type = NoteType::Tap;
    kept_note.bar = 2;
    chart.notes().push_back(kept_note);
    chart.bpm_changes().push_back(BpmChange{0, 0, 120.0});
    chart.measure_changes().push_back(MeasureChange{0, 0, 4, 4});

    chart.shift_by_offset(-96);

    REQUIRE(chart.notes().size() == 1);
    REQUIRE(chart.notes()[0].bar == 1);
    REQUIRE(chart.notes()[0].tick == 288);
    REQUIRE(chart.bpm_changes()[0].bar == 0);
    REQUIRE(chart.bpm_changes()[0].tick == 0);
    REQUIRE(chart.measure_changes()[0].bar == 0);
    REQUIRE(chart.measure_changes()[0].tick == 0);
  }
}

TEST_CASE("chart rotate flip methods") {
  const auto expected_key = [](int key, FlipMethod method) {
    switch (method) {
    case FlipMethod::Clockwise90:
      return (key + 2) % 8;
    case FlipMethod::Counterclockwise90:
      return (key + 6) % 8;
    case FlipMethod::UpSideDown:
    case FlipMethod::Clockwise180:
    case FlipMethod::Counterclockwise180:
      return (key + 4) % 8;
    case FlipMethod::LeftToRight:
      return (8 - key) % 8;
    }
    return key;
  };

  const std::vector<FlipMethod> methods = {
      FlipMethod::UpSideDown,          FlipMethod::Clockwise90,
      FlipMethod::Clockwise180,        FlipMethod::Counterclockwise90,
      FlipMethod::Counterclockwise180, FlipMethod::LeftToRight,
  };

  for (const FlipMethod method : methods) {
    Chart chart;
    for (int key = 0; key < 8; ++key) {
      Note note;
      note.type = NoteType::Tap;
      note.key = key;
      chart.notes().push_back(note);
    }
    chart.rotate(method);
    for (int key = 0; key < 8; ++key) {
      REQUIRE(chart.notes()[key].key == expected_key(key, method));
    }
  }

  // Slide end keys rotate too, while touch notes stay untouched.
  Chart chart;
  {
    Note slide;
    slide.type = NoteType::SlideStraight;
    slide.key = 1;
    slide.end_key = 2;
    chart.notes().push_back(slide);
  }
  {
    Note touch;
    touch.type = NoteType::TouchTap;
    touch.key = 5;
    touch.is_touch = true;
    chart.notes().push_back(touch);
  }
  {
    Note tap;
    tap.type = NoteType::Tap;
    tap.key = 3;
    chart.notes().push_back(tap);
  }
  chart.rotate(FlipMethod::LeftToRight);
  REQUIRE(chart.notes()[0].key == 7);
  REQUIRE(chart.notes()[0].end_key == 6);
  REQUIRE(chart.notes()[1].key == 5);
  REQUIRE(chart.notes()[2].key == 5);
}

TEST_CASE("chart non-standard definition 768") {
  Chart chart(768);
  chart.bpm_changes().push_back(BpmChange{0, 0, 120.0});
  chart.bpm_changes().push_back(BpmChange{1, 0, 240.0});
  chart.normalize();

  REQUIRE(chart.definition() == 768);
  REQUIRE(chart.bpm_at_tick(767) == Catch::Approx(120.0));
  REQUIRE(chart.bpm_at_tick(768) == Catch::Approx(240.0));

  // 2 seconds per 768-tick bar at 120 BPM, 1 second at 240 BPM.
  REQUIRE(chart.ticks_to_seconds(768) == Catch::Approx(2.0));
  REQUIRE(chart.ticks_to_seconds(1536) == Catch::Approx(3.0));
  REQUIRE(chart.seconds_to_ticks(2.0) == 768);
  REQUIRE(chart.seconds_to_ticks(3.0) == 1536);
}
