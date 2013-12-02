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

#ifndef MEGA_TRANSFER_H
#define MEGA_TRANSFER_H 1

#include "backofftimer.h"
#include "node.h"

namespace mega {

// pending/active up/download ordered by file fingerprint (size - mtime - sparse CRC)
struct Transfer : public FileFingerprint
{
	// PUT or GET
	direction type;

	// transfer slot this transfer is active in (can be NULL if still queued)
	TransferSlot* slot;

	// files belonging to this transfer - transfer terminates upon its last file is removed
	file_list files;

	// failures/backoff
	unsigned failcount;
	BackoffTimer bt;

	// representative local filename for this transfer
	string localfilename;

	m_off_t pos, size;

	byte filekey[Node::FILENODEKEYLENGTH];

	// CTR mode IV
	int64_t ctriv;

	// meta MAC
	int64_t metamac;

	// file crypto key
	SymmCipher key;

	chunkmac_map chunkmacs;

	// upload handle for file attribute attachment (only set if file attribute queued)
	handle uploadhandle;

	// signal failure
	void failed(error);

	// signal completion
	void complete();

	// position in transfers[type]
	transfer_map::iterator transfers_it;

	// backlink to base
	MegaClient* client;

	Transfer(MegaClient*, direction);
	virtual ~Transfer();
};

} // namespace

#endif
