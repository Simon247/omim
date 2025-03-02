#pragma once

#include "search/v2/search_model.hpp"

#include "base/string_utils.hpp"

#include "std/vector.hpp"

namespace search
{
namespace v2
{
// This structure represents a part of search query interpretation -
// when to a substring of tokens [m_startToken, m_endToken) is matched
// with a set of m_features of the same m_type.
struct FeaturesLayer
{
  FeaturesLayer();

  void Clear();

  // Non-owning ptr to a sorted vector of features.
  vector<uint32_t> const * m_sortedFeatures;

  strings::UniString m_subQuery;

  size_t m_startToken;
  size_t m_endToken;
  SearchModel::SearchType m_type;

  // *NOTE* This field is meaningful only when m_type equals to
  // SEARCH_TYPE_BUILDING.
  //
  // When true, m_sortedFeatures contains only features retrieved from
  // search index by m_subQuery, and it's necessary for Geocoder to
  // perform additional work to retrieve features matching by house
  // number.
  bool m_hasDelayedFeatures;

  bool m_lastTokenIsPrefix;
};

string DebugPrint(FeaturesLayer const & layer);
}  // namespace v2
}  // namespace search
