#!/bin/sh
#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# Usage: filterfiles.sh filter into-dir from-dir from-file ...
# echo filterdir.sh "$@"

FILTER="$1"; shift
INTO="$1"; shift
FROM="$1"; shift
if [ ! -d ${INTO} ]; then mkdir ${INTO}; fi
for PATTERN in "$@"; do
    for FILE in ${FROM}/${PATTERN}; do
	if [ -f ${FILE} ]; then
	    BASE=`basename ${FILE}`
	    case ${BASE} in
		*.gif )
		    echo "cp ${FILE} ${INTO}"
		          cp ${FILE} ${INTO} || exit $? ;;
		* )
		    echo "sh ${FILTER} ${FILE} > ${INTO}/${BASE}"
		          sh ${FILTER} ${FILE} > ${INTO}/${BASE} || exit $? ;;
	    esac
	fi
    done
done
