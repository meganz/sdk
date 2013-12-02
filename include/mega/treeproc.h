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

#ifndef MEGA_TREEPROC_H
#define MEGA_TREEPROC_H 1

#include "sharenodekeys.h"
#include "node.h"

// node tree processor
class TreeProc
{
public:
	virtual void proc(MegaClient*, Node*) = 0;

	virtual ~TreeProc() { }
};

class TreeProcDel : public TreeProc
{
public:
	void proc(MegaClient*, Node*);
};

class TreeProcListOutShares : public TreeProc
{
public:
	void proc(MegaClient*, Node*);
};

class TreeProcCopy : public TreeProc
{
public:
	NewNode* nn;
	unsigned nc;

	void allocnodes(void);

	void proc(MegaClient*, Node*);
	TreeProcCopy();
	~TreeProcCopy();
};

class TreeProcDU : public TreeProc
{
public:
	m_off_t numbytes;
	int numfiles;
	int numfolders;

	void proc(MegaClient*, Node*);
	TreeProcDU();
};

class TreeProcShareKeys : public TreeProc
{
	ShareNodeKeys snk;
	Node* sn;

public:
	void proc(MegaClient*, Node*);
	void get(Command*);

	TreeProcShareKeys(Node* = NULL);
};

#endif
