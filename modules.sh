#!/bin/sh
#
# BEGIN COPYRIGHT BLOCK
# This Program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; version 2 of the License.
# 
# This Program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License along with
# this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
# Place, Suite 330, Boston, MA 02111-1307 USA.
# 
# In addition, as a special exception, Red Hat, Inc. gives You the additional
# right to link the code of this Program with code not covered under the GNU
# General Public License ("Non-GPL Code") and to distribute linked combinations
# including the two, subject to the limitations in this paragraph. Non-GPL Code
# permitted under this exception must only link to the code of this Program
# through those well defined interfaces identified in the file named EXCEPTION
# found in the source code files (the "Approved Interfaces"). The files of
# Non-GPL Code may instantiate templates or use macros or inline functions from
# the Approved Interfaces without causing the resulting work to be covered by
# the GNU General Public License. Only Red Hat, Inc. may make changes or
# additions to the list of Approved Interfaces. You must obey the GNU General
# Public License in all respects for all of the Program code and other code used
# in conjunction with the Program except the Non-GPL Code covered by this
# exception. If you modify this file, you may extend this exception to your
# version of the file, but you are not obligated to do so. If you do not wish to
# do so, delete this exception statement from your version. 
# 
# 
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
