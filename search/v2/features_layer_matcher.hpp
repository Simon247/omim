#pragma once

#include "search/cancel_exception.hpp"
#include "search/reverse_geocoder.hpp"
#include "search/v2/features_layer.hpp"
#include "search/v2/house_numbers_matcher.hpp"
#include "search/v2/mwm_context.hpp"
#include "search/v2/search_model.hpp"
#include "search/v2/street_vicinity_loader.hpp"

#include "indexer/feature.hpp"
#include "indexer/feature_algo.hpp"
#include "indexer/feature_impl.hpp"
#include "indexer/features_vector.hpp"
#include "indexer/ftypes_matcher.hpp"
#include "indexer/mwm_set.hpp"

#include "geometry/mercator.hpp"
#include "geometry/point2d.hpp"
#include "geometry/rect2d.hpp"

#include "base/cancellable.hpp"
#include "base/logging.hpp"
#include "base/macros.hpp"
#include "base/stl_helpers.hpp"
#include "base/string_utils.hpp"

#include "std/algorithm.hpp"
#include "std/bind.hpp"
#include "std/limits.hpp"
#include "std/unordered_map.hpp"
#include "std/vector.hpp"

class Index;

namespace search
{
namespace v2
{
// This class performs pairwise intersection between two layers of
// features, where the first (child) layer is geographically smaller
// than the second (parent) one.  It emits all pairs
// (feature-from-child-layer, feature-from-parent-layer) of matching
// features, where feature-from-child-layer belongs-to
// feature-from-parent-layer.  Belongs-to is a partial relation on
// features, and has different meaning for different search classes:
//
// * BUILDING belongs-to STREET iff the building is located on the street;
// * BUILDING belongs-to CITY iff the building is located in the city;
// * POI belongs-to BUILDING iff the poi is (roughly) located near or inside the building;
// * STREET belongs-to CITY iff the street is (roughly) located in the city;
// * etc.
//
// NOTE: this class *IS NOT* thread-safe.
class FeaturesLayerMatcher
{
public:
  static uint32_t const kInvalidId = numeric_limits<uint32_t>::max();
  static int constexpr kBuildingRadiusMeters = 50;
  static int constexpr kStreetRadiusMeters = 100;

  FeaturesLayerMatcher(Index & index, my::Cancellable const & cancellable);
  void SetContext(MwmContext * context);

  template <typename TFn>
  void Match(FeaturesLayer const & child, FeaturesLayer const & parent, TFn && fn)
  {
    if (child.m_type >= parent.m_type)
      return;
    switch (parent.m_type)
    {
    case SearchModel::SEARCH_TYPE_POI:
    case SearchModel::SEARCH_TYPE_CITY:
    case SearchModel::SEARCH_TYPE_VILLAGE:
    case SearchModel::SEARCH_TYPE_STATE:
    case SearchModel::SEARCH_TYPE_COUNTRY:
    case SearchModel::SEARCH_TYPE_UNCLASSIFIED:
    case SearchModel::SEARCH_TYPE_COUNT:
      ASSERT(false, ("Invalid parent layer type:", parent.m_type));
      break;
    case SearchModel::SEARCH_TYPE_BUILDING:
      ASSERT_EQUAL(child.m_type, SearchModel::SEARCH_TYPE_POI, ());
      MatchPOIsWithBuildings(child, parent, forward<TFn>(fn));
      break;
    case SearchModel::SEARCH_TYPE_STREET:
      ASSERT(child.m_type == SearchModel::SEARCH_TYPE_POI ||
             child.m_type == SearchModel::SEARCH_TYPE_BUILDING,
             ("Invalid child layer type:", child.m_type));
      if (child.m_type == SearchModel::SEARCH_TYPE_POI)
        MatchPOIsWithStreets(child, parent, forward<TFn>(fn));
      else
        MatchBuildingsWithStreets(child, parent, forward<TFn>(fn));
      break;
    }
  }

  void OnQueryFinished();

private:
  template <typename TFn>
  void MatchPOIsWithBuildings(FeaturesLayer const & child, FeaturesLayer const & parent, TFn && fn)
  {
    // Following code initially loads centers of POIs and then, for
    // each building, tries to find all POIs located at distance less
    // than kBuildingRadiusMeters.

    ASSERT_EQUAL(child.m_type, SearchModel::SEARCH_TYPE_POI, ());
    ASSERT_EQUAL(parent.m_type, SearchModel::SEARCH_TYPE_BUILDING, ());

    auto const & pois = *child.m_sortedFeatures;
    auto const & buildings = *parent.m_sortedFeatures;

    BailIfCancelled(m_cancellable);

    vector<m2::PointD> poiCenters(pois.size());

    size_t const numPOIs = pois.size();
    vector<bool> isPOIProcessed(numPOIs);
    size_t processedPOIs = 0;

    for (size_t i = 0; i < pois.size(); ++i)
    {
      FeatureType poiFt;
      GetByIndex(pois[i], poiFt);
      poiCenters[i] = feature::GetCenter(poiFt, FeatureType::WORST_GEOMETRY);
    }

    for (size_t i = 0; i < buildings.size() && processedPOIs != numPOIs; ++i)
    {
      BailIfCancelled(m_cancellable);

      FeatureType buildingFt;
      GetByIndex(buildings[i], buildingFt);

      for (size_t j = 0; j < pois.size(); ++j)
      {
        if (isPOIProcessed[j])
          continue;

        double const distMeters = feature::GetMinDistanceMeters(buildingFt, poiCenters[j]);
        if (distMeters <= kBuildingRadiusMeters)
        {
          fn(pois[j], buildings[i]);
          isPOIProcessed[j] = true;
          ++processedPOIs;
        }
      }
    }

    if (!parent.m_hasDelayedFeatures)
      return;

    // |buildings| doesn't contain buildings matching by house number,
    // so following code reads buildings in POIs vicinities and checks
    // house numbers.
    vector<Parse> queryParses;
    ParseQuery(parent.m_subQuery, parent.m_lastTokenIsPrefix, queryParses);
    if (queryParses.empty())
      return;

    for (size_t i = 0; i < pois.size(); ++i)
    {
      m_context->ForEachFeature(
            MercatorBounds::RectByCenterXYAndSizeInMeters(poiCenters[i], kBuildingRadiusMeters),
            [&](FeatureType & ft)
            {
              if (HouseNumbersMatch(strings::MakeUniString(ft.GetHouseNumber()), queryParses))
              {
                double const distanceM = MercatorBounds::DistanceOnEarth(feature::GetCenter(ft), poiCenters[i]);
                if (distanceM < kBuildingRadiusMeters)
                  fn(pois[i], ft.GetID().m_index);
              }
            });
    }
  }

  template <typename TFn>
  void MatchPOIsWithStreets(FeaturesLayer const & child, FeaturesLayer const & parent, TFn && fn)
  {
    ASSERT_EQUAL(child.m_type, SearchModel::SEARCH_TYPE_POI, ());
    ASSERT_EQUAL(parent.m_type, SearchModel::SEARCH_TYPE_STREET, ());

    auto const & pois = *child.m_sortedFeatures;
    auto const & streets = *parent.m_sortedFeatures;

    // When the number of POIs is less than the number of STREETs,
    // it's faster to check nearby streets for POIs.
    if (pois.size() < streets.size())
    {
      for (uint32_t poiId : pois)
      {
        for (auto const & street : GetNearbyStreets(poiId))
        {
          if (street.m_distanceMeters > kStreetRadiusMeters)
            break;

          uint32_t const streetId = street.m_id.m_index;
          if (binary_search(streets.begin(), streets.end(), streetId))
            fn(poiId, streetId);
        }
      }
      return;
    }

    for (uint32_t streetId : streets)
    {
      BailIfCancelled(m_cancellable);
      m_loader.ForEachInVicinity(streetId, pois, kStreetRadiusMeters,
                                 bind(fn, _1, streetId));
    }
  }

  template <typename TFn>
  void MatchBuildingsWithStreets(FeaturesLayer const & child, FeaturesLayer const & parent,
                                 TFn && fn)
  {
    ASSERT_EQUAL(child.m_type, SearchModel::SEARCH_TYPE_BUILDING, ());
    ASSERT_EQUAL(parent.m_type, SearchModel::SEARCH_TYPE_STREET, ());

    auto const & buildings = *child.m_sortedFeatures;
    auto const & streets = *parent.m_sortedFeatures;

    // When all buildings are in |buildings| and the number of
    // buildings less than the number of streets, it's probably faster
    // to check nearby streets for each building instead of street
    // vicinities loading.
    if (!child.m_hasDelayedFeatures && buildings.size() < streets.size())
    {
      for (uint32_t const houseId : buildings)
      {
        uint32_t const streetId = GetMatchingStreet(houseId);
        if (binary_search(streets.begin(), streets.end(), streetId))
          fn(houseId, streetId);
      }
      return;
    }

    vector<Parse> queryParses;
    ParseQuery(child.m_subQuery, child.m_lastTokenIsPrefix, queryParses);

    uint32_t numFilterInvocations = 0;
    auto houseNumberFilter = [&](uint32_t id, FeatureType & feature, bool & loaded) -> bool
    {
      ++numFilterInvocations;
      if ((numFilterInvocations & 0xFF) == 0)
        BailIfCancelled(m_cancellable);

      if (binary_search(buildings.begin(), buildings.end(), id))
        return true;

      // HouseNumbersMatch() calls are expensive, so following code
      // tries to reduce the number of calls. The most important
      // optimization: as first tokens from the house-number part of
      // the query and feature's house numbers must be numbers, their
      // first symbols must be the same.

      if (!loaded)
      {
        GetByIndex(id, feature);
        loaded = true;
      }

      if (!child.m_hasDelayedFeatures)
        return false;

      strings::UniString const houseNumber(strings::MakeUniString(feature.GetHouseNumber()));
      if (!feature::IsHouseNumber(houseNumber))
        return false;
      return HouseNumbersMatch(houseNumber, queryParses);
    };

    unordered_map<uint32_t, bool> cache;
    auto cachingHouseNumberFilter = [&](uint32_t id, FeatureType & feature, bool & loaded) -> bool
    {
      auto const it = cache.find(id);
      if (it != cache.cend())
        return it->second;
      bool const result = houseNumberFilter(id, feature, loaded);
      cache[id] = result;
      return result;
    };

    ProjectionOnStreet proj;
    for (uint32_t streetId : streets)
    {
      BailIfCancelled(m_cancellable);
      StreetVicinityLoader::Street const & street = m_loader.GetStreet(streetId);
      if (street.IsEmpty())
        continue;

      auto const & calculator = *street.m_calculator;

      for (uint32_t houseId : street.m_features)
      {
        FeatureType feature;
        bool loaded = false;
        if (!cachingHouseNumberFilter(houseId, feature, loaded))
          continue;

        if (!loaded)
          GetByIndex(houseId, feature);

        // Best geometry is used here as feature::GetCenter(feature)
        // actually modifies internal state of a |feature| by caching
        // it's geometry. So, when GetMatchingStreet(houseId, feature)
        // is called, high precision geometry is used again to compute
        // |feature|'s center, and this is a right behavior as
        // house-to-street table was generated by using high-precision
        // centers of features.
        m2::PointD const center = feature::GetCenter(feature);
        if (calculator.GetProjection(center, proj) &&
            proj.m_distMeters <= ReverseGeocoder::kLookupRadiusM &&
            GetMatchingStreet(houseId, feature) == streetId)
        {
          fn(houseId, streetId);
        }
      }
    }
  }

  // Returns id of a street feature corresponding to a |houseId|, or
  // kInvalidId if there're not such street.
  uint32_t GetMatchingStreet(uint32_t houseId);
  uint32_t GetMatchingStreet(uint32_t houseId, FeatureType & houseFeature);
  uint32_t GetMatchingStreetImpl(uint32_t houseId, FeatureType & houseFeature);

  using TStreet = ReverseGeocoder::Street;
  using TStreets = vector<TStreet>;

  TStreets const & GetNearbyStreets(uint32_t featureId);
  TStreets const & GetNearbyStreets(uint32_t featureId, FeatureType & feature);
  TStreets const & GetNearbyStreetsImpl(uint32_t featureId, FeatureType & feature);

  inline void GetByIndex(uint32_t id, FeatureType & ft) const
  {
    /// @todo Add Cache for feature id -> (point, name / house number).
    /// TODO(vng): GetFeature below can return false if feature was deleted by user in the Editor.
    /// This code should be fixed to take that into an account.
    /// Until we don't show "Delete" button to our users, this code will work correctly.
    /// Correct fix would be injection into ForEachInIntervalAndScale, so deleted features will never
    /// be emitted and used in other code.
    UNUSED_VALUE(m_context->GetFeature(id, ft));
  }

  MwmContext * m_context;

  ReverseGeocoder m_reverseGeocoder;

  // Cache of streets in a feature's vicinity. All lists in the cache
  // are ordered by distance from the corresponding feature.
  Cache<uint32_t, TStreets> m_nearbyStreetsCache;

  // Cache of correct streets for buildings. Current search algorithm
  // supports only one street for a building, whereas buildings can be
  // located on multiple streets.
  Cache<uint32_t, uint32_t> m_matchingStreetsCache;

  StreetVicinityLoader m_loader;
  my::Cancellable const & m_cancellable;
};
}  // namespace v2
}  // namespace search
