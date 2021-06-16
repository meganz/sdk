#!/usr/bin/python

# This python script can be used to extract HTTP request and responses from an SDK log
# and beautify json payloads

import json, sys, re

for l in open(sys.argv[1]):

    if "cs POST target" in l:
        print l,
    if "-v" in sys.argv and "Request " in l and (" starting" in l or " finished" in l):
        print l,

    if ("sc Received" in l or "cs Received" in l or "sc Sending" in l or "cs Sending" in l) and "sc Received 1: 0" not in l and " sc Sending 0:" not in l:

        m = re.search('(.*): (\{.*\}|\[.*\])', l)
        if m:
            header = found = m.group(1)
            found = m.group(2)
            if header and found:
                try:
                    contents = json.dumps(json.loads(found), sort_keys=False, indent=4)
                    print header+contents
                except:
                    #try this other format:
                    m = re.search('(.*)((sc|cs) (Received|Sending) [0-9]*:  *)(\{.*\}|\[.*\])  *(\[.*cpp.*\])*', l)
                    if m and m is not None and len(m.groups()) > 4:
                        header = found = m.group(1)
                        sendrecv = m.group(2)
                        found = m.group(5)
                        fil = m.group(6)
                        if header and sendrecv and found:
                            try:
                                contents = json.dumps(json.loads(found), sort_keys=False, indent=4)
                                print header+sendrecv+contents
                            except:
                                print l
                        else:
                            print l,
                    else:
                        print l,
            else:
                print l,
        else:
            print l,
