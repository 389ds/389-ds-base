# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2009 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
# END COPYRIGHT BLOCK
#

sub pre {
    my ($inf, $configdir) = @_;
}

sub preinst {
    my ($inf, $inst, $dseldif, $conn) = @_;
}
sub runinst {
    my ($inf, $inst, $dseldif, $conn) = @_;
}

sub postinst {
    my ($inf, $inst, $dseldif, $conn) = @_;
}

sub post {
    my ($inf, $configdir) = @_;
}
