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

#ifndef MEGA_FILEATTRIBUTEFETCH_H
#define MEGA_FILEATTRIBUTEFETCH_H 1

#include "backofftimer.h"
#include "types.h"
#include "http.h"

// file attribute fetching for a specific source cluster
struct FileAttributeFetchChannel
{
	handle fahref;
	BackoffTimer bt;
	HttpReq req;

	// post request to target URL
	void dispatch(MegaClient*, int, const char*);

	// parse fetch result and remove completed attributes from pending
	void parse(MegaClient*, int, string*);

	FileAttributeFetchChannel();
};

// pending individual attribute fetch
struct FileAttributeFetch
{
	handle nodehandle;
	fatype type;
	int fac;		// attribute cluster ID
	unsigned char dispatched;
	unsigned char retries;
	int tag;

	FileAttributeFetch(handle, fatype, int, int);
};

#endif
