#pragma once

#include "maiconv/core/format.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace maiconv {

struct BpmChange {
  int bar = 0;
  int tick = 0;
  double bpm = 120.0;

  [[nodiscard]] int tick_stamp(int definition = 384) const {
    return bar * definition + tick;
  }
};

struct MeasureChange {
  int bar = 0;
  int tick = 0;
  int quaver = 4;
  int beats = 4;

  [[nodiscard]] int tick_stamp(int definition = 384) const {
    return bar * definition + tick;
  }
};

struct Note {
  NoteType type = NoteType::Rest;
  SpecialState state = SpecialState::Normal;
  int bar = 0;
  int tick = 0;
  int key = -1;
  int end_key = -1;
  int wait_ticks = 0;
  int last_ticks = 0;
  double bpm = 120.0;
  bool special_effect = false;
  bool is_touch = false;
  std::string touch_group;
  std::string touch_size = "M1";
  std::string ma2_raw_type;

  [[nodiscard]] int tick_stamp(int definition = 384) const {
    return bar * definition + tick;
  }

  [[nodiscard]] bool is_note() const {
    return type != NoteType::Bpm && type != NoteType::Measure && type != NoteType::Rest;
  }
};

class Chart {
 public:
  explicit Chart(int definition = 384);

  int definition() const { return definition_; }

  std::vector<Note>& notes() { return notes_; }
  const std::vector<Note>& notes() const { return notes_; }

  std::vector<BpmChange>& bpm_changes() { return bpm_changes_; }
  const std::vector<BpmChange>& bpm_changes() const { return bpm_changes_; }

  std::vector<MeasureChange>& measure_changes() { return measure_changes_; }
  const std::vector<MeasureChange>& measure_changes() const { return measure_changes_; }

  int source_bar_count() const { return source_bar_count_; }
  void set_source_bar_count(int count) { source_bar_count_ = count < 0 ? 0 : count; }

  void normalize();

  [[nodiscard]] double bpm_at_tick(int tick_stamp) const;
  [[nodiscard]] double ticks_to_seconds(int tick_stamp) const;
  [[nodiscard]] int seconds_to_ticks(double seconds) const;
  [[nodiscard]] int seconds_to_ticks_at(double seconds, int start_tick) const;

  void shift_by_offset(int overall_ticks);
  void rotate(FlipMethod method);

 private:
  int definition_;
  std::vector<Note> notes_;
  std::vector<BpmChange> bpm_changes_;
  std::vector<MeasureChange> measure_changes_;
  int source_bar_count_ = 0;

  static int rotate_key(int key, FlipMethod method);
};


}  // namespace maiconv
