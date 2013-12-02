/*

MEGA SDK - Client Access Engine Core Logic

(c) 2013 by Mega Limited, Wellsford, New Zealand

Applications using the MEGA API must present a valid application key
and comply with the the rules set forth in the Terms of Service.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

#ifndef MEGA_DB_H
#define MEGA_DB_H 1

#include "filesystem.h"

// generic host transactional database access interface
class DbTable
{
	static const int IDSPACING = 16;

public:
	// for a full sequential get: rewind to first record
	virtual void rewind() = 0;

	// get next record in sequence
	virtual bool next(uint32_t*, string*) = 0;
	bool next(uint32_t*, string*, SymmCipher*);

	// get specific record by key
	virtual bool get(uint32_t, string*) = 0;

	// update or add specific record
	virtual bool put(uint32_t, char*, unsigned) = 0;
	bool put(uint32_t, string*);
	bool put(uint32_t, Cachable*, SymmCipher*);

	// delete specific record
	virtual bool del(uint32_t) = 0;

	// delete all records
	virtual void truncate() = 0;

	// begin transaction
	virtual void begin() = 0;

	// commit transaction
	virtual void commit() = 0;

	// abort transaction
	virtual void abort() = 0;

	// autoincrement
	uint32_t nextid;

	DbTable();
	virtual ~DbTable() { }
};

struct DbAccess
{
	virtual DbTable* open(FileSystemAccess*, string*) = 0;

	virtual ~DbAccess() { }
};

#endif
