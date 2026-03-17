#include "maiconv/core/simai/tokenizer.hpp"

#include "maiconv/core/io.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

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

        std::optional<std::pair<std::string, std::size_t>> parse_maidata_key_at(
            std::string_view text, std::size_t pos, bool allow_without_prefix)
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
                                   const simai::Tokenizer &tokenizer,
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

    std::vector<std::string> simai::Tokenizer::tokenize_text(
        const std::string &text) const
    {
        const std::string normalized = normalize_simai_for_tokenize(text);
        std::vector<std::string> tokens = split(normalized, ',');
        for (auto &token : tokens)
        {
            token = normalize_simai_token_after_split(std::move(token));
        }
        return tokens;
    }

    std::vector<std::string> simai::Tokenizer::tokenize_file(
        const std::filesystem::path &path) const
    {
        return tokenize_text(read_text_file(path));
    }

    SimaiDocument simai::Tokenizer::parse_document(const std::string &text) const
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

    SimaiDocument simai::Tokenizer::parse_file(
        const std::filesystem::path &path) const
    {
        return parse_document(read_text_file(path));
    }

} // namespace maiconv
