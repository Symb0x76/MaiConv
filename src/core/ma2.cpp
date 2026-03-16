#include "maiconv/core/ma2.hpp"

#include "maiconv/core/io.hpp"

#include <algorithm>
#include <cmath>
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

  std::ostringstream out;
  const std::string version = (format == ChartFormat::Ma2_104) ? "1.04.00" : "1.03.00";
  out << "VERSION\t" << version << "\n";
  out << "FES_MODE\t" << ((format == ChartFormat::Ma2_104) ? "1" : "0") << "\n";

  const auto first_bpm = chart.bpm_changes().empty() ? BpmChange{0, 0, 120.0} : chart.bpm_changes().front();
  out << "BPM_DEF\t" << first_bpm.bar << "\t" << first_bpm.tick << "\t" << first_bpm.bpm << "\n";

  const auto first_measure = chart.measure_changes().empty() ? MeasureChange{0, 0, 4, 4} : chart.measure_changes().front();
  out << "MET_DEF\t" << first_measure.bar << "\t" << first_measure.tick << "\t" << first_measure.quaver << "\t"
      << first_measure.beats << "\n";

  for (std::size_t i = 1; i < chart.bpm_changes().size(); ++i) {
    const auto& bpm = chart.bpm_changes()[i];
    out << "BPM\t" << bpm.bar << "\t" << bpm.tick << "\t" << bpm.bpm << "\n";
  }

  for (std::size_t i = 1; i < chart.measure_changes().size(); ++i) {
    const auto& measure = chart.measure_changes()[i];
    out << "MET\t" << measure.bar << "\t" << measure.tick << "\t" << measure.quaver << "\t" << measure.beats << "\n";
  }

  for (const auto& note : chart.notes()) {
    const std::string type = prefixed_type(note);
    out << type << "\t" << note.bar << "\t" << note.tick << "\t" << note.key;

    if (note.type == NoteType::TouchTap) {
      out << "\t" << (note.touch_group.empty() ? "C" : note.touch_group) << "\t" << (note.special_effect ? 1 : 0)
          << "\tM1";
    } else if (note.type == NoteType::Hold) {
      out << "\t" << note.last_ticks;
    } else if (note.type == NoteType::TouchHold) {
      out << "\t" << note.last_ticks << "\t" << (note.touch_group.empty() ? "C" : note.touch_group) << "\t"
          << (note.special_effect ? 1 : 0) << "\tM1";
    } else if (is_slide_type(note.type)) {
      out << "\t" << note.wait_ticks << "\t" << note.last_ticks << "\t" << note.end_key;
    }
    out << "\n";
  }

  return out.str();
}

}  // namespace maiconv
