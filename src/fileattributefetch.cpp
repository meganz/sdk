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

#include "mega/fileattributefetch.h"
#include "mega/megaclient.h"
#include "mega/megaapp.h"

FileAttributeFetchChannel::FileAttributeFetchChannel()
{
	req.binary = true;
}

FileAttributeFetch::FileAttributeFetch(handle h, fatype t, int c, int ctag)
{
	nodehandle = h;
	type = t;
	fac = c;
	retries = 0;
	tag = ctag;
}

// post pending requests for this cluster to supplied URL
void FileAttributeFetchChannel::dispatch(MegaClient* client, int fac, const char* targeturl)
{
	// dispatch all pending fetches for this channel's cluster
	for (faf_map::iterator it = client->fafs.begin(); it != client->fafs.end(); it++)
	{
		if (it->second->fac == fac)
		{
			req.out->reserve(client->fafs.size()*sizeof(handle));	// prevent reallocations

			it->second->dispatched = 1;
			req.out->append((char*)&it->first,sizeof(handle));
		}
	}

	req.posturl = targeturl;
	req.post(client);
}

// communicate the result of a file attribute fetch to the application and remove completed records
void FileAttributeFetchChannel::parse(MegaClient* client, int fac, string* data)
{
	// data is structured (handle.8.le / position.4.le)* attribute data
	// attributes are CBC-encrypted with the file's key

	// we must have received at least one full header to continue
	if (data->size() < sizeof(handle)+sizeof(uint32_t)) return client->faf_failed(fac);

	uint32_t bod = *(uint32_t*)(data->data()+sizeof(handle));

	if (bod > data->size()) return client->faf_failed(fac);

	handle fah;
	const char* fadata;
	uint32_t falen, fapos;
	Node* n;
	faf_map::iterator it;

	fadata = data->data();

	for (unsigned h = 0; h < bod; h += sizeof(handle)+sizeof(uint32_t))
	{
		fah = *(handle*)(fadata+h);

		it = client->fafs.find(fah);

		// locate fetch request (could have been deleted by the application in the meantime)
		if (it != client->fafs.end())
		{
			// locate related node (could have been deleted)
			if ((n = client->nodebyhandle(it->second->nodehandle)))
			{
				fapos = *(uint32_t*)(fadata+h+sizeof(handle));
				falen = ((h+sizeof(handle)+sizeof(uint32_t) < bod) ? *(uint32_t*)(fadata+h+2*sizeof(handle)+sizeof(uint32_t)) : data->size())-fapos;

				if (!(falen & (SymmCipher::BLOCKSIZE-1)))
				{
					n->key.cbc_decrypt((byte*)fadata+fapos,falen);

					client->restag = it->second->tag;

					client->app->fa_complete(n,it->second->type,fadata+fapos,falen);
					delete it->second;
					client->fafs.erase(it);
				}
			}
		}
	}
}

