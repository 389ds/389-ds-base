import sys
import re
import __main__ # to use globals

# supports more than one regex - multiple regex are combined using AND logic
# OR logic is easily supported with the '|' regex modifier
regex_regex_ary = []
buffer = []

def pre(plgargs):
    global regex_regex_ary
    regexary = plgargs.get('regex', None)
    if not regexary:
        print "Error: missing required argument logregex.regex"
        return False
    if isinstance(regexary,list):
        regex_regex_ary = [re.compile(xx) for xx in regexary]
    else:
        regex_regex_ary.append(re.compile(regexary))
    return True

def post():
    global buffer
    sys.stdout.writelines(buffer)
    buffer = []

def plugin(line):
    global buffer
    for rx in regex_regex_ary:
        if not rx.search(line):
            break # must match all regex
    else: # all regexes matched
        buffer.append(line)
        if len(buffer) > __main__.maxlines:
            del buffer[0]
    return True
