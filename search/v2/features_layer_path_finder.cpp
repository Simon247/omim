#include "search/v2/features_layer_path_finder.hpp"

#include "search/cancel_exception.hpp"
#include "search/v2/features_layer_matcher.hpp"
#include "search/v2/house_numbers_matcher.hpp"

#include "indexer/features_vector.hpp"

#include "base/cancellable.hpp"

namespace search
{
namespace v2
{
namespace
{
using TParentGraph = unordered_map<uint32_t, uint32_t>;

// This function tries to estimate amount of work needed to perform an
// intersection pass on a sequence of layers.
template <typename TIt>
uint64_t CalcPassCost(TIt begin, TIt end)
{
  uint64_t cost = 0;

  if (begin == end)
    return cost;

  uint64_t reachable = max((*begin)->m_sortedFeatures->size(), size_t(1));
  for (++begin; begin != end; ++begin)
  {
    uint64_t const layer = max((*begin)->m_sortedFeatures->size(), size_t(1));
    cost += layer * reachable;
    reachable = min(reachable, layer);
  }
  return cost;
}

uint64_t CalcTopDownPassCost(vector<FeaturesLayer const *> const & layers)
{
  return CalcPassCost(layers.rbegin(), layers.rend());
}

uint64_t CalcBottomUpPassCost(vector<FeaturesLayer const *> const & layers)
{
  return CalcPassCost(layers.begin(), layers.end());
}

bool LooksLikeHouseNumber(strings::UniString const & query, bool queryIsPrefix)
{
  vector<Parse> parses;
  ParseQuery(query, queryIsPrefix, parses);
  for (auto const & parse : parses)
  {
    if (parse.IsEmpty())
      continue;
    if (feature::IsHouseNumber(parse.m_parts.front()))
      return true;
  }
  return false;
}

bool GetPath(uint32_t id, vector<FeaturesLayer const *> const & layers, TParentGraph const & parent,
             IntersectionResult & result)
{
  result.Clear();

  size_t level = 0;
  TParentGraph::const_iterator it;
  do
  {
    result.Set(layers[level]->m_type, id);
    ++level;
    it = parent.find(id);
    if (it != parent.cend())
      id = it->second;
  } while (level < layers.size() && it != parent.cend());
  return level == layers.size();
}
}  // namespace

FeaturesLayerPathFinder::FeaturesLayerPathFinder(my::Cancellable const & cancellable)
  : m_cancellable(cancellable)
{
}

void FeaturesLayerPathFinder::FindReachableVertices(FeaturesLayerMatcher & matcher,
                                                    vector<FeaturesLayer const *> const & layers,
                                                    vector<IntersectionResult> & results)
{
  if (layers.empty())
    return;

  uint64_t const topDownCost = CalcTopDownPassCost(layers);
  uint64_t const bottomUpCost = CalcBottomUpPassCost(layers);

  if (bottomUpCost < topDownCost)
    FindReachableVerticesBottomUp(matcher, layers, results);
  else
    FindReachableVerticesTopDown(matcher, layers, results);
}

void FeaturesLayerPathFinder::FindReachableVerticesTopDown(
    FeaturesLayerMatcher & matcher, vector<FeaturesLayer const *> const & layers,
    vector<IntersectionResult> & results)
{
  ASSERT(!layers.empty(), ());

  vector<uint32_t> reachable = *(layers.back()->m_sortedFeatures);
  vector<uint32_t> buffer;

  TParentGraph parent(layers.size() - 1);

  auto addEdge = [&](uint32_t childFeature, uint32_t parentFeature)
  {
    parent[childFeature] = parentFeature;
    buffer.push_back(childFeature);
  };

  for (size_t i = layers.size() - 1; i != 0; --i)
  {
    BailIfCancelled(m_cancellable);

    if (reachable.empty())
      return;

    FeaturesLayer parent(*layers[i]);
    if (i != layers.size() - 1)
      my::SortUnique(reachable);
    parent.m_sortedFeatures = &reachable;
    parent.m_hasDelayedFeatures = false;

    FeaturesLayer child(*layers[i - 1]);
    child.m_hasDelayedFeatures = child.m_type == SearchModel::SEARCH_TYPE_BUILDING &&
                                 LooksLikeHouseNumber(child.m_subQuery, child.m_lastTokenIsPrefix);

    buffer.clear();
    matcher.Match(child, parent, addEdge);
    reachable.swap(buffer);
  }

  IntersectionResult result;
  for (auto const & id : reachable)
  {
    if (GetPath(id, layers, parent, result))
      results.push_back(result);
  }
}

void FeaturesLayerPathFinder::FindReachableVerticesBottomUp(
    FeaturesLayerMatcher & matcher, vector<FeaturesLayer const *> const & layers,
    vector<IntersectionResult> & results)
{
  ASSERT(!layers.empty(), ());

  vector<uint32_t> reachable = *(layers.front()->m_sortedFeatures);
  vector<uint32_t> buffer;

  TParentGraph parent(layers.size() - 1);

  auto addEdge = [&](uint32_t childFeature, uint32_t parentFeature)
  {
    parent[childFeature] = parentFeature;
    buffer.push_back(parentFeature);
  };

  for (size_t i = 0; i + 1 != layers.size(); ++i)
  {
    BailIfCancelled(m_cancellable);

    if (reachable.empty())
      return;

    FeaturesLayer child(*layers[i]);
    if (i != 0)
      my::SortUnique(reachable);
    child.m_sortedFeatures = &reachable;
    child.m_hasDelayedFeatures = false;

    FeaturesLayer parent(*layers[i + 1]);
    parent.m_hasDelayedFeatures =
        parent.m_type == SearchModel::SEARCH_TYPE_BUILDING &&
        LooksLikeHouseNumber(parent.m_subQuery, parent.m_lastTokenIsPrefix);

    buffer.clear();
    matcher.Match(child, parent, addEdge);
    reachable.swap(buffer);
  }

  IntersectionResult result;
  for (auto const & id : *(layers.front()->m_sortedFeatures))
  {
    if (GetPath(id, layers, parent, result))
      results.push_back(result);
  }
}
}  // namespace v2
}  // namespace search
