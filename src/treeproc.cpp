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

#include "mega/treeproc.h"
#include "mega/megaclient.h"

namespace mega {

// create share keys
TreeProcShareKeys::TreeProcShareKeys(Node* n)
{
	sn = n;
}

void TreeProcShareKeys::proc(MegaClient*, Node* n)
{
	snk.add(n,sn,sn != NULL);
}

void TreeProcShareKeys::get(Command* c)
{
	snk.get(c);
}

// total disk space / node count
TreeProcDU::TreeProcDU()
{
	numbytes = 0;
	numfiles = 0;
	numfolders = 0;
}

void TreeProcDU::proc(MegaClient*, Node* n)
{
	if (n->type == FILENODE)
	{
		numbytes += n->size;
		numfiles++;
	}
	else numfolders++;
}

// mark node as removed and notify
void TreeProcDel::proc(MegaClient* client, Node* n)
{
	n->removed = 1;
	client->notifynode(n);
}

} // namespace
