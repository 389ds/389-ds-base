#{{PERL-EXEC}}
#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2004 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#

sub usage {
	print(STDERR "Usage: $0 [-v] -D rootdn { -w password | -w - | -j filename } \n");
	print(STDERR "        -n instance [-t attributeName[:indextypes[:matchingrules]]]\n");
	print(STDERR " Opts: -D rootdn          - Directory Manager\n");
	print(STDERR "     : -w password        - Directory Manager's password\n");
	print(STDERR "     : -w -               - Prompt for Directory Manager's password\n");
	print(STDERR "     : -j filename        - Read Directory Manager's password from file\n");
	print(STDERR "     : -n instance        - instance to be indexed\n");
	print(STDERR "     : -t attributeName[:indextypes[:matchingrules]]\n");
	print(STDERR "                          - attribute: name of the attribute to be indexed\n");
	print(STDERR "                            If omitted, all the indexes defined \n");
	print(STDERR "                            for that instance are generated.\n");
	print(STDERR "                          - indextypes: comma separated index types\n");
	print(STDERR "                          - matchingrules: comma separated matrules\n");
	print(STDERR "                          Example: -t foo:eq,pres\n");
	print(STDERR "     : -v                 - version\n");
}

$instance = "";
$rootdn = "";
$passwd = "";
$passwdfile = "";
$attribute_arg = "";
$vlvattribute_arg = "";
$verbose = 0;

$dsroot = "{{DS-ROOT}}";
$mydsroot = "{{MY-DS-ROOT}}";

$i = 0;
while ($i <= $#ARGV) 
{
	if ("$ARGV[$i]" eq "-n")
	{	
		# instance
		$i++; $instance = $ARGV[$i];
	}
	elsif ("$ARGV[$i]" eq "-D") 
	{	
		# Directory Manager
		$i++; $rootdn = $ARGV[$i];
	}
	elsif ("$ARGV[$i]" eq "-w") 
	{	
		# Directory Manager's password
		$i++; $passwd = $ARGV[$i];
	} 
	elsif ("$ARGV[$i]" eq "-j")
	{
		 # Read Directory Manager's password from a file
		$i++; $passwdfile = $ARGV[$i];
	}
	elsif ("$ARGV[$i]" eq "-t") 
	{	
		# Attribute to index
		$i++; $attribute_arg = $ARGV[$i];
	}
	elsif ("$ARGV[$i]" eq "-T") 
	{	
		# Vlvattribute to index
		$i++; $vlvattribute_arg = $ARGV[$i];
	}
	elsif ("$ARGV[$i]" eq "-v") 
	{	
		# verbose
		$verbose = 1;
	}
	else
	{
		&usage; exit(1);
	}
	$i++;
}

if ($passwdfile ne ""){
# Open file and get the password
	unless (open (RPASS, $passwdfile)) {
		die "Error, cannot open password file $passwdfile\n";
	}
	$passwd = <RPASS>;
	chomp($passwd);
	close(RPASS);
} elsif ($passwd eq "-"){
# Read the password from terminal
	die "The '-w -' option requires an extension library (Term::ReadKey) which is not\n",
	    "part of the standard perl distribution. If you want to use it, you must\n",
	    "download and install the module. You can find it at\n",
	    "http://www.perl.com/CPAN/CPAN.html\n";
# Remove the previous line and uncomment the following 6 lines once you have installed Term::ReadKey module.
# use Term::ReadKey; 
#	print "Bind Password: ";
#	ReadMode('noecho'); 
#	$passwd = ReadLine(0); 
#	chomp($passwd);
#	ReadMode('normal');
}

if ( $rootdn eq "" || $passwd eq "" ) 
{ 
	&usage; 
	exit(1); 
}

$vstr = "";
if ($verbose != 0) 
{ 
	$vstr = "-v"; 
}

($s, $m, $h, $dy, $mn, $yr, $wdy, $ydy, $r) = localtime(time);
$mn++; $yr += 1900;
$taskname = "db2index_${yr}_${mn}_${dy}_${h}_${m}_${s}";

if ( $instance eq "" )
{
	&usage;
	exit(1);
}
else
{
    # No attribute name has been specified: let's get them from the configuration
    $attribute="";
    $indexes_list="";
    $vlvattribute="";
    $vlvindexes_list="";
    if ( $attribute_arg eq "" && $vlvattribute_arg eq "" )
    {
        # Get the list of indexes from the entry
        $indexes_list="$dsroot{{SEP}}shared{{SEP}}bin{{SEP}}ldapsearch $vstr -h {{SERVER-NAME}} -p {{SERVER-PORT}} -D \"$rootdn\" -w \"$passwd\" -s one " .
        "-b \"cn=index,cn=\"$instance\", cn=ldbm database,cn=plugins,cn=config\" \"(&(objectclass=*)(nsSystemIndex=false))\" cn";
    
        # build the values of the attribute nsIndexAttribute
        open(LDAP1, "$indexes_list |");
        while (<LDAP1>) {
            s/\n //g;
            if (/^cn: (.*)\n/) {
                $IndexAttribute="nsIndexAttribute";
                $attribute="$attribute$IndexAttribute: $1\n";
            }
        }
        close(LDAP1);
        if ( $attribute eq "" )
        {
            # No attribute to index, just exit
            exit(0);
        }

        # Get the list of indexes from the entry
        $vlvindexes_list="$dsroot{{SEP}}shared{{SEP}}bin{{SEP}}ldapsearch $vstr -h {{SERVER-NAME}} -p {{SERVER-PORT}} -D \"$rootdn\" -w \"$passwd\" -s sub -b \"cn=\"$instance\", cn=ldbm database,cn=plugins,cn=config\" \"objectclass=vlvIndex\" cn";
    
        # build the values of the attribute nsIndexVlvAttribute
        open(LDAP1, "$vlvindexes_list |");
        while (<LDAP1>) {
            s/\n //g;
            if (/^cn: (.*)\n/) {
                $vlvIndexAttribute="nsIndexVlvAttribute";
                $vlvattribute="$vlvattribute$vlvIndexAttribute: $1\n";
            }
        }
        close(LDAP1);
    }
    else
    {
        if ( $attribute_arg ne "" )
        {
            $attribute="nsIndexAttribute: $attribute_arg\n";
        }
        if ( $vlvattribute_arg ne "" )
        {
            $vlvattribute="nsIndexVlvAttribute: $vlvattribute_arg\n";
        }
    }
    
    # Build the task entry to add
    
    $dn = "dn: cn=$taskname, cn=index, cn=tasks, cn=config\n";
    $misc = "changetype: add\nobjectclass: top\nobjectclass: extensibleObject\n";
    $cn =  "cn: $taskname\n";
    $nsinstance = "nsInstance: ${instance}\n";
    
    $entry = "${dn}${misc}${cn}${nsinstance}${attribute}${vlvattribute}";
}
chdir("$dsroot{{SEP}}shared{{SEP}}bin");
open(FOO, "| $dsroot{{SEP}}shared{{SEP}}bin{{SEP}}ldapmodify $vstr -h {{SERVER-NAME}} -p {{SERVER-PORT}} -D \"$rootdn\" -w \"$passwd\" -a" );
print(FOO "$entry");
close(FOO);
