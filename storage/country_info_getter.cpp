#include "storage/country.hpp"
#include "storage/country_info_getter.hpp"
#include "storage/country_polygon.hpp"

#include "platform/local_country_file_utils.hpp"

#include "indexer/geometry_serialization.hpp"

#include "geometry/latlon.hpp"
#include "geometry/mercator.hpp"
#include "geometry/region2d.hpp"

#include "coding/read_write_utils.hpp"

#include "base/logging.hpp"
#include "base/string_utils.hpp"

#include "3party/Alohalytics/src/alohalytics.h"

#include "std/bind.hpp"
#include "std/function.hpp"
#include "std/limits.hpp"

namespace storage
{
namespace
{
size_t const kInvalidId = numeric_limits<size_t>::max();

struct DoFreeCacheMemory
{
  void operator()(vector<m2::RegionD> & v) const { vector<m2::RegionD>().swap(v); }
};

class DoCalcUSA
{
public:
  DoCalcUSA(m2::RectD * rects) : m_rects(rects) {}

  void operator()(CountryDef const & c)
  {
    if (c.m_name == "USA_Alaska")
      m_rects[1] = c.m_rect;
    else if (c.m_name == "USA_Hawaii")
      m_rects[2] = c.m_rect;
    else
      m_rects[0].Add(c.m_rect);
  }

private:
  m2::RectD * const m_rects;
};
}  // namespace

// CountryInfoGetter -------------------------------------------------------------------------------
TCountryId CountryInfoGetter::GetRegionCountryId(m2::PointD const & pt) const
{
  IdType const id = FindFirstCountry(pt);
  return id != kInvalidId ? m_countries[id].m_name : kInvalidCountryId;
}

void CountryInfoGetter::GetRegionsCountryId(m2::PointD const & pt, TCountriesVec & closestCoutryIds)
{
  double const kLookupRadiusM = 30 /* km */ * 1000;

  closestCoutryIds.clear();

  m2::RectD const lookupRect = MercatorBounds::RectByCenterXYAndSizeInMeters(pt, kLookupRadiusM);

  for (size_t id = 0; id < m_countries.size(); ++id)
  {
    if (m_countries[id].m_rect.IsIntersect(lookupRect) && IsCloseEnough(id, pt, kLookupRadiusM))
      closestCoutryIds.emplace_back(m_countries[id].m_name);
  }
}

void CountryInfoGetter::GetRegionInfo(m2::PointD const & pt, CountryInfo & info) const
{
  IdType const id = FindFirstCountry(pt);
  if (id != kInvalidId)
    GetRegionInfo(m_countries[id].m_name, info);
}

void CountryInfoGetter::GetRegionInfo(string const & id, CountryInfo & info) const
{
  auto const it = m_id2info.find(id);
  if (it == m_id2info.end())
    return;

  info = it->second;
  if (info.m_name.empty())
    info.m_name = id;

  CountryInfo::FileName2FullName(info.m_name);
}

void CountryInfoGetter::CalcUSALimitRect(m2::RectD rects[3]) const
{
  ForEachCountry("USA_", DoCalcUSA(rects));
}

m2::RectD CountryInfoGetter::CalcLimitRect(string const & prefix) const
{
  m2::RectD rect;
  ForEachCountry(prefix, [&rect](CountryDef const & c)
  {
    rect.Add(c.m_rect);
  });
  return rect;
}

m2::RectD CountryInfoGetter::GetLimitRectForLeaf(TCountryId const & leafCountryId) const
{
  auto const it = this->m_countryIndex.find(leafCountryId);
  ASSERT(it != this->m_countryIndex.end(), ());
  ASSERT_LESS(it->second, this->m_countries.size(), ());
  return m_countries[it->second].m_rect;
}

void CountryInfoGetter::GetMatchedRegions(string const & affiliation, IdSet & regions) const
{
  CHECK(m_affiliations, ());
  auto it = m_affiliations->find(affiliation);
  if (it == m_affiliations->end())
    return;

  for (size_t i = 0; i < m_countries.size(); ++i)
  {
    if (binary_search(it->second.begin(), it->second.end(), m_countries[i].m_name))
      regions.push_back(i);
  }
}

bool CountryInfoGetter::IsBelongToRegions(m2::PointD const & pt, IdSet const & regions) const
{
  for (auto const & id : regions)
  {
    if (m_countries[id].m_rect.IsPointInside(pt) && IsBelongToRegionImpl(id, pt))
      return true;
  }
  return false;
}

bool CountryInfoGetter::IsBelongToRegions(string const & fileName, IdSet const & regions) const
{
  for (auto const & id : regions)
  {
    if (m_countries[id].m_name == fileName)
      return true;
  }
  return false;
}

void CountryInfoGetter::InitAffiliationsInfo(TMappingAffiliations const * affiliations)
{
  m_affiliations = affiliations;
}

CountryInfoGetter::IdType CountryInfoGetter::FindFirstCountry(m2::PointD const & pt) const
{
  for (size_t id = 0; id < m_countries.size(); ++id)
  {
    if (m_countries[id].m_rect.IsPointInside(pt) && IsBelongToRegionImpl(id, pt))
      return id;
  }

  ms::LatLon const latLon = MercatorBounds::ToLatLon(pt);
  alohalytics::LogEvent(m_isSingleMwm ? "Small mwm case. CountryInfoGetter could not find any mwm by point."
                                      : "Big mwm case. CountryInfoGetter could not find any mwm by point.",
                        alohalytics::Location::FromLatLon(latLon.lat, latLon.lon));
  return kInvalidId;
}

template <typename ToDo>
void CountryInfoGetter::ForEachCountry(string const & prefix, ToDo && toDo) const
{
  for (auto const & country : m_countries)
  {
    if (strings::StartsWith(country.m_name, prefix.c_str()))
      toDo(country);
  }
}

// CountryInfoReader -------------------------------------------------------------------------------
// static
unique_ptr<CountryInfoGetter> CountryInfoReader::CreateCountryInfoReader(Platform const & platform)
{
  if (platform::migrate::NeedMigrate())
    return CreateCountryInfoReaderTwoComponentMwms(platform);
  return CreateCountryInfoReaderOneComponentMwms(platform);
}

// static
unique_ptr<CountryInfoGetter> CountryInfoReader::CreateCountryInfoReaderTwoComponentMwms(
    Platform const & platform)
{
  try
  {
    CountryInfoReader * result = new CountryInfoReader(platform.GetReader(PACKED_POLYGONS_OBSOLETE_FILE),
                                                       platform.GetReader(COUNTRIES_OBSOLETE_FILE));
    return unique_ptr<CountryInfoReader>(result);
  }
  catch (RootException const & e)
  {
    LOG(LCRITICAL, ("Can't load needed resources for storage::CountryInfoGetter:", e.Msg()));
  }
  return unique_ptr<CountryInfoReader>();
}

// static
unique_ptr<CountryInfoGetter> CountryInfoReader::CreateCountryInfoReaderOneComponentMwms(
    Platform const & platform)
{
  try
  {
    CountryInfoReader * result =
        new CountryInfoReader(platform.GetReader(PACKED_POLYGONS_FILE),
                              platform.GetReader(COUNTRIES_FILE));
    return unique_ptr<CountryInfoReader>(result);
  }
  catch (RootException const & e)
  {
    LOG(LCRITICAL, ("Can't load needed resources for storage::CountryInfoGetter:", e.Msg()));
  }
  return unique_ptr<CountryInfoReader>();
}

CountryInfoReader::CountryInfoReader(ModelReaderPtr polyR, ModelReaderPtr countryR)
  : CountryInfoGetter(true), m_reader(polyR), m_cache(3)
{
  ReaderSource<ModelReaderPtr> src(m_reader.GetReader(PACKED_POLYGONS_INFO_TAG));
  rw::Read(src, m_countries);

  size_t const countrySz = m_countries.size();
  m_countryIndex.reserve(countrySz);
  for (size_t i = 0; i < countrySz; ++i)
    m_countryIndex[m_countries[i].m_name] = i;

  string buffer;
  countryR.ReadAsString(buffer);
  LoadCountryFile2CountryInfo(buffer, m_id2info, m_isSingleMwm);
}

void CountryInfoReader::ClearCachesImpl() const
{
  lock_guard<mutex> lock(m_cacheMutex);

  m_cache.ForEachValue(DoFreeCacheMemory());
  m_cache.Reset();
}

template <typename TFn>
typename result_of<TFn(vector<m2::RegionD>)>::type CountryInfoReader::WithRegion(size_t id, TFn && fn) const
{
  lock_guard<mutex> lock(m_cacheMutex);

  bool isFound = false;
  vector<m2::RegionD> & rgns = m_cache.Find(static_cast<uint32_t>(id), isFound);

  if (!isFound)
  {
    rgns.clear();
    // Load regions from file.
    ReaderSource<ModelReaderPtr> src(m_reader.GetReader(strings::to_string(id)));

    uint32_t const count = ReadVarUint<uint32_t>(src);
    for (size_t i = 0; i < count; ++i)
    {
      vector<m2::PointD> points;
      serial::LoadOuterPath(src, serial::CodingParams(), points);
      rgns.emplace_back(move(points));
    }
  }

  return fn(rgns);
}


bool CountryInfoReader::IsBelongToRegionImpl(size_t id, m2::PointD const & pt) const
{
  auto contains = [&pt](vector<m2::RegionD> const & regions)
  {
    for (auto const & region : regions)
    {
      if (region.Contains(pt))
        return true;
    }
    return false;
  };

  return WithRegion(id, contains);
}

bool CountryInfoReader::IsCloseEnough(size_t id, m2::PointD const & pt, double distance)
{
  m2::RectD const lookupRect = MercatorBounds::RectByCenterXYAndSizeInMeters(pt, distance);
  auto isCloseEnough = [&](vector<m2::RegionD> const & regions)
  {
    for (auto const & region : regions)
    {
      if (region.Contains(pt) || region.AtBorder(pt, lookupRect.SizeX() / 2))
        return true;
    }
    return false;
  };

  return WithRegion(id, isCloseEnough);
}

// CountryInfoGetterForTesting ---------------------------------------------------------------------
CountryInfoGetterForTesting::CountryInfoGetterForTesting(vector<CountryDef> const & countries)
  : CountryInfoGetter(true)
{
  for (auto const & country : countries)
    AddCountry(country);
}

void CountryInfoGetterForTesting::AddCountry(CountryDef const & country)
{
  m_countries.push_back(country);
  string const & name = country.m_name;
  m_id2info[name].m_name = name;
}

void CountryInfoGetterForTesting::GetMatchedRegions(string const & affiliation,
                                                    IdSet & regions) const
{
  for (size_t i = 0; i < m_countries.size(); ++i)
  {
    if (m_countries[i].m_name == affiliation)
      regions.push_back(i);
  }
}

void CountryInfoGetterForTesting::ClearCachesImpl() const {}

bool CountryInfoGetterForTesting::IsBelongToRegionImpl(size_t id,
                                                       m2::PointD const & pt) const
{
  CHECK_LESS(id, m_countries.size(), ());
  return m_countries[id].m_rect.IsPointInside(pt);
}

bool CountryInfoGetterForTesting::IsCloseEnough(size_t id, m2::PointD const & pt, double distance)
{
  CHECK_LESS(id, m_countries.size(), ());

  m2::RegionD rgn;
  rgn.AddPoint(m_countries[id].m_rect.LeftTop());
  rgn.AddPoint(m_countries[id].m_rect.RightTop());
  rgn.AddPoint(m_countries[id].m_rect.RightBottom());
  rgn.AddPoint(m_countries[id].m_rect.LeftBottom());
  rgn.AddPoint(m_countries[id].m_rect.LeftTop());

  m2::RectD const lookupRect = MercatorBounds::RectByCenterXYAndSizeInMeters(pt, distance);
  return rgn.Contains(pt) || rgn.AtBorder(pt, lookupRect.SizeX() / 2);
}
}  // namespace storage
