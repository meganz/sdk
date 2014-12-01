#import "MainTabBarController.h"

//#define IPAD UI_USER_INTERFACE_IDIOM() == UIUserInterfaceIdiomPad

@interface MainTabBarController () 

@end

@implementation MainTabBarController

- (void)viewDidLoad {
    [super viewDidLoad];
    
    NSMutableArray *viewControllerArray = [[NSMutableArray alloc] initWithCapacity:5];

    [viewControllerArray addObject:[[UIStoryboard storyboardWithName:@"Cloud" bundle:nil] instantiateInitialViewController]];
    [viewControllerArray addObject:[[UIStoryboard storyboardWithName:@"Offline" bundle:nil] instantiateInitialViewController]];
    [viewControllerArray addObject:[[UIStoryboard storyboardWithName:@"Contacts" bundle:nil] instantiateInitialViewController]];
    [viewControllerArray addObject:[[UIStoryboard storyboardWithName:@"Settings" bundle:nil] instantiateInitialViewController]];

    [self setViewControllers:viewControllerArray];
}

- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}



//    [viewControllerArray addObject:[[UIStoryboard storyboardWithName:storyboardNameForCurrentDevice(@"Cloud") bundle:nil] instantiateInitialViewController]];
//NSString * storyboardNameForCurrentDevice(NSString *storyboardName)
//{
//    return [storyboardName stringByAppendingString:IPAD?@"_iPad":@"_iPhone"];
//}

/*
#pragma mark - Navigation

// In a storyboard-based application, you will often want to do a little preparation before navigation
- (void)prepareForSegue:(UIStoryboardSegue *)segue sender:(id)sender {
    // Get the new view controller using [segue destinationViewController].
    // Pass the selected object to the new view controller.
}
*/

@end
