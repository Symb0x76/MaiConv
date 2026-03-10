#include "maiconv/core/simai.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace maiconv;

TEST_CASE("simai tokenizer handles inote document") {
  SimaiTokenizer tokenizer;
  const auto doc = tokenizer.parse_document("&title=x&inote_3=(120)1,2,3,");
  REQUIRE(doc.chart_tokens.count(3) == 1);
  REQUIRE(doc.chart_tokens.at(3).size() >= 3);
}

TEST_CASE("simai parser basic notes") {
  SimaiTokenizer tokenizer;
  SimaiParser parser;
  const auto tokens = tokenizer.tokenize_text("(120){4}1,2h[4:1],3-5[16:1],");
  Chart chart = parser.parse_tokens(tokens);
  REQUIRE(chart.notes().size() >= 3);
}

TEST_CASE("simai cross-bpm slide duration uses action bpm") {
  SimaiTokenizer tokenizer;
  SimaiParser parser;
  const auto tokens = tokenizer.tokenize_text("(199)1-2[16:1],(12.4375),,");
  Chart chart = parser.parse_tokens(tokens);
  REQUIRE_FALSE(chart.notes().empty());
  const auto slide = chart.notes().front();
  REQUIRE(is_slide_type(slide.type));
  REQUIRE(slide.last_ticks <= 2);
}

TEST_CASE("simai compose can be parsed back") {
  SimaiTokenizer tokenizer;
  SimaiParser parser;
  SimaiComposer composer;

  const auto tokens = tokenizer.tokenize_text("(120){4}1,2,3,4,");
  Chart chart = parser.parse_tokens(tokens);
  const std::string simai = composer.compose_chart(chart);
  Chart parsed = parser.parse_tokens(tokenizer.tokenize_text(simai));
  REQUIRE(parsed.notes().size() >= 4);
}

TEST_CASE("simai compose emits canonical maidata style bars and beat durations") {
  SimaiTokenizer tokenizer;
  SimaiParser parser;
  SimaiComposer composer;

  const auto tokens = tokenizer.tokenize_text("(185){1}, {1}, {1}1x, {1}1-5[4:3],");
  Chart chart = parser.parse_tokens(tokens);
  const std::string simai = composer.compose_chart(chart);

  REQUIRE(simai.find("(185){1},\n") != std::string::npos);
  REQUIRE(simai.find("{1}1x,\n") != std::string::npos);
  REQUIRE(simai.find("{1}1-5[4:3],\n") != std::string::npos);
  REQUIRE(simai.find("##") == std::string::npos);
  REQUIRE(simai.ends_with("E"));
}

TEST_CASE("simai tokenizer preserves ampersands in line-based metadata") {
  SimaiTokenizer tokenizer;
  const std::string doc_text = "&title=Rock & Roll\n&genre=ゲーム&バラエティ\n&inote_2=\n(120){4}1,2,3,4,\nE\n";
  const auto doc = tokenizer.parse_document(doc_text);
  REQUIRE(doc.metadata.at("title") == "Rock & Roll");
  REQUIRE(doc.metadata.at("genre") == "ゲーム&バラエティ");
  REQUIRE(doc.chart_tokens.count(2) == 1);
}

TEST_CASE("simai compose uses real export slide notation mapping") {
  Chart chart;
  chart.bpm_changes().push_back(BpmChange{ 0, 0, 185.0 });
  chart.measure_changes().push_back(MeasureChange{ 0, 0, 1, 1 });

  auto add_slide = [&](NoteType type, int key, int end_key, int bar) {
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

  SimaiComposer composer;
  const std::string simai = composer.compose_chart(chart);

  REQUIRE(simai.find("{1}3p6[8:5],\n") != std::string::npos);
  REQUIRE(simai.find("{1}7q4[8:5],\n") != std::string::npos);
  REQUIRE(simai.find("{1}2pp5[8:5],\n") != std::string::npos);
  REQUIRE(simai.find("{1}7qq4[8:5],\n") != std::string::npos);
  REQUIRE(simai.find("{1}6>1[8:5],\n") != std::string::npos);
  REQUIRE(simai.find("{1}3<7[8:5],\n") != std::string::npos);
  REQUIRE(simai.find("{1}2V46[8:5],\n") != std::string::npos);
}

TEST_CASE("simai compose keeps special slide starts and chains contiguous slides") {
  Chart chart;
  chart.bpm_changes().push_back(BpmChange{ 0, 0, 185.0 });
  chart.measure_changes().push_back(MeasureChange{ 0, 0, 1, 1 });

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

  SimaiComposer composer;
  const std::string simai = composer.compose_chart(chart);

  REQUIRE(simai.find("2x_/2-7[8:1]*-2[8:1]") != std::string::npos);
}

TEST_CASE("simai compose chains connecting slides that keep original ma2 start key") {
  Chart chart;
  chart.bpm_changes().push_back(BpmChange{ 0, 0, 185.0 });
  chart.measure_changes().push_back(MeasureChange{ 0, 0, 1, 1 });

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

  SimaiComposer composer;
  const std::string simai = composer.compose_chart(chart);

  REQUIRE(simai.find("1-4[8:1]*-6[8:1]") != std::string::npos);
}

TEST_CASE("simai compose inherits special slide starts without emitting underscore token") {
  Chart chart;
  chart.bpm_changes().push_back(BpmChange{ 0, 0, 185.0 });
  chart.measure_changes().push_back(MeasureChange{ 0, 0, 1, 1 });

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

  SimaiComposer composer;
  const std::string simai = composer.compose_chart(chart);

  REQUIRE(simai.find("6b<4[4:1]") != std::string::npos);
  REQUIRE(simai.find("6b_/") == std::string::npos);
}

TEST_CASE("simai compose orders touch and regular notes before slides in shared slot") {
  Chart chart;
  chart.bpm_changes().push_back(BpmChange{ 0, 0, 185.0 });
  chart.measure_changes().push_back(MeasureChange{ 0, 0, 1, 1 });

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

  SimaiComposer composer;
  const std::string simai = composer.compose_chart(chart);

  REQUIRE(simai.find("{1}E5/8/7-3[8:1],\n") != std::string::npos);
}

TEST_CASE("simai compose repeats break suffix for slides ending on 1 or 8") {
  Chart chart;
  chart.bpm_changes().push_back(BpmChange{ 0, 0, 185.0 });
  chart.measure_changes().push_back(MeasureChange{ 0, 0, 1, 1 });

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

  SimaiComposer composer;
  const std::string simai = composer.compose_chart(chart);

  REQUIRE(simai.find("{1}4b-8b[8:1],\n") != std::string::npos);
  REQUIRE(simai.find("{1}8b<1b[8:1],\n") != std::string::npos);
}

TEST_CASE("simai compose keeps three trailing empty bars after non-slide endings") {
  Chart chart;
  chart.bpm_changes().push_back(BpmChange{ 0, 0, 185.0 });
  chart.measure_changes().push_back(MeasureChange{ 0, 0, 1, 1 });

  Note tap;
  tap.type = NoteType::Tap;
  tap.bar = 0;
  tap.key = 0;
  chart.notes().push_back(tap);

  SimaiComposer composer;
  const std::string simai = composer.compose_chart(chart);

  REQUIRE(simai.ends_with("{1}1,\n{1},\n{1},\n{1},\nE"));
}

TEST_CASE("simai compose keeps two trailing empty bars after slide endings") {
  Chart chart;
  chart.bpm_changes().push_back(BpmChange{ 0, 0, 185.0 });
  chart.measure_changes().push_back(MeasureChange{ 0, 0, 1, 1 });

  Note slide;
  slide.type = NoteType::SlideStraight;
  slide.bar = 0;
  slide.key = 0;
  slide.end_key = 4;
  slide.wait_ticks = 96;
  slide.last_ticks = 48;
  chart.notes().push_back(slide);

  SimaiComposer composer;
  const std::string simai = composer.compose_chart(chart);

  REQUIRE(simai.ends_with("{1}1-5[8:1],\n{1},\n{1},\nE"));
}