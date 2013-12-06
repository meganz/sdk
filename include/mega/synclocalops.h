/**
 * @file mega/synclocalops.h
 * @brief Various sync-related local filesystem operations
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

#ifndef MEGA_LOCALSYNCOPS_H
#define MEGA_LOCALSYNCOPS_H 1

#include "types.h"
#include "megaclient.h"

namespace mega {

// local file
class SyncLocalOp
{
protected:
	MegaClient* client;

public:
	virtual bool exec() = 0;
	virtual void notify() = 0;
	
	virtual ~SyncLocalOp() { }
};
							
class SyncLocalOpMove : public SyncLocalOp
{
	string from;
	string to;

public:
	bool exec();
	void notify();

	SyncLocalOpMove(MegaClient*, string*, string*);
};

class SyncLocalOpDel : public SyncLocalOp
{
	string path;

public:
	bool exec();
	void notify();
	
	SyncLocalOpDel(MegaClient*, string*);
};

class SyncLocalOpDelDir : public SyncLocalOp
{
	string path;

public:
	bool exec();
	void notify();
	
	SyncLocalOpDelDir(MegaClient*, string*);
};				

} // namespace

#endif
