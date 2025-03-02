#include "drape/overlay_handle.hpp"

#include "base/macros.hpp"

#include "base/internal/message.hpp"
#include "base/logging.hpp"

namespace dp
{

struct OverlayHandle::OffsetNodeFinder
{
public:
  OffsetNodeFinder(uint8_t bufferID) : m_bufferID(bufferID) {}

  bool operator()(OverlayHandle::TOffsetNode const & node) const
  {
    return node.first.GetID() == m_bufferID;
  }

private:
  uint8_t m_bufferID;
};

OverlayHandle::OverlayHandle(FeatureID const & id,
                             dp::Anchor anchor,
                             uint64_t priority,
                             bool isBillboard)
  : m_id(id)
  , m_anchor(anchor)
  , m_priority(priority)
  , m_overlayRank(OverlayRank0)
  , m_extendingSize(0.0)
  , m_pivotZ(0.0)
  , m_isBillboard(isBillboard)
  , m_isVisible(false)
  , m_enableCaching(false)
  , m_extendedShapeDirty(true)
  , m_extendedRectDirty(true)
{}

void OverlayHandle::SetCachingEnable(bool enable)
{
  m_enableCaching = enable;
  m_extendedShapeDirty = true;
  m_extendedRectDirty = true;
}

bool OverlayHandle::IsVisible() const
{
  return m_isVisible;
}

void OverlayHandle::SetIsVisible(bool isVisible)
{
  m_isVisible = isVisible;
}

bool OverlayHandle::IsBillboard() const
{
  return m_isBillboard;
}

m2::PointD OverlayHandle::GetPivot(ScreenBase const & screen, bool perspective) const
{
  m2::RectD r = GetPixelRect(screen, false);
  m2::PointD size(0.5 * r.SizeX(), 0.5 * r.SizeY());
  m2::PointD result = r.Center();

  if (m_anchor & dp::Left)
    result.x -= size.x;
  else if (m_anchor & dp::Right)
    result.x += size.x;

  if (m_anchor & dp::Top)
    result.y -= size.y;
  else if (m_anchor & dp::Bottom)
    result.y += size.y;

  if (perspective)
    result = screen.PtoP3d(result, -m_pivotZ / screen.GetScale());

  return result;
}

bool OverlayHandle::IsIntersect(ScreenBase const & screen, ref_ptr<OverlayHandle> const h) const
{
  Rects const & ar1 = GetExtendedPixelShape(screen);
  Rects const & ar2 = h->GetExtendedPixelShape(screen);

  for (size_t i = 0; i < ar1.size(); ++i)
    for (size_t j = 0; j < ar2.size(); ++j)
      if (ar1[i].IsIntersect(ar2[j]))
        return true;

  return false;
}

void * OverlayHandle::IndexStorage(uint32_t size)
{
  m_indexes.Resize(size);
  return m_indexes.GetRaw();
}

void OverlayHandle::GetElementIndexes(ref_ptr<IndexBufferMutator> mutator) const
{
  ASSERT_EQUAL(m_isVisible, true, ());
  mutator->AppendIndexes(m_indexes.GetRawConst(), m_indexes.Size());
}

void OverlayHandle::GetAttributeMutation(ref_ptr<AttributeBufferMutator> mutator) const
{
  UNUSED_VALUE(mutator);
}

bool OverlayHandle::HasDynamicAttributes() const
{
  return !m_offsets.empty();
}

void OverlayHandle::AddDynamicAttribute(BindingInfo const & binding, uint32_t offset, uint32_t count)
{
  ASSERT(binding.IsDynamic(), ());
  ASSERT(find_if(m_offsets.begin(), m_offsets.end(), OffsetNodeFinder(binding.GetID())) == m_offsets.end(), ());
  m_offsets.insert(make_pair(binding, MutateRegion(offset, count)));
}

FeatureID const & OverlayHandle::GetFeatureID() const
{
  return m_id;
}

uint64_t const & OverlayHandle::GetPriority() const
{
  return m_priority;
}

OverlayHandle::TOffsetNode const & OverlayHandle::GetOffsetNode(uint8_t bufferID) const
{
  set<TOffsetNode>::const_iterator it = find_if(m_offsets.begin(), m_offsets.end(), OffsetNodeFinder(bufferID));
  ASSERT(it != m_offsets.end(), ());
  return *it;
}

m2::RectD OverlayHandle::GetExtendedPixelRect(ScreenBase const & screen) const
{
  if (m_enableCaching && !m_extendedRectDirty)
    return m_extendedRectCache;

  m_extendedRectCache = GetPixelRect(screen, screen.isPerspective());
  m_extendedRectCache.Inflate(m_extendingSize, m_extendingSize);
  m_extendedRectDirty = false;
  return m_extendedRectCache;
}

OverlayHandle::Rects const & OverlayHandle::GetExtendedPixelShape(ScreenBase const & screen) const
{
  if (m_enableCaching && !m_extendedShapeDirty)
    return m_extendedShapeCache;

  m_extendedShapeCache.clear();
  GetPixelShape(screen, screen.isPerspective(), m_extendedShapeCache);
  for (auto & rect : m_extendedShapeCache)
    rect.Inflate(m_extendingSize, m_extendingSize);
  m_extendedShapeDirty = false;
  return m_extendedShapeCache;
}

m2::RectD OverlayHandle::GetPerspectiveRect(m2::RectD const & pixelRect, ScreenBase const & screen) const
{
  m2::PointD const tmpPoint = screen.PtoP3d(pixelRect.LeftTop());
  m2::RectD perspectiveRect(tmpPoint, tmpPoint);
  perspectiveRect.Add(screen.PtoP3d(pixelRect.LeftBottom()));
  perspectiveRect.Add(screen.PtoP3d(pixelRect.RightBottom()));
  perspectiveRect.Add(screen.PtoP3d(pixelRect.RightTop()));

  return perspectiveRect;
}

m2::RectD OverlayHandle::GetPixelRectPerspective(ScreenBase const & screen) const
{
  if (m_isBillboard)
  {
    m2::PointD const pxPivot = GetPivot(screen, false);
    m2::PointD const pxPivotPerspective = screen.PtoP3d(pxPivot, -m_pivotZ / screen.GetScale());

    m2::RectD pxRectPerspective = GetPixelRect(screen, false);
    pxRectPerspective.Offset(-pxPivot);
    pxRectPerspective.Offset(pxPivotPerspective);

    return pxRectPerspective;
  }

  return GetPerspectiveRect(GetPixelRect(screen, false), screen);
}

uint64_t OverlayHandle::GetPriorityInFollowingMode() const
{
  return GetPriority();
}


SquareHandle::SquareHandle(FeatureID const & id, dp::Anchor anchor,
                           m2::PointD const & gbPivot, m2::PointD const & pxSize,
                           uint64_t priority,
                           string const & debugStr,
                           bool isBillboard)
  : TBase(id, anchor, priority, isBillboard)
  , m_gbPivot(gbPivot)
  , m_pxHalfSize(pxSize.x / 2.0, pxSize.y / 2.0)
#ifdef DEBUG_OVERLAYS_OUTPUT
  , m_debugStr(debugStr)
#endif
{}

m2::RectD SquareHandle::GetPixelRect(ScreenBase const & screen, bool perspective) const
{
  if (perspective)
    return GetPixelRectPerspective(screen);

  m2::PointD const pxPivot = screen.GtoP(m_gbPivot);
  m2::RectD  result(pxPivot - m_pxHalfSize, pxPivot + m_pxHalfSize);
  m2::PointD offset(0.0, 0.0);

  if (m_anchor & dp::Left)
    offset.x = m_pxHalfSize.x;
  else if (m_anchor & dp::Right)
    offset.x = -m_pxHalfSize.x;

  if (m_anchor & dp::Top)
    offset.y = m_pxHalfSize.y;
  else if (m_anchor & dp::Bottom)
    offset.y = -m_pxHalfSize.y;

  result.Offset(offset);
  return result;
}

void SquareHandle::GetPixelShape(ScreenBase const & screen, bool perspective, Rects & rects) const
{
  rects.emplace_back(GetPixelRect(screen, perspective));
}

#ifdef DEBUG_OVERLAYS_OUTPUT
string SquareHandle::GetOverlayDebugInfo()
{
  ostringstream out;
  out << "POI Priority(" << GetPriority() << ") " << GetFeatureID().m_index << " " << m_debugStr;
  return out.str();
}
#endif

uint64_t CalculateOverlayPriority(int minZoomLevel, uint8_t rank, float depth)
{
  // Overlay priority consider the following:
  // - Minimum visible zoom level (the less the better);
  // - Manual priority from styles (equals to the depth);
  // - Rank of the feature (the more the better);
  // [1 byte - zoom][4 bytes - priority][1 byte - rank][1 byte - reserved][1 byte - reserved].
  uint8_t const minZoom = 0xFF - static_cast<uint8_t>(max(minZoomLevel, 0));

  float const kMinDepth = -100000.0f;
  float const kMaxDepth = 100000.0f;
  float const d = my::clamp(depth, kMinDepth, kMaxDepth) - kMinDepth;
  uint32_t const priority = static_cast<uint32_t>(d);

  return (static_cast<uint64_t>(minZoom) << 56) |
         (static_cast<uint64_t>(priority) << 24) |
         (static_cast<uint64_t>(rank) << 16) |
         static_cast<uint64_t>(0xFFFF);
}

} // namespace dp
