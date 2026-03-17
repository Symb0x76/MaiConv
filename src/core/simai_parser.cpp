#include "maiconv/core/simai/parser.hpp"

#include "maiconv/core/io.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

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
            return !s.empty() &&
                   std::all_of(s.begin(), s.end(), [](unsigned char c)
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

        std::pair<int, int> to_bar_tick(int tick, int definition)
        {
            if (tick < 0)
            {
                return {0, 0};
            }
            return {tick / definition, tick % definition};
        }

    } // namespace

    bool simai::Parser::contains_slide_notation(const std::string &token)
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

    std::vector<std::string> simai::Parser::each_group_of_token(
        const std::string &token)
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

    Chart simai::Parser::parse_document(const SimaiDocument &document,
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

    Chart simai::Parser::parse_tokens(const std::vector<std::string> &tokens) const
    {
        Chart chart;
        chart.bpm_changes().push_back(BpmChange{0, 0, 120.0});
        chart.measure_changes().push_back(MeasureChange{0, 0, 4, 4});

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
                        group.erase(std::remove(group.begin(), group.end(), '%'), group.end());
                    }
                    if (!group.empty() && group.front() == '(' && group.back() == ')')
                    {
                        const double bpm = to_double(group.substr(1, group.size() - 2), 120.0);
                        planned_bpm.push_back(BpmChange{scan_bar, scan_tick, bpm});
                    }
                    else if (!group.empty() && group.front() == '{' && group.back() == '}')
                    {
                        const int quaver = std::max(1, to_int(group.substr(1, group.size() - 2), 4));
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

                        const int absolute_tick = bar * chart.definition() + tick + local_offset;
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

} // namespace maiconv
