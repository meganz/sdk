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

#ifndef MEGA_SYNC_H
#define MEGA_SYNC_H 1

#include "megaclient.h"

class Sync
{
public:
	MegaClient* client;

	// root of local filesystem tree, holding the sync's root folder
	LocalNode localroot;

	// queued ScanItems
	scanitem_deque scanq;

	// current state
	syncstate state;

	// change state, signal to application
	void changestate(syncstate);

	// process and remove one scanq item
	void procscanq();

	m_off_t localbytes;
	unsigned localnodes[2];

	// add or update LocalNode item, scan newly added folders
	void queuescan(string*, string*, LocalNode*, LocalNode*, bool);

	// examine filesystem item and queue it for scanning
	LocalNode* queuefsrecord(string*, string*, LocalNode*, bool);

	// scan items in specified path and add as children of the specified LocalNode
	void scan(string*, FileAccess*, LocalNode*, bool);

	// determine status of a given path
	pathstate_t pathstate(string*);

	// own position in session sync list
	sync_list::iterator sync_it;

	// notified nodes originating from this sync bear this tag
	int tag;

	Sync(MegaClient*, string*, Node*, int = 0);
	~Sync();
};

#endif
