/**
 * (c) 2013 by Mega Limited, Auckland, New Zealand
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

#include "gfxworker/tasks.h"

#include "mega/logging.h"

namespace gfx {

GfxSize GfxSize::fromString(const std::string &sizeStr)
{
    GfxSize size;
    auto xPos = sizeStr.find("x");
    if (xPos == std::string::npos || xPos == sizeStr.size() - 1)
    {
        return size;
    }
    std::string widthStr = sizeStr.substr(0, xPos);
    try
    {
        size.mWidth = std::stoi(widthStr);
    }
    catch(std::invalid_argument const& ex)
    {
        LOG_err << "Failed to parse size width, invalid argument: " << ex.what();
        return size;
    }
    catch(std::out_of_range const& ex)
    {
        LOG_err << "Failed to parse size width, out of range: " << ex.what();
        return size;
    }
    std::string heightStr = sizeStr.substr(xPos + 1);
    try
    {
        size.mHeight = std::stoi(heightStr);
    }
    catch(std::invalid_argument const& ex)
    {
        LOG_err << "Failed to parse size height, invalid argument: " << ex.what();
        return size;
    }
    catch(std::out_of_range const& ex)
    {
        LOG_err << "Failed to parse size height, out of range: " << ex.what();
        return size;
    }
    return size;
}

}
