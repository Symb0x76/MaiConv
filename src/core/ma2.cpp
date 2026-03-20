#include "maiconv/core/ma2.hpp"

#include "maiconv/core/io.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace maiconv {
namespace {

int to_int(const std::string& s, int fallback = 0) {
  try {
    return std::stoi(s);
  } catch (...) {
    return fallback;
  }
}

double to_double(const std::string& s, double fallback = 0.0) {
  try {
    return std::stod(s);
  } catch (...) {
    return fallback;
  }
}

bool looks_int_token(const std::string& value) {
  if (value.empty()) {
    return false;
  }
  std::size_t i = 0;
  if (value[0] == '+' || value[0] == '-') {
    i = 1;
  }
  if (i >= value.size()) {
    return false;
  }
  for (; i < value.size(); ++i) {
    const char ch = value[i];
    if (ch < '0' || ch > '9') {
      return false;
    }
  }
  return true;
}

int round_to_even_int(double value) {
  if (!std::isfinite(value)) {
    return 0;
  }
  const double floor_value = std::floor(value);
  const double fraction = value - floor_value;
  const int floor_int = static_cast<int>(floor_value);
  constexpr double kMidpoint = 0.5;
  constexpr double kEpsilon = 1e-12;
  if (fraction < kMidpoint - kEpsilon) {
    return floor_int;
  }
  if (fraction > kMidpoint + kEpsilon) {
    return floor_int + 1;
  }
  return (floor_int % 2 == 0) ? floor_int : (floor_int + 1);
}

struct ParsedType {
  std::string base;
  SpecialState state = SpecialState::Normal;
};

ParsedType parse_type(std::string raw) {
  ParsedType out{raw, SpecialState::Normal};
  if (raw.size() == 5) {
    const std::string prefix = raw.substr(0, 2);
    const std::string core = raw.substr(2);
    if (prefix == "NM") {
      out.state = SpecialState::Normal;
      out.base = core;
    } else if (prefix == "BR") {
      out.state = SpecialState::Break;
      out.base = core;
    } else if (prefix == "EX") {
      out.state = SpecialState::Ex;
      out.base = core;
    } else if (prefix == "BX") {
      out.state = SpecialState::BreakEx;
      out.base = core;
    } else if (prefix == "CN") {
      out.state = SpecialState::ConnectingSlide;
      out.base = core;
    }
  }

  if (out.base == "XTP") {
    out.base = "TAP";
    if (out.state == SpecialState::Normal) {
      out.state = SpecialState::Ex;
    }
  } else if (out.base == "XST") {
    out.base = "STR";
    if (out.state == SpecialState::Normal) {
      out.state = SpecialState::Ex;
    }
  } else if (out.base == "BRK") {
    out.base = "TAP";
    if (out.state == SpecialState::Normal) {
      out.state = SpecialState::Break;
    }
  } else if (out.base == "BST") {
    out.base = "STR";
    if (out.state == SpecialState::Normal) {
      out.state = SpecialState::Break;
    }
  } else if (out.base == "XHO") {
    out.base = "HLD";
    if (out.state == SpecialState::Normal) {
      out.state = SpecialState::Ex;
    }
  }

  return out;
}

NoteType map_note_type(const std::string& base) {
  static const std::unordered_map<std::string, NoteType> kMap = {
      {"TAP", NoteType::Tap},       {"STR", NoteType::SlideStart},   {"NST", NoteType::SlideStart},
      {"NSS", NoteType::SlideStart}, {"TTP", NoteType::TouchTap},     {"HLD", NoteType::Hold},
      {"THO", NoteType::TouchHold}, {"SI_", NoteType::SlideStraight},
      {"SV_", NoteType::SlideV},    {"SF_", NoteType::SlideWifi},    {"SCL", NoteType::SlideCurveLeft},
      {"SCR", NoteType::SlideCurveRight},                            {"SUL", NoteType::SlideQ},
      {"SUR", NoteType::SlideP},    {"SLL", NoteType::SlideVTurnLeft}, {"SLR", NoteType::SlideVTurnRight},
      {"SXL", NoteType::SlideQQ},   {"SXR", NoteType::SlidePP},      {"SSL", NoteType::SlideS},
      {"SSR", NoteType::SlideZ},
  };
  const auto it = kMap.find(base);
  if (it == kMap.end()) {
    return NoteType::Rest;
  }
  return it->second;
}

std::string base_ma2_type(NoteType type) {
  switch (type) {
    case NoteType::Tap:
      return "TAP";
    case NoteType::SlideStart:
      return "STR";
    case NoteType::TouchTap:
      return "TTP";
    case NoteType::Hold:
      return "HLD";
    case NoteType::TouchHold:
      return "THO";
    case NoteType::SlideStraight:
      return "SI_";
    case NoteType::SlideV:
      return "SV_";
    case NoteType::SlideWifi:
      return "SF_";
    case NoteType::SlideCurveLeft:
      return "SCL";
    case NoteType::SlideCurveRight:
      return "SCR";
    case NoteType::SlideQ:
      return "SUL";
    case NoteType::SlideP:
      return "SUR";
    case NoteType::SlideVTurnLeft:
      return "SLL";
    case NoteType::SlideVTurnRight:
      return "SLR";
    case NoteType::SlideQQ:
      return "SXL";
    case NoteType::SlidePP:
      return "SXR";
    case NoteType::SlideS:
      return "SSL";
    case NoteType::SlideZ:
      return "SSR";
    default:
      return "RST";
  }
}

std::string prefixed_type(const Note& note) {
  std::string prefix = "NM";
  switch (note.state) {
    case SpecialState::Normal:
      prefix = "NM";
      break;
    case SpecialState::Break:
      prefix = "BR";
      break;
    case SpecialState::Ex:
      prefix = "EX";
      break;
    case SpecialState::BreakEx:
      prefix = "BX";
      break;
    case SpecialState::ConnectingSlide:
      prefix = "CN";
      break;
  }
  return prefix + base_ma2_type(note.type);
}

std::string ma2_type_for_format(const Note& note, ChartFormat format) {
  if (format == ChartFormat::Ma2_103) {
    const bool is_slide_start = note.type == NoteType::SlideStart;
    if (note.type == NoteType::Tap || note.type == NoteType::SlideStart) {
      if (note.state == SpecialState::Ex) {
        return is_slide_start ? "XST" : "XTP";
      }
      if (note.state == SpecialState::Break || note.state == SpecialState::BreakEx) {
        return is_slide_start ? "BST" : "BRK";
      }
      return is_slide_start ? "STR" : "TAP";
    }

    if (note.type == NoteType::Hold) {
      if (note.state == SpecialState::Ex || note.state == SpecialState::BreakEx) {
        return "XHO";
      }
      return "HLD";
    }

    if (note.type == NoteType::TouchTap) {
      return "TTP";
    }

    if (note.type == NoteType::TouchHold) {
      return "THO";
    }

    return base_ma2_type(note.type);
  }

  return prefixed_type(note);
}

struct Ma2Stats {
  int normal_tap = 0;
  int break_tap = 0;
  int ex_tap = 0;
  int break_ex_tap = 0;
  int normal_hold = 0;
  int ex_hold = 0;
  int break_hold = 0;
  int break_ex_hold = 0;
  int normal_slide_start = 0;
  int break_slide_start = 0;
  int ex_slide_start = 0;
  int break_ex_slide_start = 0;
  int touch_tap = 0;
  int touch_hold = 0;
  int normal_slide = 0;
  int break_slide = 0;
  int all_note_rec = 0;
  int each_pairs = 0;
};

bool is_tap_genre_type(NoteType type) {
  return type == NoteType::Tap || type == NoteType::SlideStart ||
         type == NoteType::TouchTap;
}

bool is_hold_genre_type(NoteType type) {
  return type == NoteType::Hold || type == NoteType::TouchHold;
}

Ma2Stats compute_ma2_stats(const Chart& chart) {
  Ma2Stats stats;
  std::map<int, int> each_pairs_by_stamp;

  for (const auto& note : chart.notes()) {
    if (note.state != SpecialState::ConnectingSlide) {
      ++stats.all_note_rec;
    }

    if (!note.is_note()) {
      continue;
    }

    if (is_tap_genre_type(note.type) || is_hold_genre_type(note.type)) {
      ++each_pairs_by_stamp[note.tick_stamp(chart.definition())];
    }

    if (note.type == NoteType::Tap) {
      if (note.state == SpecialState::Break) {
        ++stats.break_tap;
      } else if (note.state == SpecialState::Ex) {
        ++stats.ex_tap;
      } else if (note.state == SpecialState::BreakEx) {
        ++stats.break_ex_tap;
      } else {
        ++stats.normal_tap;
      }
      continue;
    }

    if (note.type == NoteType::SlideStart) {
      if (note.state == SpecialState::Break) {
        ++stats.break_slide_start;
      } else if (note.state == SpecialState::Ex) {
        ++stats.ex_slide_start;
      } else if (note.state == SpecialState::BreakEx) {
        ++stats.break_ex_slide_start;
      } else {
        ++stats.normal_slide_start;
      }
      continue;
    }

    if (note.type == NoteType::TouchTap) {
      ++stats.touch_tap;
      continue;
    }

    if (note.type == NoteType::Hold) {
      if (note.state == SpecialState::Break) {
        ++stats.break_hold;
      } else if (note.state == SpecialState::Ex) {
        ++stats.ex_hold;
      } else if (note.state == SpecialState::BreakEx) {
        ++stats.break_ex_hold;
      } else {
        ++stats.normal_hold;
      }
      continue;
    }

    if (note.type == NoteType::TouchHold) {
      ++stats.touch_hold;
      continue;
    }

    if (is_slide_type(note.type)) {
      if (note.state == SpecialState::Break) {
        ++stats.break_slide;
      } else if (note.state == SpecialState::Normal) {
        ++stats.normal_slide;
      }
    }
  }

  for (const auto& [stamp, count] : each_pairs_by_stamp) {
    (void)stamp;
    if (count > 1) {
      ++stats.each_pairs;
    }
  }
  return stats;
}

std::string compose_ma2_statistics(const Chart& chart, ChartFormat format) {
  const Ma2Stats stats = compute_ma2_stats(chart);
  const bool include_104 = (format == ChartFormat::Ma2_104);

  const int tap_num =
      stats.normal_tap + stats.ex_tap + stats.normal_slide_start +
      stats.ex_slide_start + stats.touch_tap;
  const int break_num =
      stats.break_tap + stats.break_ex_tap + stats.break_hold +
      stats.break_ex_hold + stats.break_slide_start +
      stats.break_ex_slide_start + stats.break_slide;
  const int hold_num = stats.normal_hold + stats.ex_hold + stats.touch_hold;
  const int slide_num = stats.normal_slide;
  const int all_note_num = tap_num + break_num + hold_num + slide_num;

  const int tap_judge_num =
      tap_num + stats.break_tap + stats.break_ex_tap +
      stats.break_slide_start + stats.break_ex_slide_start;
  const int hold_judge_num =
      (hold_num + stats.break_hold + stats.break_ex_hold) * 2;
  const int slide_judge_num = stats.normal_slide + stats.break_slide;
  const int all_judge_num = tap_judge_num + hold_judge_num + slide_judge_num;

  const int tap_score = tap_num * 500;
  const int break_score = break_num * 2600;
  const int hold_score = hold_num * 1000;
  const int slide_score = slide_num * 1500;
  const int all_score = tap_score + break_score + hold_score + slide_score;
  const int score_s = round_to_even_int(all_score * 0.97);
  const int score_ss = round_to_even_int(all_score * 0.99);
  const int rated_achievement = all_score == 0
                                    ? 0
                                    : round_to_even_int(
                                          (1.0 + (static_cast<double>(break_num) * 100.0) /
                                                     static_cast<double>(all_score)) *
                                          10000.0);

  std::ostringstream out;
  out << "T_REC_TAP\t" << stats.normal_tap << "\n";
  out << "T_REC_BRK\t" << stats.break_tap << "\n";
  out << "T_REC_XTP\t" << stats.ex_tap << "\n";
  if (include_104) {
    out << "T_REC_BXX\t" << stats.break_ex_tap << "\n";
  }
  out << "T_REC_HLD\t" << stats.normal_hold << "\n";
  out << "T_REC_XHO\t" << stats.ex_hold << "\n";
  if (include_104) {
    out << "T_REC_BHO\t" << stats.break_hold << "\n";
    out << "T_REC_BXH\t" << stats.break_ex_hold << "\n";
  }
  out << "T_REC_STR\t" << stats.normal_slide_start << "\n";
  out << "T_REC_BST\t" << stats.break_slide_start << "\n";
  out << "T_REC_XST\t" << stats.ex_slide_start << "\n";
  if (include_104) {
    out << "T_REC_XBS\t" << stats.break_ex_slide_start << "\n";
  }
  out << "T_REC_TTP\t" << stats.touch_tap << "\n";
  out << "T_REC_THO\t" << stats.touch_hold << "\n";
  out << "T_REC_SLD\t" << stats.normal_slide << "\n";
  if (include_104) {
    out << "T_REC_BSL\t" << stats.break_slide << "\n";
  }
  out << "T_REC_ALL\t" << stats.all_note_rec << "\n";

  out << "T_NUM_TAP\t" << tap_num << "\n";
  out << "T_NUM_BRK\t" << break_num << "\n";
  out << "T_NUM_HLD\t" << hold_num << "\n";
  out << "T_NUM_SLD\t" << slide_num << "\n";
  out << "T_NUM_ALL\t" << all_note_num << "\n";

  out << "T_JUDGE_TAP\t" << tap_judge_num << "\n";
  out << "T_JUDGE_HLD\t" << hold_judge_num << "\n";
  out << "T_JUDGE_SLD\t" << slide_judge_num << "\n";
  out << "T_JUDGE_ALL\t" << all_judge_num << "\n";

  out << "TTM_EACHPAIRS\t" << stats.each_pairs << "\n";
  out << "TTM_SCR_TAP\t" << tap_score << "\n";
  out << "TTM_SCR_BRK\t" << break_score << "\n";
  out << "TTM_SCR_HLD\t" << hold_score << "\n";
  out << "TTM_SCR_SLD\t" << slide_score << "\n";
  out << "TTM_SCR_ALL\t" << all_score << "\n";
  out << "TTM_SCR_S\t" << score_s << "\n";
  out << "TTM_SCR_SS\t" << score_ss << "\n";
  out << "TTM_RAT_ACV\t" << rated_achievement << "\n";
  return out.str();
}

}  // namespace

std::vector<std::string> Ma2Tokenizer::tokenize_file(const std::filesystem::path& path) const {
  return read_lines(path);
}

std::vector<std::string> Ma2Tokenizer::tokenize_text(const std::string& text) const {
  return split(text, '\n');
}

Chart Ma2Parser::parse(const std::vector<std::string>& lines) const {
  Chart chart;
  chart.bpm_changes().push_back(BpmChange{0, 0, 120.0});
  chart.measure_changes().push_back(MeasureChange{0, 0, 4, 4});

  for (const auto& raw_line : lines) {
    const std::string line = trim(raw_line);
    if (line.empty() || line[0] == '#') {
      continue;
    }

    const auto fields = split(line, '\t');
    if (fields.empty()) {
      continue;
    }

    const std::string type = fields[0];
    if (type == "BPM_DEF") {
      if (fields.size() >= 4 && looks_int_token(fields[1]) && looks_int_token(fields[2])) {
        chart.bpm_changes().clear();
        chart.bpm_changes().push_back(BpmChange{to_int(fields[1]), to_int(fields[2]), to_double(fields[3], 120.0)});
      } else if (fields.size() >= 2) {
        chart.bpm_changes().clear();
        chart.bpm_changes().push_back(BpmChange{0, 0, to_double(fields[1], 120.0)});
      }
      continue;
    }
    if (type == "MET_DEF") {
      if (fields.size() >= 5 && looks_int_token(fields[1]) && looks_int_token(fields[2])) {
        chart.measure_changes().clear();
        chart.measure_changes().push_back(
            MeasureChange{to_int(fields[1]), to_int(fields[2]), to_int(fields[3], 4), to_int(fields[4], 4)});
      } else if (fields.size() >= 3) {
        chart.measure_changes().clear();
        chart.measure_changes().push_back(MeasureChange{0, 0, to_int(fields[1], 4), to_int(fields[2], 4)});
      }
      continue;
    }
    if (type == "BPM") {
      if (fields.size() >= 4) {
        chart.bpm_changes().push_back(BpmChange{to_int(fields[1]), to_int(fields[2]), to_double(fields[3], 120.0)});
      }
      continue;
    }
    if (type == "MET") {
      if (fields.size() >= 5) {
        chart.measure_changes().push_back(
            MeasureChange{to_int(fields[1]), to_int(fields[2]), to_int(fields[3], 4), to_int(fields[4], 4)});
      }
      continue;
    }

    ParsedType parsed = parse_type(type);
    const NoteType note_type = map_note_type(parsed.base);
    if (note_type == NoteType::Rest) {
      continue;
    }

    Note note;
    note.type = note_type;
    note.state = parsed.state;
    if (fields.size() >= 3) {
      note.bar = to_int(fields[1]);
      note.tick = to_int(fields[2]);
    }
    if (fields.size() >= 4) {
      note.key = to_int(fields[3]);
    }
    note.ma2_raw_type = type;

    if (note.type == NoteType::TouchTap) {
      if (fields.size() >= 5) {
        note.touch_group = fields[4];
        note.is_touch = true;
      }
      if (fields.size() >= 6) {
        note.special_effect = to_int(fields[5]) == 1;
      }
      if (fields.size() >= 7) {
        note.touch_size = fields[6];
      }
    } else if (note.type == NoteType::Hold) {
      if (fields.size() >= 5) {
        note.last_ticks = to_int(fields[4]);
      }
    } else if (note.type == NoteType::TouchHold) {
      if (fields.size() >= 5) {
        note.last_ticks = to_int(fields[4]);
      }
      if (fields.size() >= 6) {
        note.touch_group = fields[5];
        note.is_touch = true;
      }
      if (fields.size() >= 7) {
        note.special_effect = to_int(fields[6]) == 1;
      }
      if (fields.size() >= 8) {
        note.touch_size = fields[7];
      }
    } else if (is_slide_type(note.type)) {
      if (fields.size() >= 5) {
        note.wait_ticks = to_int(fields[4]);
      }
      if (fields.size() >= 6) {
        note.last_ticks = to_int(fields[5]);
      }
      if (fields.size() >= 7) {
        note.end_key = to_int(fields[6]);
      }
    }

    chart.notes().push_back(note);
  }

  chart.normalize();
  return chart;
}

std::string Ma2Composer::compose(const Chart& source, ChartFormat format) const {
  Chart chart = source;
  chart.normalize();

  std::vector<BpmChange> deduped_bpms;
  deduped_bpms.reserve(chart.bpm_changes().size());
  for (const auto& bpm : chart.bpm_changes()) {
    if (!deduped_bpms.empty()) {
      const auto& prev = deduped_bpms.back();
      if (prev.bar == bpm.bar && prev.tick == bpm.tick && prev.bpm == bpm.bpm) {
        continue;
      }
    }
    deduped_bpms.push_back(bpm);
  }
  if (deduped_bpms.empty()) {
    deduped_bpms.push_back(BpmChange{0, 0, 120.0});
  }

  std::ostringstream out;
  const std::string version = (format == ChartFormat::Ma2_104) ? "1.04.00" : "1.03.00";
  out << "VERSION\t0.00.00\t" << version << "\n";
  out << "FES_MODE\t0\n";

  const auto first_bpm = deduped_bpms.front();
  double bpm_def_values[4] = {
      first_bpm.bpm,
      first_bpm.bpm,
      first_bpm.bpm,
      first_bpm.bpm,
  };
  for (std::size_t i = 0; i < deduped_bpms.size() && i < 4; ++i) {
    bpm_def_values[i] = deduped_bpms[i].bpm;
  }
  out << "BPM_DEF\t" << std::fixed << std::setprecision(3) << bpm_def_values[0] << "\t"
      << bpm_def_values[1] << "\t" << bpm_def_values[2] << "\t" << bpm_def_values[3] << "\t\n";
  out.unsetf(std::ios::floatfield);
  out << std::setprecision(6);

  const auto first_measure = chart.measure_changes().empty() ? MeasureChange{0, 0, 4, 4} : chart.measure_changes().front();
  out << "MET_DEF\t" << first_measure.quaver << "\t" << first_measure.beats << "\n";
  out << "RESOLUTION\t" << chart.definition() << "\n";
  out << "CLK_DEF\t" << chart.definition() << "\n";
  out << "COMPATIBLE_CODE\tMA2\n\n";

  for (std::size_t i = 0; i < deduped_bpms.size(); ++i) {
    const auto& bpm = deduped_bpms[i];
    out << "BPM\t" << bpm.bar << "\t" << bpm.tick << "\t" << bpm.bpm << "\n";
  }

  const auto& measures = chart.measure_changes();
  if (measures.size() <= 1) {
    out << "MET\t0\t0\t" << first_measure.quaver << "\t" << first_measure.beats << "\n";
  } else {
    for (std::size_t i = 1; i < measures.size(); ++i) {
      const auto& target = measures[i];
      const auto& source = (i == 1) ? first_measure : measures[i - 1];
      out << "MET\t" << target.bar << "\t" << target.tick << "\t" << source.quaver << "\t" << source.beats << "\n";
    }
  }

  out << "\n";

  for (const auto& note : chart.notes()) {
    if (!note.is_note()) {
      continue;
    }
    const std::string type = ma2_type_for_format(note, format);
    out << type << "\t" << note.bar << "\t" << note.tick << "\t" << note.key;

    if (note.type == NoteType::TouchTap) {
      out << "\t" << (note.touch_group.empty() ? "C" : note.touch_group) << "\t" << (note.special_effect ? 1 : 0)
          << "\tM1";
    } else if (note.type == NoteType::Hold) {
      out << "\t" << note.last_ticks;
    } else if (note.type == NoteType::TouchHold) {
      out << "\t" << note.last_ticks << "\t" << (note.touch_group.empty() ? "C" : note.touch_group) << "\t"
          << (note.special_effect ? 1 : 0) << "\t" << (note.touch_size.empty() ? "M1" : note.touch_size);
    } else if (is_slide_type(note.type)) {
      out << "\t" << note.wait_ticks << "\t" << note.last_ticks << "\t" << note.end_key;
    }
    out << "\n";
  }

  out << "\n";
  out << compose_ma2_statistics(chart, format);

  return out.str();
}

}  // namespace maiconv
