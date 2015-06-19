#!/bin/sh
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2009 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
# END COPYRIGHT BLOCK
#

# There are several environment variables passed in:
PRE_STAGE="pre";
PREINST_STAGE="preinst";
RUNINST_STAGE="runinst";
POSTINST_STAGE="postinst";
POST_STAGE="post";

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
