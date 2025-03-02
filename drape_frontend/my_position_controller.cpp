#include "drape_frontend/my_position_controller.hpp"
#include "drape_frontend/animation_utils.hpp"
#include "drape_frontend/visual_params.hpp"
#include "drape_frontend/animation/base_interpolator.hpp"
#include "drape_frontend/animation/interpolations.hpp"
#include "drape_frontend/animation/model_view_animation.hpp"

#include "geometry/mercator.hpp"

#include "base/math.hpp"

#include "3party/Alohalytics/src/alohalytics.h"

namespace df
{

namespace
{

int const POSITION_Y_OFFSET = 75;
int const POSITION_Y_OFFSET_3D = 80;
double const GPS_BEARING_LIFETIME_S = 5.0;
double const MIN_SPEED_THRESHOLD_MPS = 1.0;

uint16_t SetModeBit(uint32_t mode, uint32_t bit)
{
  return mode | bit;
}

uint16_t ResetModeBit(uint32_t mode, uint32_t bit)
{
  return mode & (~bit);
}

location::EMyPositionMode ResetAllModeBits(uint32_t mode)
{
  return (location::EMyPositionMode)(mode & 0xF);
}

uint16_t ChangeMode(uint32_t mode, location::EMyPositionMode newMode)
{
  return (mode & 0xF0) | newMode;
}

bool TestModeBit(uint32_t mode, uint32_t bit)
{
  return (mode & bit) != 0;
}

} // namespace

class MyPositionController::MyPositionAnim : public BaseInterpolator
{
  using TBase = BaseInterpolator;
public:
  MyPositionAnim(m2::PointD const & startPt, m2::PointD const & endPt, double moveDuration,
                 double startAzimut, double endAzimut, double rotationDuration)
    : TBase(max(moveDuration, rotationDuration))
    , m_startPt(startPt)
    , m_endPt(endPt)
    , m_moveDuration(moveDuration)
    , m_angleInterpolator(startAzimut, endAzimut)
    , m_rotateDuration(rotationDuration)
  {
  }

  m2::PointD GetCurrentPosition() const
  {
    return InterpolatePoint(m_startPt, m_endPt,
                            my::clamp(GetElapsedTime() / m_moveDuration, 0.0, 1.0));
  }

  bool IsMovingActive() const { return m_moveDuration > 0.0; }

  double GetCurrentAzimut() const
  {
    return m_angleInterpolator.Interpolate(my::clamp(GetElapsedTime() / m_rotateDuration, 0.0, 1.0));
  }

  bool IsRotatingActive() const { return m_rotateDuration > 0.0; }

private:
  m2::PointD m_startPt;
  m2::PointD m_endPt;
  double m_moveDuration;

  InerpolateAngle m_angleInterpolator;
  double m_rotateDuration;
};

MyPositionController::MyPositionController(location::EMyPositionMode initMode)
  : m_modeInfo(location::MODE_PENDING_POSITION)
  , m_afterPendingMode(location::MODE_FOLLOW)
  , m_errorRadius(0.0)
  , m_position(m2::PointD::Zero())
  , m_drawDirection(0.0)
  , m_lastGPSBearing(false)
  , m_positionYOffset(POSITION_Y_OFFSET)
  , m_isVisible(false)
  , m_isDirtyViewport(false)
  , m_isPositionAssigned(false)
{
  if (initMode > location::MODE_UNKNOWN_POSITION)
    m_afterPendingMode = initMode;
  else
    m_modeInfo = location::MODE_UNKNOWN_POSITION;
}

MyPositionController::~MyPositionController()
{
  m_anim.reset();
}

void MyPositionController::OnNewPixelRect()
{
  Follow();
}

void MyPositionController::UpdatePixelPosition(ScreenBase const & screen)
{
  m_pixelRect = screen.isPerspective() ? screen.PixelRectIn3d() : screen.PixelRect();
  m_positionYOffset = screen.isPerspective() ? POSITION_Y_OFFSET_3D : POSITION_Y_OFFSET;
  m_pixelPositionRaF = screen.P3dtoP(GetRaFPixelBinding());
  m_pixelPositionF = screen.P3dtoP(m_pixelRect.Center());
}

void MyPositionController::SetListener(ref_ptr<MyPositionController::Listener> listener)
{
  m_listener = listener;
}

m2::PointD const & MyPositionController::Position() const
{
  return m_position;
}

double MyPositionController::GetErrorRadius() const
{
  return m_errorRadius;
}

bool MyPositionController::IsModeChangeViewport() const
{
  return GetMode() >= location::MODE_FOLLOW;
}

bool MyPositionController::IsModeHasPosition() const
{
  return GetMode() >= location::MODE_NOT_FOLLOW;
}

void MyPositionController::DragStarted()
{
  SetModeInfo(SetModeBit(m_modeInfo, BlockAnimation));
}

void MyPositionController::DragEnded(m2::PointD const & distance)
{
  float const kBindingDistance = 0.1;
  SetModeInfo(ResetModeBit(m_modeInfo, BlockAnimation));
  if (distance.Length() > kBindingDistance * min(m_pixelRect.SizeX(), m_pixelRect.SizeY()))
    StopLocationFollow();

  Follow();
}

void MyPositionController::ScaleStarted()
{
  SetModeInfo(SetModeBit(m_modeInfo, BlockAnimation));
}

void MyPositionController::Rotated()
{
  location::EMyPositionMode mode = GetMode();
  if (mode == location::MODE_ROTATE_AND_FOLLOW)
    SetModeInfo(SetModeBit(m_modeInfo, StopFollowOnActionEnd));
}

void MyPositionController::CorrectScalePoint(m2::PointD & pt) const
{
  if (IsModeChangeViewport())
    pt = GetCurrentPixelBinding();
}

void MyPositionController::CorrectScalePoint(m2::PointD & pt1, m2::PointD & pt2) const
{
  if (IsModeChangeViewport())
  {
    m2::PointD const oldPt1(pt1);
    pt1 = GetCurrentPixelBinding();
    pt2 = pt2 - oldPt1 + pt1;
  }
}

void MyPositionController::CorrectGlobalScalePoint(m2::PointD & pt) const
{
  if (IsModeChangeViewport())
    pt = m_position;
}

void MyPositionController::ScaleEnded()
{
  SetModeInfo(ResetModeBit(m_modeInfo, BlockAnimation));
  if (TestModeBit(m_modeInfo, StopFollowOnActionEnd))
  {
    SetModeInfo(ResetModeBit(m_modeInfo, StopFollowOnActionEnd));
    StopLocationFollow();
  }

  Follow();
}

void MyPositionController::SetRenderShape(drape_ptr<MyPosition> && shape)
{
  m_shape = move(shape);
}

void MyPositionController::NextMode(int preferredZoomLevel)
{
  string const kAlohalyticsClickEvent = "$onClick";
  location::EMyPositionMode currentMode = GetMode();
  location::EMyPositionMode newMode = currentMode;

  if (!IsInRouting())
  {
    switch (currentMode)
    {
    case location::MODE_UNKNOWN_POSITION:
      alohalytics::LogEvent(kAlohalyticsClickEvent, "@UnknownPosition");
      newMode = location::MODE_PENDING_POSITION;
      break;
    case location::MODE_PENDING_POSITION:
      alohalytics::LogEvent(kAlohalyticsClickEvent, "@PendingPosition");
      newMode = location::MODE_UNKNOWN_POSITION;
      m_afterPendingMode = location::MODE_FOLLOW;
      break;
    case location::MODE_NOT_FOLLOW:
      alohalytics::LogEvent(kAlohalyticsClickEvent, "@NotFollow");
      newMode = location::MODE_FOLLOW;
      break;
    case location::MODE_FOLLOW:
      alohalytics::LogEvent(kAlohalyticsClickEvent, "@Follow");
      if (IsRotationActive())
        newMode = location::MODE_ROTATE_AND_FOLLOW;
      else
      {
        newMode = location::MODE_UNKNOWN_POSITION;
        m_afterPendingMode = location::MODE_FOLLOW;
      }
      break;
    case location::MODE_ROTATE_AND_FOLLOW:
      alohalytics::LogEvent(kAlohalyticsClickEvent, "@RotateAndFollow");
      newMode = location::MODE_UNKNOWN_POSITION;
      m_afterPendingMode = location::MODE_FOLLOW;
      break;
    }
  }
  else
  {
    newMode = IsRotationActive() ? location::MODE_ROTATE_AND_FOLLOW : location::MODE_FOLLOW;
  }

  SetModeInfo(ChangeMode(m_modeInfo, newMode), IsInRouting());
  Follow(preferredZoomLevel);
}

void MyPositionController::TurnOff()
{
  StopLocationFollow();
  SetModeInfo(location::MODE_UNKNOWN_POSITION);
  SetIsVisible(false);
}

void MyPositionController::Invalidate()
{
  location::EMyPositionMode currentMode = GetMode();
  if (currentMode > location::MODE_PENDING_POSITION)
  {
    SetModeInfo(ChangeMode(m_modeInfo, location::MODE_UNKNOWN_POSITION));
    SetModeInfo(ChangeMode(m_modeInfo, location::MODE_PENDING_POSITION));
    m_afterPendingMode = currentMode;
    SetIsVisible(true);
  }
  else if (currentMode == location::MODE_UNKNOWN_POSITION)
  {
    m_afterPendingMode = location::MODE_FOLLOW;
    SetIsVisible(false);
  }
}

void MyPositionController::OnLocationUpdate(location::GpsInfo const & info, bool isNavigable,
                                            ScreenBase const & screen)
{
  Assign(info, isNavigable, screen);

  SetIsVisible(true);

  if (GetMode() == location::MODE_PENDING_POSITION)
  {
    SetModeInfo(ChangeMode(m_modeInfo, m_afterPendingMode));
    m_afterPendingMode = location::MODE_FOLLOW;
  }
}

void MyPositionController::OnCompassUpdate(location::CompassInfo const & info,
                                           ScreenBase const & screen)
{
  Assign(info, screen);
}

void MyPositionController::SetModeListener(location::TMyPositionModeChanged const & fn)
{
  m_modeChangeCallback = fn;
  CallModeListener(m_modeInfo);
}

void MyPositionController::Render(uint32_t renderMode, ScreenBase const & screen, ref_ptr<dp::GpuProgramManager> mng,
                                  dp::UniformValuesStorage const  & commonUniforms)
{
  location::EMyPositionMode currentMode = GetMode();
  if (m_shape != nullptr && IsVisible() && currentMode > location::MODE_PENDING_POSITION)
  {
    if (m_isDirtyViewport && !TestModeBit(m_modeInfo, BlockAnimation))
    {
      Follow();
      m_isDirtyViewport = false;
    }

    if (!IsModeChangeViewport())
      m_isPendingAnimation = false;

    m_shape->SetPosition(GetDrawablePosition());
    m_shape->SetAzimuth(GetDrawableAzimut());
    m_shape->SetIsValidAzimuth(IsRotationActive());
    m_shape->SetAccuracy(m_errorRadius);
    m_shape->SetRoutingMode(IsInRouting());

    if ((renderMode & RenderAccuracy) != 0)
      m_shape->RenderAccuracy(screen, mng, commonUniforms);

    if ((renderMode & RenderMyPosition) != 0)
      m_shape->RenderMyPosition(screen, mng, commonUniforms);
  }

  CheckAnimFinished();
}

bool MyPositionController::IsFollowingActive() const
{
  return IsInRouting() && GetMode() == location::MODE_ROTATE_AND_FOLLOW;
}

void MyPositionController::AnimateStateTransition(location::EMyPositionMode oldMode, location::EMyPositionMode newMode)
{
  if (oldMode == location::MODE_PENDING_POSITION && newMode == location::MODE_FOLLOW)
  {
    ChangeModelView(m_position, -1);
  }
  else if (oldMode == location::MODE_ROTATE_AND_FOLLOW &&
           (newMode == location::MODE_FOLLOW || newMode == location::MODE_UNKNOWN_POSITION))
  {
    ChangeModelView(m_position, 0.0, m_pixelPositionF, -1);
  }
}

bool MyPositionController::AlmostCurrentPosition(const m2::PointD & pos) const
{
  double const kPositionEqualityDelta = 1e-5;

  return pos.EqualDxDy(m_position, kPositionEqualityDelta);
}

bool MyPositionController::AlmostCurrentAzimut(double azimut) const
{
  double const kDirectionEqualityDelta = 1e-5;

  return my::AlmostEqualAbs(azimut, m_drawDirection, kDirectionEqualityDelta);
}

void MyPositionController::Assign(location::GpsInfo const & info, bool isNavigable, ScreenBase const & screen)
{
  m2::PointD oldPos = GetDrawablePosition();
  double oldAzimut = GetDrawableAzimut();

  m2::RectD rect = MercatorBounds::MetresToXY(info.m_longitude,
                                              info.m_latitude,
                                              info.m_horizontalAccuracy);
  m_position = rect.Center();
  m_errorRadius = rect.SizeX() / 2;

  bool const hasBearing = info.HasBearing();
  if ((isNavigable && hasBearing) ||
      (!isNavigable && hasBearing && info.HasSpeed() && info.m_speed > MIN_SPEED_THRESHOLD_MPS))
  {
    SetDirection(my::DegToRad(info.m_bearing));
    m_lastGPSBearing.Reset();
  }

  if (m_listener)
    m_listener->PositionChanged(Position());

  if (m_isPositionAssigned && (!AlmostCurrentPosition(oldPos) || !AlmostCurrentAzimut(oldAzimut)))
  {
    CreateAnim(oldPos, oldAzimut, screen);
    m_isDirtyViewport = true;
  }

  m_isPositionAssigned = true;
}

void MyPositionController::Assign(location::CompassInfo const & info, ScreenBase const & screen)
{
  double oldAzimut = GetDrawableAzimut();

  if ((IsInRouting() && GetMode() >= location::MODE_FOLLOW) ||
      (m_lastGPSBearing.ElapsedSeconds() < GPS_BEARING_LIFETIME_S))
  {
    return;
  }

  SetDirection(info.m_bearing);

  if (m_isPositionAssigned && !AlmostCurrentAzimut(oldAzimut) && GetMode() == location::MODE_ROTATE_AND_FOLLOW)
  {
    CreateAnim(GetDrawablePosition(), oldAzimut, screen);
    m_isDirtyViewport = true;
  }

  m_isPositionAssigned = true;
}

void MyPositionController::SetDirection(double bearing)
{
  m_drawDirection = bearing;
  SetModeInfo(SetModeBit(m_modeInfo, KnownDirectionBit));
}

void MyPositionController::SetModeInfo(uint32_t modeInfo, bool force)
{
  location::EMyPositionMode const newMode = ResetAllModeBits(modeInfo);
  location::EMyPositionMode const oldMode = GetMode();
  m_modeInfo = modeInfo;
  if (newMode != oldMode || force)
  {
    AnimateStateTransition(oldMode, newMode);
    CallModeListener(newMode);
  }
}

location::EMyPositionMode MyPositionController::GetMode() const
{
  return ResetAllModeBits(m_modeInfo);
}

void MyPositionController::CallModeListener(uint32_t mode)
{
  if (m_modeChangeCallback != nullptr)
    m_modeChangeCallback(ResetAllModeBits(mode));
}

bool MyPositionController::IsInRouting() const
{
  return TestModeBit(m_modeInfo, RoutingSessionBit);
}

bool MyPositionController::IsRotationActive() const
{
  return TestModeBit(m_modeInfo, KnownDirectionBit);
}

void MyPositionController::StopLocationFollow()
{
  location::EMyPositionMode currentMode = GetMode();
  if (currentMode > location::MODE_NOT_FOLLOW)
    SetModeInfo(ChangeMode(m_modeInfo, location::MODE_NOT_FOLLOW));
  else if (currentMode == location::MODE_PENDING_POSITION)
    m_afterPendingMode = location::MODE_NOT_FOLLOW;
}

bool MyPositionController::StopCompassFollow()
{
  if (GetMode() != location::MODE_ROTATE_AND_FOLLOW)
    return false;

  SetModeInfo(ChangeMode(m_modeInfo, location::MODE_FOLLOW));
  Follow();

  return true;
}

void MyPositionController::ChangeModelView(m2::PointD const & center, int zoomLevel)
{
  if (m_listener)
    m_listener->ChangeModelView(center, zoomLevel);
}

void MyPositionController::ChangeModelView(double azimuth)
{
  if (m_listener)
    m_listener->ChangeModelView(azimuth);
}

void MyPositionController::ChangeModelView(m2::RectD const & rect)
{
  if (m_listener)
    m_listener->ChangeModelView(rect);
}

void MyPositionController::ChangeModelView(m2::PointD const & userPos, double azimuth,
                                           m2::PointD const & pxZero, int preferredZoomLevel)
{
  if (m_listener)
    m_listener->ChangeModelView(userPos, azimuth, pxZero, preferredZoomLevel);
}

void MyPositionController::Follow(int preferredZoomLevel)
{
  location::EMyPositionMode currentMode = GetMode();
  if (currentMode == location::MODE_FOLLOW)
    ChangeModelView(m_position, preferredZoomLevel);
  else if (currentMode == location::MODE_ROTATE_AND_FOLLOW)
    ChangeModelView(m_position, m_drawDirection, m_pixelPositionRaF, preferredZoomLevel);
}

m2::PointD MyPositionController::GetRaFPixelBinding() const
{
  return m2::PointD(m_pixelRect.Center().x,
                    m_pixelRect.maxY() - m_positionYOffset * VisualParams::Instance().GetVisualScale());
}

m2::PointD MyPositionController::GetCurrentPixelBinding() const
{
  location::EMyPositionMode mode = GetMode();
  if (mode == location::MODE_FOLLOW)
    return m_pixelRect.Center();
  else if (mode == location::MODE_ROTATE_AND_FOLLOW)
    return GetRaFPixelBinding();
  else
    ASSERT(false, ());

  return m2::PointD::Zero();
}

m2::PointD MyPositionController::GetDrawablePosition() const
{
  if (m_anim && m_anim->IsMovingActive())
    return m_anim->GetCurrentPosition();

  if (m_isPendingAnimation)
    return m_oldPosition;

  return m_position;
}

double MyPositionController::GetDrawableAzimut() const
{
  if (m_anim && m_anim->IsRotatingActive())
    return m_anim->GetCurrentAzimut();

  if (m_isPendingAnimation)
    return m_oldDrawDirection;

  return m_drawDirection;
}

void MyPositionController::CheckAnimFinished() const
{
  if (m_anim && m_anim->IsFinished())
    m_anim.reset();
}

void MyPositionController::AnimationStarted(ref_ptr<BaseModelViewAnimation> anim)
{
  if (m_isPendingAnimation && m_animCreator != nullptr && anim != nullptr &&
      (anim->GetType() == ModelViewAnimationType::FollowAndRotate ||
       anim->GetType() == ModelViewAnimationType::Default))
  {
    m_isPendingAnimation = false;
    m_animCreator();
  }
}

void MyPositionController::CreateAnim(m2::PointD const & oldPos, double oldAzimut, ScreenBase const & screen)
{
  double const moveDuration = ModelViewAnimation::GetMoveDuration(oldPos, m_position, screen);
  double const rotateDuration = ModelViewAnimation::GetRotateDuration(oldAzimut, m_drawDirection);
  if (df::IsAnimationAllowed(max(moveDuration, rotateDuration), screen))
  {
    if (IsModeChangeViewport())
    {
      m_animCreator = [this, oldPos, moveDuration, oldAzimut, rotateDuration]()
      {
        m_anim = make_unique_dp<MyPositionAnim>(oldPos, m_position, moveDuration, oldAzimut, m_drawDirection, rotateDuration);
      };
      m_oldPosition = oldPos;
      m_oldDrawDirection = oldAzimut;
      m_isPendingAnimation = true;
    }
    else
    {
      m_anim = make_unique_dp<MyPositionAnim>(oldPos, m_position, moveDuration, oldAzimut, m_drawDirection, rotateDuration);
    }
  }
}

void MyPositionController::ActivateRouting()
{
  if (!IsInRouting())
  {
    location::EMyPositionMode newMode = GetMode();
    if (IsModeHasPosition())
      newMode = location::MODE_NOT_FOLLOW;

    SetModeInfo(ChangeMode(SetModeBit(m_modeInfo, RoutingSessionBit), newMode));
  }
}

void MyPositionController::DeactivateRouting()
{
  if (IsInRouting())
  {
    SetModeInfo(ResetModeBit(m_modeInfo, RoutingSessionBit));

    location::EMyPositionMode currentMode = GetMode();
    if (currentMode == location::MODE_ROTATE_AND_FOLLOW)
      SetModeInfo(ChangeMode(m_modeInfo, location::MODE_FOLLOW));
    else
      ChangeModelView(0.0);
  }
}

}
