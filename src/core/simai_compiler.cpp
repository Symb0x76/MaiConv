#include "maiconv/core/simai/compiler.hpp"
#include "maiconv/core/simai/parser.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <sstream>

namespace maiconv
{
    namespace
    {

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

        std::string slide_notation(NoteType type, int start_key, int end_key)
        {
            const bool outer_start =
                start_key == 0 || start_key == 1 || start_key == 6 || start_key == 7;
            static_cast<void>(end_key);
            switch (type)
            {
            case NoteType::SlideStraight:
                return "-";
            case NoteType::SlideV:
                return "v";
            case NoteType::SlideWifi:
                return "w";
            case NoteType::SlideCurveLeft:
                if (outer_start)
                {
                    return "<";
                }
                return ">";
            case NoteType::SlideCurveRight:
                if (outer_start)
                {
                    return ">";
                }
                return "<";
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

            if (token.find('?') != std::string::npos &&
                simai::Parser::contains_slide_notation(token))
            {
                return 3;
            }

            if (simai::Parser::contains_slide_notation(token))
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

    } // namespace

    std::string simai::Compiler::compile_chart(const Chart &source) const
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
            return std::any_of(chart.notes().begin(), chart.notes().end(), [&](const Note &note)
                               { return is_slide_type(note.type) && note.tick_stamp(def) == stamp &&
                                        note.key == candidate.key; });
        };

        std::map<int, SlotTokens> tokens_at;
        std::vector<SlideBundle> slide_bundles;
        std::map<std::pair<int, int>, std::vector<ChainHandle>> open_slide_chains_by_end;
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
            return ref.expanded_body;
        };

        const auto render_slide_bundle = [&](const SlideBundle &bundle) -> std::string
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

        const auto erase_open_chain_entry = [&](auto &table, const std::pair<int, int> &key,
                                                const ChainHandle &handle)
        {
            const auto it = table.find(key);
            if (it == table.end())
            {
                return;
            }
            auto &entries = it->second;
            const auto entry_it = std::find_if(entries.begin(), entries.end(),
                                               [&](const ChainHandle &candidate)
                                               {
                                                   return candidate.bundle_index == handle.bundle_index &&
                                                          candidate.branch_index == handle.branch_index;
                                               });
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

        const auto find_open_chain = [&](const Note &note) -> std::optional<ChainHandle>
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

            const auto begin_stamp =
                open_slide_chains_by_end.lower_bound({stamp, std::numeric_limits<int>::min()});

            std::optional<ChainHandle> preferred;
            bool preferred_ambiguous = false;
            for (auto it = begin_stamp;
                 it != open_slide_chains_by_end.end() && it->first.first == stamp; ++it)
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

            std::optional<ChainHandle> unique_any;
            bool any_ambiguous = false;
            for (auto it = begin_stamp;
                 it != open_slide_chains_by_end.end() && it->first.first == stamp; ++it)
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

        for (std::size_t note_index = 0; note_index < chart.notes().size(); ++note_index)
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
            else if (note.type == NoteType::Hold || note.type == NoteType::TouchHold)
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
                const SpecialState effective_slide_state =
                    inherited_slide_start_state != SpecialState::Normal
                        ? inherited_slide_start_state
                        : note.state;
                const std::string effective_state = state_suffix(effective_slide_state);
                const auto anchor_it = slide_start_index_by_stamp_key.find({ts, note.key});
                if (anchor_it != slide_start_index_by_stamp_key.end())
                {
                    order_hint = std::min(order_hint, anchor_it->second);
                }

                const auto chain_handle = find_open_chain(note);
                const int display_start_key =
                    chain_handle.has_value()
                        ? slide_bundles[chain_handle->bundle_index]
                              .branches[chain_handle->branch_index]
                              .current_end_key
                        : note.key;
                const std::string notation =
                    slide_notation(note.type, display_start_key, note.end_key);
                const std::string segment_path = notation + std::to_string(note.end_key + 1);
                const std::string segment_state;
                const std::string segment_duration = format_slide_duration(chart, ts, note);
                const bool note_has_break = effective_slide_state == SpecialState::Break;
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
                        const ChainHandle handle{bundle_it->second, bundle.branches.size() - 1};
                        store_open_chain(ts + note.wait_ticks + note.last_ticks, handle);
                        continue;
                    }
                }

                if (chain_handle.has_value() &&
                    (note.state == SpecialState::ConnectingSlide ||
                     note.state == SpecialState::Normal))
                {
                    auto &bundle = slide_bundles[chain_handle->bundle_index];
                    auto &ref = bundle.branches[chain_handle->branch_index];
                    Note chained_note = note;
                    chained_note.state = SpecialState::ConnectingSlide;
                    ref.compact_path += segment_path;
                    ref.expanded_body +=
                        "*" + segment_path + segment_state +
                        format_slide_duration(chart, ts, chained_note);
                    ref.compact_last_ticks += std::max(0, note.last_ticks);
                    ref.has_continuation = true;
                    ref.compact_has_break = ref.compact_has_break || note_has_break;

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
                bundle_prefix += effective_state;
                token = bundle_prefix + segment_path + segment_state + segment_duration;
                created_slide_bundle =
                    SlideBundle{ts,
                                0,
                                bundle_prefix,
                                std::vector<ChainRef>{ChainRef{ts,
                                                               note.key,
                                                               note.end_key,
                                                               ts + note.wait_ticks + note.last_ticks,
                                                               segment_path,
                                                               segment_path + segment_state +
                                                                   segment_duration,
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
                notes.push_back(OrderedToken{token, slot_token_weight(token), order_hint,
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
        int max_note_start_tick = -1;
        bool last_start_has_slide = false;
        bool last_start_has_non_slide = false;
        for (const auto &note : chart.notes())
        {
            const int start_tick = note.tick_stamp(def);
            if (start_tick > max_note_start_tick)
            {
                max_note_start_tick = start_tick;
                last_start_has_slide = is_slide_type(note.type);
                last_start_has_non_slide = !is_slide_type(note.type);
            }
            else if (start_tick == max_note_start_tick)
            {
                if (is_slide_type(note.type))
                {
                    last_start_has_slide = true;
                }
                else
                {
                    last_start_has_non_slide = true;
                }
            }
        }

        int last_bar = max_note_end_tick > 0 ? std::max(0, max_note_end_tick / def) : 0;
        if (max_note_start_tick >= 0)
        {
            const int last_note_bar = max_note_start_tick / def;
            const int trailing_padding_bars =
                (last_start_has_slide && !last_start_has_non_slide) ? 2 : 3;
            last_bar = last_note_bar + trailing_padding_bars;
        }
        else if (max_note_end_tick > 0)
        {
            last_bar += 2;
        }

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
