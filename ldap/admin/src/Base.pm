#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#

package NSSetupSDK::Base;

use POSIX;

use strict;
use vars qw($VERSION @ISA @EXPORT @EXPORT_OK $AUTOLOAD);

require Exporter;
require DynaLoader;
require AutoLoader;

@ISA = qw(Exporter DynaLoader);
# Items to export into callers namespace by default. Note: do not export
# names by default without a very good reason. Use EXPORT_OK instead.
# Do not simply export all your public functions/methods/constants.

# Items to export into callers namespace by default. Note: do not export
# names by default without a very good reason. Use EXPORT_OK instead.
# Do not simply export all your public functions/methods/constants.
@EXPORT = qw(
	toLocal toUTF8
);
$VERSION = '1.00';

bootstrap NSSetupSDK::Base $VERSION;

# Autoload methods go after =cut, and are processed by the autosplit program.

1;
__END__
# Below is the stub of documentation for your module. You better edit it!

=head1 NAME

NSSetupSDK::Base - Perl extension for directory server administrative utility functions

=head1 SYNOPSIS

  use NSSetupSDK::Base;

=head1 DESCRIPTION

The NSSetupSDK::Base module is used by directory server administration scripts, such as
those used for installation/uninstallation, instance creation/removal, CGIs,
etc.

=head1 AUTHOR

Richard Megginson richm@netscape.com

=head1 SEE ALSO

perl(1).

=cut
