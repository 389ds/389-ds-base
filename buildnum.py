#!/usr/bin/python3
# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

# Generate a build number in the format YYYY.DDD.HHMM

import os
import time

SDE = os.getenv('SOURCE_DATE_EPOCH')
if SDE is not None:
    obj = time.gmtime(SDE)
else:
    obj = time.gmtime()
    
year = obj[0]
doy = obj[7]
if doy < 100:
    doy = "0" + str(doy)
tod = str(obj[3]) + str(obj[4])
buildnum = f"{year}.{doy}.{tod}"

print(f'\\"{buildnum}\\"', end = '')
