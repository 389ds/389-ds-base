#!/bin/sh
# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# --- END COPYRIGHT BLOCK ---

# Configure nsPerl
if [ ! -f "./tools/perl" ]; then
    ./tools/nsPerl5.6.1/install > /dev/null
    ln -s ./nsPerl5.6.1/nsperl ./tools/perl
fi

# Kick off setup script
./setup.pl $*
