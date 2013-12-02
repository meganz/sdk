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

#ifndef MEGA_ATTRMAP_H
#define MEGA_ATTRMAP_H 1

#include "types.h"

namespace mega {

// maps attribute names to attribute values
typedef map<nameid,string> attr_map;

struct AttrMap
{
	attr_map map;

	// compute rough storage size
	unsigned storagesize(int);

	// convert nameid to string
	int nameid2string(nameid, char*);

	// export as JSON string
	void getjson(string*);

	// export as raw binary serialize
	void serialize(string*);

	// import raw binary serialize
	const char* unserialize(const char*, unsigned);
};

} // namespace

#endif
