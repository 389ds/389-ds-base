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
# Copyright (C) 2007 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#

package Util;
require Exporter;
@ISA       = qw(Exporter);
@EXPORT    = qw(portAvailable getAvailablePort isValidDN);
@EXPORT_OK = qw(portAvailable getAvailablePort isValidDN);

use strict;

use Socket;

# return true if the given port number is available, false otherwise
sub portAvailable {
    my $port = shift;
    my $proto = getprotobyname('tcp');
    my $rc = socket(SOCK, PF_INET, SOCK_STREAM, $proto);
    if ($rc == 1) {
        $rc = bind(SOCK, sockaddr_in($port, INADDR_ANY));
    }
    close(SOCK);
    return $rc and ($rc == 1);
}

# returns a randomly assigned port number, or -1
# if not able to find an available port
sub getAvailablePort {
    my $MINPORT = 1024;
    my $MAXPORT = 65535;

    srand( time() ^ ($$ + ($$ << 15)) );
    while (1) {
        my $port = $MINPORT + int(rand($MAXPORT-$MINPORT));

        if (portAvailable($port)) {
            return $port;
        }
    }
}

sub isValidDN {
    my $dn = shift;
    return ($dn =~ /^[0-9a-zA-Z_-]+=.*$/);
}

1;
