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

#ifndef MEGA_FILEFINGERPRINT_H
#define MEGA_FILEFINGERPRINT_H 1

#include "types.h"
#include "filesystem.h"

namespace mega {

// sparse file fingerprint, including size & mtime
struct FileFingerprint
{
	m_off_t size;
	time_t mtime;
	byte crc[32];

	// if true, represents actual file data
	// if false, constructed from node ctime/key
	bool isvalid;

	bool genfingerprint(FileAccess*);
	void serializefingerprint(string*);
	int unserializefingerprint(string*);

	FileFingerprint& operator=(FileFingerprint&);

	FileFingerprint();
};

// orders transfers by file fingerprints, ordered by size / mtime / sparse CRC
struct FileFingerprintCmp
{
    bool operator() (const FileFingerprint* a, const FileFingerprint* b) const;
};

bool operator==(FileFingerprint&, FileFingerprint&);

} // namespace

#endif
