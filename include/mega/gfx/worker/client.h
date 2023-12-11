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

#include "mega/gfx/worker/comms.h"
#include "mega/gfx/worker/tasks.h"

#include <memory>
#include <vector>
#include <string>
#include <assert.h>

namespace mega {
namespace gfx {

/**
 * @brief The GfxClient class.
 */
class GfxClient
{
public:

    /**
     * @brief GfxClient cotr
     * @param comms. The implementation of GfxCommunications to be used. GfxClient takes the
     * ownership of the provided object.
     *
     */
    GfxClient(std::unique_ptr<IGfxCommunicationsClient> comms) : mComms{std::move(comms)}
    {
        assert(mComms);
    }

    bool runHello(const std::string& text);

    bool runShutDown();

    bool runGfxTask(const std::string& localpath,
                    const std::vector<GfxDimension>& dimensions,
                    std::vector<std::string>& images);

    bool runSupportFormats(std::string& formats, std::string& videoformats);

    static GfxClient create(const std::string& pipename);

private:

    bool isRetryError(CommError error) const;

    // it retries on some errors
    std::unique_ptr<IEndpoint> connectWithRetry(std::chrono::milliseconds backoff, unsigned int maxRetries);

    std::unique_ptr<IEndpoint> connect();

    template<typename ResponseT, typename RequestT>
    std::unique_ptr<ResponseT> sendAndReceive(IEndpoint* endpoint, RequestT command, TimeoutMs sendTimeout = TimeoutMs(5000), TimeoutMs receiveTimeout = TimeoutMs(5000));

    std::unique_ptr<IGfxCommunicationsClient> mComms;
};

} //namespace gfx
} //namespace mega

