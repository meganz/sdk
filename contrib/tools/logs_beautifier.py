#!/usr/bin/python

# This python script can be used to extract HTTP request and responses from an SDK log
# and beautify json payloads

import json, sys, re

for l in open(sys.argv[1]):

    if "cs POST target" in l:
        print l,
    if ("sc Received" in l or "cs Received" in l or "sc Sending" in l or "cs Sending" in l) and "sc Received 1: 0" not in l and " sc Sending 0:" not in l:

        m = re.search('(.*): (\{.*\}|\[.*\])', l)
        if m:
            header = found = m.group(1)
            found = m.group(2)
            if header and found:
                print header,
                try:
                    print json.dumps(json.loads(found), sort_keys=False, indent=4)
                except:
                    print found
            else:
                print l,
        else:
            print l,
