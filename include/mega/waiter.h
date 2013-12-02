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

#ifndef MEGA_WAITER_H
#define MEGA_WAITER_H 1

#include "types.h"

// wait for events
struct Waiter
{
	// current time
	dstime ds;

	// wait ceiling
	dstime maxds;

	// current time in deciseconds
	virtual dstime getdstime() = 0;

	// beging waiting cycle
	virtual void init(dstime) = 0;

	// add wakeup events
	void wakeupby(struct EventTrigger*);

	// wait for all added wakeup criteria (plus the host app's own), up to the specified number of deciseconds
	virtual int wait() = 0;

	static const int NEEDEXEC = 1;
	static const int HAVESTDIN = 2;
};

#endif
