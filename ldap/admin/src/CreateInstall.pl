#!perl
#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# figure out what the server root assuming the path to this script is
# server root/bin/slapd/admin/bin

($serverRoot = $0) =~ s@[\\/]?bin[\\/]slapd[\\/]admin[\\/]bin.*$@@g;

# run the post install program
$isNT = -d '\\';
$quote = $isNT ? "\"" : "";
# make sure the arguments are correctly quoted on NT
@fixargs = map { /^[\"].*[\"]$/ ? $_ : $quote . $_ . $quote } @ARGV;
if (! $serverRoot) {
  $serverRoot = ".";
}
chdir "$serverRoot/bin/slapd/admin/bin";

# note: exec on NT doesn't work the same way as exec on Unix. On Unix, exec replaces the calling
# process with the called process.  The parent, if waiting for the calling process, will happily
# wait for it's replacement.  On NT however, the parent thinks the calling process has gone, and
# it doesn't know about the called process, so it stops waiting.  So, we have to do a system()
# on NT to force the calling process to wait for the called process.  On Unix, we can do the
# faster and more memory efficient exec() call.
if ($isNT) {
  system {'./ds_create'} './ds_create', @fixargs;
} else {
  exec {'./ds_create'} './ds_create', @fixargs;
}
