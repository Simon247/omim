#include "testing/testing.hpp"

#include "search/keyword_matcher.hpp"
#include "search/search_common.hpp"

#include "indexer/search_delimiters.hpp"
#include "indexer/search_string_utils.hpp"

#include "base/buffer_vector.hpp"
#include "base/stl_add.hpp"

#include "std/sstream.hpp"
#include "std/vector.hpp"

namespace
{

using search::KeywordMatcher;
using search::MAX_TOKENS;

enum ExpectedMatchResult
{
  NOMATCH,
  MATCHES,
  ANY_RES
};

enum ExpectedScoreComparison
{
  DOES_NOT_MATTER,  // Score does not matter.
  PERFECTLY_EQUAL,  // Matches with the score == previous.
  BETTER_OR_EQUAL,  // Matches with the score <= previous.
  STRONGLY_BETTER   // Matched with the score <  previous.
};

struct KeywordMatcherTestCase
{
  ExpectedMatchResult m_eMatch;
  ExpectedScoreComparison m_eMatchType;
  char const * m_name;
};

void InitMatcher(char const * query, KeywordMatcher & matcher)
{
  vector<strings::UniString> keywords;
  strings::UniString prefix;
  if (search::TokenizeStringAndCheckIfLastTokenIsPrefix(query, keywords, search::Delimiters()))
  {
    prefix = keywords.back();
    keywords.pop_back();
  }

  matcher.SetKeywords(&keywords[0], keywords.size(), prefix);
}

class TestScore
{
  typedef KeywordMatcher::ScoreT ScoreT;
  ScoreT m_score;

public:
  TestScore() {}
  TestScore(ScoreT const & score) : m_score(score) {}

  bool operator<(TestScore const & s) const
  {
    if (m_score < s.m_score)
      return true;
    return m_score.LessInTokensLength(s.m_score);
  }

  bool IsQueryMatched() const { return m_score.IsQueryMatched(); }

  friend string DebugPrint(TestScore const & score);
};

string DebugPrint(TestScore const & s)
{
  return DebugPrint(s.m_score);
}

template <size_t N>
void TestKeywordMatcher(char const * const query, KeywordMatcherTestCase const (&testCases)[N])
{
  KeywordMatcher matcher;
  InitMatcher(query, matcher);

  TestScore prevScore;
  for (size_t i = 0; i < N; ++i)
  {
    char const * const name = testCases[i].m_name;
    char const * const prevName = (i == 0 ? "N/A" : testCases[i-1].m_name);
    TestScore const testScore = matcher.Score(name);

    // Test that a newly created matcher returns the same result
    {
      KeywordMatcher freshMatcher;
      InitMatcher(query, freshMatcher);

      TestScore const freshScore = freshMatcher.Score(name);
      // TEST_EQUAL(testScore, freshScore, (query, name));
      TEST(!(testScore < freshScore), (query, name));
      TEST(!(freshScore < testScore), (query, name));
    }

    if (testCases[i].m_eMatch != ANY_RES)
    {
      TEST_EQUAL(testCases[i].m_eMatch == MATCHES,
                 testScore.IsQueryMatched(),
                 (query, name, testScore));
    }

    switch (testCases[i].m_eMatchType)
    {
    case DOES_NOT_MATTER:
      break;
    case PERFECTLY_EQUAL:
      TEST(!(testScore < prevScore), (query, name, testScore, prevName, prevScore));
      TEST(!(prevScore < testScore), (query, name, testScore, prevName, prevScore));
      break;
    case BETTER_OR_EQUAL:
      TEST(!(testScore < prevScore), (query, name, testScore, prevName, prevScore));
      break;
    case STRONGLY_BETTER:
      TEST(prevScore < testScore, (query, name, testScore, prevName, prevScore));
      break;
    default:
      ASSERT(false, ());
    }

    prevScore = testScore;
  }
}

}  // unnamed namespace


UNIT_TEST(KeywordMatcher_Prefix)
{
  char const query[] = "new";
  KeywordMatcherTestCase const testCases[] =
  {
    {NOMATCH, DOES_NOT_MATTER, ""},
    {NOMATCH, DOES_NOT_MATTER, "zzz"},
    {NOMATCH, DOES_NOT_MATTER, "ne"},

    {MATCHES, STRONGLY_BETTER, "the newark"},
    {MATCHES, BETTER_OR_EQUAL, "york new"},

    {MATCHES, STRONGLY_BETTER, "new york gym"},
    {MATCHES, BETTER_OR_EQUAL, "new new york"},
    {MATCHES, STRONGLY_BETTER, "new york"},

    {MATCHES, STRONGLY_BETTER, "newark"},

    {MATCHES, STRONGLY_BETTER, "new"},
  };
  TestKeywordMatcher(query, testCases);
}

UNIT_TEST(KeywordMatcher_Keyword)
{
  char const query[] = "new ";
  KeywordMatcherTestCase const testCases[] =
  {
    {NOMATCH, DOES_NOT_MATTER, ""},
    {NOMATCH, DOES_NOT_MATTER, "zzz"},
    {NOMATCH, DOES_NOT_MATTER, "ne"},
    {NOMATCH, DOES_NOT_MATTER, "the netherlands"},
    {NOMATCH, DOES_NOT_MATTER, "newark"},

    {MATCHES, STRONGLY_BETTER, "york new"},

    {MATCHES, STRONGLY_BETTER, "new york gym"},
    {MATCHES, BETTER_OR_EQUAL, "new new york"},

    {MATCHES, STRONGLY_BETTER, "new york"},
  };
  TestKeywordMatcher(query, testCases);
}

UNIT_TEST(KeywordMatcher_SanSa_ShouldMatch_SanSalvador_BetterThan_San)
{
  char const query[] = "San Sa";

  KeywordMatcherTestCase const testCases[] =
  {
    {NOMATCH, DOES_NOT_MATTER, "San"},
    {MATCHES, STRONGLY_BETTER, "San Salvador"},
  };
  TestKeywordMatcher(query, testCases);
}

UNIT_TEST(KeywordMatcher_KeywordAndPrefix)
{
  char const query[] = "new yo";

  KeywordMatcherTestCase const testCases[] =
  {
    {NOMATCH, DOES_NOT_MATTER, "new"},
    {NOMATCH, DOES_NOT_MATTER, "new old"},
    {NOMATCH, DOES_NOT_MATTER, "old york"},

    {MATCHES, STRONGLY_BETTER, "the york new"},

    {MATCHES, STRONGLY_BETTER, "the new york"},
    {MATCHES, BETTER_OR_EQUAL, "york new the"},
    {MATCHES, BETTER_OR_EQUAL, "york new"},

    {MATCHES, STRONGLY_BETTER, "yo new"},

    {MATCHES, STRONGLY_BETTER, "new york pizza"},

    {MATCHES, STRONGLY_BETTER, "new york"},

    {MATCHES, STRONGLY_BETTER, "new yo"},
  };
  TestKeywordMatcher(query, testCases);
}

UNIT_TEST(KeywordMatcher_KeywordAndKeyword)
{
  char const query[] = "new york ";

  KeywordMatcherTestCase const testCases[] =
  {
    {NOMATCH, DOES_NOT_MATTER, "new"},
    {NOMATCH, DOES_NOT_MATTER, "new old"},
    {NOMATCH, DOES_NOT_MATTER, "old york"},
    {NOMATCH, DOES_NOT_MATTER, "new yorkshire"},
    {NOMATCH, DOES_NOT_MATTER, "york newcastle"},

    {MATCHES, STRONGLY_BETTER, "the york new"},

    {MATCHES, STRONGLY_BETTER, "the new york"},
    {MATCHES, BETTER_OR_EQUAL, "york new the"},

    {MATCHES, STRONGLY_BETTER, "york new"},

    {MATCHES, STRONGLY_BETTER, "new york pizza"},
    {MATCHES, STRONGLY_BETTER, "new york"},
  };
  TestKeywordMatcher(query, testCases);
}


namespace
{

string GetManyTokens(string tokenPrefix, int tokenCount, bool countForward = true)
{
  ostringstream out;
  for (int i = 0; i < tokenCount; ++i)
    out << tokenPrefix << (countForward ? i : tokenCount - 1 - i) << " ";
  return out.str();
}

}  // unnamed namespace

UNIT_TEST(KeywordMatcher_QueryTooLong)
{
  for (int queryLength = MAX_TOKENS - 2; queryLength <= MAX_TOKENS + 2; ++queryLength)
  {
    string const query = GetManyTokens("Q", queryLength);
    string const queryWithPrefix = query + " Prefix";
    string const queryWithPrefixAndSomethingElse = query + " PrefixAndSomethingElse";

    KeywordMatcherTestCase const testCases[] =
    {
      {NOMATCH, DOES_NOT_MATTER, ""},
      {NOMATCH, DOES_NOT_MATTER, "Q"},
      {NOMATCH, DOES_NOT_MATTER, "Q "},
      {NOMATCH, DOES_NOT_MATTER, "Q3"},
      {NOMATCH, DOES_NOT_MATTER, "Q3 "},
      {NOMATCH, DOES_NOT_MATTER, "Q3 Q"},
      {NOMATCH, DOES_NOT_MATTER, "Q3 Q4"},
      {NOMATCH, DOES_NOT_MATTER, "zzz"},

      {NOMATCH, DOES_NOT_MATTER, "Q"},
      {ANY_RES, STRONGLY_BETTER, query.c_str()},

      {NOMATCH, DOES_NOT_MATTER, "Q"},
      {ANY_RES, STRONGLY_BETTER, queryWithPrefix.c_str()},

      {NOMATCH, DOES_NOT_MATTER, "Q"},
      {ANY_RES, STRONGLY_BETTER, queryWithPrefixAndSomethingElse.c_str()},
    };
    TestKeywordMatcher(query.c_str(), testCases);
    TestKeywordMatcher(queryWithPrefix.c_str(), testCases);
  }
}

UNIT_TEST(KeywordMatcher_NameTooLong)
{
  string const name[] =
  {
    "Aa Bb " + GetManyTokens("T", MAX_TOKENS + 1),
    "Aa Bb " + GetManyTokens("T", MAX_TOKENS),
    "Aa Bb " + GetManyTokens("T", MAX_TOKENS - 1),
  };

  KeywordMatcherTestCase const testCases[] =
  {
    {NOMATCH, DOES_NOT_MATTER, "zzz"},

    {MATCHES, STRONGLY_BETTER, name[0].c_str()},
    {MATCHES, BETTER_OR_EQUAL, name[1].c_str()},
    {MATCHES, BETTER_OR_EQUAL, name[2].c_str()},
  };

  char const * query[] = { "a", "aa", "aa ", "b", "bb", "bb ", "t" };
  for (int i = 0; i < ARRAY_SIZE(query); ++i)
    TestKeywordMatcher(query[i], testCases);
}

UNIT_TEST(KeywordMatcher_ManyTokensInReverseOrder)
{
  string const query = GetManyTokens("Q", MAX_TOKENS);
  string const name = GetManyTokens("Q", MAX_TOKENS);
  string const reversedName = GetManyTokens("Q", MAX_TOKENS, false);

  KeywordMatcherTestCase const testCases[] =
  {
    {NOMATCH, DOES_NOT_MATTER, "zzz"},

    {MATCHES, STRONGLY_BETTER, reversedName.c_str()},

    {MATCHES, STRONGLY_BETTER, name.c_str()},
  };
  TestKeywordMatcher(query.c_str(), testCases);
}

UNIT_TEST(KeywordMatcher_DifferentLangs)
{
  KeywordMatcher matcher;

  InitMatcher("не", matcher);

  char const * arr[] = { "Невский переулок", "Неўскі завулак" };
  TEST(!(matcher.Score(arr[0]) < matcher.Score(arr[1])), ());
  TEST(!(matcher.Score(arr[1]) < matcher.Score(arr[0])), ());
}
