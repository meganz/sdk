#!/usr/bin/python

# This python script can be used to extract HTTP request and responses from an SDK log
# and beautify json payloads

from __future__ import print_function
import json, sys, re
import argparse

parser = argparse.ArgumentParser()

parser.add_argument('--print-cs-post', '-r', action='store_true', dest='postcs', help='print lines with cs POST')
parser.set_defaults(postcs=False)

parser.add_argument('--print-sc-post', '-a', action='store_true', dest='postsc', help='print lines with sc POST')
parser.set_defaults(postsc=False)

parser.add_argument('--only-action-packets', '-s', action='store_true', dest='onlyactionpackets', help='do not print cs requests/responses')
parser.set_defaults(onlyactionpackets=False)

parser.add_argument('--only-client-requests', '-c', action='store_true', dest='onlyclientreqs', help='do not print sc requests/responses')
parser.set_defaults(onlyclientreqs=False)

parser.add_argument('--include-lines-matching', '-i', dest='includepattern', help='include lines matching some pattern')
parser.set_defaults(includepattern=None)


parser.add_argument('file', nargs=argparse.REMAINDER)

args = parser.parse_args()
fToParse = sys.stdin if not len(args.file) else open(args.file[0])

scpatterns=["sc Received", "sc Sending"]
cspatterns=["cs Received", "cs Sending"]
patterns=[]

if not args.onlyactionpackets:
    patterns+=(cspatterns)
if not args.onlyclientreqs:
    patterns+=(scpatterns)


for l in fToParse:

    if args.postcs and "cs POST target" in l:
        print (l.strip(),)
    if args.postsc and "sc POST target" in l:
        print (l.strip(),)
    if any(x in l for x in patterns) and "sc Received 1: 0" not in l and " sc Sending 0:" not in l:

        m = re.search('(.*): (\{.*\}|\[.*\])', l)
        if m:
            header = found = m.group(1)
            found = m.group(2)
            if header and found:
                try:
                    contents = json.dumps(json.loads(found), sort_keys=False, indent=4)
                    print (header+contents)
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
                                print (header+sendrecv+contents)
                            except:
                                print (l)
                        else:
                            print (l,)
                    else:
                        print (l,)
            else:
                print (l,)
        else:
            print (l,)
    elif args.includepattern:
        m = re.search(args.includepattern, l)
        if m:
            print (l.strip(),)
