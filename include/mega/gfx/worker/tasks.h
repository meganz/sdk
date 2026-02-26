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

#pragma once

#include "mega/gfx.h"

#include <string>
#include <vector>
#include <limits>
#include <memory>

namespace mega {
namespace gfx {

struct GfxTask final
{
    std::string          Path;
    std::vector<GfxDimension> Dimensions;
};

/**
 * @brief Defines the possible result status of GfxProcessor::process
 */
enum class GfxTaskProcessStatus
{
    SUCCESS = 0,
    ERR     = 1,
};

struct GfxTaskResult final
{
    GfxTaskResult(std::vector<std::string>&& outputImages, const GfxTaskProcessStatus processStatus)
        : ProcessStatus(processStatus)
        , OutputImages(std::move(outputImages)) {}

    GfxTaskProcessStatus     ProcessStatus;
    std::vector<std::string> OutputImages;
};

} //namespace gfx
} //namespace mega

