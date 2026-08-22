// TypeAheadSearch had no tests in wotr-access; these are new, and exist because the C++ port had to
// hand-write the case folding and diacritic folding that C# got from Unicode normalization (see
// core/typeahead.h). They pin the six match tiers, the list-order/name-before-metadata ranking, and the
// repeat-letter cycle described in the class comment.
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "core/typeahead.h"

using gd::core::TypeAheadSearch;

namespace {

int tier_of(const std::string& name, const std::string& prefix) {
  int pos = -1;
  return TypeAheadSearch::match_tier(gd::core::text_util::to_lower_invariant(name),
                                     gd::core::text_util::to_lower_invariant(prefix), pos);
}

// Types `text` and runs a search, appending every announced item index to `hits`. The callback must
// outlive the search object, which keeps it for later result navigation.
void type_and_search(TypeAheadSearch& search, const std::string& text,
                     const std::vector<std::string>& items, std::vector<int>& hits) {
  for (char c : text) search.add_char(c);
  search.search(
      static_cast<int>(items.size()), [&items](int i) { return items[static_cast<std::size_t>(i)]; },
      [&hits](int index) { hits.push_back(index); });
}

}  // namespace

TEST_CASE("match tiers rank start-of-string over mid-string over substring") {
  CHECK(tier_of("Load Game", "load") == 0);   // start of string, whole word
  CHECK(tier_of("License", "lic") == 1);      // start of string, prefix
  CHECK(tier_of("New Game", "game") == 2);    // mid-string whole word
  CHECK(tier_of("New Game", "gam") == 3);     // mid-string word prefix
  CHECK(tier_of("DLC", "l") == 4);            // substring anywhere
  CHECK(tier_of("gas pipe", "ga pi") == 5);   // space-delimited abbreviation
  CHECK(tier_of("Continue", "zz") == -1);
}

TEST_CASE("a comma also delimits a whole word") {
  CHECK(tier_of("Continue, button", "continue") == 0);
  CHECK(tier_of("Continue, button", "button") == 2);
}

TEST_CASE("diacritics and case are folded away") {
  CHECK(tier_of("Caf\xC3\xA9 Menu", "cafe") == 0);  // "Café Menu"
  CHECK(tier_of("Elden R\xC3\xAFng", "ring") == 2);  // "Elden Rïng"
  CHECK(tier_of("\xC3\x86THER", "aether") == 0);     // "ÆTHER"
}

TEST_CASE("within a tier results keep list order, and repeating the letter cycles them") {
  TypeAheadSearch search;
  const std::vector<std::string> items{"Continue", "Load Game", "New Game", "License", "DLC"};
  std::vector<int> hits;

  type_and_search(search, "l", items, hits);
  REQUIRE(hits.size() == 1);
  CHECK(hits[0] == 1);  // Load Game: tier 1 and earliest in list order
  CHECK(search.result_count() == 3);

  // Repeating the letter cycles every match of it, weaker tiers last, wrapping.
  type_and_search(search, "l", items, hits);
  REQUIRE(hits.size() == 2);
  CHECK(hits[1] == 3);  // License
  type_and_search(search, "l", items, hits);
  REQUIRE(hits.size() == 3);
  CHECK(hits[2] == 4);  // DLC, on the substring tier
  type_and_search(search, "l", items, hits);
  REQUIRE(hits.size() == 4);
  CHECK(hits[3] == 1);  // wrapped back to Load Game
}

TEST_CASE("matches in the name outrank matches in the appended metadata") {
  TypeAheadSearch search;
  const std::vector<std::string> items{"Sword, legendary blade", "Blade of dawn"};
  std::vector<int> hits;

  type_and_search(search, "blade", items, hits);
  REQUIRE(hits.size() == 1);
  CHECK(hits[0] == 1);  // the pre-comma match wins even though it is later in the list
  CHECK(search.result_count() == 2);
}

TEST_CASE("an unmatched buffer reports no match and clears the results") {
  TypeAheadSearch search;
  std::string reported;
  std::vector<int> hits;
  search.on_no_match = [&reported](const std::string& buffer) { reported = buffer; };

  type_and_search(search, "zq", {"Continue", "Load Game"}, hits);
  CHECK(hits.empty());
  CHECK(reported == "zq");
  CHECK(search.result_count() == 0);
  CHECK(search.is_search_active());

  search.clear();
  CHECK_FALSE(search.has_buffer());
  CHECK_FALSE(search.is_search_active());
}

TEST_CASE("result navigation wraps in both directions") {
  TypeAheadSearch search;
  const std::vector<std::string> items{"Alpha", "Beta", "Alto"};
  std::vector<int> hits;

  type_and_search(search, "a", items, hits);
  REQUIRE(hits.size() == 1);
  CHECK(hits[0] == 0);                // Alpha
  CHECK(search.result_count() == 3);  // Alpha, Alto (tier 1), then Beta (substring)

  search.navigate_results(-1);  // wrap backwards to the last result
  CHECK(hits.back() == 1);      // Beta
  search.jump_to_first_result();
  CHECK(hits.back() == 0);
  search.jump_to_last_result();
  CHECK(hits.back() == 1);
  search.navigate_results(+1);  // wrap forwards
  CHECK(hits.back() == 0);
}
