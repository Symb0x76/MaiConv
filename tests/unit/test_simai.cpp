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