#!/bin/sh
#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# This is extracted from the 2.0 config shell script to decouple the module
# extraction functionality.
#
# Input:  modules.awk
# Output: modules.mk  (this will be included in nsconfig.mk)
#

awk '
/^LIBRARY.*/ { 
        printf "%s:", $2
        for (i = 4; i <= NF; i++) printf " %s", $i
        printf "\n\tcd %s; $(MAKE) $(MFLAGS)\n", $3
        printf "clean_%s:", $2
        for (i = 4; i <= NF; i++) printf " clean_%s", $i
        printf "\n\tcd %s; $(MAKE) clean\n\n", $3
        printf "depend_%s:", $2
        for (i = 4; i <= NF; i++) printf " depend_%s", $i
        printf "\n\tcd %s; $(MAKE) depend\n\n", $3
        printf("else\nclean:");
        for (i = 4; i <= NF; i++) printf " clean_%s", $i
        printf("\ndepend:");
        for (i = 4; i <= NF; i++) printf " depend_%s", $i
        printf("\n");
        next 
} 
/^DISTLIB.*/ { 
        printf "%s:", $2
        for (i = 4; i <= NF; i++) printf " %s", $i
        printf "\n\tcd %s; $(MAKE) export $(NSDEFS)\n", $3
        printf "clean_%s:", $2
        for (i = 4; i <= NF; i++) printf " clean_%s", $i
        printf "\n\tcd %s; $(MAKE) clean\n\n", $3
        printf "depend_%s:", $2
        for (i = 4; i <= NF; i++) printf " depend_%s", $i
        printf "\n\tcd %s; $(MAKE) depend\n\n", $3
        printf("else\nclean:");
        for (i = 4; i <= NF; i++) printf " clean_%s", $i
        printf("\ndepend:");
        for (i = 4; i <= NF; i++) printf " depend_%s", $i
        printf("\n");
        next 
} 
/^MODULE.*/ { 
        printf "%s:", $2
        for (i = 4; i <= NF; i++) printf " %s", $i
        printf "\n\tcd %s; $(MAKE) $(MFLAGS)\n\n", $3
        printf "clean_%s:", $2
        for (i = 4; i <= NF; i++) printf " clean_%s", $i
        printf "\n\tcd %s; $(MAKE) clean\n\n", $3
        printf "depend_%s:", $2
        for (i = 4; i <= NF; i++) printf " depend_%s", $i
        printf "\n\tcd %s; $(MAKE) depend\n\n", $3
        printf("else\nclean:");
        for (i = 4; i <= NF; i++) printf " clean_%s", $i
        printf("\ndepend:");
        for (i = 4; i <= NF; i++) printf " depend_%s", $i
        printf("\n");
        next 
} 
/^PACKAGE.*/ {
        printf "%s:", $2
        for (i = 3; i <= NF; i++) printf " %s", $i
        printf "\nclean_%s:", $2
        for (i = 3; i <= NF; i++) printf " clean_%s", $i
        printf "\ndepend_%s:", $2
        for (i = 3; i <= NF; i++) printf " depend_%s", $i
        printf("\n");
        next 
} 
{print} ' modules.awk > modules.mk
