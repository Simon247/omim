#import "Common.h"
#import "LocationManager.h"
#import "MapsAppDelegate.h"
#import "MapViewController.h"
#import "MWMMapViewControlsManager.h"
#import "Statistics.h"

#import "3party/Alohalytics/src/alohalytics_objc.h"

#include "Framework.h"

#include "map/gps_tracker.hpp"
#include "platform/measurement_utils.hpp"
#include "platform/settings.hpp"
#include "base/math.hpp"

#include "platform/file_logging.hpp"

static CLAuthorizationStatus const kRequestAuthStatus = kCLAuthorizationStatusAuthorizedAlways;
static NSString * const kAlohalyticsLocationRequestAlwaysFailed = @"$locationAlwaysRequestErrorDenied";

@interface LocationManager ()

@property (nonatomic) BOOL deferringUpdates;

@property (nonatomic) CLLocationManager * locationManager;

@property (nonatomic) BOOL isStarted;
@property (nonatomic) NSMutableSet * observers;
@property (nonatomic) NSDate * lastLocationTime;

@end

@implementation LocationManager

- (void)batteryStateChangedNotification:(NSNotification *)notification
{
  [self refreshAccuracy];
}

- (void)refreshAccuracy
{
  UIDeviceBatteryState state = [UIDevice currentDevice].batteryState;
  self.locationManager.desiredAccuracy = (state == UIDeviceBatteryStateCharging) ? kCLLocationAccuracyBestForNavigation : kCLLocationAccuracyBest;
}

- (void)dealloc
{
  [self reset];
}

- (void)onDaemonMode
{
  [self.locationManager stopMonitoringSignificantLocationChanges];
  [self.locationManager startUpdatingLocation];
}

- (void)onBackground
{
  if (!GpsTracker::Instance().IsEnabled())
    [self.locationManager stopUpdatingLocation];
}

- (void)beforeTerminate
{
  if (!GpsTracker::Instance().IsEnabled())
    return;
  [self.locationManager startMonitoringSignificantLocationChanges];
}

- (void)onForeground
{
  [self onDaemonMode];
  [self.locationManager disallowDeferredLocationUpdates];
  self.deferringUpdates = NO;
  [self orientationChanged];
}

- (void)start:(id <LocationObserver>)observer
{
  if (!self.isStarted)
  {
    //YES if location services are enabled; NO if they are not.
    if ([CLLocationManager locationServicesEnabled])
    {
      CLAuthorizationStatus const authStatus = [CLLocationManager authorizationStatus];
      switch (authStatus)
      {
        case kCLAuthorizationStatusAuthorizedWhenInUse:
        case kCLAuthorizationStatusAuthorizedAlways:
        case kCLAuthorizationStatusNotDetermined:
          if (kRequestAuthStatus == kCLAuthorizationStatusAuthorizedAlways && [self.locationManager respondsToSelector:@selector(requestAlwaysAuthorization)])
            [self.locationManager requestAlwaysAuthorization];
          [self.locationManager startUpdatingLocation];
          if ([CLLocationManager headingAvailable])
            [self.locationManager startUpdatingHeading];
          self.isStarted = YES;
          [self.observers addObject:observer];
          break;
        case kCLAuthorizationStatusRestricted:
        case kCLAuthorizationStatusDenied:
          [self observer:observer onLocationError:location::EDenied];
          break;
      }
    }
    else
      [self observer:observer onLocationError:location::EDenied];
  }
  else
  {
    BOOL updateLocation = ![self.observers containsObject:observer];
    [self.observers addObject:observer];
    if ([self lastLocationIsValid] && updateLocation)
    {
      // pass last location known location when new observer is attached
      // (default CLLocationManagerDelegate behaviour)
      [observer onLocationUpdate:gpsInfoFromLocation(self.lastLocation)];
    }
  }
}

- (void)stop:(id <LocationObserver>)observer
{
  [self.observers removeObject:observer];
  if (self.isStarted && self.observers.count == 0)
  {
    // stop only if no more observers are subsribed
    self.isStarted = NO;
    if ([CLLocationManager headingAvailable])
      [self.locationManager stopUpdatingHeading];
    [self.locationManager stopUpdatingLocation];
  }
}

- (CLLocation *)lastLocation
{
  return self.isStarted ? self.locationManager.location : nil;
}

- (CLHeading *)lastHeading
{
  return self.isStarted ? self.locationManager.heading : nil;
}

- (void)triggerCompass
{
  [self locationManager:self.locationManager didUpdateHeading:self.locationManager.heading];
}

- (void)locationManager:(CLLocationManager *)manager didUpdateHeading:(CLHeading *)heading
{
  [self notifyCompassUpdate:compasInfoFromHeading(heading)];
}

- (void)locationManager:(CLLocationManager *)manager didUpdateLocations:(NSArray<CLLocation *> *)locations
{
  [self processLocation:locations.lastObject];
  if (!self.deferringUpdates)
    return;
  [self.locationManager allowDeferredLocationUpdatesUntilTraveled:300 timeout:15];
  self.deferringUpdates = NO;
}

- (BOOL)deferringUpdates
{
  return _deferringUpdates && [CLLocationManager deferredLocationUpdatesAvailable] &&
                              [UIApplication sharedApplication].applicationState == UIApplicationStateBackground;
}

- (void)processLocation:(CLLocation *)location
{
  // According to documentation, lat and lon are valid only if horz acc is non-negative.
  // So we filter out such events completely.
  if (location.horizontalAccuracy < 0.)
    return;

  // Save current device time for location.
  self.lastLocationTime = [NSDate date];
  [[Statistics instance] logLocation:location];
  auto const newInfo = gpsInfoFromLocation(location);
  if (!MapsAppDelegate.theApp.isDaemonMode)
  {
    for (id observer in self.observers.copy)
       [observer onLocationUpdate:newInfo];
    // TODO(AlexZ): Temporary, remove in the future.
  }
  GpsTracker::Instance().OnLocationUpdated(newInfo);
}

- (void)locationManager:(CLLocationManager *)manager didFinishDeferredUpdatesWithError:(NSError *)error
{
  self.deferringUpdates = YES;
}

- (void)locationManager:(CLLocationManager *)manager didFailWithError:(NSError *)error
{
  NSLog(@"locationManager failed with error: %ld, %@", (long)error.code, error.description);
  if (error.code == kCLErrorDenied)
  {
    if (kRequestAuthStatus == kCLAuthorizationStatusAuthorizedAlways && [self.locationManager respondsToSelector:@selector(requestAlwaysAuthorization)])
      [Alohalytics logEvent:kAlohalyticsLocationRequestAlwaysFailed];
    for (id observer in self.observers.copy)
      [self observer:observer onLocationError:location::EDenied];
  }
}

- (BOOL)locationManagerShouldDisplayHeadingCalibration:(CLLocationManager *)manager
{
  if (MapsAppDelegate.theApp.isDaemonMode)
    return NO;
  bool on = false;
  settings::Get("CompassCalibrationEnabled", on);
  if (!on)
    return NO;

  if (!MapsAppDelegate.theApp.mapViewController.controlsManager.searchHidden)
    return NO;
  if (!manager.heading)
    return YES;
  else if (manager.heading.headingAccuracy < 0)
    return YES;
  else if (manager.heading.headingAccuracy > 5)
    return YES;
  return NO;
}

- (bool)getLat:(double &)lat Lon:(double &)lon
{
  CLLocation * l = [self lastLocation];

  // Return last saved location if it's not later than 5 minutes.
  if ([self lastLocationIsValid])
  {
    lat = l.coordinate.latitude;
    lon = l.coordinate.longitude;
    return true;
  }

  return false;
}

- (bool)getNorthRad:(double &)rad
{
  CLHeading * h = [self lastHeading];

  if (h != nil)
  {
    rad = (h.trueHeading < 0) ? h.magneticHeading : h.trueHeading;
    rad = my::DegToRad(rad);
    return true;
  }

  return false;
}

+ (NSString *)formattedDistance:(double)meters
{
  if (meters < 0.)
    return nil;

  string s;
  MeasurementUtils::FormatDistance(meters, s);
  return @(s.c_str());
}

+ (char const *)getSpeedSymbol:(double)metersPerSecond
{
  // 0-1 m/s
  static char const * turtle = "\xF0\x9F\x90\xA2 ";
  // 1-2 m/s
  static char const * pedestrian = "\xF0\x9F\x9A\xB6 ";
  // 2-5 m/s
  static char const * tractor = "\xF0\x9F\x9A\x9C ";
  // 5-10 m/s
  static char const * bicycle = "\xF0\x9F\x9A\xB2 ";
  // 10-36 m/s
  static char const * car = "\xF0\x9F\x9A\x97 ";
  // 36-120 m/s
  static char const * train = "\xF0\x9F\x9A\x85 ";
  // 120-278 m/s
  static char const * airplane = "\xE2\x9C\x88\xEF\xB8\x8F ";
  // 278+
  static char const * rocket = "\xF0\x9F\x9A\x80 ";

  if (metersPerSecond <= 1.) return turtle;
  else if (metersPerSecond <= 2.) return pedestrian;
  else if (metersPerSecond <= 5.) return tractor;
  else if (metersPerSecond <= 10.) return bicycle;
  else if (metersPerSecond <= 36.) return car;
  else if (metersPerSecond <= 120.) return train;
  else if (metersPerSecond <= 278.) return airplane;
  else return rocket;
}

- (NSString *)formattedSpeedAndAltitude:(BOOL &)hasSpeed
{
  hasSpeed = NO;
  CLLocation * l = [self lastLocation];
  if (l)
  {
    string result;
    if (l.altitude)
      result = "\xE2\x96\xB2 " /* this is simple mountain symbol */ + MeasurementUtils::FormatAltitude(l.altitude);
    // Speed is actual only for just received location
    if (l.speed > 0. && [l.timestamp timeIntervalSinceNow] >= -2.0)
    {
      hasSpeed = YES;
      if (!result.empty())
        result += "   ";
      result += [LocationManager getSpeedSymbol:l.speed] + MeasurementUtils::FormatSpeed(l.speed);
    }
    return result.empty() ? nil : @(result.c_str());
  }
  return nil;
}

- (bool)lastLocationIsValid
{
  return (([self lastLocation] != nil) && ([self.lastLocationTime timeIntervalSinceNow] > -300.0));
}

- (bool)isLocationModeUnknownOrPending
{
  using location::EMyPositionMode;
  EMyPositionMode mode;
  if (!settings::Get(settings::kLocationStateMode, mode))
    return true;
  return mode == EMyPositionMode::MODE_PENDING_POSITION || mode == EMyPositionMode::MODE_UNKNOWN_POSITION;
}

- (BOOL)enabledOnMap
{
  for (id observer in self.observers.copy)
    if ([observer isKindOfClass:[MapViewController class]])
      return YES;
  return NO;
}

- (void)orientationChanged
{
  self.locationManager.headingOrientation = (CLDeviceOrientation)[UIDevice currentDevice].orientation;
}

- (void)notifyCompassUpdate:(location::CompassInfo const &)newInfo
{
  for (id observer in self.observers.copy)
    if ([observer respondsToSelector:@selector(onCompassUpdate:)])
      [observer onCompassUpdate:newInfo];
}

- (void)observer:(id<LocationObserver>)observer onLocationError:(location::TLocationError)errorCode
{
  if ([(NSObject *)observer respondsToSelector:@selector(onLocationError:)])
    [observer onLocationError:errorCode];
}

location::GpsInfo gpsInfoFromLocation(CLLocation const * l)
{
  location::GpsInfo info;
  info.m_source = location::EAppleNative;

  info.m_latitude = l.coordinate.latitude;
  info.m_longitude = l.coordinate.longitude;
  info.m_timestamp = [l.timestamp timeIntervalSince1970];

  if (l.horizontalAccuracy >= 0.0)
    info.m_horizontalAccuracy = l.horizontalAccuracy;

  if (l.verticalAccuracy >= 0.0)
  {
    info.m_verticalAccuracy = l.verticalAccuracy;
    info.m_altitude = l.altitude;
  }

  if (l.course >= 0.0)
    info.m_bearing = l.course;

  if (l.speed >= 0.0)
    info.m_speed = l.speed;
  return info;
}

location::CompassInfo compasInfoFromHeading(CLHeading const * h)
{
  location::CompassInfo info;
  if (h.trueHeading >= 0.0)
    info.m_bearing = my::DegToRad(h.trueHeading);
  else if (h.headingAccuracy >= 0.0)
    info.m_bearing = my::DegToRad(h.magneticHeading);
  return info;
}

- (void)reset
{
  _isStarted = NO;
  _lastLocationTime = nil;
  _observers = [NSMutableSet set];
  _locationManager.delegate = nil;
  _locationManager = nil;
  [[NSNotificationCenter defaultCenter] removeObserver:self];
}

#pragma mark - Properties

- (CLLocationManager *)locationManager
{
  if (!_locationManager)
  {
    [self reset];
    _locationManager = [[CLLocationManager alloc] init];
    _locationManager.delegate = self;
    [UIDevice currentDevice].batteryMonitoringEnabled = YES;
    [self refreshAccuracy];
    if (!(isIOS7 || isIOS8))
      _locationManager.allowsBackgroundLocationUpdates = YES;
    _locationManager.pausesLocationUpdatesAutomatically = YES;
    _locationManager.headingFilter = 3.0;

    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(orientationChanged) name:UIDeviceOrientationDidChangeNotification object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(batteryStateChangedNotification:) name:UIDeviceBatteryStateDidChangeNotification object:nil];
  }
  return _locationManager;
}

@end

@implementation CLLocation (Mercator)

- (m2::PointD)mercator
{
  return ToMercator(self.coordinate);
}

@end
