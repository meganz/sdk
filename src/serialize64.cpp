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

#include "mega/serialize64.h"

int Serialize64::serialize(byte* b, int64_t v)
{
	int p = 0;

	while (v)
	{
		b[++p] = (byte)v;
		v >>= 8;
	}

	return (*b = p)+1;
}

int Serialize64::unserialize(byte* b, int blen, int64_t* v)
{
	byte p = *b;

	if (p > sizeof(*v) || p >= blen) return -1;

	*v = 0;
	while (p) *v = (*v<<8)+b[(int)p--];

	return *b+1;
}

