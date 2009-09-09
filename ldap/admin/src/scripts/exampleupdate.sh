#!/bin/sh
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
# provide this exception without modification, you must delete this exception
# statement from your version and license this file solely under the GPL without
# exception. 
# 
# 
# Copyright (C) 2009 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#

# There are several environment variables passed in:
my $PRE_STAGE = "pre";
my $PREINST_STAGE = "preinst";
my $RUNINST_STAGE = "runinst";
my $POSTINST_STAGE = "postinst";
my $POST_STAGE = "post";

# $DS_UPDATE_STAGE - the current stage of the update - one of
# pre - called at the beginning of the update
# preinst - called before processing an instance
# runinst - the main update stage for an instance
# postinst - called after processing an instance
# post - called the the end of the update
# you should definitely check the stage to make sure you only perform
# your actions during the correct stage e.g.

if [ "$DS_UPDATE_STAGE" != "pre" ] ; then
    exit 0
fi

# $DS_UPDATE_DIR - the main config directory containing the schema dir
#   the config dir and the instance specific (slapd-instance) directories
# $DS_UPDATE_INST - the name of the instance (slapd-instance), if one of the instance specific stages
# $DS_UPDATE_DSELDIF - the full path ane filename of the dse.ldif file for the instance
