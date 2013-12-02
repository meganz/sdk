/*

MEGA SDK - Client Access Engine Core Logic

(c) 2013 by Mega Limited, Wellsford, New Zealand

Author: mo
Bugfixing: js, mr

Applications using the MEGA API must present a valid application key
and comply with the the rules set forth in the Terms of Service.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

#include "mega/db.h"
#include "mega/utils.h"

DbTable::DbTable()
{
	nextid = 0;
}

// add or update record from string
bool DbTable::put(uint32_t index, string* data)
{
	return put(index,(char*)data->data(),data->size());
}

// add or update record with padding and encryption
bool DbTable::put(uint32_t type, Cachable* record, SymmCipher* key)
{
	string data;

	if (!record->serialize(&data)) return -1;

	PaddedCBC::encrypt(&data,key);

	if (!record->dbid) record->dbid = (nextid += IDSPACING) | type;

	return put(record->dbid,&data);
}

// get next record, decrypt and unpad
bool DbTable::next(uint32_t* type, string* data, SymmCipher* key)
{
	if (next(type,data))
	{
		if (!*type) return true;

		if (*type > nextid) nextid = *type & -IDSPACING;

		return PaddedCBC::decrypt(data,key);
	}

	return false;
}


