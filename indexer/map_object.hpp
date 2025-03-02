#pragma once

#include "indexer/feature_data.hpp"
#include "indexer/feature_decl.hpp"
#include "indexer/feature_meta.hpp"

#include "geometry/latlon.hpp"
#include "geometry/mercator.hpp"

#include "coding/multilang_utf8_string.hpp"

#include "base/stl_helpers.hpp"

#include "std/string.hpp"
#include "std/vector.hpp"

class FeatureType;

namespace osm
{
class EditableMapObject;
/// OSM internet_access tag values.
enum class Internet
{
  Unknown,  //!< Internet state is unknown (default).
  Wlan,     //!< Wireless Internet access is present.
  Wired,    //!< Wired Internet access is present.
  Yes,      //!< Unspecified Internet access is available.
  No        //!< There is definitely no any Internet access.
};
string DebugPrint(Internet internet);

/// Metadata fields in the sorted order, visible to users.
enum class Props
{
  OpeningHours,
  Phone,
  Fax,
  Website,
  Email,
  Cuisine,
  Stars,
  Operator,
  Elevation,
  Internet,
  Wikipedia,
  Flats,
  BuildingLevels
};
string DebugPrint(Props props);

class MapObject
{
public:
  void SetFromFeatureType(FeatureType const & ft);

  FeatureID const & GetID() const;

  ms::LatLon GetLatLon() const;
  m2::PointD const & GetMercator() const;

  /// @returns "the best" type to display in UI.
  string GetLocalizedType() const;
  feature::TypesHolder const & GetTypes() const;
  string GetDefaultName() const;

  /// @name Metadata fields.
  //@{
  vector<Props> AvailableProperties() const;
  string GetPhone() const;
  string GetFax() const;
  string GetEmail() const;
  string GetWebsite() const;
  Internet GetInternet() const;
  /// @returns not localized cuisines keys.
  vector<string> GetCuisines() const;
  /// @returns translated cuisine(s).
  vector<string> GetLocalizedCuisines() const;
  /// @returns translated and formatted cuisines.
  string FormatCuisines() const;
  string GetOpeningHours() const;
  string GetOperator() const;
  int GetStars() const;
  /// @returns formatted elevation in feet or meters, or empty string.
  string GetElevationFormatted() const;
  bool GetElevation(double & outElevationInMeters) const;
  /// @returns URL to Wikipedia or empty string.
  string GetWikipediaLink() const;
  string GetFlats() const;
  string GetBuildingLevels() const;
  //@}

  // TODO(Vlad, yunikkk): Use Props enum + getters instead of direct metadata access.
  // TODO: Remove this method.
  feature::Metadata const & GetMetadata() const;

  bool IsPointType() const;

protected:
  FeatureID m_featureID;
  m2::PointD m_mercator;
  StringUtf8Multilang m_name;
  feature::TypesHolder m_types;
  feature::Metadata m_metadata;

  feature::EGeomType m_geomType = feature::EGeomType::GEOM_UNDEFINED;
};

/// Helper to convert internal feature::Metadata::FMD_* enum into a users-visible one.
template <class T>
vector<Props> MetadataToProps(vector<T> const & metadata)
{
  vector<Props> res;
  using feature::Metadata;
  for (auto const type : metadata)
  {
    switch (static_cast<Metadata::EType>(type))
    {
    case Metadata::FMD_CUISINE: res.push_back(Props::Cuisine); break;
    case Metadata::FMD_OPEN_HOURS: res.push_back(Props::OpeningHours); break;
    case Metadata::FMD_PHONE_NUMBER: res.push_back(Props::Phone); break;
    case Metadata::FMD_FAX_NUMBER: res.push_back(Props::Fax); break;
    case Metadata::FMD_STARS: res.push_back(Props::Stars); break;
    case Metadata::FMD_OPERATOR:
      res.push_back(Props::Operator);
      break;
    // Url is not used in UI and should be matched to Website.
    case Metadata::FMD_URL:
    case Metadata::FMD_WEBSITE: res.push_back(Props::Website); break;
    case Metadata::FMD_INTERNET: res.push_back(Props::Internet); break;
    case Metadata::FMD_ELE: res.push_back(Props::Elevation); break;
    case Metadata::FMD_EMAIL: res.push_back(Props::Email); break;
    case Metadata::FMD_WIKIPEDIA: res.push_back(Props::Wikipedia); break;
    case Metadata::FMD_FLATS: res.push_back(Props::Flats); break;
    case Metadata::FMD_BUILDING_LEVELS: res.push_back(Props::BuildingLevels); break;
    case Metadata::FMD_TURN_LANES:
    case Metadata::FMD_TURN_LANES_FORWARD:
    case Metadata::FMD_TURN_LANES_BACKWARD:
    // Postcode should be processed separately, in the address.
    case Metadata::FMD_POSTCODE:
    case Metadata::FMD_MAXSPEED:
    case Metadata::FMD_HEIGHT:
    case Metadata::FMD_MIN_HEIGHT:
    case Metadata::FMD_DENOMINATION:
    case Metadata::FMD_TEST_ID:
    case Metadata::FMD_COUNT:
      break;
      // Please add new cases when compiler issues an "unhandled switch case" warning here.
    }
  }
  my::SortUnique(res);
  return res;
}
}  // namespace osm
