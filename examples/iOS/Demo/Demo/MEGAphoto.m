#import "MEGAphoto.h"
#import "SVProgressHUD.h"
#import "Helper.h"


@interface MEGAphoto () {

    BOOL _isLoading;

}

- (void)imageLoaded;

@end

@implementation MEGAphoto


#pragma mark - Class Methods

+ (MEGAphoto *)photoWithNode:(MEGANode *)node {
    return [[MEGAphoto alloc] initWithNode:node];
}


#pragma mark - Init

- (id)initWithNode:(MEGANode*)node {
    self = [super init];
    if (self) {
        _node = node;
    }
    return self;
}

#pragma mark - MWPhoto Protocol Methods

- (UIImage *)underlyingImage {
    self.imagePath = [Helper pathForNode:self.node searchPath:NSCachesDirectory directory:@"previews"];
    if(![[NSFileManager defaultManager] fileExistsAtPath:self.imagePath])return nil;

    return [UIImage imageWithContentsOfFile:self.imagePath];
}

- (void)loadUnderlyingImageAndNotify {
  
    if(_isLoading) return;
    
    _isLoading = YES;
    
    if(self.underlyingImage)
        [self imageLoaded];
    else
        [self performLoadUnderlyingImageAndNotify];
    
}

- (void)performLoadUnderlyingImageAndNotify {
    
    if([self.node hasPreview])
        [[MEGASdkManager sharedMEGASdk] getPreviewNode:self.node destinationFilePath:self.imagePath delegate:self];
}

- (void)unloadUnderlyingImage {

}

- (void)imageLoaded {
    _isLoading = NO;
    [[NSNotificationCenter defaultCenter] postNotificationName:MWPHOTO_LOADING_DID_END_NOTIFICATION object:self];
}


#pragma mark - MEGARequestDelegate


- (void)onRequestStart:(MEGASdk *)api request:(MEGARequest *)request {
    NSLog(@"request start: %s",__FUNCTION__);
}

- (void)onRequestFinish:(MEGASdk *)api request:(MEGARequest *)request error:(MEGAError *)error{

    NSLog(@"request finish: %s",__FUNCTION__);
    self.caption = [self.node base64Handle];
    [self performSelector:@selector(imageLoaded) withObject:nil afterDelay:0];

}

- (void)onRequestTemporaryError:(MEGASdk *)api request:(MEGARequest *)request error:(MEGAError *)error{
    NSLog(@"request error: %s",__FUNCTION__);
}

@end


