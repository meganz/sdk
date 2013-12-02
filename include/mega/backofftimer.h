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

#ifndef MEGA_BACKOFF_TIMER_H
#define MEGA_BACKOFF_TIMER_H 1

// FIXME: #define PRI*64 if missing
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

typedef uint32_t dstime;

//#include "mega.h"

// generic timer facility with exponential backoff
class BackoffTimer
{
	dstime next;
	dstime delta;

public:
	// reset timer
	void reset();

	// trigger exponential backoff
	void backoff(dstime);

	// set absolute backoff
	void backoff(dstime, dstime);

	// check if timer has elapsed
	bool armed(dstime) const;

	// arm timer
	bool arm(dstime);

	// time left for event to become armed
	dstime retryin(dstime);

	// current backoff delta
	dstime backoff();

	// put on hold indefinitely
	void freeze();

	// time of next trigger or 0 if no trigger since last backoff
	dstime nextset() const;

	// update time to wait
	void update(dstime, dstime*);

	BackoffTimer();
};

#endif
