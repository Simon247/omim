#pragma once

#include "drape_frontend/backend_renderer.hpp"
#include "drape_frontend/color_constants.hpp"
#include "drape_frontend/frontend_renderer.hpp"
#include "drape_frontend/threads_commutator.hpp"
#include "drape_frontend/selection_shape.hpp"

#include "drape/pointers.hpp"
#include "drape/texture_manager.hpp"

#include "platform/location.hpp"

#include "geometry/polyline2d.hpp"
#include "geometry/screenbase.hpp"

#include "base/strings_bundle.hpp"

#include "std/map.hpp"
#include "std/mutex.hpp"

namespace dp { class OGLContextFactory; }

namespace df
{

class UserMarksProvider;
class MapDataProvider;
class Viewport;

class DrapeEngine
{
public:
  struct Params
  {
    Params(ref_ptr<dp::OGLContextFactory> factory,
           ref_ptr<StringsBundle> stringBundle,
           Viewport const & viewport,
           MapDataProvider const & model,
           double vs,
           gui::TWidgetsInitInfo && info,
           pair<location::EMyPositionMode, bool> const & initialMyPositionMode,
           bool allow3dBuildings,
           bool blockTapEvents,
           bool showChoosePositionMark)
      : m_factory(factory)
      , m_stringsBundle(stringBundle)
      , m_viewport(viewport)
      , m_model(model)
      , m_vs(vs)
      , m_info(move(info))
      , m_initialMyPositionMode(initialMyPositionMode)
      , m_allow3dBuildings(allow3dBuildings)
      , m_blockTapEvents(blockTapEvents)
      , m_showChoosePositionMark(showChoosePositionMark)
    {}

    ref_ptr<dp::OGLContextFactory> m_factory;
    ref_ptr<StringsBundle> m_stringsBundle;
    Viewport m_viewport;
    MapDataProvider m_model;
    double m_vs;
    gui::TWidgetsInitInfo m_info;
    pair<location::EMyPositionMode, bool> m_initialMyPositionMode;
    bool m_allow3dBuildings;
    bool m_blockTapEvents;
    bool m_showChoosePositionMark;
  };

  DrapeEngine(Params && params);
  ~DrapeEngine();

  void Resize(int w, int h);
  void Invalidate();

  void AddTouchEvent(TouchEvent const & event);
  void Scale(double factor, m2::PointD const & pxPoint, bool isAnim);

  /// if zoom == -1, then current zoom will not change
  void SetModelViewCenter(m2::PointD const & centerPt, int zoom, bool isAnim);
  void SetModelViewRect(m2::RectD const & rect, bool applyRotation, int zoom, bool isAnim);
  void SetModelViewAnyRect(m2::AnyRectD const & rect, bool isAnim);

  using TModelViewListenerFn = FrontendRenderer::TModelViewChanged;
  int AddModelViewListener(TModelViewListenerFn const & listener);
  void RemoveModeViewListener(int slotID);

  void ClearUserMarksLayer(TileKey const & tileKey);
  void ChangeVisibilityUserMarksLayer(TileKey const & tileKey, bool isVisible);
  void UpdateUserMarksLayer(TileKey const & tileKey, UserMarksProvider * provider);

  void SetRenderingEnabled(bool const isEnabled);
  void InvalidateRect(m2::RectD const & rect);
  void UpdateMapStyle();

  void SetCompassInfo(location::CompassInfo const & info);
  void SetGpsInfo(location::GpsInfo const & info, bool isNavigable, location::RouteMatchingInfo const & routeInfo);
  void MyPositionNextMode(int preferredZoomLevel = -1);
  void CancelMyPosition();
  void StopLocationFollow();
  void InvalidateMyPosition();
  void SetMyPositionModeListener(location::TMyPositionModeChanged const & fn);

  using TTapEventInfoFn = FrontendRenderer::TTapEventInfoFn;
  void SetTapEventInfoListener(TTapEventInfoFn const & fn);
  using TUserPositionChangedFn = FrontendRenderer::TUserPositionChangedFn;
  void SetUserPositionListener(TUserPositionChangedFn const & fn);

  FeatureID GetVisiblePOI(m2::PointD const & glbPoint);
  void SelectObject(SelectionShape::ESelectedObject obj, m2::PointD const & pt, bool isAnim);
  void DeselectObject();
  bool GetMyPosition(m2::PointD & myPosition);
  SelectionShape::ESelectedObject GetSelectedObject();

  void AddRoute(m2::PolylineD const & routePolyline, vector<double> const & turns,
                df::ColorConstant color);
  void RemoveRoute(bool deactivateFollowing);
  void FollowRoute(int preferredZoomLevel, int preferredZoomLevel3d, double rotationAngle, double angleFOV);
  void DeactivateRouteFollowing();
  void SetRoutePoint(m2::PointD const & position, bool isStart, bool isValid);

  void SetWidgetLayout(gui::TWidgetsLayoutInfo && info);
  gui::TWidgetsSizeInfo const & GetWidgetSizes();

  void Allow3dMode(bool allowPerspectiveInNavigation, bool allow3dBuildings, double rotationAngle, double angleFOV);
  void EnablePerspective(double rotationAngle, double angleFOV);

  void UpdateGpsTrackPoints(vector<df::GpsTrackPoint> && toAdd, vector<uint32_t> && toRemove);
  void ClearGpsTrackPoints();

  void EnableChoosePositionMode(bool enable);
  void BlockTapEvents(bool block);

  void SetKineticScrollEnabled(bool enabled);

private:
  void AddUserEvent(UserEvent const & e);
  void ModelViewChanged(ScreenBase const & screen);
  void ModelViewChangedGuiThread(ScreenBase const & screen);

  void MyPositionModeChanged(location::EMyPositionMode mode);
  void TapEvent(TapInfo const & tapInfo);
  void UserPositionChanged(m2::PointD const & position);

  void ResizeImpl(int w, int h);
  void RecacheGui(bool needResetOldGui);

private:
  drape_ptr<FrontendRenderer> m_frontend;
  drape_ptr<BackendRenderer> m_backend;
  drape_ptr<ThreadsCommutator> m_threadCommutator;
  drape_ptr<dp::TextureManager> m_textureManager;
  drape_ptr<RequestedTiles> m_requestedTiles;

  Viewport m_viewport;

  using TListenerMap = map<int, TModelViewListenerFn>;
  TListenerMap m_listeners;

  location::TMyPositionModeChanged m_myPositionModeChanged;
  TTapEventInfoFn m_tapListener;
  TUserPositionChangedFn m_userPositionChangedFn;

  gui::TWidgetsInitInfo m_widgetsInfo;
  gui::TWidgetsSizeInfo m_widgetSizes;
  gui::TWidgetsLayoutInfo m_widgetsLayout;

  bool m_choosePositionMode = false;
  bool m_kineticScrollEnabled = true;
};

} // namespace df
