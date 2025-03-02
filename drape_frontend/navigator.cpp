#include "drape_frontend/navigator.hpp"
#include "drape_frontend/visual_params.hpp"

#include "indexer/scales.hpp"

#include "platform/settings.hpp"

#include "geometry/angles.hpp"
#include "geometry/transformations.hpp"
#include "geometry/point2d.hpp"
#include "geometry/distance_on_sphere.hpp"

#include "base/logging.hpp"

#include "std/function.hpp"
#include "std/bind.hpp"

namespace
{

/// @todo Review this logic in future.
/// Fix bug with floating point calculations (before Android release).
void ReduceRectHack(m2::RectD & r)
{
  r.Inflate(-1.0E-9, -1.0E-9);
}

} // namespace

namespace df
{

Navigator::Navigator()
  : m_InAction(false)
{
}

void Navigator::SetFromRects(m2::AnyRectD const & glbRect, m2::RectD const & pxRect)
{
  m2::RectD const & worldR = df::GetWorldRect();

  m_Screen.SetFromRects(glbRect, pxRect);
  m_Screen = ScaleInto(m_Screen, worldR);

  if (!m_InAction)
  {
    m_StartScreen.SetFromRects(glbRect, pxRect);
    m_StartScreen = ScaleInto(m_StartScreen, worldR);
  }
}

void Navigator::SetFromRect(m2::AnyRectD const & r)
{
  VisualParams const & p = VisualParams::Instance();
  SetFromRect(r, p.GetTileSize(), p.GetVisualScale());
}

void Navigator::SetFromRect(m2::AnyRectD const & r, uint32_t tileSize, double visualScale)
{
  m2::RectD const & worldR = df::GetWorldRect();

  ScreenBase tmp = m_Screen;

  tmp.SetFromRect(r);
  tmp = ScaleInto(tmp, worldR);
  if (!CheckMaxScale(tmp, tileSize, visualScale))
  {
    int const scale = scales::GetUpperStyleScale() - 1;
    m2::RectD newRect = df::GetRectForDrawScale(scale, r.Center());
    CheckMinMaxVisibleScale(newRect, scale);
    tmp = m_Screen;
    tmp.SetFromRect(m2::AnyRectD(newRect));
    ASSERT(CheckMaxScale(tmp, tileSize, visualScale), ());
  }
  m_Screen = tmp;

  if (!m_InAction)
    m_StartScreen = tmp;
}

void Navigator::CenterViewport(m2::PointD const & p)
{
  // Rounding center point to a pixel boundary to obtain crisp centered picture.
  m2::PointD pt = m_Screen.GtoP(p);
  pt.x = ceil(pt.x);
  pt.y = ceil(pt.y);

  pt = m_Screen.PtoG(pt);

  m_Screen.SetOrg(pt);
  if (!m_InAction)
    m_StartScreen.SetOrg(pt);
}

void Navigator::OnSize(int w, int h)
{
  m2::RectD const & worldR = df::GetWorldRect();

  double const fov = m_Screen.GetAngleFOV();
  double const rotation = m_Screen.GetRotationAngle();
  if (m_Screen.isPerspective())
  {
    m_Screen.ResetPerspective();
    m_StartScreen.ResetPerspective();
  }

  m_Screen.OnSize(0, 0, w, h);
  m_Screen = ShrinkAndScaleInto(m_Screen, worldR);

  m_StartScreen.OnSize(0, 0, w, h);
  m_StartScreen = ShrinkAndScaleInto(m_StartScreen, worldR);

  if (fov != 0.0)
  {
    m_Screen.ApplyPerspective(rotation, rotation, fov);
    m_StartScreen.ApplyPerspective(rotation, rotation, fov);
  }
}

m2::PointD Navigator::GtoP(m2::PointD const & pt) const
{
  return m_Screen.GtoP(pt);
}

m2::PointD Navigator::PtoG(m2::PointD const & pt) const
{
  return m_Screen.PtoG(pt);
}

m2::PointD Navigator::P3dtoP(m2::PointD const & pt) const
{
  return m_Screen.P3dtoP(pt);
}

bool Navigator::CanShrinkInto(ScreenBase const & screen, m2::RectD const & boundRect)
{
  m2::RectD clipRect = screen.ClipRect();
  return (boundRect.SizeX() >= clipRect.SizeX())
      && (boundRect.SizeY() >= clipRect.SizeY());
}

ScreenBase const Navigator::ShrinkInto(ScreenBase const & screen, m2::RectD boundRect)
{
  ReduceRectHack(boundRect);

  ScreenBase res = screen;

  m2::RectD clipRect = res.ClipRect();
  if (clipRect.minX() < boundRect.minX())
    clipRect.Offset(boundRect.minX() - clipRect.minX(), 0);
  if (clipRect.maxX() > boundRect.maxX())
    clipRect.Offset(boundRect.maxX() - clipRect.maxX(), 0);
  if (clipRect.minY() < boundRect.minY())
    clipRect.Offset(0, boundRect.minY() - clipRect.minY());
  if (clipRect.maxY() > boundRect.maxY())
    clipRect.Offset(0, boundRect.maxY() - clipRect.maxY());

  res.SetOrg(clipRect.Center());

  // This assert fails near x = 180 (Philipines).
  //ASSERT ( boundRect.IsRectInside(res.ClipRect()), (clipRect, res.ClipRect()) );
  return res;
}

ScreenBase const Navigator::ScaleInto(ScreenBase const & screen, m2::RectD boundRect)
{
  ReduceRectHack(boundRect);

  ScreenBase res = screen;

  double scale = 1;

  m2::RectD clipRect = res.ClipRect();

  ASSERT(boundRect.IsPointInside(clipRect.Center()), ("center point should be inside boundRect"));

  if (clipRect.minX() < boundRect.minX())
  {
    double k = (boundRect.minX() - clipRect.Center().x) / (clipRect.minX() - clipRect.Center().x);
    scale /= k;
    clipRect.Scale(k);
  }
  if (clipRect.maxX() > boundRect.maxX())
  {
    double k = (boundRect.maxX() - clipRect.Center().x) / (clipRect.maxX() - clipRect.Center().x);
    scale /= k;
    clipRect.Scale(k);
  }
  if (clipRect.minY() < boundRect.minY())
  {
    double k = (boundRect.minY() - clipRect.Center().y) / (clipRect.minY() - clipRect.Center().y);
    scale /= k;
    clipRect.Scale(k);
  }
  if (clipRect.maxY() > boundRect.maxY())
  {
    double k = (boundRect.maxY() - clipRect.Center().y) / (clipRect.maxY() - clipRect.Center().y);
    scale /= k;
    clipRect.Scale(k);
  }

  res.Scale(scale);
  res.SetOrg(clipRect.Center());

  return res;
}

ScreenBase const Navigator::ShrinkAndScaleInto(ScreenBase const & screen, m2::RectD boundRect)
{
  ReduceRectHack(boundRect);

  ScreenBase res = screen;

  m2::RectD globalRect = res.ClipRect();

  m2::PointD newOrg = res.GetOrg();
  double scale = 1;
  double offs = 0;

  if (globalRect.minX() < boundRect.minX())
  {
    offs = boundRect.minX() - globalRect.minX();
    globalRect.Offset(offs, 0);
    newOrg.x += offs;

    if (globalRect.maxX() > boundRect.maxX())
    {
      double k = boundRect.SizeX() / globalRect.SizeX();
      scale /= k;
      /// scaling always occur pinpointed to the rect center...
      globalRect.Scale(k);
      /// ...so we should shift a rect after scale
      globalRect.Offset(boundRect.minX() - globalRect.minX(), 0);
    }
  }

  if (globalRect.maxX() > boundRect.maxX())
  {
    offs = boundRect.maxX() - globalRect.maxX();
    globalRect.Offset(offs, 0);
    newOrg.x += offs;

    if (globalRect.minX() < boundRect.minX())
    {
      double k = boundRect.SizeX() / globalRect.SizeX();
      scale /= k;
      globalRect.Scale(k);
      globalRect.Offset(boundRect.maxX() - globalRect.maxX(), 0);
    }
  }

  if (globalRect.minY() < boundRect.minY())
  {
    offs = boundRect.minY() - globalRect.minY();
    globalRect.Offset(0, offs);
    newOrg.y += offs;

    if (globalRect.maxY() > boundRect.maxY())
    {
      double k = boundRect.SizeY() / globalRect.SizeY();
      scale /= k;
      globalRect.Scale(k);
      globalRect.Offset(0, boundRect.minY() - globalRect.minY());
    }
  }

  if (globalRect.maxY() > boundRect.maxY())
  {
    offs = boundRect.maxY() - globalRect.maxY();
    globalRect.Offset(0, offs);
    newOrg.y += offs;

    if (globalRect.minY() < boundRect.minY())
    {
      double k = boundRect.SizeY() / globalRect.SizeY();
      scale /= k;
      globalRect.Scale(k);
      globalRect.Offset(0, boundRect.maxY() - globalRect.maxY());
    }
  }

  res.SetOrg(globalRect.Center());
  res.Scale(scale);

  return res;
}

void Navigator::StartDrag(m2::PointD const & pt)
{
  m_StartPt1 = m_LastPt1 = pt;
  m_StartScreen = m_Screen;
  m_InAction = true;
}

void Navigator::DoDrag(m2::PointD const & pt)
{
  if (m_LastPt1 == pt)
    return;

  ScreenBase const s = ShrinkInto(m_StartScreen, df::GetWorldRect());

  double dx = pt.x - m_StartPt1.x;
  double dy = pt.y - m_StartPt1.y;

  ScreenBase tmp = s;
  tmp.Move(dx, 0);
  if (!CheckBorders(tmp))
    dx = 0;

  tmp = s;
  tmp.Move(0, dy);
  if (!CheckBorders(tmp))
    dy = 0;

  tmp = s;
  tmp.Move(dx, dy);

  if (CheckBorders(tmp))
  {
    m_StartScreen = tmp;
    m_StartPt1 = pt;
    m_LastPt1 = pt;
    m_Screen = tmp;
  }
}

void Navigator::StopDrag(m2::PointD const & pt)
{
  DoDrag(pt);
  m_InAction = false;
}

bool Navigator::InAction() const
{
  return m_InAction;
}

void Navigator::StartScale(m2::PointD const & pt1, m2::PointD const & pt2)
{
  m_StartScreen = m_Screen;
  m_StartPt1 = m_LastPt1 = pt1;
  m_StartPt2 = m_LastPt2 = pt2;

  m_DoCheckRotationThreshold = true;
  m_IsRotatingDuringScale = false;
  m_InAction = true;
}

namespace
{
  void CalcScalePoints(m2::PointD const & pt, double factor, m2::RectD const & pxRect,
                       m2::PointD & startPt, m2::PointD & endPt)
  {
    // pt is in x0, x0 + width

    if (pt.x != pxRect.maxX())
    {
      // start scaling point is 1 / factor way between pt and right border
      startPt.x = pt.x + (pxRect.maxX() - pt.x) / factor;
      endPt.x = pxRect.maxX();
    }
    else
    {
      // start scaling point is 1 - 1/factor way between left border and pt
      startPt.x = pt.x + (pxRect.minX() - pt.x) / factor;
      endPt.x = pxRect.minX();
    }

    if (pt.y != pxRect.maxY())
    {
      startPt.y = pt.y + (pxRect.maxY() - pt.y) / factor;
      endPt.y = pxRect.maxY();
    }
    else
    {
      startPt.y = pt.y + (pxRect.minY() - pt.y) / factor;
      endPt.y = pxRect.minY();
    }
  }
}

void Navigator::Scale(m2::PointD const & pt, double factor)
{
  CalculateScale(pt, factor, m_Screen);
}

void Navigator::CalculateScale(m2::PointD const & pt, double factor, ScreenBase & screen)
{
  m2::PointD startPt, endPt;
  CalcScalePoints(pt, factor, screen.isPerspective() ? screen.PixelRectIn3d() : screen.PixelRect(), startPt, endPt);
  m2::PointD const newOffset = (endPt - pt) / 2.0;
  m2::PointD const oldOffset = (startPt - pt) / 2.0;
  ScaleImpl(pt - newOffset, pt + newOffset, pt - oldOffset, pt + oldOffset, factor > 1, false, screen);
}

bool Navigator::CheckMinScale(ScreenBase const & screen) const
{
  m2::RectD const & r = screen.ClipRect();
  m2::RectD const & worldR = df::GetWorldRect();

  return (r.SizeX() <= worldR.SizeX() || r.SizeY() <= worldR.SizeY());
}

bool Navigator::CheckMaxScale(ScreenBase const & screen) const
{
  VisualParams const & p = VisualParams::Instance();
  return CheckMaxScale(screen, p.GetTileSize(), p.GetVisualScale());
}

bool Navigator::CheckMaxScale(ScreenBase const & screen, uint32_t tileSize, double visualScale) const
{
  return (df::GetDrawTileScale(screen, tileSize, visualScale) <= scales::GetUpperStyleScale());
}

bool Navigator::CheckBorders(ScreenBase const & screen) const
{
  m2::RectD const & r = screen.ClipRect();
  m2::RectD const & worldR = df::GetWorldRect();

  return (r.IsRectInside(worldR) || worldR.IsRectInside(r));
}

bool Navigator::ScaleImpl(m2::PointD const & newPt1, m2::PointD const & newPt2,
                          m2::PointD const & oldPt1, m2::PointD const & oldPt2,
                          bool skipMinScaleAndBordersCheck, bool doRotateScreen,
                          ScreenBase & screen)
{
  m2::PointD const center3d = oldPt1;
  m2::PointD const center2d = screen.P3dtoP(center3d);
  m2::PointD const offset =  center2d - center3d;
  math::Matrix<double, 3, 3> const newM =
      screen.GtoPMatrix() * ScreenBase::CalcTransform(oldPt1 + offset, oldPt2 + offset,
                                                      newPt1 + offset, newPt2 + offset,
                                                      doRotateScreen);
  ScreenBase tmp = screen;
  tmp.SetGtoPMatrix(newM);

  if (!skipMinScaleAndBordersCheck && !CheckMinScale(tmp))
    return false;

  m2::RectD const & worldR = df::GetWorldRect();

  if (!skipMinScaleAndBordersCheck && !CheckBorders(tmp))
  {
    if (CanShrinkInto(tmp, worldR))
      tmp = ShrinkInto(tmp, worldR);
    else
      return false;
  }

  if (!CheckMaxScale(tmp))
    return false;

  // re-checking the borders, as we might violate them a bit (don't know why).
  if (!CheckBorders(tmp))
    tmp = ScaleInto(tmp, worldR);

  screen = tmp;
  return true;
}

void Navigator::DoScale(m2::PointD const & pt1, m2::PointD const & pt2)
{
  double const threshold = df::VisualParams::Instance().GetScaleThreshold();
  double const deltaPt1 = (pt1 - m_LastPt1).Length();
  double const deltaPt2 = (pt2 - m_LastPt2).Length();

  if (deltaPt1 < threshold && deltaPt2 < threshold)
    return;
  if (pt1 == pt2)
    return;

  ScreenBase PrevScreen = m_Screen;
  m_Screen = m_StartScreen;

  // Checking for rotation threshold.
  if (m_DoCheckRotationThreshold)
  {
    double s = pt1.Length(pt2) / m_StartPt1.Length(m_StartPt2);
    double a = ang::AngleTo(pt1, pt2) - ang::AngleTo(m_StartPt1, m_StartPt2);

    double aThresh = 10.0 / 180.0 * math::pi;
    double sThresh = 1.2;

    bool isScalingInBounds = (s > 1 / sThresh) && (s < sThresh);
    bool isRotationOutBounds = fabs(a) > aThresh;

    if (isScalingInBounds)
    {
      if (isRotationOutBounds)
      {
        m_IsRotatingDuringScale = true;
        m_DoCheckRotationThreshold = false;
      }
    }
    else
    {
      m_IsRotatingDuringScale = false;
      m_DoCheckRotationThreshold = false;
    }
  }

  m_Screen = PrevScreen;

  if (!ScaleImpl(pt1, pt2, m_LastPt1, m_LastPt2,
                 pt1.Length(pt2) > m_LastPt1.Length(m_LastPt2),
                 m_IsRotatingDuringScale, m_Screen))
  {
    m_Screen = PrevScreen;
  }

  m_LastPt1 = pt1;
  m_LastPt2 = pt2;
}

void Navigator::StopScale(m2::PointD const & pt1, m2::PointD const & pt2)
{
  DoScale(pt1, pt2);
  m_InAction = false;
}

bool Navigator::IsRotatingDuringScale() const
{
  return m_IsRotatingDuringScale;
}

void Navigator::Enable3dMode(double currentRotationAngle, double maxRotationAngle, double angleFOV)
{
  ASSERT(!m_Screen.isPerspective(), ());
  m_Screen.ApplyPerspective(currentRotationAngle, maxRotationAngle, angleFOV);
}

void Navigator::SetRotationIn3dMode(double rotationAngle)
{
  m_Screen.SetRotationAngle(rotationAngle);
}

void Navigator::Disable3dMode()
{
  ASSERT(m_Screen.isPerspective(), ());
  m_Screen.ResetPerspective();
}

m2::AnyRectD ToRotated(Navigator const & navigator, m2::RectD const & rect)
{
  double const dx = rect.SizeX();
  double const dy = rect.SizeY();

  return m2::AnyRectD(rect.Center(),
                      navigator.Screen().GetAngle(),
                      m2::RectD(-dx/2, -dy/2, dx/2, dy/2));
}

void CheckMinGlobalRect(m2::RectD & rect, uint32_t tileSize, double visualScale)
{
  m2::RectD const minRect = df::GetRectForDrawScale(scales::GetUpperStyleScale(), rect.Center(), tileSize, visualScale);
  if (minRect.IsRectInside(rect))
    rect = minRect;
}

void CheckMinGlobalRect(m2::RectD & rect)
{
  VisualParams const & p = VisualParams::Instance();
  CheckMinGlobalRect(rect, p.GetTileSize(), p.GetVisualScale());
}

void CheckMinMaxVisibleScale(m2::RectD & rect, int maxScale,
                             uint32_t tileSize, double visualScale)
{
  CheckMinGlobalRect(rect, tileSize, visualScale);
  int scale = df::GetDrawTileScale(rect, tileSize, visualScale);
  if (maxScale != -1 && scale > maxScale)
  {
    // limit on passed maximal scale
    m2::PointD const c = rect.Center();
    rect = df::GetRectForDrawScale(maxScale, c, tileSize, visualScale);
  }
}

void CheckMinMaxVisibleScale(m2::RectD & rect, int maxScale)
{
  VisualParams const & p = VisualParams::Instance();
  CheckMinMaxVisibleScale(rect, maxScale, p.GetTileSize(), p.GetVisualScale());
}

} // namespace df
