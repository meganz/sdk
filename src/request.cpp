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

#include "mega/request.h"
#include "mega/command.h"

void Request::add(Command* c)
{
	cmds.push_back(c);
}

int Request::cmdspending()
{
	return cmds.size();
}

int Request::get(string* req)
{
	// concatenate all command objects, resulting in an API request
	*req = "[";

	for (int i = 0; i < (int)cmds.size(); i++)
	{
		req->append(i ? ",{" : "{");
		req->append(cmds[i]->getstring());
		req->append("}");
	}

	req->append("]");

	return 1;
}

void Request::procresult(MegaClient* client)
{
	client->json.enterarray();

	for (int i = 0; i < (int)cmds.size(); i++)
	{
		client->restag = cmds[i]->tag;

		cmds[i]->client = client;

		if (client->json.enterobject())
		{
			cmds[i]->procresult();
			client->json.leaveobject();
		}
		else if (client->json.enterarray())
		{
			cmds[i]->procresult();
			client->json.leavearray();
		}
		else cmds[i]->procresult();

		if (!cmds[i]->persistent) delete cmds[i];
	}

	cmds.clear();
}

void Request::clear()
{
	cmds.clear();
}

