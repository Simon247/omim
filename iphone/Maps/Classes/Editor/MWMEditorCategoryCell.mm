#import "MWMEditorCategoryCell.h"
#import "MWMEditorCommon.h"
#import "UIColor+MapsMeColor.h"
#import "UIImageView+Coloring.h"

@interface MWMEditorCategoryCell ()

@property (weak, nonatomic) IBOutlet UIImageView * accessoryIcon;
@property (weak, nonatomic) IBOutlet UILabel * detail;
@property (weak, nonatomic) IBOutlet NSLayoutConstraint * detailRightSpace;
@property (weak, nonatomic) id<MWMEditorCellProtocol> delegate;

@end

@implementation MWMEditorCategoryCell

- (void)configureWithDelegate:(id<MWMEditorCellProtocol>)delegate detailTitle:(NSString *)detail isCreating:(BOOL)isCreating
{
  self.delegate = delegate;
  self.detail.text = detail;
  self.accessoryIcon.hidden = !isCreating;
  if (isCreating)
  {
    self.selectedBackgroundView = [[UIView alloc] init];
    self.selectedBackgroundView.backgroundColor = [UIColor pressBackground];
  }
  else
  {
    self.selectionStyle = UITableViewCellSelectionStyleNone;
    self.detailRightSpace.constant -= self.accessoryIcon.width / 2;
  }
}

- (IBAction)cellTap
{
  if (self.accessoryIcon.hidden)
    return;
  [self.delegate cellSelect:self];
}

@end
