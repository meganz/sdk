/**
 * @file mega/banner.h
 * @brief Banner data structure
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
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

#pragma once

#include "types.h"

namespace mega
{
struct BannerDetails
{
    int id = 0;
    std::string title;
    std::string description;
    std::string image;
    std::string url;
    std::string backgroundImage;
    std::string imageLocation;
    int variant = 0;
    std::string button;
};
} // namespace