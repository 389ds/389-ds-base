#{{PERL-EXEC}}
#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#

use Cgi;

$params = "";
$params .= " -h $cgiVars{'servhost'}" if $cgiVars{'servhost'};
$params .= " -p $cgiVars{'servport'}" if $cgiVars{'servport'};
$params .= " -f $cgiVars{'configfile'}" if $cgiVars{'configfile'};
$params .= " -t $cgiVars{'refreshinterval'}" if $cgiVars{'refreshinterval'};
if ($cgiVars{'admurl'}) {
	$admurl = "$cgiVars{'admurl'}";
	if ( $ENV{'QUERY_STRING'} ) {
		$admurl .= "?$ENV{'QUERY_STRING'}";
	}
	elsif ( $ENV{'CONTENT_LENGTH'} ) {
		$admurl .= "?$Cgi::CONTENT";
	}
	$params .= " -u \"$admurl\"";
}
$siteroot = $cgiVars{'siteroot'};
$perl = "$siteroot/bin/slapd/admin/bin/perl";
$ENV{'LD_LIBRARY_PATH'} = "$siteroot/lib:$siteroot/lib/nsPerl5.005_03/lib";

# Save user-specified parameters as cookies in monreplication.properties.
# Sync up with the property file so that monreplication2 is interval, and
# monreplication3 the config file pathname.
$propertyfile = "$siteroot/bin/admin/admin/bin/property/monreplication.properties";
$edit1 = "s#monreplication2=.*#monreplication2=$cgiVars{'refreshinterval'}#;";
$edit2 = "s#^monreplication3=.*#monreplication3=$cgiVars{'configfile'}#;";
system("$perl -p -i.bak -e \"$edit1\" -e \"$edit2\" $propertyfile");

# Now the real work
$replmon = "$siteroot/bin/slapd/admin/scripts/template-repl-monitor.pl";
system("$perl $replmon $params");
