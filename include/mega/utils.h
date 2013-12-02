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

#ifndef MEGA_UTILS_H
#define MEGA_UTILS_H 1

#include "types.h"

// convert
// XXX: endianness
#define MAKENAMEID2(a,b) (nameid)(((a)<<8)+(b))
#define MAKENAMEID3(a,b,c) (nameid)(((a)<<16)+((b)<<8)+(c))
#define MAKENAMEID4(a,b,c,d) (nameid)(((a)<<24)+((b)<<16)+((c)<<8)+(d))
#define MAKENAMEID5(a,b,c,d,e) (nameid)((((uint64_t)a)<<32)+((b)<<24)+((c)<<16)+((d)<<8)+(e))
#define MAKENAMEID6(a,b,c,d,e,f) (nameid)((((uint64_t)a)<<40)+(((uint64_t)b)<<32)+((c)<<24)+((d)<<16)+((e)<<8)+(f))
#define MAKENAMEID7(a,b,c,d,e,f,g) (nameid)((((uint64_t)a)<<48)+(((uint64_t)b)<<40)+(((uint64_t)c)<<32)+((d)<<24)+((e)<<16)+((f)<<8)+(g))
#define MAKENAMEID8(a,b,c,d,e,f,g,h) (nameid)((((uint64_t)a)<<56)+(((uint64_t)b)<<48)+(((uint64_t)c)<<40)+(((uint64_t)d)<<32)+((e)<<24)+((f)<<16)+((g)<<8)+(h))


struct ChunkedHash
{
	static const int SEGSIZE = 131072;

	static m_off_t chunkfloor(m_off_t);
	static m_off_t chunkceil(m_off_t);
};


// padded CBC encryption
struct PaddedCBC
{
	static void encrypt(string*, SymmCipher*);
	static bool decrypt(string*, SymmCipher*);
};


class HashSignature
{
	Hash* hash;

public:
	// add data
	void add(const byte*, unsigned);

	// generate signature
	unsigned get(AsymmCipher*, byte*, unsigned);

	// verify signature
	int check(AsymmCipher*, const byte*, unsigned);

	HashSignature(Hash*);
	~HashSignature();
};


#endif
