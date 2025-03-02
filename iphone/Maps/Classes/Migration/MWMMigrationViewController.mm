#import "LocationManager.h"
#import "MapsAppDelegate.h"
#import "MWMAlertViewController.h"
#import "MWMCircularProgress.h"
#import "MWMMapDownloaderViewController.h"
#import "MWMMigrationView.h"
#import "MWMMigrationViewController.h"
#import "MWMStorage.h"
#import "Statistics.h"

#include "Framework.h"

#include "platform/platform.hpp"
#include "storage/storage.hpp"

namespace
{
NSString * const kDownloaderSegue = @"Migration2MapDownloaderSegue";
} // namespace

using namespace storage;

@interface MWMStorage ()

+ (void)checkConnectionAndPerformAction:(TMWMVoidBlock)action alertController:(MWMAlertViewController *)alertController;

@end

@interface MWMMigrationViewController () <MWMCircularProgressProtocol>

@end

@implementation MWMMigrationViewController
{
  TCountryId m_countryId;
}

- (void)viewDidLoad
{
  [super viewDidLoad];
  m_countryId = kInvalidCountryId;
  [self configNavBar];
  [self checkState];
  static_cast<MWMMigrationView *>(self.view).delegate = self;
}

- (void)configNavBar
{
  self.title = L(@"download_maps");
}

- (void)checkState
{
  if (!GetFramework().IsEnoughSpaceForMigrate())
    [self setState:MWMMigrationViewState::ErrorNoSpace];
  else if (!Platform::IsConnected())
    [self setState:MWMMigrationViewState::ErrorNoConnection];
  else
    [self setState:MWMMigrationViewState::Default];
}

- (void)performLimitedMigration:(BOOL)limited
{
  [Statistics logEvent:kStatDownloaderMigrationStarted
        withParameters:@{kStatType : limited ? kStatCurrentMap : kStatAllMaps}];
  auto & f = GetFramework();
  LocationManager * lm = [MapsAppDelegate theApp].locationManager;
  ms::LatLon position{};
  if (![lm getLat:position.lat Lon:position.lon])
    position = MercatorBounds::ToLatLon(f.GetViewportCenter());

  auto migrate = ^
  {
    GetFramework().Migrate(!limited);
    [self performSegueWithIdentifier:kDownloaderSegue sender:self];
    [Statistics logEvent:kStatDownloaderMigrationCompleted];
  };

  auto onStatusChanged = [self, migrate](TCountryId const & countryId)
  {
    if (m_countryId == kInvalidCountryId || m_countryId != countryId)
      return;
    auto & f = GetFramework();
    auto s = f.Storage().GetPrefetchStorage();
    NodeStatuses nodeStatuses{};
    s->GetNodeStatuses(countryId, nodeStatuses);
    switch (nodeStatuses.m_status)
    {
      case NodeStatus::OnDisk:
        migrate();
        break;
      case NodeStatus::Undefined:
      case NodeStatus::Error:
        [self showError:nodeStatuses.m_error countryId:countryId]; break;
      default:
        break;
    }
  };

  auto onProgressChanged = [self](TCountryId const & countryId, TLocalAndRemoteSize const & progress)
  {
    MWMMigrationView * view = static_cast<MWMMigrationView *>(self.view);
    [view setProgress:static_cast<CGFloat>(progress.first) / progress.second];
  };

  [MWMStorage checkConnectionAndPerformAction:^
  {
    self->m_countryId = f.PreMigrate(position, onStatusChanged, onProgressChanged);
    if (self->m_countryId != kInvalidCountryId)
      [self setState:MWMMigrationViewState::Processing];
    else
      migrate();
  }
  alertController:self.alertController];
}

- (void)showError:(NodeErrorCode)errorCode countryId:(TCountryId const &)countryId
{
  [self setState:MWMMigrationViewState::Default];
  MWMAlertViewController * avc = self.alertController;
  auto const retryBlock = ^
  {
    GetFramework().Storage().GetPrefetchStorage()->RetryDownloadNode(self->m_countryId);
    [self setState:MWMMigrationViewState::Processing];
  };
  auto const cancelBlock = ^
  {
    GetFramework().Storage().GetPrefetchStorage()->CancelDownloadNode(self->m_countryId);
  };
  switch (errorCode)
  {
    case NodeErrorCode::NoError:
      break;
    case NodeErrorCode::UnknownError:
      [Statistics logEvent:kStatDownloaderMigrationError withParameters:@{kStatType : kStatUnknownError}];
      [avc presentDownloaderInternalErrorAlertWithOkBlock:retryBlock cancelBlock:cancelBlock];
      [avc presentInternalErrorAlert];
      break;
    case NodeErrorCode::OutOfMemFailed:
      [Statistics logEvent:kStatDownloaderMigrationError withParameters:@{kStatType : kStatNoSpace}];
      [avc presentDownloaderNotEnoughSpaceAlert];
      break;
    case NodeErrorCode::NoInetConnection:
      [Statistics logEvent:kStatDownloaderMigrationError withParameters:@{kStatType : kStatNoConnection}];
      [avc presentDownloaderNoConnectionAlertWithOkBlock:retryBlock cancelBlock:cancelBlock];
      break;
  }
}

- (void)setState:(MWMMigrationViewState)state
{
  MWMMigrationView * migrationView = static_cast<MWMMigrationView *>(self.view);
  if (state == MWMMigrationViewState::Processing)
  {
    NodeAttrs nodeAttrs;
    GetFramework().Storage().GetPrefetchStorage()->GetNodeAttrs(m_countryId, nodeAttrs);
    migrationView.nodeLocalName = @(nodeAttrs.m_nodeLocalName.c_str());
    self.navigationItem.leftBarButtonItem.enabled = NO;
  }
  else
  {
    self.navigationItem.leftBarButtonItem.enabled = YES;
  }
  migrationView.state = state;
}

#pragma mark - MWMCircularProgressProtocol

- (void)progressButtonPressed:(MWMCircularProgress *)progress
{
  GetFramework().Storage().GetPrefetchStorage()->CancelDownloadNode(m_countryId);
  [self setState:MWMMigrationViewState::Default];
}

#pragma mark - Segue

- (void)prepareForSegue:(UIStoryboardSegue *)segue sender:(id)sender
{
  if ([segue.identifier isEqualToString:kDownloaderSegue])
  {
    MWMMapDownloaderViewController * dvc = segue.destinationViewController;
    dvc.parentCountryId = @(GetFramework().Storage().GetRootId().c_str());
  }
}

#pragma mark - Actions

- (IBAction)primaryAction
{
  [self performLimitedMigration:NO];
}

- (IBAction)secondaryAction
{
  [self performLimitedMigration:YES];
}

@end
