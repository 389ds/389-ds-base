# --- BEGIN COPYRIGHT BLOCK ---
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
# --- END COPYRIGHT BLOCK ---
#
# The script is used to load the config mapping tree node for the null suffix
# and to load the test sample plugin node into the Directory Server used to
# demonstrate the Data Interoperability feature for Verisign. 

# The dse.ldif configuration file used as the default configuration by the 
# Server gets edited for adding the above mentioned two config nodes and the
# server is restarted to load the plugin. 

# The loading of the DataInterop test plugin is only to demostrate the use
# of the test plugin and can be replaced with Verisign's database plugin for use.

# Written by binduk@netscape.com

# Edits to be done for use
# $host  = Hostname of the Directory Server
# $port  = port used by the Server
# $mgrdn = Bind Dn 
# $mgrpass = Password  
# $installDir = Installation root of the Server <server-root>


###### Begin Edits ##################################

$host 		= "trika.nscp.aoltw.net";
$instance   = "trika2";
$port 		= "7775";
$mgrdn 		= "cn=directory manager";
$mgrpass 	= "password";
$installDir = "/export/msyBuild/install";  						# Installation root of the Server <server-root>

###### End of  Edits ##################################


$dseOrgFile = "$installDir/slapd-$instance/config/dse.ldif"; 	# default configuration file to be edited
$dseFile = "$installDir/slapd-$instance/config/load_dse.ldif"; 	# additional configuration file to be added 
$pidFile = "$installDir/slapd-$instance/logs/pid"; 				# pid file for the running server 
my $editedNode = 0;
my $editedPlugin = 0;
my $serverStatus = 1;

	
	if(!(-e $pidFile)){
		open( START_SERVER, "| $installDir/slapd-$instance/start-slapd ") || die "Can't Start the Server \n";  
		close(START_SERVER);
	
		if(-e $pidFile){
			print  " ######## Started the Server \n";
		}
		else {
			print  " ######## Unable to Start the Server \n";
			$serverStatus = 0;			
		}
	}


open(DSE_ORG, "$dseOrgFile") || die "Can't open $dseOrgFile for checks \n";
	while(<DSE_ORG>){
		$isEditedNode = 1 if (/^dn: cn=\"\",cn=mapping tree,cn=config/);
		$isEditedPlugin = 1 if (/^dn: cn=datainterop,cn=plugins,cn=config/);
	}
close(DSE_ORG);

open(DSE, ">$dseFile") || die "Can't open $dseFile for editing \n";

	my $changesMade = 0;
	unless($isEditedNode){
		print DSE "dn: cn=\"\",cn=mapping tree,cn=config\n";
		print DSE "objectClass: top\n";
		print DSE "objectClass: extensibleObject\n";
		print DSE "objectClass: nsMappingTree\n";
		print DSE "cn: \"\"\n";
		print DSE "nsslapd-state: container\n";
		print DSE "\n";
		$changesMade =  1;
	}

	unless($isEditedPlugin){
		print DSE "dn: cn=datainterop,cn=plugins,cn=config\n";
		print DSE "objectClass: top\n";
		print DSE "objectClass: nsSlapdPlugin\n";
		print DSE "cn: datainterop\n";
		print DSE "nsslapd-pluginPath: $installDir/plugins/slapd/slapi/examples/libtest-plugin.so\n";
		print DSE "nsslapd-pluginInitfunc: nullsuffix_init\n";
		print DSE "nsslapd-pluginType: preoperation\n";
		print DSE "nsslapd-pluginEnabled: on\n";
		print DSE "nsslapd-pluginId: nullsuffix-preop\n";
		print DSE "nsslapd-pluginVersion: 6.2\n"; 
		print DSE "nsslapd-pluginVendor: Netscape\n";
		print DSE "nsslapd-pluginDescription: sample pre-operation null suffix search plugin\n";
		$changesMade =  1;
	}
close(DSE);


if($changesMade){
	chdir "$installDir/shared/bin" or die "cannot cd over error=$! \n";

	open(LDAPMODIFY, "|ldapmodify -p \"${port}\" -h \"${host}\" -D \"${mgrdn}\" -w \"${mgrpass}\" -v -c -a -f $dseFile  " ) || die "Can't modify the configuration of the Server \n";

	close(LDAPMODIFY);

	print " Modifications to the dse.ldif file have been done....restarting the server to load plugin \n";

	open( STOP_SERVER, "| $installDir/slapd-$instance/stop-slapd ") || die "Can't Stop the Server \n";;  
	close(STOP_SERVER);

	print  " Now stopped the Server to load the plugin \n";

	open( START_SERVER, "| $installDir/slapd-$instance/start-slapd ") || die "Can't Start the Server \n";  
	close(START_SERVER);

    if(-e $pidFile){
    	print  "  Started the Server Successfully\n";
	}
	else{
       $serverStatus = 0;
       print "Failure in starting the Server - Check to see if the sample plugin has been compiled \n";
	}

}
else {
		if($serverStatus){
			print  " Nothing needs to be done \n";
		}
		else {
			print  " The Server did not Start Successfully \n";
		}

}
