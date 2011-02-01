import sys
import re
import __main__ # to use globals

# supports more than one regex - multiple regex are combined using AND logic
# OR logic is easily supported with the '|' regex modifier
regex_regex_ary = []

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

def plugin(line):
    __main__.totallines = __main__.totallines + 1
    for rx in regex_regex_ary:
        if not rx.search(line):
            return True # regex did not match - get next line
    else: # all regexes matched
        __main__.totallines = __main__.totallines - 1
        return __main__.defaultplugin(line)
