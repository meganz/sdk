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

#ifndef MEGA_TRANSFERSLOT_H
#define MEGA_TRANSFERSLOT_H 1

#include "http.h"
#include "node.h"

namespace mega {

// active transfer
struct TransferSlot
{
	// link to related transfer (never NULL)
	struct Transfer* transfer;

	// associated source/destination file
	FileAccess* file;

	// command in flight to obtain temporary URL
	Command* pendingcmd;

	// transfer attempts are considered failed after XFERTIMEOUT seconds without data flow
	static const dstime XFERTIMEOUT = 600;

	m_off_t progressreported, progresscompleted;

	dstime starttime, lastdata;

	// upload result
	byte ultoken[NewNode::UPLOADTOKENLEN+1];

	// file attribute string
	string fileattrstring;

	// file attributes mutable
	int fileattrsmutable;

	// storage server access URL
	string tempurl;

	// maximum number of parallel connections and connection aray
	int connections;
	HttpReqXfer** reqs;

	// handle I/O for this slot
	void doio(MegaClient*);

	// disconnect and reconnect all open connections for this transfer
	void disconnect();

	// indicate progress
	void progress();

	// compute the meta MAC based on the chunk MACs
	int64_t macsmac(chunkmac_map*);

	// tslots list position
	transferslot_list::iterator slots_it;

	TransferSlot(Transfer*);
	~TransferSlot();
};

} // namespace

#endif
