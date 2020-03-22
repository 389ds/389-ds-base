#!/usr/bin/env python3
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
    gmtime_obj = time.gmtime(int(SDE))
else:
    gmtime_obj = time.gmtime()
    
# Print build number
buildnum = time.strftime("%Y.%j.%H%M", gmtime_obj)
print(f'\\"{buildnum}\\"', end = '')
