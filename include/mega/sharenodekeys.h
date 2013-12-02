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

#ifndef MEGA_SHARENODEKEYS_H
#define MEGA_SHARENODEKEYS_H 1

#include "types.h"

namespace mega {

// cr element share/node map key generator
class ShareNodeKeys
{
	node_vector shares;
	vector<string> items;

	string keys;

	int addshare(Node*);

public:
	void add(Node*, Node*, int);
	void add(NodeCore*, Node*, int, const byte* = NULL, int = 0);

	void get(Command*);
};

} // namespace

#endif
