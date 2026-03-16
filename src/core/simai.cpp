#include "maiconv/core/simai.hpp"

#include "maiconv/core/io.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <numeric>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace maiconv
{
  namespace
  {

    int to_int(const std::string &s, int fallback = 0)
    {
      try
      {
        return std::stoi(s);
      }
      catch (...)
      {
        return fallback;
      }
    }

    double to_double(const std::string &s, double fallback = 0.0)
    {
      try
      {
        return std::stod(s);
      }
      catch (...)
      {
        return fallback;
      }
    }

    bool is_digit_token(const std::string &s)
    {
      return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c)
                                       { return std::isdigit(c) != 0; });
    }

    std::optional<int> first_key_from_token(const std::string &token)
    {
      for (char c : token)
      {
        if (c >= '1' && c <= '8')
        {
          return static_cast<int>(c - '1');
        }
      }
      return std::nullopt;
    }

    SpecialState parse_state(const std::string &token)
    {
      const bool has_break = token.find('b') != std::string::npos;
      const bool has_ex = token.find('x') != std::string::npos;
      if (has_break && has_ex)
      {
        return SpecialState::BreakEx;
      }
      if (has_break)
      {
        return SpecialState::Break;
      }
      if (has_ex)
      {
        return SpecialState::Ex;
      }
      return SpecialState::Normal;
    }

    std::string extract_duration(const std::string &token, std::string &stripped)
    {
      const auto lb = token.find('[');
      const auto rb = token.rfind(']');
      if (lb == std::string::npos || rb == std::string::npos || rb <= lb)
      {
        stripped = token;
        return "";
      }
      stripped = token.substr(0, lb) + token.substr(rb + 1);
      return token.substr(lb, rb - lb + 1);
    }

    int gcd_int(int a, int b)
    {
      if (a < 0)
      {
        a = -a;
      }
      if (b < 0)
      {
        b = -b;
      }
      while (b != 0)
      {
        const int t = a % b;
        a = b;
        b = t;
      }
      return a;
    }

    struct DurationTicks
    {
      int wait = 0;
      int last = 0;
    };

    DurationTicks parse_duration(const std::string &raw, const Chart &chart,
                                 int start_tick, bool is_slide,
                                 double start_bpm_hint = -1.0,
                                 double action_bpm_hint = -1.0)
    {
      DurationTicks out;
      std::string body = raw;
      if (!body.empty() && body.front() == '[' && body.back() == ']')
      {
        body = body.substr(1, body.size() - 2);
      }
      if (body.empty())
      {
        out.wait = is_slide ? 96 : 0;
        out.last = is_slide ? 24 : 96;
        return out;
      }

      const auto to_ticks = [&](double sec, int at_tick)
      {
        return std::max(1, chart.seconds_to_ticks_at(sec, at_tick));
      };

      if (body.find("##") != std::string::npos)
      {
        const auto parts = split(body, '#');
        if (parts.size() >= 4 && body.find(':') != std::string::npos)
        {
          const double wait_sec = to_double(parts[0]);
          const double bpm = to_double(parts[2], chart.bpm_at_tick(start_tick));
          const auto qb = split(parts[3], ':');
          const double quaver = to_double(qb[0], 4.0);
          const double beat = qb.size() > 1 ? to_double(qb[1], 1.0) : 1.0;
          const double base_ticks = 384.0 / quaver * beat;
          const double sec = base_ticks * (60.0 / bpm * 4.0 / 384.0);
          out.wait = to_ticks(wait_sec, start_tick);
          out.last = to_ticks(sec, start_tick + out.wait);
          return out;
        }

        const auto chunks = split(body, '#');
        if (chunks.size() >= 3)
        {
          const double wait_sec = to_double(chunks[0]);
          const double last_sec = to_double(chunks[2]);
          out.wait = to_ticks(wait_sec, start_tick);
          out.last = to_ticks(last_sec, start_tick + out.wait);
          return out;
        }
      }

      if (body.find(':') != std::string::npos &&
          body.find('#') == std::string::npos)
      {
        const auto qb = split(body, ':');
        const double quaver = to_double(qb[0], 4.0);
        const double beat = qb.size() > 1 ? to_double(qb[1], 1.0) : 1.0;
        const int base_ticks =
            std::max(1, static_cast<int>(std::llround(384.0 / quaver * beat)));
        if (is_slide)
        {
          out.wait = 96;
          const double bpm0 =
              start_bpm_hint > 0.0 ? start_bpm_hint : chart.bpm_at_tick(start_tick);
          const double bpm1 = action_bpm_hint > 0.0
                                  ? action_bpm_hint
                                  : chart.bpm_at_tick(start_tick + out.wait);
          const double adjusted =
              static_cast<double>(base_ticks) * (bpm1 / std::max(1.0, bpm0));
          out.last = std::max(1, static_cast<int>(std::llround(adjusted)));
        }
        else
        {
          out.wait = 0;
          out.last = base_ticks;
        }
        return out;
      }

      if (body.find('#') != std::string::npos)
      {
        const auto chunks = split(body, '#');
        if (!chunks.empty() && chunks[0].empty())
        {
          out.last = to_ticks(to_double(chunks[1], 0.5), start_tick);
          out.wait = is_slide ? 96 : 0;
          return out;
        }
        if (chunks.size() >= 2)
        {
          const double bpm = to_double(chunks[0], chart.bpm_at_tick(start_tick));
          const double sec = to_double(chunks[1], 0.5);
          out.last = to_ticks(sec, start_tick);
          if (is_slide)
          {
            const double wait_sec = 60.0 / bpm;
            out.wait = to_ticks(wait_sec, start_tick);
          }
          return out;
        }
      }

      out.last = to_ticks(to_double(body, 0.5), start_tick);
      out.wait = is_slide ? 96 : 0;
      return out;
    }

    NoteType parse_slide_type(const std::string &token, std::size_t offset,
                              int start_key, int end_key, std::size_t &consumed,
                              int &inflection_out)
    {
      inflection_out = -1;
      consumed = 1;
      if (token.compare(offset, 2, "qq") == 0)
      {
        consumed = 2;
        return NoteType::SlidePP;
      }
      if (token.compare(offset, 2, "pp") == 0)
      {
        consumed = 2;
        return NoteType::SlideQQ;
      }
      const char c = token[offset];
      switch (c)
      {
      case '-':
        return NoteType::SlideStraight;
      case 'v':
        return NoteType::SlideV;
      case 'w':
        return NoteType::SlideWifi;
      case '<':
        return NoteType::SlideCurveRight;
      case '>':
        return NoteType::SlideCurveLeft;
      case 'q':
        return NoteType::SlideP;
      case 'p':
        return NoteType::SlideQ;
      case 's':
        return NoteType::SlideS;
      case 'z':
        return NoteType::SlideZ;
      case '^':
      {
        int left = start_key - end_key;
        if (left <= 0)
        {
          left += 8;
        }
        int right = end_key - start_key;
        if (right <= 0)
        {
          right += 8;
        }
        return left <= right ? NoteType::SlideCurveLeft : NoteType::SlideCurveRight;
      }
      case 'V':
        consumed = 2;
        if (offset + 1 < token.size() &&
            std::isdigit(static_cast<unsigned char>(token[offset + 1])))
        {
          inflection_out = static_cast<int>(token[offset + 1] - '1');
        }
        if (inflection_out >= 0)
        {
          const int clockwise = (inflection_out - start_key + 8) % 8;
          if (clockwise == 2)
          {
            return NoteType::SlideVTurnRight;
          }
          if (clockwise == 6)
          {
            return NoteType::SlideVTurnLeft;
          }
        }
        return NoteType::SlideVTurnRight;
      default:
        return NoteType::SlideStraight;
      }
    }

    std::string slide_notation(NoteType type, int start_key, int end_key)
    {
      static_cast<void>(end_key);
      const bool outer_start =
          start_key == 0 || start_key == 1 || start_key == 6 || start_key == 7;
      switch (type)
      {
      case NoteType::SlideStraight:
        return "-";
      case NoteType::SlideV:
        return "v";
      case NoteType::SlideWifi:
        return "w";
      case NoteType::SlideCurveLeft:
      {
        // Keep curve glyph selection aligned with MaiLib/MaiChartManager:
        // outer starts use '<', inner starts use '>'.
        return outer_start ? "<" : ">";
      }
      case NoteType::SlideCurveRight:
      {
        return outer_start ? ">" : "<";
      }
      case NoteType::SlideP:
        return "q";
      case NoteType::SlidePP:
        return "qq";
      case NoteType::SlideQ:
        return "p";
      case NoteType::SlideQQ:
        return "pp";
      case NoteType::SlideS:
        return "s";
      case NoteType::SlideZ:
        return "z";
      case NoteType::SlideVTurnLeft:
      case NoteType::SlideVTurnRight:
      {
        const int via_key = (type == NoteType::SlideVTurnRight)
                                ? (start_key + 2) % 8
                                : (start_key + 6) % 8;
        return "V" + std::to_string(via_key + 1);
      }
      default:
        return "-";
      }
    }

    std::string format_decimal_compact(double value)
    {
      std::ostringstream out;
      out << std::fixed << std::setprecision(4) << value;
      std::string s = out.str();
      while (!s.empty() && s.back() == '0')
      {
        s.pop_back();
      }
      if (!s.empty() && s.back() == '.')
      {
        s.pop_back();
      }
      if (s.empty())
      {
        return "0";
      }
      return s;
    }

    std::optional<std::pair<int, int>> ticks_to_quaver_ratio(int ticks)
    {
      if (ticks <= 0)
      {
        return std::nullopt;
      }
      const int whole = 384;
      const int divisor = gcd_int(whole, ticks);
      if (divisor <= 0)
      {
        return std::nullopt;
      }
      return std::make_pair(whole / divisor, ticks / divisor);
    }

    bool has_bpm_change_between(const Chart &chart, int start_tick, int end_tick)
    {
      if (end_tick <= start_tick)
      {
        return false;
      }
      for (const auto &change : chart.bpm_changes())
      {
        const int stamp = change.tick_stamp(chart.definition());
        if (stamp > start_tick && stamp < end_tick)
        {
          return true;
        }
      }
      return false;
    }

    int fixed_last_length_like_mailib(const Chart &chart, int start_tick, int wait_ticks,
                                      int last_ticks)
    {
      const int resolved_wait = std::max(0, wait_ticks);
      const int resolved_last = std::max(0, last_ticks);
      const int end_tick = start_tick + resolved_wait + resolved_last;

      const double tick_time = chart.ticks_to_seconds(start_tick);
      const double end_time = chart.ticks_to_seconds(end_tick);
      const double calculated_last_time = std::max(0.0, end_time - tick_time);

      double bpm = chart.bpm_at_tick(start_tick);
      if (bpm <= 0.0)
      {
        bpm = 120.0;
      }
      const double bpm_unit =
          60.0 / bpm * 4.0 / static_cast<double>(chart.definition());
      if (bpm_unit <= 0.0)
      {
        return resolved_last;
      }

      return std::max(
          0, static_cast<int>(std::llround(calculated_last_time / bpm_unit)));
    }

    std::string format_ratio_duration(int ticks)
    {
      if (ticks <= 0)
      {
        return "[1:0]";
      }
      const auto ratio = ticks_to_quaver_ratio(ticks);
      if (!ratio.has_value())
      {
        return "[1:0]";
      }
      return "[" + std::to_string(ratio->first) + ":" +
             std::to_string(ratio->second) + "]";
    }

    std::string format_slide_duration(const Chart &chart, int start_tick,
                                      const Note &note)
    {
      const int resolved_wait = std::max(0, note.wait_ticks);
      const int resolved_last = std::max(0, note.last_ticks);
      const int end_tick = start_tick + resolved_wait + resolved_last;
      const bool tick_bpm_disagree =
          has_bpm_change_between(chart, start_tick, end_tick);
      const bool delayed = resolved_wait != chart.definition() / 4;

      if (!tick_bpm_disagree && !delayed)
      {
        return format_ratio_duration(resolved_last);
      }

      if (note.state == SpecialState::ConnectingSlide)
      {
        const int fixed = fixed_last_length_like_mailib(
            chart, start_tick, resolved_wait, resolved_last);
        return format_ratio_duration(fixed);
      }

      const double wait_sec =
          chart.ticks_to_seconds(start_tick + resolved_wait) -
          chart.ticks_to_seconds(start_tick);
      const double duration_sec = chart.ticks_to_seconds(end_tick) -
                                  chart.ticks_to_seconds(start_tick + resolved_wait);
      return "[" + format_decimal_compact(wait_sec) + "##" +
             format_decimal_compact(duration_sec) + "]";
    }

    std::string format_chained_slide_duration(const Chart &chart, int start_tick,
                                              const Note &note)
    {
      return format_slide_duration(chart, start_tick, note);
    }

    std::string format_hold_duration(const Chart &chart, int start_tick,
                                     const Note &note)
    {
      const int resolved_last = std::max(0, note.last_ticks);
      const bool tick_bpm_disagree = has_bpm_change_between(
          chart, start_tick, start_tick + resolved_last);
      const int output_last =
          tick_bpm_disagree
              ? fixed_last_length_like_mailib(chart, start_tick, 0, resolved_last)
              : resolved_last;
      return format_ratio_duration(output_last);
    }

    std::string state_suffix(SpecialState state)
    {
      switch (state)
      {
      case SpecialState::Break:
        return "b";
      case SpecialState::Ex:
        return "x";
      case SpecialState::BreakEx:
        return "bx";
      default:
        return "";
      }
    }

    int slot_token_weight(const std::string &token)
    {
      if (token.empty())
      {
        return 3;
      }

      const char head = token.front();
      if (head == 'A' || head == 'B' || head == 'C' || head == 'D' || head == 'E')
      {
        return 0;
      }

      if (token.find('$') != std::string::npos)
      {
        return 2;
      }

      // Keep unresolved slide starters ("?") behind concrete notes at same slot,
      // matching MaiLib's serialized ordering in each-set output.
      if (token.find('?') != std::string::npos &&
          SimaiParser::contains_slide_notation(token))
      {
        return 3;
      }

      if (SimaiParser::contains_slide_notation(token))
      {
        return 2;
      }
      return 1;
    }

    std::pair<int, int> to_bar_tick(int tick, int definition)
    {
      if (tick < 0)
      {
        return {0, 0};
      }
      return {tick / definition, tick % definition};
    }

    struct DelayBounds
    {
      bool has_note = false;
      int max_bar = 0;
      int max_end_tick = 0;
    };

    DelayBounds estimate_delay_bounds_like_mailib(const Chart &chart)
    {
      const int def = chart.definition();

      std::vector<Note> non_slide_notes;
      std::vector<Note> slide_notes;
      non_slide_notes.reserve(chart.notes().size());
      slide_notes.reserve(chart.notes().size());
      for (const auto &note : chart.notes())
      {
        if (!note.is_note())
        {
          continue;
        }
        if (is_slide_type(note.type))
        {
          slide_notes.push_back(note);
        }
        else
        {
          non_slide_notes.push_back(note);
        }
      }

      std::vector<Note> grouped_slide_notes;
      grouped_slide_notes.reserve(slide_notes.size());
      std::vector<bool> slide_processed(slide_notes.size(), false);
      for (std::size_t i = 0; i < slide_notes.size(); ++i)
      {
        Note parent = slide_notes[i];
        if (slide_processed[i] || parent.state == SpecialState::ConnectingSlide)
        {
          continue;
        }

        int total_wait = parent.wait_ticks;
        int total_last = parent.last_ticks;
        int current_end_stamp =
            parent.tick_stamp(def) + parent.wait_ticks + parent.last_ticks;
        int current_end_key = parent.end_key;
        bool found_next = true;

        while (found_next)
        {
          found_next = false;
          for (std::size_t j = 0; j < slide_notes.size(); ++j)
          {
            if (slide_processed[j] || i == j)
            {
              continue;
            }
            const auto &candidate = slide_notes[j];
            if (candidate.state != SpecialState::ConnectingSlide)
            {
              continue;
            }
            if (candidate.tick_stamp(def) != current_end_stamp ||
                candidate.key != current_end_key)
            {
              continue;
            }

            slide_processed[j] = true;
            total_wait += candidate.wait_ticks;
            total_last += candidate.last_ticks;
            current_end_stamp =
                candidate.tick_stamp(def) + candidate.wait_ticks + candidate.last_ticks;
            current_end_key = candidate.end_key;
            found_next = true;
            break;
          }
        }

        slide_processed[i] = true;
        parent.wait_ticks = total_wait;
        parent.last_ticks = total_last;
        parent.end_key = current_end_key;
        grouped_slide_notes.push_back(parent);
      }

      std::vector<Note> compose_group_notes;
      compose_group_notes.reserve(non_slide_notes.size() + grouped_slide_notes.size());
      compose_group_notes.insert(compose_group_notes.end(), non_slide_notes.begin(),
                                 non_slide_notes.end());
      compose_group_notes.insert(compose_group_notes.end(), grouped_slide_notes.begin(),
                                 grouped_slide_notes.end());

      struct SlideEachDelaySet
      {
        int origin_stamp = 0;
        int origin_key = -1;
        int seed_wait_ticks = 0;
        int seed_last_ticks = 0;
      };

      std::vector<Note> folded_notes;
      std::vector<SlideEachDelaySet> each_sets;
      folded_notes.reserve(compose_group_notes.size());
      each_sets.reserve(compose_group_notes.size());

      for (const auto &note : compose_group_notes)
      {
        const bool is_slide = is_slide_type(note.type);
        const int stamp = note.tick_stamp(def);

        if (note.type != NoteType::SlideStart && !is_slide)
        {
          folded_notes.push_back(note);
          continue;
        }

        bool combined = false;
        for (auto &set : each_sets)
        {
          if (set.origin_key != note.key || set.origin_stamp != stamp)
          {
            continue;
          }
          combined = true;
        }

        if (!combined)
        {
          SlideEachDelaySet created;
          created.origin_key = note.key;
          created.origin_stamp = stamp;
          created.seed_wait_ticks = note.wait_ticks;
          created.seed_last_ticks = note.last_ticks;
          each_sets.push_back(std::move(created));
        }
      }

      for (const auto &set : each_sets)
      {
        Note folded;
        folded.type = NoteType::SlideStart;
        const auto [bar, tick] = to_bar_tick(set.origin_stamp, def);
        folded.bar = bar;
        folded.tick = tick;
        folded.key = set.origin_key;
        folded.wait_ticks = set.seed_wait_ticks;
        folded.last_ticks = set.seed_last_ticks;
        folded_notes.push_back(std::move(folded));
      }

      DelayBounds bounds;
      for (const auto &note : folded_notes)
      {
        if (!note.is_note())
        {
          continue;
        }
        bounds.has_note = true;
        const int stamp = note.tick_stamp(def);
        bounds.max_bar = std::max(bounds.max_bar, stamp / def);
        bounds.max_end_tick =
            std::max(bounds.max_end_tick, stamp + note.wait_ticks + note.last_ticks);
      }
      return bounds;
    }

    std::string replace_all(std::string input, std::string_view from,
                            std::string_view to)
    {
      if (from.empty())
      {
        return input;
      }
      std::size_t pos = 0;
      while ((pos = input.find(from, pos)) != std::string::npos)
      {
        input.replace(pos, from.size(), to);
        pos += to.size();
      }
      return input;
    }

    std::string strip_simai_comments(std::string_view text)
    {
      std::string out;
      out.reserve(text.size());
      bool line_comment = false;
      int square_depth = 0;
      int curly_depth = 0;

      for (std::size_t i = 0; i < text.size(); ++i)
      {
        const char c = text[i];
        if (line_comment)
        {
          if (c == '\n')
          {
            line_comment = false;
            out.push_back('\n');
          }
          continue;
        }

        if (c == '\r')
        {
          continue;
        }

        if (c == '|' && i + 1 < text.size() && text[i + 1] == '|')
        {
          line_comment = true;
          ++i;
          continue;
        }

        if (c == '#' && square_depth == 0 && curly_depth == 0)
        {
          line_comment = true;
          continue;
        }

        if (c == '[')
        {
          ++square_depth;
        }
        else if (c == ']')
        {
          square_depth = std::max(0, square_depth - 1);
        }
        else if (c == '{')
        {
          ++curly_depth;
        }
        else if (c == '}')
        {
          curly_depth = std::max(0, curly_depth - 1);
        }
        out.push_back(c);
      }
      return out;
    }

    std::string normalize_simai_for_tokenize(std::string_view text)
    {
      std::string cleaned = strip_simai_comments(text);
      if (cleaned.size() >= 3 &&
          static_cast<unsigned char>(cleaned[0]) == 0xEF &&
          static_cast<unsigned char>(cleaned[1]) == 0xBB &&
          static_cast<unsigned char>(cleaned[2]) == 0xBF)
      {
        cleaned.erase(0, 3);
      }

      std::string compact;
      compact.reserve(cleaned.size());
      for (const char c : cleaned)
      {
        if (!std::isspace(static_cast<unsigned char>(c)))
        {
          compact.push_back(c);
        }
      }

      compact = replace_all(std::move(compact), "{{", "{");
      compact = replace_all(std::move(compact), "}}", "}");
      compact = replace_all(std::move(compact), "-?", "?-");

      static const std::regex kFixMissingCommaBeforeMeasure(R"((\d)\{)");
      static const std::regex kFixMissingCommaBeforeBpm(R"((\d)\()");
      static const std::regex kFixRangeDuration(R"(\[(\d+)-(\d+)\])");
      static const std::regex kFixEmptyControlSlots(R"(,[csbx\.\{\}],)");
      static const std::regex kFixQxOrder(R"((\d)qx(\d))");

      compact = std::regex_replace(compact, kFixMissingCommaBeforeMeasure, "$1,{");
      compact = std::regex_replace(compact, kFixMissingCommaBeforeBpm, "$1,(");
      compact = std::regex_replace(compact, kFixRangeDuration, "[$1:$2]");
      compact = std::regex_replace(compact, kFixEmptyControlSlots, ",,");
      compact = std::regex_replace(compact, kFixQxOrder, "$1xq$2");

      return compact;
    }

    std::string normalize_simai_token_after_split(std::string token)
    {
      if (token.find("]b") == std::string::npos)
      {
        return token;
      }
      token = replace_all(std::move(token), "]b", "]");
      token = replace_all(std::move(token), "[", "b[");
      return token;
    }

    bool is_maidata_key_head(char c)
    {
      const unsigned char uc = static_cast<unsigned char>(c);
      return std::isalpha(uc) != 0 || c == '_';
    }

    bool is_maidata_key_body(char c)
    {
      const unsigned char uc = static_cast<unsigned char>(c);
      return std::isalnum(uc) != 0 || c == '_';
    }

    std::optional<std::pair<std::string, std::size_t>>
    parse_maidata_key_at(std::string_view text, std::size_t pos,
                         bool allow_without_prefix)
    {
      if (pos >= text.size())
      {
        return std::nullopt;
      }

      std::size_t cursor = pos;
      if (text[cursor] == '&')
      {
        ++cursor;
      }
      else if (!allow_without_prefix)
      {
        return std::nullopt;
      }

      if (cursor >= text.size() || !is_maidata_key_head(text[cursor]))
      {
        return std::nullopt;
      }

      const std::size_t key_begin = cursor;
      while (cursor < text.size() && is_maidata_key_body(text[cursor]))
      {
        ++cursor;
      }
      if (cursor >= text.size() || text[cursor] != '=')
      {
        return std::nullopt;
      }

      return std::make_pair(std::string(text.substr(key_begin, cursor - key_begin)),
                            cursor + 1);
    }

    using MaidataEntries = std::vector<std::pair<std::string, std::string>>;

    MaidataEntries parse_compact_maidata_entries(std::string_view text)
    {
      MaidataEntries entries;

      std::size_t cursor = 0;
      while (cursor < text.size() &&
             std::isspace(static_cast<unsigned char>(text[cursor])) != 0)
      {
        ++cursor;
      }

      const auto first = parse_maidata_key_at(text, cursor, true);
      if (!first.has_value())
      {
        return entries;
      }

      while (cursor < text.size())
      {
        const auto current = parse_maidata_key_at(text, cursor, true);
        if (!current.has_value())
        {
          break;
        }

        const std::size_t value_begin = current->second;
        std::size_t next_entry = std::string_view::npos;
        std::size_t probe = value_begin;
        while (probe < text.size())
        {
          if (text[probe] == '&')
          {
            if (parse_maidata_key_at(text, probe, false).has_value())
            {
              next_entry = probe;
              break;
            }
          }
          ++probe;
        }

        const std::size_t value_end =
            next_entry == std::string_view::npos ? text.size() : next_entry;
        entries.emplace_back(current->first,
                             std::string(text.substr(value_begin,
                                                     value_end - value_begin)));

        if (next_entry == std::string_view::npos)
        {
          break;
        }
        cursor = next_entry;
      }

      return entries;
    }

    MaidataEntries parse_line_maidata_entries(const std::string &text)
    {
      MaidataEntries entries;
      std::istringstream stream(text);
      std::string line;
      std::string current_key;
      std::string current_value;
      bool first_line = true;

      auto flush_entry = [&]()
      {
        if (current_key.empty())
        {
          return;
        }
        entries.emplace_back(current_key, current_value);
        current_key.clear();
        current_value.clear();
      };

      while (std::getline(stream, line))
      {
        if (!line.empty() && line.back() == '\r')
        {
          line.pop_back();
        }
        if (first_line && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF)
        {
          line.erase(0, 3);
        }
        first_line = false;

        std::size_t line_start = 0;
        while (line_start < line.size() &&
               (line[line_start] == ' ' || line[line_start] == '\t'))
        {
          ++line_start;
        }

        if (line_start < line.size() && line[line_start] == '&')
        {
          const auto parsed_key = parse_maidata_key_at(line, line_start, false);
          if (parsed_key.has_value())
          {
            flush_entry();
            current_key = parsed_key->first;
            current_value += line.substr(parsed_key->second);
            continue;
          }
        }

        if (!current_key.empty())
        {
          current_value.push_back('\n');
          current_value += line;
        }
      }
      flush_entry();

      return entries;
    }

    void apply_maidata_entries(const MaidataEntries &entries,
                               const SimaiTokenizer &tokenizer,
                               SimaiDocument &doc)
    {
      for (const auto &[key, value] : entries)
      {
        const std::string normalized_key = lower(key);
        if (normalized_key.rfind("inote_", 0) == 0)
        {
          const int diff = to_int(normalized_key.substr(6), 1);
          doc.chart_tokens[diff] = tokenizer.tokenize_text(value);
          continue;
        }
        doc.metadata[key] = value;
      }
    }

  } // namespace

  std::vector<std::string>
  SimaiTokenizer::tokenize_text(const std::string &text) const
  {
    const std::string normalized = normalize_simai_for_tokenize(text);
    std::vector<std::string> tokens = split(normalized, ',');
    for (auto &token : tokens)
    {
      token = normalize_simai_token_after_split(std::move(token));
    }
    return tokens;
  }

  std::vector<std::string>
  SimaiTokenizer::tokenize_file(const std::filesystem::path &path) const
  {
    return tokenize_text(read_text_file(path));
  }

  SimaiDocument SimaiTokenizer::parse_document(const std::string &text) const
  {
    SimaiDocument doc;
    const bool compact_document = text.find('\n') == std::string::npos &&
                                  text.find('\r') == std::string::npos;
    if (compact_document)
    {
      const auto entries = parse_compact_maidata_entries(text);
      if (entries.empty())
      {
        doc.chart_tokens[1] = tokenize_text(text);
        return doc;
      }
      apply_maidata_entries(entries, *this, doc);
      if (doc.chart_tokens.empty())
      {
        doc.chart_tokens[1] = tokenize_text(text);
      }
      return doc;
    }

    const auto entries = parse_line_maidata_entries(text);
    if (entries.empty())
    {
      doc.chart_tokens[1] = tokenize_text(text);
      return doc;
    }
    apply_maidata_entries(entries, *this, doc);
    if (doc.chart_tokens.empty())
    {
      doc.chart_tokens[1] = tokenize_text(text);
    }
    return doc;
  }

  SimaiDocument
  SimaiTokenizer::parse_file(const std::filesystem::path &path) const
  {
    return parse_document(read_text_file(path));
  }

  bool SimaiParser::contains_slide_notation(const std::string &token)
  {
    return token.find('-') != std::string::npos ||
           token.find('v') != std::string::npos ||
           token.find('w') != std::string::npos ||
           token.find('<') != std::string::npos ||
           token.find('>') != std::string::npos ||
           token.find('p') != std::string::npos ||
           token.find('q') != std::string::npos ||
           token.find('s') != std::string::npos ||
           token.find('z') != std::string::npos ||
           token.find('V') != std::string::npos ||
           token.find('^') != std::string::npos;
  }

  std::vector<std::string>
  SimaiParser::each_group_of_token(const std::string &token)
  {
    std::vector<std::string> extracted;
    std::string buffer;

    for (char c : token)
    {
      switch (c)
      {
      case '/':
        extracted.push_back(buffer);
        buffer.clear();
        break;
      case '(':
      case '{':
        if (!buffer.empty())
        {
          extracted.push_back(buffer);
          buffer.clear();
        }
        buffer.push_back(c);
        break;
      case ')':
      case '}':
        buffer.push_back(c);
        extracted.push_back(buffer);
        buffer.clear();
        break;
      case '`':
        buffer.push_back('%');
        extracted.push_back(buffer);
        buffer.clear();
        break;
      default:
        buffer.push_back(c);
        break;
      }
    }
    if (!buffer.empty())
    {
      extracted.push_back(buffer);
    }

    std::vector<std::string> out;
    for (const auto &part : extracted)
    {
      if (is_digit_token(part) && part.size() > 1)
      {
        for (char c : part)
        {
          out.push_back(std::string(1, c));
        }
      }
      else
      {
        out.push_back(part);
      }
    }
    return out;
  }

  Chart SimaiParser::parse_document(const SimaiDocument &document,
                                    int difficulty) const
  {
    if (document.chart_tokens.empty())
    {
      return Chart{};
    }

    auto it = document.chart_tokens.find(difficulty);
    if (it == document.chart_tokens.end())
    {
      it = document.chart_tokens.begin();
    }
    return parse_tokens(it->second);
  }

  Chart SimaiParser::parse_tokens(const std::vector<std::string> &tokens) const
  {
    Chart chart;
    chart.bpm_changes().push_back(BpmChange{0, 0, 120.0});
    chart.measure_changes().push_back(MeasureChange{0, 0, 4, 4});

    // Prescan BPM timeline so slide duration in [q:b] can resolve action BPM
    // correctly.
    std::vector<BpmChange> planned_bpm;
    planned_bpm.push_back(BpmChange{0, 0, 120.0});
    {
      int scan_bar = 0;
      int scan_tick = 0;
      int scan_step = 96;
      for (const auto &raw : tokens)
      {
        const auto groups = each_group_of_token(raw);
        for (auto group : groups)
        {
          bool grace = false;
          if (group.find('%') != std::string::npos)
          {
            grace = true;
            group.erase(std::remove(group.begin(), group.end(), '%'),
                        group.end());
          }
          if (!group.empty() && group.front() == '(' && group.back() == ')')
          {
            const double bpm =
                to_double(group.substr(1, group.size() - 2), 120.0);
            planned_bpm.push_back(BpmChange{scan_bar, scan_tick, bpm});
          }
          else if (!group.empty() && group.front() == '{' &&
                   group.back() == '}')
          {
            const int quaver =
                std::max(1, to_int(group.substr(1, group.size() - 2), 4));
            scan_step = std::max(1, chart.definition() / quaver);
          }

          if (grace)
          {
            ++scan_tick;
            while (scan_tick >= chart.definition())
            {
              scan_tick -= chart.definition();
              ++scan_bar;
            }
          }
        }

        scan_tick += scan_step;
        while (scan_tick >= chart.definition())
        {
          scan_tick -= chart.definition();
          ++scan_bar;
        }
      }
      std::sort(planned_bpm.begin(), planned_bpm.end(),
                [&](const BpmChange &a, const BpmChange &b)
                {
                  return a.tick_stamp(chart.definition()) <
                         b.tick_stamp(chart.definition());
                });
    }

    const auto planned_bpm_at = [&](int tick_stamp)
    {
      double bpm = planned_bpm.front().bpm;
      for (const auto &change : planned_bpm)
      {
        if (change.tick_stamp(chart.definition()) > tick_stamp)
        {
          break;
        }
        bpm = change.bpm;
      }
      return bpm;
    };

    int bar = 0;
    int tick = 0;
    int tick_step = 96;
    int previous_slide_start_key = 0;

    const auto add_tap = [&](const std::string &token, int at_bar, int at_tick,
                             bool commit) -> std::optional<Note>
    {
      Note note;
      const SpecialState state = parse_state(token);
      if (!token.empty() && token[0] >= 'A' && token[0] <= 'F')
      {
        note.type = NoteType::TouchTap;
        note.is_touch = true;
        note.touch_group = std::string(1, token[0]);
        if (token.size() > 1 &&
            std::isdigit(static_cast<unsigned char>(token[1])))
        {
          note.key = static_cast<int>(token[1] - '1');
        }
        else
        {
          note.key = 0;
        }
        note.special_effect = token.find('f') != std::string::npos;
      }
      else
      {
        const auto key = first_key_from_token(token);
        if (!key.has_value())
        {
          return std::nullopt;
        }
        note.key = *key;
        note.type = (token.find('_') != std::string::npos ||
                     token.find('$') != std::string::npos)
                        ? NoteType::SlideStart
                        : NoteType::Tap;
      }
      note.state = state;
      note.bar = at_bar;
      note.tick = at_tick;
      note.bpm = chart.bpm_at_tick(note.tick_stamp(chart.definition()));
      if (commit)
      {
        chart.notes().push_back(note);
      }
      return note;
    };

    for (const auto &raw : tokens)
    {
      const auto groups = each_group_of_token(raw);

      for (auto group : groups)
      {
        if (group.empty())
        {
          continue;
        }

        bool grace = false;
        if (group.find('%') != std::string::npos)
        {
          grace = true;
          group.erase(std::remove(group.begin(), group.end(), '%'), group.end());
        }

        if (group.empty())
        {
          if (grace)
          {
            ++tick;
          }
          continue;
        }

        if (group.front() == '(' && group.back() == ')')
        {
          const double bpm =
              to_double(group.substr(1, group.size() - 2),
                        chart.bpm_at_tick(bar * chart.definition() + tick));
          chart.bpm_changes().push_back(BpmChange{bar, tick, bpm});
        }
        else if (group.front() == '{' && group.back() == '}')
        {
          const int quaver =
              std::max(1, to_int(group.substr(1, group.size() - 2), 4));
          tick_step = std::max(1, chart.definition() / quaver);
          chart.measure_changes().push_back(MeasureChange{bar, tick, quaver, 4});
        }
        else if (contains_slide_notation(group))
        {
          std::vector<std::string> segments = split(group, '*');
          std::string shared_duration;
          int duration_count = 0;
          for (const auto &seg : segments)
          {
            if (seg.find('[') != std::string::npos)
            {
              ++duration_count;
              if (shared_duration.empty())
              {
                std::string stripped;
                shared_duration = extract_duration(seg, stripped);
              }
            }
          }

          int local_offset = 0;
          int current_start_key = previous_slide_start_key;

          for (std::size_t i = 0; i < segments.size(); ++i)
          {
            std::string segment = segments[i];
            std::string stripped;
            std::string duration = extract_duration(segment, stripped);
            if (duration.empty() && duration_count == 1)
            {
              duration = shared_duration;
            }
            segment = stripped;

            const auto start_key_opt = first_key_from_token(segment);
            std::size_t cursor = 0;
            if (start_key_opt.has_value())
            {
              current_start_key = *start_key_opt;
              const auto pos = segment.find_first_of("12345678");
              if (pos != std::string::npos)
              {
                cursor = pos + 1;
              }
            }

            int end_key = current_start_key;
            while (cursor < segment.size() &&
                   !std::isdigit(static_cast<unsigned char>(segment[cursor])) &&
                   segment[cursor] != '-' && segment[cursor] != 'v' &&
                   segment[cursor] != 'w' && segment[cursor] != '<' &&
                   segment[cursor] != '>' && segment[cursor] != 'p' &&
                   segment[cursor] != 'q' && segment[cursor] != 's' &&
                   segment[cursor] != 'z' && segment[cursor] != '^' &&
                   segment[cursor] != 'V')
            {
              ++cursor;
            }

            std::size_t consumed = 1;
            int inflection = -1;
            if (cursor >= segment.size())
            {
              continue;
            }

            std::size_t key_pos = cursor;
            const NoteType type =
                parse_slide_type(segment, key_pos, current_start_key,
                                 current_start_key, consumed, inflection);
            key_pos += consumed;
            while (key_pos < segment.size() &&
                   !std::isdigit(static_cast<unsigned char>(segment[key_pos])))
            {
              ++key_pos;
            }
            if (key_pos < segment.size())
            {
              end_key = static_cast<int>(segment[key_pos] - '1');
            }

            const int absolute_tick =
                bar * chart.definition() + tick + local_offset;
            const auto [note_bar, note_tick] =
                to_bar_tick(absolute_tick, chart.definition());

            const double start_bpm = planned_bpm_at(absolute_tick);
            const double action_bpm = planned_bpm_at(absolute_tick + 96);
            DurationTicks dur =
                parse_duration(duration.empty() ? "[16:1]" : duration, chart,
                               absolute_tick, true, start_bpm, action_bpm);
            if (i > 0)
            {
              dur.wait = 0;
            }

            Note note;
            note.type = type;
            note.state =
                (i > 0) ? SpecialState::ConnectingSlide : parse_state(segment);
            note.bar = note_bar;
            note.tick = note_tick;
            note.key = current_start_key;
            note.end_key = end_key;
            note.wait_ticks = std::max(0, dur.wait);
            note.last_ticks = std::max(1, dur.last);
            note.bpm = chart.bpm_at_tick(absolute_tick);
            chart.notes().push_back(note);

            local_offset += note.wait_ticks + note.last_ticks;
            current_start_key = end_key;
          }

          previous_slide_start_key = current_start_key;
        }
        else if (group.find('h') != std::string::npos)
        {
          const auto h_pos = group.find('h');
          const std::string key_token = group.substr(0, h_pos);
          const std::string duration_token = group.substr(h_pos + 1);

          Note note;
          note.state = parse_state(group);
          note.bar = bar;
          note.tick = tick;

          if (!key_token.empty() && key_token[0] >= 'A' && key_token[0] <= 'F')
          {
            note.type = NoteType::TouchHold;
            note.is_touch = true;
            note.touch_group = std::string(1, key_token[0]);
            if (key_token.size() > 1 &&
                std::isdigit(static_cast<unsigned char>(key_token[1])))
            {
              note.key = static_cast<int>(key_token[1] - '1');
            }
            else
            {
              note.key = 0;
            }
            note.special_effect = group.find('f') != std::string::npos;
          }
          else
          {
            note.type = NoteType::Hold;
            const auto key = first_key_from_token(key_token);
            note.key = key.has_value() ? *key : 0;
          }

          const DurationTicks dur =
              parse_duration(duration_token.empty() ? "[4:1]" : duration_token,
                             chart, note.tick_stamp(chart.definition()), false);
          note.wait_ticks = 0;
          note.last_ticks = std::max(1, dur.last);
          note.bpm = chart.bpm_at_tick(note.tick_stamp(chart.definition()));
          chart.notes().push_back(note);
        }
        else if (group.find('!') != std::string::npos ||
                 group.find('?') != std::string::npos)
        {
          const auto maybe = add_tap(group, bar, tick, false);
          if (maybe.has_value() && maybe->key >= 0)
          {
            previous_slide_start_key = maybe->key;
          }
        }
        else if (!group.empty() && group != "E")
        {
          const auto maybe = add_tap(group, bar, tick, true);
          if (maybe.has_value() && maybe->type == NoteType::SlideStart &&
              maybe->key >= 0)
          {
            previous_slide_start_key = maybe->key;
          }
        }

        if (grace)
        {
          ++tick;
          while (tick >= chart.definition())
          {
            tick -= chart.definition();
            ++bar;
          }
        }
      }

      tick += tick_step;
      while (tick >= chart.definition())
      {
        tick -= chart.definition();
        ++bar;
      }
    }

    chart.normalize();
    return chart;
  }

  std::string SimaiComposer::compose_chart(const Chart &source) const
  {
    Chart chart = source;
    chart.normalize();

    const int def = chart.definition();

    struct OrderedToken
    {
      std::string text;
      int weight = 1;
      int order_hint = 0;
      int insertion_order = 0;
    };

    struct SlotTokens
    {
      std::vector<std::string> controls;
      std::vector<OrderedToken> notes;
    };

    struct ChainRef
    {
      int origin_stamp = 0;
      int origin_key = -1;
      int current_end_key = -1;
      int current_end_stamp = 0;
      std::string compact_path;
      std::string expanded_body;
      int compact_wait_ticks = 96;
      int compact_last_ticks = 0;
      bool has_continuation = false;
      bool compact_enabled = true;
      bool compact_has_break = false;
    };

    struct SlideBundle
    {
      int origin_stamp = 0;
      std::size_t note_index = 0;
      std::string token_prefix;
      std::vector<ChainRef> branches;
    };

    struct ChainHandle
    {
      std::size_t bundle_index = 0;
      std::size_t branch_index = 0;
    };

    const auto find_matching_slide_start_state =
        [&](const Note &slide) -> std::optional<SpecialState>
    {
      const int stamp = slide.tick_stamp(def);
      for (const auto &candidate : chart.notes())
      {
        if (candidate.type != NoteType::SlideStart)
        {
          continue;
        }
        if (candidate.tick_stamp(def) != stamp || candidate.key != slide.key)
        {
          continue;
        }
        return candidate.state;
      }
      return std::nullopt;
    };

    const auto has_matching_slide = [&](const Note &candidate)
    {
      if (candidate.type != NoteType::SlideStart)
      {
        return false;
      }
      const int stamp = candidate.tick_stamp(def);
      return std::any_of(
          chart.notes().begin(), chart.notes().end(), [&](const Note &note)
          { return is_slide_type(note.type) && note.tick_stamp(def) == stamp &&
                   note.key == candidate.key; });
    };

    std::map<int, SlotTokens> tokens_at;
    std::vector<SlideBundle> slide_bundles;
    std::map<std::pair<int, int>, std::vector<ChainHandle>>
        open_slide_chains_by_end;
    std::map<std::pair<int, int>, std::size_t> simultaneous_slide_bundles;
    std::map<std::pair<int, int>, int> slide_start_index_by_stamp_key;
    int note_token_insertion_order = 0;

    for (std::size_t i = 0; i < chart.notes().size(); ++i)
    {
      const auto &note = chart.notes()[i];
      if (note.type != NoteType::SlideStart)
      {
        continue;
      }
      const auto key = std::make_pair(note.tick_stamp(def), note.key);
      if (slide_start_index_by_stamp_key.find(key) ==
          slide_start_index_by_stamp_key.end())
      {
        slide_start_index_by_stamp_key.emplace(key, static_cast<int>(i));
      }
    }

    const auto render_chain_branch = [&](const ChainRef &ref) -> std::string
    {
      if (ref.has_continuation && ref.compact_enabled)
      {
        Note compact_note;
        compact_note.wait_ticks = std::max(0, ref.compact_wait_ticks);
        compact_note.last_ticks = std::max(0, ref.compact_last_ticks);
        return ref.compact_path +
               format_slide_duration(chart, ref.origin_stamp, compact_note) +
               (ref.compact_has_break ? "b" : "");
      }
      return ref.expanded_body;
    };

    const auto render_slide_bundle =
        [&](const SlideBundle &bundle) -> std::string
    {
      std::string rendered = bundle.token_prefix;
      for (std::size_t i = 0; i < bundle.branches.size(); ++i)
      {
        if (i != 0)
        {
          rendered += "*";
        }
        rendered += render_chain_branch(bundle.branches[i]);
      }
      return rendered;
    };

    const auto erase_open_chain_entry = [&](auto &table,
                                            const std::pair<int, int> &key,
                                            const ChainHandle &handle)
    {
      const auto it = table.find(key);
      if (it == table.end())
      {
        return;
      }
      auto &entries = it->second;
      const auto entry_it = std::find_if(
          entries.begin(), entries.end(), [&](const ChainHandle &candidate)
          { return candidate.bundle_index == handle.bundle_index &&
                   candidate.branch_index == handle.branch_index; });
      if (entry_it != entries.end())
      {
        entries.erase(entry_it);
      }
      if (entries.empty())
      {
        table.erase(it);
      }
    };

    const auto erase_open_chain = [&](int stamp, const ChainHandle &handle)
    {
      const auto &bundle = slide_bundles[handle.bundle_index];
      const auto &ref = bundle.branches[handle.branch_index];
      erase_open_chain_entry(open_slide_chains_by_end, {stamp, ref.current_end_key},
                             handle);
    };

    const auto store_open_chain = [&](int stamp, const ChainHandle &handle)
    {
      const auto &bundle = slide_bundles[handle.bundle_index];
      const auto &ref = bundle.branches[handle.branch_index];
      open_slide_chains_by_end[{stamp, ref.current_end_key}].push_back(handle);
    };

    const auto find_open_chain =
        [&](const Note &note) -> std::optional<ChainHandle>
    {
      const int stamp = note.tick_stamp(def);

      const auto by_end = open_slide_chains_by_end.find({stamp, note.key});
      if (by_end != open_slide_chains_by_end.end() && !by_end->second.empty())
      {
        return by_end->second.front();
      }

      if (note.state != SpecialState::ConnectingSlide)
      {
        return std::nullopt;
      }

      const auto begin_stamp = open_slide_chains_by_end.lower_bound(
          {stamp, std::numeric_limits<int>::min()});

      // Some ma2s keep the original start key on connecting segments.
      // Prefer a same-origin chain when exact end-key match is unavailable.
      std::optional<ChainHandle> preferred;
      bool preferred_ambiguous = false;
      for (auto it = begin_stamp; it != open_slide_chains_by_end.end() &&
                                  it->first.first == stamp;
           ++it)
      {
        for (const auto &handle : it->second)
        {
          const auto &bundle = slide_bundles[handle.bundle_index];
          const auto &ref = bundle.branches[handle.branch_index];
          if (ref.origin_key != note.key)
          {
            continue;
          }
          if (preferred.has_value())
          {
            preferred_ambiguous = true;
            break;
          }
          preferred = handle;
        }
        if (preferred_ambiguous)
        {
          break;
        }
      }
      if (preferred.has_value() && !preferred_ambiguous)
      {
        return preferred;
      }

      // Only use a loose timestamp fallback when there is exactly one choice.
      std::optional<ChainHandle> unique_any;
      bool any_ambiguous = false;
      for (auto it = begin_stamp; it != open_slide_chains_by_end.end() &&
                                  it->first.first == stamp;
           ++it)
      {
        for (const auto &handle : it->second)
        {
          if (unique_any.has_value())
          {
            any_ambiguous = true;
            break;
          }
          unique_any = handle;
        }
        if (any_ambiguous)
        {
          break;
        }
      }
      if (unique_any.has_value() && !any_ambiguous)
      {
        return unique_any;
      }

      return std::nullopt;
    };
    const DelayBounds delay_bounds = estimate_delay_bounds_like_mailib(chart);
    int max_note_bar = delay_bounds.max_bar;
    int max_note_end_tick = delay_bounds.max_end_tick;
    const bool has_note = delay_bounds.has_note;

    for (const auto &bpm : chart.bpm_changes())
    {
      const std::string token = "(" + format_decimal_compact(bpm.bpm) + ")";
      const int stamp = bpm.tick_stamp(def);
      if (stamp / def > max_note_bar)
      {
        continue;
      }
      auto &controls = tokens_at[stamp].controls;
      if (controls.empty() || controls.back() != token)
      {
        controls.push_back(token);
      }
    }

    for (std::size_t note_index = 0; note_index < chart.notes().size();
         ++note_index)
    {
      const auto &note = chart.notes()[note_index];
      if (has_matching_slide(note))
      {
        continue;
      }

      const int ts = note.tick_stamp(def);
      int order_hint = static_cast<int>(note_index);

      std::string token;
      std::optional<SlideBundle> created_slide_bundle;
      std::optional<std::pair<int, int>> created_bundle_key;
      const auto matching_slide_start_state =
          is_slide_type(note.type) ? find_matching_slide_start_state(note)
                                   : std::nullopt;
      const SpecialState inherited_slide_start_state =
          matching_slide_start_state.has_value() &&
                  matching_slide_start_state.value() != SpecialState::Normal
              ? matching_slide_start_state.value()
              : SpecialState::Normal;
      const std::string inherited_state = state_suffix(inherited_slide_start_state);
      const std::string state = state_suffix(note.state);

      if (note.type == NoteType::Tap || note.type == NoteType::SlideStart)
      {
        token = std::to_string(note.key + 1) + state;
        if (note.type == NoteType::SlideStart)
        {
          token += "$";
        }
      }
      else if (note.type == NoteType::TouchTap)
      {
        token = (note.touch_group.empty() ? "C" : note.touch_group) +
                std::to_string(note.key + 1);
        if (note.special_effect)
        {
          token += "f";
        }
      }
      else if (note.type == NoteType::Hold ||
               note.type == NoteType::TouchHold)
      {
        if (note.type == NoteType::TouchHold)
        {
          token = (note.touch_group.empty() ? "C" : note.touch_group) +
                  std::to_string(note.key + 1);
          if (note.special_effect)
          {
            token += "f";
          }
        }
        else
        {
          token = std::to_string(note.key + 1) + state;
        }
        token += "h" + format_hold_duration(chart, ts, note);
      }
      else if (is_slide_type(note.type))
      {
        const auto anchor_it =
            slide_start_index_by_stamp_key.find({ts, note.key});
        if (anchor_it != slide_start_index_by_stamp_key.end())
        {
          order_hint = std::min(order_hint, anchor_it->second);
        }

        const auto chain_handle =
            note.state == SpecialState::ConnectingSlide ? find_open_chain(note)
                                                        : std::nullopt;
        const int display_start_key =
            chain_handle.has_value()
                ? slide_bundles[chain_handle->bundle_index]
                      .branches[chain_handle->branch_index]
                      .current_end_key
                : note.key;
        const std::string notation =
            slide_notation(note.type, display_start_key, note.end_key);
        const std::string segment_path = notation + std::to_string(note.end_key + 1);
        const std::string segment_state = state_suffix(note.state);
        const std::string segment_duration = format_slide_duration(chart, ts, note);
        const bool note_has_break = note.state == SpecialState::Break;
        const auto bundle_key = std::make_pair(ts, display_start_key);

        if (!chain_handle.has_value() &&
            note.state != SpecialState::ConnectingSlide)
        {
          const auto bundle_it = simultaneous_slide_bundles.find(bundle_key);
          if (bundle_it != simultaneous_slide_bundles.end())
          {
            auto &bundle = slide_bundles[bundle_it->second];
            bundle.branches.push_back(ChainRef{
                ts,
                note.key,
                note.end_key,
                ts + note.wait_ticks + note.last_ticks,
                segment_path,
                segment_path + segment_state + segment_duration,
                note.wait_ticks,
                std::max(0, note.last_ticks),
                false,
                true,
                note_has_break});
            auto &prior_notes = tokens_at[bundle.origin_stamp].notes;
            prior_notes[bundle.note_index].text = render_slide_bundle(bundle);
            const ChainHandle handle{bundle_it->second,
                                     bundle.branches.size() - 1};
            store_open_chain(ts + note.wait_ticks + note.last_ticks, handle);
            continue;
          }
        }

        if (chain_handle.has_value() &&
            note.state == SpecialState::ConnectingSlide)
        {
          auto &bundle = slide_bundles[chain_handle->bundle_index];
          auto &ref = bundle.branches[chain_handle->branch_index];
          ref.compact_path += segment_path;
          ref.expanded_body +=
              segment_path + segment_state +
              format_chained_slide_duration(chart, ts, note);
          ref.compact_last_ticks += std::max(0, note.last_ticks);
          ref.has_continuation = true;
          ref.compact_has_break = ref.compact_has_break || note_has_break;

          // Match MaiLib/MaiChartManager: connecting chains with zero wait are
          // serialized as a compact path plus one total duration.
          const bool continuation_compactable = note.wait_ticks == 0;
          ref.compact_enabled = ref.compact_enabled && continuation_compactable;

          auto &prior_notes = tokens_at[bundle.origin_stamp].notes;
          prior_notes[bundle.note_index].text = render_slide_bundle(bundle);

          erase_open_chain(ts, *chain_handle);
          ref.current_end_key = note.end_key;
          ref.current_end_stamp = ts + note.wait_ticks + note.last_ticks;
          store_open_chain(ts + note.wait_ticks + note.last_ticks, *chain_handle);
          continue;
        }

        std::string bundle_prefix = std::to_string(display_start_key + 1);
        if (matching_slide_start_state.has_value())
        {
          bundle_prefix += inherited_state;
        }
        else
        {
          bundle_prefix += "?";
        }
        token = bundle_prefix + segment_path + segment_state + segment_duration;
        created_slide_bundle = SlideBundle{
            ts,
            0,
            bundle_prefix,
            std::vector<ChainRef>{ChainRef{
                ts,
                note.key,
                note.end_key,
                ts + note.wait_ticks + note.last_ticks,
                segment_path,
                segment_path + segment_state + segment_duration,
                note.wait_ticks,
                std::max(0, note.last_ticks),
                false,
                true,
                note_has_break}}};
        created_bundle_key = bundle_key;
      }

      if (!token.empty())
      {
        auto &notes = tokens_at[ts].notes;
        notes.push_back(
            OrderedToken{token, slot_token_weight(token), order_hint,
                         note_token_insertion_order++});
        if (is_slide_type(note.type) && created_slide_bundle.has_value() &&
            created_bundle_key.has_value())
        {
          SlideBundle bundle = *created_slide_bundle;
          bundle.note_index = notes.size() - 1;
          const std::size_t bundle_index = slide_bundles.size();
          slide_bundles.push_back(std::move(bundle));
          simultaneous_slide_bundles[*created_bundle_key] = bundle_index;
          const ChainHandle handle{bundle_index, 0};
          store_open_chain(ts + note.wait_ticks + note.last_ticks, handle);
        }
      }
    }

    for (auto &[stamp, slot] : tokens_at)
    {
      (void)stamp;
      std::stable_sort(slot.notes.begin(), slot.notes.end(),
                       [](const OrderedToken &lhs, const OrderedToken &rhs)
                       {
                         if (lhs.weight != rhs.weight)
                         {
                           return lhs.weight < rhs.weight;
                         }
                         if (lhs.order_hint != rhs.order_hint)
                         {
                           return lhs.order_hint < rhs.order_hint;
                         }
                         return lhs.insertion_order < rhs.insertion_order;
                       });
    }

    std::string out;
    const int stored_bar_count = std::max(1, max_note_bar + 1);
    const int total_delay =
        has_note ? (max_note_end_tick - stored_bar_count * def) : 0;
    const int delay_bar = total_delay / def + 2;
    const int trailing_empty_bar_count = std::max(0, delay_bar + 1);
    const int last_bar = max_note_bar + trailing_empty_bar_count;
    out.reserve(static_cast<std::size_t>(std::max(1, last_bar + 1)) * 96U);
    for (int bar = 0; bar <= last_bar; ++bar)
    {
      int step = def;
      for (const auto &[stamp, slot] : tokens_at)
      {
        (void)slot;
        if (stamp < bar * def || stamp >= (bar + 1) * def)
        {
          continue;
        }
        const int offset = stamp - bar * def;
        if (offset > 0)
        {
          step = gcd_int(step, offset);
        }
      }
      if (step <= 0)
      {
        step = def;
      }

      const int quaver = std::max(1, def / step);
      for (int slot_index = 0; slot_index < quaver; ++slot_index)
      {
        const int stamp = bar * def + slot_index * step;
        std::string cell;
        if (slot_index == 0)
        {
          const auto it = tokens_at.find(stamp);
          if (it != tokens_at.end())
          {
            for (const auto &control : it->second.controls)
            {
              cell += control;
            }
          }
          cell += "{" + std::to_string(quaver) + "}";
          if (it != tokens_at.end() && !it->second.notes.empty())
          {
            for (std::size_t i = 0; i < it->second.notes.size(); ++i)
            {
              if (i != 0)
              {
                cell += '/';
              }
              cell += it->second.notes[i].text;
            }
          }
        }
        else
        {
          const auto it = tokens_at.find(stamp);
          if (it != tokens_at.end())
          {
            for (const auto &control : it->second.controls)
            {
              cell += control;
            }
            for (std::size_t i = 0; i < it->second.notes.size(); ++i)
            {
              if (!cell.empty() && i == 0 && !it->second.controls.empty())
              {
                // Controls and simultaneous notes share the same slot token.
              }
              else if (i != 0)
              {
                cell += '/';
              }
              cell += it->second.notes[i].text;
            }
          }
        }

        out += cell;
        out.push_back(',');
      }
      out.push_back('\n');
    }

    out.push_back('E');

    return out;
  }

} // namespace maiconv
