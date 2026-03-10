#pragma once

#include <optional>
#include <string>

namespace maiconv {

  enum class ChartFormat {
    Simai,
    SimaiFes,
    Maidata,
    Ma2_103,
    Ma2_104,
  };

  enum class FlipMethod {
    UpSideDown,
    Clockwise90,
    Clockwise180,
    Counterclockwise90,
    Counterclockwise180,
    LeftToRight,
  };

  enum class SpecialState {
    Normal,
    Break,
    Ex,
    BreakEx,
    ConnectingSlide,
  };

  enum class NoteType {
    Rest,
    Tap,
    SlideStart,
    TouchTap,
    Hold,
    TouchHold,
    SlideStraight,
    SlideCurveLeft,
    SlideCurveRight,
    SlideV,
    SlideWifi,
    SlideP,
    SlidePP,
    SlideQ,
    SlideQQ,
    SlideS,
    SlideZ,
    SlideVTurnLeft,
    SlideVTurnRight,
    Bpm,
    Measure,
  };

  std::optional<ChartFormat> parse_chart_format(const std::string& value);
  std::string to_string(ChartFormat format);

  std::optional<FlipMethod> parse_flip_method(const std::string& value);
  std::string to_string(FlipMethod method);

  bool is_slide_type(NoteType type);

}  // namespace maiconv