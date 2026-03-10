#include "maiconv/core/format.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <unordered_map>

namespace maiconv {
  namespace {

    std::string lower(std::string value) {
      std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      return value;
    }

  }  // namespace

  std::optional<ChartFormat> parse_chart_format(const std::string& value) {
    const std::string normalized = lower(value);
    static const std::unordered_map<std::string, ChartFormat> kMap = {
        {"simai", ChartFormat::Simai},
        {"simai-fes", ChartFormat::SimaiFes},
        {"simaifes", ChartFormat::SimaiFes},
        {"maidata", ChartFormat::Maidata},
        {"ma2", ChartFormat::Ma2_103},
        {"ma2-103", ChartFormat::Ma2_103},
        {"ma2_103", ChartFormat::Ma2_103},
        {"ma2-104", ChartFormat::Ma2_104},
        {"ma2_104", ChartFormat::Ma2_104},
    };
    const auto it = kMap.find(normalized);
    if (it == kMap.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  std::string to_string(ChartFormat format) {
    switch (format) {
    case ChartFormat::Simai:
      return "simai";
    case ChartFormat::SimaiFes:
      return "simai-fes";
    case ChartFormat::Maidata:
      return "maidata";
    case ChartFormat::Ma2_103:
      return "ma2-103";
    case ChartFormat::Ma2_104:
      return "ma2-104";
    }
    return "simai";
  }

  std::optional<FlipMethod> parse_flip_method(const std::string& value) {
    const std::string normalized = lower(value);
    static const std::unordered_map<std::string, FlipMethod> kMap = {
        {"upsidedown", FlipMethod::UpSideDown},
        {"clockwise90", FlipMethod::Clockwise90},
        {"clockwise180", FlipMethod::Clockwise180},
        {"counterclockwise90", FlipMethod::Counterclockwise90},
        {"counterclockwise180", FlipMethod::Counterclockwise180},
        {"lefttoright", FlipMethod::LeftToRight},
    };
    const auto it = kMap.find(normalized);
    if (it == kMap.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  std::string to_string(FlipMethod method) {
    switch (method) {
    case FlipMethod::UpSideDown:
      return "UpSideDown";
    case FlipMethod::Clockwise90:
      return "Clockwise90";
    case FlipMethod::Clockwise180:
      return "Clockwise180";
    case FlipMethod::Counterclockwise90:
      return "Counterclockwise90";
    case FlipMethod::Counterclockwise180:
      return "Counterclockwise180";
    case FlipMethod::LeftToRight:
      return "LeftToRight";
    }
    return "UpSideDown";
  }

  bool is_slide_type(NoteType type) {
    switch (type) {
    case NoteType::SlideStraight:
    case NoteType::SlideCurveLeft:
    case NoteType::SlideCurveRight:
    case NoteType::SlideV:
    case NoteType::SlideWifi:
    case NoteType::SlideP:
    case NoteType::SlidePP:
    case NoteType::SlideQ:
    case NoteType::SlideQQ:
    case NoteType::SlideS:
    case NoteType::SlideZ:
    case NoteType::SlideVTurnLeft:
    case NoteType::SlideVTurnRight:
      return true;
    default:
      return false;
    }
  }

}  // namespace maiconv