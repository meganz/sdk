/**
 * @file MEGALogLevel..h
 * @brief Log levels
 *
 * (c) 2022- by Mega Limited, Auckland, New Zealand
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

typedef NS_ENUM (NSInteger, MEGALogLevel) {
    MEGALogLevelFatal = 0,
    MEGALogLevelError,      // Error information but will continue application to keep running.
    MEGALogLevelWarning,    // Information representing errors in application but application will keep running
    MEGALogLevelInfo,       // Mainly useful to represent current progress of application.
    MEGALogLevelDebug,      // Informational logs, that are useful for developers. Only applicable if DEBUG is defined.
    MEGALogLevelMax
};
