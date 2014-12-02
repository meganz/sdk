#import <UIKit/UIKit.h>

@interface NodeTableViewCell : UITableViewCell

@property (weak, nonatomic) IBOutlet UIImageView *thumbnailImageView;
@property (weak, nonatomic) IBOutlet UILabel *nameLabel;
@property (weak, nonatomic) IBOutlet UILabel *modificationLabel;
@property (nonatomic) uint64_t nodeHandle;

@end
