#include "retrieval.hpp"

#include "cancel_exception.hpp"
#include "feature_offset_match.hpp"
#include "interval_set.hpp"
#include "search_index_values.hpp"
#include "search_trie.hpp"

#include "v2/mwm_context.hpp"

#include "indexer/feature.hpp"
#include "indexer/feature_algo.hpp"
#include "indexer/index.hpp"
#include "indexer/osm_editor.hpp"
#include "indexer/scales.hpp"
#include "indexer/search_delimiters.hpp"
#include "indexer/search_string_utils.hpp"
#include "indexer/trie_reader.hpp"

#include "platform/mwm_traits.hpp"
#include "platform/mwm_version.hpp"

#include "coding/compressed_bit_vector.hpp"
#include "coding/reader_wrapper.hpp"

#include "std/algorithm.hpp"

using osm::Editor;

namespace search
{
namespace
{

unique_ptr<coding::CompressedBitVector> SortFeaturesAndBuildCBV(vector<uint64_t> && features)
{
  my::SortUnique(features);
  return coding::CompressedBitVectorBuilder::FromBitPositions(move(features));
}

/// Check that any from first matches any from second.
template <class TComp, class T>
bool IsFirstMatchesSecond(vector<T> const & first, vector<T> const & second, TComp const & comp)
{
  if (second.empty())
    return true;

  for (auto const & s : second)
  {
    for (auto const & f : first)
    {
      if (comp(f, s))
        return true;
    }
  }
  return false;
}

bool MatchFeatureByName(FeatureType const & ft, SearchQueryParams const & params)
{
  using namespace strings;

  bool matched = false;
  ft.ForEachName([&](int8_t lang, string const & utf8Name)
  {
    if (utf8Name.empty() || params.m_langs.count(lang) == 0)
      return true;

    vector<UniString> nameTokens;
    SplitUniString(NormalizeAndSimplifyString(utf8Name), MakeBackInsertFunctor(nameTokens), Delimiters());

    auto const matchPrefix = [](UniString const & s1, UniString const & s2)
    {
      return StartsWith(s1, s2);
    };
    if (!IsFirstMatchesSecond(nameTokens, params.m_prefixTokens, matchPrefix))
      return true;

    for (auto const & synonyms : params.m_tokens)
    {
      if (!IsFirstMatchesSecond(nameTokens, synonyms, equal_to<UniString>()))
        return true;
    }

    matched = true;
    return false;
  });

  return matched;
}

// Retrieves from the search index corresponding to |value| all
// features matching to |params|.
template <typename TValue>
unique_ptr<coding::CompressedBitVector> RetrieveAddressFeaturesImpl(
    MwmSet::MwmId const & id, MwmValue & value, my::Cancellable const & cancellable,
    SearchQueryParams const & params)
{
  // Exclude from search all deleted/modified features and match all edited/created features separately.
  Editor & editor = Editor::Instance();

  auto const deleted = editor.GetFeaturesByStatus(id, Editor::FeatureStatus::Deleted);
  auto const modified = editor.GetFeaturesByStatus(id, Editor::FeatureStatus::Modified);
  auto const filter = [&](uint32_t featureIndex) -> bool
  {
    return (!binary_search(deleted.begin(), deleted.end(), featureIndex) &&
            !binary_search(modified.begin(), modified.end(), featureIndex));
  };

  serial::CodingParams codingParams(trie::GetCodingParams(value.GetHeader().GetDefCodingParams()));
  ModelReaderPtr searchReader = value.m_cont.GetReader(SEARCH_INDEX_FILE_TAG);

  auto const trieRoot = trie::ReadTrie<SubReaderWrapper<Reader>, ValueList<TValue>>(
      SubReaderWrapper<Reader>(searchReader.GetPtr()), SingleValueSerializer<TValue>(codingParams));

  // TODO (@y, @m): This code may be optimized in the case where
  // bit vectors are sorted in the search index.
  vector<uint64_t> features;
  uint32_t counter = 0;
  auto const collector = [&](TValue const & value)
  {
    if ((++counter & 0xFF) == 0)
      BailIfCancelled(cancellable);
    features.push_back(value.m_featureId);
  };

  MatchFeaturesInTrie(params, *trieRoot, filter, collector);

  // Match all edited/created features separately.
  auto const matcher = [&](uint32_t featureIndex)
  {
    FeatureType ft;
    VERIFY(editor.GetEditedFeature(id, featureIndex, ft), ());
    // TODO(AlexZ): Should we match by some feature's metafields too?
    if (MatchFeatureByName(ft, params))
      features.push_back(featureIndex);
  };

  for_each(modified.begin(), modified.end(), matcher);
  auto const created = editor.GetFeaturesByStatus(id, Editor::FeatureStatus::Created);
  for_each(created.begin(), created.end(), matcher);

  return SortFeaturesAndBuildCBV(move(features));
}

// Retrieves from the geometry index corresponding to handle all
// features from |coverage|.
unique_ptr<coding::CompressedBitVector> RetrieveGeometryFeaturesImpl(
    v2::MwmContext const & context, my::Cancellable const & cancellable,
    covering::IntervalsT const & coverage, int scale)
{
  uint32_t counter = 0;
  vector<uint64_t> features;

  context.ForEachIndex(coverage, scale, [&](uint64_t featureId)
  {
    if ((++counter & 0xFF) == 0)
      BailIfCancelled(cancellable);
    features.push_back(featureId);
  });

  return SortFeaturesAndBuildCBV(move(features));
}

}  // namespace

namespace v2
{
unique_ptr<coding::CompressedBitVector> RetrieveAddressFeatures(
    MwmSet::MwmId const & id, MwmValue & value, my::Cancellable const & cancellable,
    SearchQueryParams const & params)
{
  version::MwmTraits mwmTraits(value.GetMwmVersion().GetFormat());

  if (mwmTraits.GetSearchIndexFormat() ==
      version::MwmTraits::SearchIndexFormat::FeaturesWithRankAndCenter)
  {
    using TValue = FeatureWithRankAndCenter;
    return RetrieveAddressFeaturesImpl<TValue>(id, value, cancellable, params);
  }
  else if (mwmTraits.GetSearchIndexFormat() ==
           version::MwmTraits::SearchIndexFormat::CompressedBitVector)
  {
    using TValue = FeatureIndexValue;
    return RetrieveAddressFeaturesImpl<TValue>(id, value, cancellable, params);
  }
  return unique_ptr<coding::CompressedBitVector>();
}

unique_ptr<coding::CompressedBitVector> RetrieveGeometryFeatures(
    MwmContext const & context, my::Cancellable const & cancellable,
    m2::RectD const & rect, int scale)
{
  covering::IntervalsT coverage;
  v2::CoverRect(rect, scale, coverage);
  return RetrieveGeometryFeaturesImpl(context, cancellable, coverage, scale);
}

} // namespace v2
} // namespace search
