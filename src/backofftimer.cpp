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

#include "mega/backofftimer.h"

// timer with capped exponential backoff
BackoffTimer::BackoffTimer()
{
	reset();
}

void BackoffTimer::reset()
{
	next = 0;
	delta = 1;
}

void BackoffTimer::backoff(dstime ds)
{
	next = ds+delta;
	delta <<= 1;
	if (delta > 36000) delta = 36000;
}

void BackoffTimer::backoff(dstime ds, dstime newdelta)
{
	next = ds+newdelta;
	delta = newdelta;
}

void BackoffTimer::freeze()
{
	delta = next = ~(dstime)0;
}

bool BackoffTimer::armed(dstime ds) const
{
	return !next || ds >= next;
}

bool BackoffTimer::arm(dstime ds)
{
	if (next+delta > ds)
	{
		next = ds;
		delta = 1;

		return true;
	}

	return false;
}

dstime BackoffTimer::retryin(dstime ds)
{
	if (armed(ds)) return 0;

	return next-ds;
}

dstime BackoffTimer::backoff()
{
	return delta;
}

dstime BackoffTimer::nextset() const
{
	return (int)next;
}

// event in the future: potentially updates waituntil
// event in the past: zeros out waituntil and clears event
void BackoffTimer::update(dstime ds, dstime* waituntil)
{
	if (next)
	{
		if (next <= ds)
		{
			*waituntil = 0;
			next = 1;
		}
		else if (next < *waituntil) *waituntil = next;
	}
}

