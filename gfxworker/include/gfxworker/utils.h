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

#include <string>
#include <vector>
#include <functional>

namespace initutils
{
    // TODO : update in PRX-312
    std::string getHomeFolder();

    bool extractArg(std::vector<const char*>& args, const char* what);

    bool extractArgParam(std::vector<const char*>& args, const char* what, std::string& param);

    std::string getSanitizedTestFilter(std::vector<const char*>& args);
}

// TODO GSL finally
class ScopeGuard {
    using ExitCallback = std::function<void()>;
public:
    ScopeGuard(ExitCallback&& exitCb) : mExitCb{ std::move(exitCb) } { }
    ~ScopeGuard() { mExitCb(); }
private:
    ExitCallback mExitCb;
};

