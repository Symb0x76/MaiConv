#include "maiconv/core/simai/compiler.hpp"
#include "maiconv/core/simai/parser.hpp"
#include "maiconv/core/simai/tokenizer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace maiconv;

TEST_CASE("simai tokenizer handles inote document")
{
  simai::Tokenizer tokenizer;
  const auto doc = tokenizer.parse_document("&title=x&inote_3=(120)1,2,3,");
  REQUIRE(doc.chart_tokens.count(3) == 1);
  REQUIRE(doc.chart_tokens.at(3).size() >= 3);
}

TEST_CASE("simai parser basic notes")
{
  simai::Tokenizer tokenizer;
  simai::Parser parser;
  const auto tokens = tokenizer.tokenize_text("(120){4}1,2h[4:1],3-5[16:1],");
  Chart chart = parser.parse_tokens(tokens);
  REQUIRE(chart.notes().size() >= 3);
}

TEST_CASE("simai parser tracks source bar count at end marker")
{
  simai::Tokenizer tokenizer;
  simai::Parser parser;
  const auto tokens = tokenizer.tokenize_text("(120){1}1,{1},{1},E");
  Chart chart = parser.parse_tokens(tokens);
  REQUIRE(chart.source_bar_count() == 6);
}

TEST_CASE("simai parser keeps source bar count through parse_document")
{
  simai::Tokenizer tokenizer;
  simai::Parser parser;
  const auto doc = tokenizer.parse_document("&inote_2=(120){1}1,{1},{1},E");
  Chart chart = parser.parse_document(doc, 2);
  REQUIRE(chart.source_bar_count() == 6);
}

TEST_CASE("simai separated tokenizer/parser/compiler is deterministic")
{
  const std::string source =
      "&title=Parity&inote_2=(185){4}1,2h[4:1],3-5[16:1],(120),4,E";

  simai::Tokenizer reference_tokenizer;
  simai::Parser reference_parser;
  simai::Compiler reference_composer;

  simai::Tokenizer tokenizer;
  simai::Parser parser;
  simai::Compiler compiler;

  const auto legacy_doc = reference_tokenizer.parse_document(source);
  const auto new_doc = tokenizer.parse_document(source);
  REQUIRE(new_doc.metadata == legacy_doc.metadata);
  REQUIRE(new_doc.chart_tokens == legacy_doc.chart_tokens);

  const Chart legacy_chart = reference_parser.parse_document(legacy_doc, 2);
  const Chart new_chart = parser.parse_document(new_doc, 2);
  REQUIRE(new_chart.notes().size() == legacy_chart.notes().size());
  REQUIRE(new_chart.bpm_changes().size() == legacy_chart.bpm_changes().size());
  REQUIRE(new_chart.measure_changes().size() == legacy_chart.measure_changes().size());

  const std::string legacy_text = reference_composer.compile_chart(legacy_chart);
  const std::string new_text = compiler.compile_chart(new_chart);
  REQUIRE(new_text == legacy_text);
}

TEST_CASE("simai separated parser utility methods behave as expected")
{
  const std::string token = "1/2-3[8:1]*(4){1}`";
  REQUIRE(simai::Parser::contains_slide_notation(token));
  const auto groups = simai::Parser::each_group_of_token(token);
  REQUIRE_FALSE(groups.empty());
  REQUIRE(std::find(groups.begin(), groups.end(), "2_") != groups.end());
  REQUIRE(std::find(groups.begin(), groups.end(), "-3[8:1]") != groups.end());
}

TEST_CASE("simai parser keeps ratio slide duration ticks when bpm changes later")
{
  simai::Tokenizer tokenizer;
  simai::Parser parser;
  const auto tokens = tokenizer.tokenize_text("(199)1-2[16:1],(12.4375),,");
  Chart chart = parser.parse_tokens(tokens);
  REQUIRE_FALSE(chart.notes().empty());
  const auto slide_it =
      std::find_if(chart.notes().begin(), chart.notes().end(),
                   [](const Note &n) { return is_slide_type(n.type); });
  REQUIRE(slide_it != chart.notes().end());
  const auto &slide = *slide_it;
  REQUIRE(is_slide_type(slide.type));
  REQUIRE(slide.last_ticks == 24);
}

TEST_CASE("simai compose can be parsed back")
{
  simai::Tokenizer tokenizer;
  simai::Parser parser;
  simai::Compiler composer;

  const auto tokens = tokenizer.tokenize_text("(120){4}1,2,3,4,");
  Chart chart = parser.parse_tokens(tokens);
  const std::string simai = composer.compile_chart(chart);
  Chart parsed = parser.parse_tokens(tokenizer.tokenize_text(simai));
  REQUIRE(parsed.notes().size() >= 4);
}

TEST_CASE(
    "simai compose emits canonical maidata style bars and beat durations")
{
  simai::Tokenizer tokenizer;
  simai::Parser parser;
  simai::Compiler composer;

  const auto tokens =
      tokenizer.tokenize_text("(185){1}, {1}, {1}1x, {1}1-5[4:3],");
  Chart chart = parser.parse_tokens(tokens);
  const std::string simai = composer.compile_chart(chart);

  REQUIRE(simai.find("(185){1},\n") != std::string::npos);
  REQUIRE(simai.find("{1}1x,\n") != std::string::npos);
  REQUIRE(simai.find("{1}1-5[4:3],\n") != std::string::npos);
  REQUIRE(simai.find("##") == std::string::npos);
  REQUIRE(simai.ends_with("E\n"));
}

TEST_CASE("simai tokenizer preserves ampersands in line-based metadata")
{
  simai::Tokenizer tokenizer;
  const std::string doc_text =
      "&title=Rock & "
      "Roll\n&genre=ゲーム&バラエティ\n&inote_2=\n(120){4}1,2,3,4,\nE\n";
  const auto doc = tokenizer.parse_document(doc_text);
  REQUIRE(doc.metadata.at("title") == "Rock & Roll");
  REQUIRE(doc.metadata.at("genre") == "ゲーム&バラエティ");
  REQUIRE(doc.chart_tokens.count(2) == 1);
}

TEST_CASE("simai tokenizer preserves ampersands in compact maidata metadata")
{
  simai::Tokenizer tokenizer;
  const std::string doc_text =
      "&title=Rock & Roll? [DX]&genre=ゲーム&バラエティ&inote_2=(120){4}1,2,3,4,E";
  const auto doc = tokenizer.parse_document(doc_text);

  REQUIRE(doc.metadata.at("title") == "Rock & Roll? [DX]");
  REQUIRE(doc.metadata.at("genre") == "ゲーム&バラエティ");
  REQUIRE(doc.chart_tokens.count(2) == 1);
  REQUIRE_FALSE(doc.chart_tokens.at(2).empty());
}

TEST_CASE("simai tokenizer supports compact maidata without first ampersand")
{
  simai::Tokenizer tokenizer;
  const std::string doc_text =
      "title=No Prefix&inote_3=(120){4}1,2,3,4,E";
  const auto doc = tokenizer.parse_document(doc_text);

  REQUIRE(doc.metadata.at("title") == "No Prefix");
  REQUIRE(doc.chart_tokens.count(3) == 1);
  REQUIRE_FALSE(doc.chart_tokens.at(3).empty());
}

TEST_CASE("simai tokenizer strips supported comments while keeping # in "
          "duration tags")
{
  simai::Tokenizer tokenizer;
  const std::string text =
      "(120){4}1,2,3,||drop this line\n4,#drop this comment\n5h[1#2],6,\nE";
  const auto tokens = tokenizer.tokenize_text(text);

  REQUIRE(std::find(tokens.begin(), tokens.end(), "4") != tokens.end());
  REQUIRE(std::find(tokens.begin(), tokens.end(), "5h[1#2]") != tokens.end());
  REQUIRE(std::none_of(tokens.begin(), tokens.end(), [](const std::string &t)
                       { return t.find("drop") != std::string::npos || t.find("||") != std::string::npos; }));
}

TEST_CASE("simai tokenizer applies compatibility fixes from external import "
          "workflows")
{
  simai::Tokenizer tokenizer;
  const std::string text = "1{4},2(120),3qx4,5[16-3],6-?7[4:1],8[4:1]b,";
  const auto tokens = tokenizer.tokenize_text(text);

  REQUIRE(std::find(tokens.begin(), tokens.end(), "1") != tokens.end());
  REQUIRE(std::find(tokens.begin(), tokens.end(), "{4}") != tokens.end());
  REQUIRE(std::find(tokens.begin(), tokens.end(), "2") != tokens.end());
  REQUIRE(std::find(tokens.begin(), tokens.end(), "(120)") != tokens.end());
  REQUIRE(std::find(tokens.begin(), tokens.end(), "3xq4") != tokens.end());
  REQUIRE(std::find(tokens.begin(), tokens.end(), "5[16:3]") != tokens.end());
  REQUIRE(std::find(tokens.begin(), tokens.end(), "6?-7[4:1]") != tokens.end());
  REQUIRE(std::find(tokens.begin(), tokens.end(), "8b[4:1]") != tokens.end());
}

TEST_CASE("simai compose uses real export slide notation mapping")
{
  Chart chart;
  chart.bpm_changes().push_back(BpmChange{0, 0, 185.0});
  chart.measure_changes().push_back(MeasureChange{0, 0, 1, 1});

  auto add_slide = [&](NoteType type, int key, int end_key, int bar)
  {
    Note note;
    note.type = type;
    note.bar = bar;
    note.key = key;
    note.end_key = end_key;
    note.wait_ticks = 96;
    note.last_ticks = 240;
    chart.notes().push_back(note);
  };

  add_slide(NoteType::SlideQ, 2, 5, 0);
  add_slide(NoteType::SlideP, 6, 3, 1);
  add_slide(NoteType::SlideQQ, 1, 4, 2);
  add_slide(NoteType::SlidePP, 6, 3, 3);
  add_slide(NoteType::SlideCurveLeft, 5, 0, 4);
  add_slide(NoteType::SlideCurveRight, 2, 6, 5);
  add_slide(NoteType::SlideVTurnRight, 1, 5, 6);

  simai::Compiler composer;
  const std::string simai = composer.compile_chart(chart);

  REQUIRE(simai.find("{1}3?p6[8:5],\n") != std::string::npos);
  REQUIRE(simai.find("{1}7?q4[8:5],\n") != std::string::npos);
  REQUIRE(simai.find("{1}2?pp5[8:5],\n") != std::string::npos);
  REQUIRE(simai.find("{1}7?qq4[8:5],\n") != std::string::npos);
  REQUIRE(simai.find("{1}6?>1[8:5],\n") != std::string::npos);
  REQUIRE(simai.find("{1}3?<7[8:5],\n") != std::string::npos);
  REQUIRE(simai.find("{1}2?V46[8:5],\n") != std::string::npos);
}

TEST_CASE(
    "simai compose merges ex slide starts and chains contiguous slides")
{
  Chart chart;
  chart.bpm_changes().push_back(BpmChange{0, 0, 185.0});
  chart.measure_changes().push_back(MeasureChange{0, 0, 1, 1});

  Note start;
  start.type = NoteType::SlideStart;
  start.state = SpecialState::Ex;
  start.key = 1;
  chart.notes().push_back(start);

  Note first;
  first.type = NoteType::SlideStraight;
  first.bar = 0;
  first.key = 1;
  first.end_key = 6;
  first.wait_ticks = 96;
  first.last_ticks = 48;
  chart.notes().push_back(first);

  Note second;
  second.type = NoteType::SlideStraight;
  second.bar = 0;
  second.tick = 144;
  second.key = 6;
  second.end_key = 1;
  second.wait_ticks = 0;
  second.last_ticks = 48;
  chart.notes().push_back(second);

  simai::Compiler composer;
  const std::string simai = composer.compile_chart(chart);

  REQUIRE(simai.find("2x-7-2[4:1]") != std::string::npos);
  REQUIRE(simai.find("2x_/") == std::string::npos);
}

TEST_CASE("simai compose merges simultaneous ex slide starts into q-slides")
{
  Chart chart;
  chart.bpm_changes().push_back(BpmChange{0, 0, 185.0});
  chart.measure_changes().push_back(MeasureChange{0, 0, 1, 1});

  Note start_left;
  start_left.type = NoteType::SlideStart;
  start_left.state = SpecialState::Ex;
  start_left.key = 0;
  chart.notes().push_back(start_left);

  Note start_right;
  start_right.type = NoteType::SlideStart;
  start_right.state = SpecialState::Ex;
  start_right.key = 4;
  chart.notes().push_back(start_right);

  Note left_slide;
  left_slide.type = NoteType::SlideP;
  left_slide.bar = 0;
  left_slide.key = 0;
  left_slide.end_key = 6;
  left_slide.wait_ticks = 96;
  left_slide.last_ticks = 192;
  chart.notes().push_back(left_slide);

  Note right_slide;
  right_slide.type = NoteType::SlideP;
  right_slide.bar = 0;
  right_slide.key = 4;
  right_slide.end_key = 2;
  right_slide.wait_ticks = 96;
  right_slide.last_ticks = 192;
  chart.notes().push_back(right_slide);

  simai::Compiler composer;
  const std::string simai = composer.compile_chart(chart);

  REQUIRE(simai.find("1xq7[2:1]/5xq3[2:1]") != std::string::npos);
  REQUIRE(simai.find("1x_/") == std::string::npos);
  REQUIRE(simai.find("5x_/") == std::string::npos);
}

TEST_CASE(
    "simai compose chains connecting slides that keep original ma2 start key")
{
  Chart chart;
  chart.bpm_changes().push_back(BpmChange{0, 0, 185.0});
  chart.measure_changes().push_back(MeasureChange{0, 0, 1, 1});

  Note first;
  first.type = NoteType::SlideStraight;
  first.bar = 0;
  first.key = 0;
  first.end_key = 3;
  first.wait_ticks = 96;
  first.last_ticks = 48;
  chart.notes().push_back(first);

  Note second;
  second.type = NoteType::SlideStraight;
  second.state = SpecialState::ConnectingSlide;
  second.bar = 0;
  second.tick = 144;
  second.key = 0;
  second.end_key = 5;
  second.wait_ticks = 0;
  second.last_ticks = 48;
  chart.notes().push_back(second);

  simai::Compiler composer;
  const std::string simai = composer.compile_chart(chart);

  REQUIRE(simai.find("1?-4-6[4:1]") != std::string::npos);
}

TEST_CASE("simai compose inherits special slide starts without emitting "
          "underscore token")
{
  Chart chart;
  chart.bpm_changes().push_back(BpmChange{0, 0, 185.0});
  chart.measure_changes().push_back(MeasureChange{0, 0, 1, 1});

  Note slide_start;
  slide_start.type = NoteType::SlideStart;
  slide_start.state = SpecialState::Break;
  slide_start.key = 5;
  chart.notes().push_back(slide_start);

  Note slide;
  slide.type = NoteType::SlideCurveLeft;
  slide.bar = 0;
  slide.key = 5;
  slide.end_key = 3;
  slide.wait_ticks = 96;
  slide.last_ticks = 192;
  chart.notes().push_back(slide);

  simai::Compiler composer;
  const std::string simai = composer.compile_chart(chart);

  REQUIRE(simai.find("6b>4[") != std::string::npos);
  REQUIRE(simai.find("6b_/") == std::string::npos);
}

TEST_CASE("simai compose orders touch and regular notes before slides in "
          "shared slot")
{
  Chart chart;
  chart.bpm_changes().push_back(BpmChange{0, 0, 185.0});
  chart.measure_changes().push_back(MeasureChange{0, 0, 1, 1});

  Note slide;
  slide.type = NoteType::SlideStraight;
  slide.bar = 0;
  slide.key = 6;
  slide.end_key = 2;
  slide.wait_ticks = 96;
  slide.last_ticks = 48;
  chart.notes().push_back(slide);

  Note tap;
  tap.type = NoteType::Tap;
  tap.bar = 0;
  tap.key = 7;
  chart.notes().push_back(tap);

  Note touch;
  touch.type = NoteType::TouchTap;
  touch.bar = 0;
  touch.key = 4;
  touch.touch_group = "E";
  chart.notes().push_back(touch);

  simai::Compiler composer;
  const std::string simai = composer.compile_chart(chart);

  REQUIRE(simai.find("{1}E5/8/7?-3[8:1],\n") != std::string::npos);
}

TEST_CASE("simai compose does not repeat break suffix on slide end key")
{
  Chart chart;
  chart.bpm_changes().push_back(BpmChange{0, 0, 185.0});
  chart.measure_changes().push_back(MeasureChange{0, 0, 1, 1});

  Note straight;
  straight.type = NoteType::SlideStraight;
  straight.bar = 0;
  straight.key = 3;
  straight.end_key = 7;
  straight.state = SpecialState::Break;
  straight.wait_ticks = 96;
  straight.last_ticks = 48;
  chart.notes().push_back(straight);

  Note curve;
  curve.type = NoteType::SlideCurveLeft;
  curve.bar = 1;
  curve.key = 7;
  curve.end_key = 0;
  curve.state = SpecialState::Break;
  curve.wait_ticks = 96;
  curve.last_ticks = 48;
  chart.notes().push_back(curve);

  simai::Compiler composer;
  const std::string simai = composer.compile_chart(chart);

  REQUIRE(simai.find("{1}4?-8b[8:1],\n") != std::string::npos);
  REQUIRE(simai.find("{1}8?<1b[8:1],\n") != std::string::npos);
}

TEST_CASE(
    "simai compose keeps three trailing empty bars after non-slide endings")
{
  Chart chart;
  chart.bpm_changes().push_back(BpmChange{0, 0, 185.0});
  chart.measure_changes().push_back(MeasureChange{0, 0, 1, 1});

  Note tap;
  tap.type = NoteType::Tap;
  tap.bar = 0;
  tap.key = 0;
  chart.notes().push_back(tap);

  simai::Compiler composer;
  const std::string simai = composer.compile_chart(chart);

  REQUIRE(simai.ends_with("{1}1,\n{1},\n{1},\nE\n"));
}

TEST_CASE("simai compose keeps two trailing empty bars after slide endings")
{
  Chart chart;
  chart.bpm_changes().push_back(BpmChange{0, 0, 185.0});
  chart.measure_changes().push_back(MeasureChange{0, 0, 1, 1});

  Note slide;
  slide.type = NoteType::SlideStraight;
  slide.bar = 0;
  slide.key = 0;
  slide.end_key = 4;
  slide.wait_ticks = 96;
  slide.last_ticks = 48;
  chart.notes().push_back(slide);
  Note start;
  start.type = NoteType::SlideStart;
  start.bar = 0;
  start.key = 0;
  chart.notes().push_back(start);

  simai::Compiler composer;
  const std::string simai = composer.compile_chart(chart);

  REQUIRE(simai.ends_with("{1}1-5[8:1],\n{1},\n{1},\nE\n"));
}
