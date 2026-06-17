/**
 * @file mega/commands_ws.h
 * @brief Command wrappers specific to the websocket-upload engine.
 *
 * (c) 2026 by MEGA Privacy Kft, Csomad, Hungary
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the rules set forth in the Terms of Service.
 *
 * @copyright Simplified (2-clause) BSD License.
 */

#ifndef MEGA_COMMANDS_WS_H
#define MEGA_COMMANDS_WS_H 1

#ifdef MEGA_USE_WSUPLOAD

#include "mega/command.h"
#include "mega/types.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace mega
{

class MegaClient;

// API command wrapper for "usc" (Upload Size Classes) used by websocket uploads.
//
// NOTE: WS uploads are pool-based (not per-file), so the response can contain multiple endpoints
// across size classes. We keep parsing minimal and convert to "wss://<host>/<path>" URLs.
class MEGA_API CommandUSCForWsUpload final : public Command
{
public:
    using SizeClass = std::pair<std::string, m_off_t>; // (wss url, max size)
    using Completion = std::function<void(Error, std::vector<SizeClass>&&)>;

    CommandUSCForWsUpload(MegaClient& client, Completion completion);

    bool procresult(Result r, JSON& json) override;

private:
    Completion mCompletion;
};

} // namespace mega

#endif // MEGA_USE_WSUPLOAD

#endif // MEGA_COMMANDS_WS_H
