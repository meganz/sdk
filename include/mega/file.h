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

#ifndef MEGA_FILE_H
#define MEGA_FILE_H 1

#include "filefingerprint.h"

// file to be transferred
struct File : public FileFingerprint
{
	// set localfilename in attached transfer
	virtual void prepare();

	// file transfer dispatched, expect updates/completion/failure
	virtual void start();

	// progress update
	virtual void progress();

	// transfer completion
	virtual void completed(Transfer*, LocalNode*);

	// transfer failed
	virtual bool failed(error);

	// generic filename for this transfer
	void displayname(string*);

	// normalized name (UTF-8 with unescaped special chars)
	string name;

	// local filename (must be set upon injection for uploads, can be set in start() for downloads)
	string localname;

	// source/target node handle
	handle h;

	// for remote file drops: uid or e-mail address of recipient
	string targetuser;

	// transfer linkage
	Transfer* transfer;
	file_list::iterator file_it;

	File();
	virtual ~File();
};

struct SyncFileGet : public File
{
	Node* n;

	// self-destruct after completion
	void completed(Transfer*, LocalNode*);

	SyncFileGet(Node*, string*);
	~SyncFileGet();
};

#endif
