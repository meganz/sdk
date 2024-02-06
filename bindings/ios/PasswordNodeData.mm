/**
 * @file PasswordNodeData.mm
 * @brief Object Data for Password Node attributes
 *
 * (c) 2023- by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */
#import "PasswordNodeData.h"

@interface PasswordNodeData ()

@property (readwrite, nonatomic) NSString *password;
@property (readwrite, nonatomic, nullable) NSString *notes;
@property (readwrite, nonatomic, nullable) NSString *url;
@property (readwrite, nonatomic, nullable) NSString *userName;

@end

@implementation PasswordNodeData

- (instancetype)initWithPassword:(NSString *)password notes:(nullable NSString *)notes url:(nullable NSString *)url userName:(nullable NSString *)userName {
    self = [super init];
    if (self) {
        _password = [password copy];
        _notes = [notes copy];
        _url = [url copy];
        _userName = [userName copy];
    }
    return self;
}

@end