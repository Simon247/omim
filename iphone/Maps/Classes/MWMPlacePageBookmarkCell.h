#import "MWMTableViewCell.h"

@class MWMPlacePage;

@interface MWMPlacePageBookmarkCell : MWMTableViewCell

- (void)config:(MWMPlacePage *)placePage forHeight:(BOOL)forHeight;

- (CGFloat)cellHeight;

@end
