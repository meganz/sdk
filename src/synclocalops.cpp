/**
 * @file localsyncops.cpp
 * @brief Implementation of various sync-related local filesystem operations
 *
 * (c) 2013 by Mega Limited, Wellsford, New Zealand
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

#include "mega/synclocalops.h"
#include "mega/megaapp.h"

namespace mega {

bool SyncLocalOpMove::exec()
{
	// FIXME: perform a full recursive move top to bottom
	if (from.size())
	{
		if (client->fsaccess->renamelocal(&from,&to)) return true;

		if (client->fsaccess->copylocal(&from,&to)) from.clear();
	}

	// FIXME: is delayed deletion semantically desirable?
	if (to.size())
	{
		if (client->fsaccess->unlinklocal(&from)) return true;
	}

	return false;
}

void SyncLocalOpMove::notify()
{
	// FIXME
}

SyncLocalOpMove::SyncLocalOpMove(MegaClient* cclient, string* cfrom, string* cto)
{
	client = cclient;
	from = *cfrom;
	to = *cto;
}

bool SyncLocalOpDel::exec()
{
	return client->fsaccess->rmdirlocal(&path);
}

void SyncLocalOpDel::notify()
{
//	client->app->syncupdate_remote_rmdir(n);
}

SyncLocalOpDel::SyncLocalOpDel(MegaClient* cclient, string* cpath)
{
	client = cclient;
	path = *cpath;
}

bool SyncLocalOpDelDir::exec()
{
	return client->fsaccess->rmdirlocal(&path);
}

void SyncLocalOpDelDir::notify()
{
//	client->app->syncupdate_remote_rmdir(n);
}

SyncLocalOpDelDir::SyncLocalOpDelDir(MegaClient* cclient, string* cpath)
{
	client = cclient;
	path = *cpath;
}

} // namespace
