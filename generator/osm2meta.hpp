#pragma once

#include "indexer/feature_data.hpp"
#include "indexer/classificator.hpp"
#include "indexer/ftypes_matcher.hpp"

#include "std/string.hpp"

struct MetadataTagProcessorImpl
{
  MetadataTagProcessorImpl(FeatureParams &params)
  : m_params(params)
  {
  }

  string ValidateAndFormat_maxspeed(string const & v) const;
  string ValidateAndFormat_stars(string const & v) const;
  string ValidateAndFormat_cuisine(string v) const;
  string ValidateAndFormat_operator(string const & v) const;
  string ValidateAndFormat_url(string const & v) const;
  string ValidateAndFormat_phone(string const & v) const;
  string ValidateAndFormat_opening_hours(string const & v) const;
  string ValidateAndFormat_ele(string const & v) const;
  string ValidateAndFormat_turn_lanes(string const & v) const;
  string ValidateAndFormat_turn_lanes_forward(string const & v) const;
  string ValidateAndFormat_turn_lanes_backward(string const & v) const;
  string ValidateAndFormat_email(string const & v) const;
  string ValidateAndFormat_postcode(string const & v) const;
  string ValidateAndFormat_flats(string const & v) const;
  string ValidateAndFormat_internet(string v) const;
  string ValidateAndFormat_height(string const & v) const;
  string ValidateAndFormat_building_levels(string v) const;
  string ValidateAndFormat_denomination(string const & v) const;
  string ValidateAndFormat_wikipedia(string v) const;

protected:
  FeatureParams & m_params;
};

class MetadataTagProcessor : private MetadataTagProcessorImpl
{
public:
  /// Make base class constructor public.
  using MetadataTagProcessorImpl::MetadataTagProcessorImpl;
  /// Since it is used as a functor wich stops iteration in ftype::ForEachTag
  /// and the is no need for interrupting it always returns false.
  /// TODO(mgsergio): Move to cpp after merge with https://github.com/mapsme/omim/pull/1314
  bool operator() (string const & k, string const & v)
  {
    if (v.empty())
      return false;

    using feature::Metadata;
    Metadata & md = m_params.GetMetadata();

    Metadata::EType mdType;
    if (!Metadata::TypeFromString(k, mdType))
    {
      // Specific cases which do not map directly to our metadata types.
      if (k == "building:min_level")
      {
        // Converting this attribute into height only if min_height has not been already set.
        if (!md.Has(Metadata::FMD_MIN_HEIGHT))
          md.Set(Metadata::FMD_MIN_HEIGHT, ValidateAndFormat_building_levels(v));
      }
      return false;
    }

    string valid;
    switch (mdType)
    {
    case Metadata::FMD_CUISINE: valid = ValidateAndFormat_cuisine(v); break;
    case Metadata::FMD_OPEN_HOURS: valid = ValidateAndFormat_opening_hours(v); break;
    case Metadata::FMD_FAX_NUMBER:  // The same validator as for phone.
    case Metadata::FMD_PHONE_NUMBER: valid = ValidateAndFormat_phone(v); break;
    case Metadata::FMD_STARS: valid = ValidateAndFormat_stars(v); break;
    case Metadata::FMD_OPERATOR: valid = ValidateAndFormat_operator(v); break;
    case Metadata::FMD_URL:  // The same validator as for website.
    case Metadata::FMD_WEBSITE: valid = ValidateAndFormat_url(v); break;
    case Metadata::FMD_INTERNET: ValidateAndFormat_internet(v); break;
    case Metadata::FMD_ELE: valid = ValidateAndFormat_ele(v); break;
    case Metadata::FMD_TURN_LANES: valid = ValidateAndFormat_turn_lanes(v); break;
    case Metadata::FMD_TURN_LANES_FORWARD: valid = ValidateAndFormat_turn_lanes_forward(v); break;
    case Metadata::FMD_TURN_LANES_BACKWARD: valid = ValidateAndFormat_turn_lanes_backward(v); break;
    case Metadata::FMD_EMAIL: valid = ValidateAndFormat_email(v); break;
    case Metadata::FMD_POSTCODE: valid = ValidateAndFormat_postcode(v); break;
    case Metadata::FMD_WIKIPEDIA: valid = ValidateAndFormat_wikipedia(v); break;
    case Metadata::FMD_MAXSPEED: valid = ValidateAndFormat_maxspeed(v); break;
    case Metadata::FMD_FLATS: valid = ValidateAndFormat_flats(v); break;
    case Metadata::FMD_MIN_HEIGHT:  // The same validator as for height.
    case Metadata::FMD_HEIGHT: valid = ValidateAndFormat_height(v); break;
    case Metadata::FMD_DENOMINATION: valid = ValidateAndFormat_denomination(v); break;
    case Metadata::FMD_BUILDING_LEVELS: valid = ValidateAndFormat_building_levels(v); break;

    case Metadata::FMD_TEST_ID:
    case Metadata::FMD_COUNT: CHECK(false, ("FMD_COUNT can not be used as a type."));
    }
    md.Set(mdType, valid);
    return false;
  }
};
