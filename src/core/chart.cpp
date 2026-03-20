#include "maiconv/core/chart.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace maiconv {
  namespace {

    constexpr double kEpsilon = 1e-6;

    double bpm_time_unit(double bpm, int definition) {
      if (bpm <= 0.0) {
        bpm = 120.0;
      }
      return 60.0 / bpm * 4.0 / static_cast<double>(definition);
    }

  }  // namespace

  Chart::Chart(int definition) : definition_(definition) {}

  void Chart::normalize() {
    if (bpm_changes_.empty()) {
      bpm_changes_.push_back(BpmChange{ 0, 0, 120.0 });
    }
    if (measure_changes_.empty()) {
      measure_changes_.push_back(MeasureChange{ 0, 0, 4, 4 });
    }

    std::sort(bpm_changes_.begin(), bpm_changes_.end(),
      [this](const BpmChange& a, const BpmChange& b) {
        return a.tick_stamp(definition_) < b.tick_stamp(definition_);
      });
    std::sort(measure_changes_.begin(), measure_changes_.end(),
      [this](const MeasureChange& a, const MeasureChange& b) {
        return a.tick_stamp(definition_) < b.tick_stamp(definition_);
      });
    const bool from_simai_source = source_bar_count_ > 0;
    std::stable_sort(notes_.begin(), notes_.end(), [this, from_simai_source](const Note& a, const Note& b) {
      const int ta = a.tick_stamp(definition_);
      const int tb = b.tick_stamp(definition_);
      if (ta != tb) {
        return ta < tb;
      }

      const bool a_slide_start = a.type == NoteType::SlideStart;
      const bool b_slide_start = b.type == NoteType::SlideStart;
      const bool a_slide = is_slide_type(a.type);
      const bool b_slide = is_slide_type(b.type);

      // Simai parsing path in MaichartConverter puts start anchors before slide
      // bodies on identical stamps.
      if (from_simai_source && a_slide_start && b_slide && !b_slide_start) {
        return true;
      }
      if (from_simai_source && b_slide_start && a_slide && !a_slide_start) {
        return false;
      }

      // MA2 parsing path only requires same-key start/body pairing.
      if (a_slide_start && b_slide && !b_slide_start && a.key == b.key) {
        return true;
      }
      if (b_slide_start && a_slide && !a_slide_start && a.key == b.key) {
        return false;
      }

      return false;
      });

    if (bpm_changes_.front().tick_stamp(definition_) != 0) {
      bpm_changes_.insert(bpm_changes_.begin(), BpmChange{ 0, 0, bpm_changes_.front().bpm });
    }
    if (measure_changes_.front().tick_stamp(definition_) != 0) {
      measure_changes_.insert(measure_changes_.begin(), MeasureChange{ 0, 0, 4, 4 });
    }
  }

  double Chart::bpm_at_tick(int tick_stamp) const {
    if (bpm_changes_.empty()) {
      return 120.0;
    }
    double current = bpm_changes_.front().bpm;
    for (const auto& change : bpm_changes_) {
      if (change.tick_stamp(definition_) > tick_stamp) {
        break;
      }
      current = change.bpm;
    }
    return current;
  }

  double Chart::ticks_to_seconds(int tick_stamp) const {
    if (tick_stamp <= 0) {
      return 0.0;
    }
    if (bpm_changes_.empty()) {
      return static_cast<double>(tick_stamp) * bpm_time_unit(120.0, definition_);
    }

    double total = 0.0;
    int consumed_tick = 0;
    double current_bpm = bpm_changes_.front().bpm;

    for (std::size_t i = 1; i < bpm_changes_.size(); ++i) {
      const int segment_end = bpm_changes_[i].tick_stamp(definition_);
      if (tick_stamp <= segment_end) {
        total += static_cast<double>(tick_stamp - consumed_tick) * bpm_time_unit(current_bpm, definition_);
        return total;
      }
      total += static_cast<double>(segment_end - consumed_tick) * bpm_time_unit(current_bpm, definition_);
      consumed_tick = segment_end;
      current_bpm = bpm_changes_[i].bpm;
    }

    total += static_cast<double>(tick_stamp - consumed_tick) * bpm_time_unit(current_bpm, definition_);
    return total;
  }

  int Chart::seconds_to_ticks(double seconds) const {
    if (seconds <= 0.0) {
      return 0;
    }
    if (bpm_changes_.empty()) {
      return static_cast<int>(std::llround(seconds / bpm_time_unit(120.0, definition_)));
    }

    int accumulated_tick = 0;
    double remaining = seconds;
    double current_bpm = bpm_changes_.front().bpm;

    for (std::size_t i = 1; i < bpm_changes_.size(); ++i) {
      const int segment_end = bpm_changes_[i].tick_stamp(definition_);
      const int segment_ticks = segment_end - accumulated_tick;
      const double segment_seconds = static_cast<double>(segment_ticks) * bpm_time_unit(current_bpm, definition_);
      if (remaining <= segment_seconds + kEpsilon) {
        return accumulated_tick + static_cast<int>(std::llround(remaining / bpm_time_unit(current_bpm, definition_)));
      }
      remaining -= segment_seconds;
      accumulated_tick = segment_end;
      current_bpm = bpm_changes_[i].bpm;
    }

    return accumulated_tick + static_cast<int>(std::llround(remaining / bpm_time_unit(current_bpm, definition_)));
  }

  int Chart::seconds_to_ticks_at(double seconds, int start_tick) const {
    if (seconds <= 0.0) {
      return 0;
    }

    int consumed = start_tick;
    double remaining = seconds;

    while (remaining > kEpsilon) {
      const double bpm = bpm_at_tick(consumed);
      int next_boundary = std::numeric_limits<int>::max();
      for (const auto& change : bpm_changes_) {
        const int stamp = change.tick_stamp(definition_);
        if (stamp > consumed) {
          next_boundary = std::min(next_boundary, stamp);
          break;
        }
      }

      const double unit = bpm_time_unit(bpm, definition_);
      if (next_boundary == std::numeric_limits<int>::max()) {
        return static_cast<int>(std::llround(remaining / unit));
      }

      const int segment_ticks = next_boundary - consumed;
      const double segment_seconds = static_cast<double>(segment_ticks) * unit;
      if (remaining <= segment_seconds + kEpsilon) {
        return static_cast<int>(std::llround(remaining / unit));
      }

      remaining -= segment_seconds;
      consumed = next_boundary;
    }

    return 0;
  }

  void Chart::shift_by_offset(int overall_ticks) {
    if (overall_ticks == 0) {
      return;
    }

    const int new_zero = std::max(0, overall_ticks * -1);
    for (auto& note : notes_) {
      const int shifted = note.tick_stamp(definition_) + overall_ticks;
      if (shifted < new_zero) {
        note.bar = 0;
        note.tick = 0;
        note.wait_ticks = 0;
        note.last_ticks = 0;
        note.type = NoteType::Rest;
        continue;
      }
      note.bar = shifted / definition_;
      note.tick = shifted % definition_;
    }
    notes_.erase(std::remove_if(notes_.begin(), notes_.end(), [](const Note& n) { return n.type == NoteType::Rest; }),
      notes_.end());

    for (auto& bpm : bpm_changes_) {
      const int shifted = bpm.tick_stamp(definition_) + overall_ticks;
      bpm.bar = std::max(0, shifted / definition_);
      bpm.tick = std::max(0, shifted % definition_);
    }
    for (auto& measure : measure_changes_) {
      const int shifted = measure.tick_stamp(definition_) + overall_ticks;
      measure.bar = std::max(0, shifted / definition_);
      measure.tick = std::max(0, shifted % definition_);
    }

    normalize();
  }

  int Chart::rotate_key(int key, FlipMethod method) {
    if (key < 0) {
      return key;
    }
    key %= 8;
    switch (method) {
    case FlipMethod::UpSideDown:
    case FlipMethod::Clockwise180:
    case FlipMethod::Counterclockwise180:
      return (key + 4) % 8;
    case FlipMethod::Clockwise90:
      return (key + 2) % 8;
    case FlipMethod::Counterclockwise90:
      return (key + 6) % 8;
    case FlipMethod::LeftToRight:
      return (8 - key) % 8;
    }
    return key;
  }

  void Chart::rotate(FlipMethod method) {
    for (auto& note : notes_) {
      if (note.is_touch || note.key < 0) {
        continue;
      }
      note.key = rotate_key(note.key, method);
      if (is_slide_type(note.type) && note.end_key >= 0) {
        note.end_key = rotate_key(note.end_key, method);
      }
    }
  }

}  // namespace maiconv
