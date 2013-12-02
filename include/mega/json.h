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

#ifndef MEGA_JSON_H
#define MEGA_JSON_H 1

//#include "mega.h"
#include "types.h"

namespace mega {

// linear non-strict JSON scanner
struct JSON
{
	const char* pos;	// make private

	bool isnumeric();

	void begin(const char*);

	m_off_t getint();
	double getfloat();
	const char* getvalue();

	nameid getnameid();
	nameid getnameid(const char*);

	bool is(const char*);

	int storebinary(byte*, int);
	bool storebinary(string*);

	handle gethandle(int = 6);

	bool enterarray();
	bool leavearray();

	bool enterobject();
	bool leaveobject();

	bool storestring(string*);
	bool storeobject(string* = NULL);

	static void unescape(string*);
};

} // namespace

#endif
