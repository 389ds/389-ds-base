#{{PERL-EXEC}}

use Getopt::Std;
use File::Copy "cp";
use warnings;
#use strict;

sub usage {
  print (STDERR "Arguments:\n");
  print (STDERR " -i              - install\n");
  print (STDERR " -u              - uninstall\n");
  print (STDERR " -p              - optional dsml port, defaults to 8080\n\n");

  exit 100;
}

# Process the command line arguments
{
    usage() if (!getopts('uip:'));
    
    $DSMLPORT=8080;
    $DSMLPORT=$opt_p if ($opt_p);
    my $SERVERNAME = "{{SERVER-NAME}}";
    my $SERVERROOT = "{{DS-ROOT}}";
    my $PATH="{{SEP}}admin-serv{{SEP}}config{{SEP}}";
    my @FILES= ( "server.xml", "web-apps.xml", "obj.conf", "jvm12.conf" );

    die "-i or -u required" if (!($opt_i || $opt_u));
    die "-i OR -u required" if ($opt_i && $opt_u);    

    if ($opt_i) {
	if (-e "$SERVERROOT{{SEP}}clients{{SEP}}dsmlgw{{SEP}}dsmlgw.cfg" ) {
	    print STDERR "leaving existing dsmlgw.cfg untouched.\n";
	    } else {
	open DSMLCFG, ">$SERVERROOT{{SEP}}clients{{SEP}}dsmlgw{{SEP}}dsmlgw.cfg";
	select DSMLCFG;
	print "\#properties file for the Netscape DSMLGW\n\nServerHost=${SERVERNAME}\nServerPort={{SERVER-PORT}}\nBindDN=\nBindPW=\n\n";
	print "MinLoginPool=1\nMaxLoginPool=2\nMinPool=3\nMaxPool=15\n";
	close DSMLCFG;
    }

	open WEB, ">$SERVERROOT{{SEP}}clients{{SEP}}dsmlgw{{SEP}}WEB-INF{{SEP}}web.xml";
	select WEB;
	    print <<EOF;
<?xml version="1.0" encoding="ISO-8859-1"?>

<!DOCTYPE web-app SYSTEM "../web-app_2_3.dtd">

<web-app>
  <display-name>Apache-Axis</display-name>
    
    <listener>
        <listener-class>org.apache.axis.transport.http.AxisHTTPSessionListener</listener-class>
    </listener>
    
  <servlet>
    <servlet-name>AxisServlet</servlet-name>
    <display-name>Apache-Axis Servlet</display-name>
    <servlet-class>
        org.apache.axis.transport.http.AxisServlet
    </servlet-class>
  </servlet>

  <servlet-mapping>
    <servlet-name>AxisServlet</servlet-name>
    <url-pattern>/services/*</url-pattern>
  </servlet-mapping>

    <session-config>
        <!-- Default to 5 minute session timeouts -->
        <session-timeout>5</session-timeout>
    </session-config>

  <mime-mapping>
    <extension>wsdl</extension>
    <mime-type>text/xml</mime-type>
  </mime-mapping>
  

  <mime-mapping>
    <extension>xsd</extension>
    <mime-type>text/xml</mime-type>
  </mime-mapping>

  <welcome-file-list>
    <welcome-file>index.html</welcome-file>
    <welcome-file>index.jsp</welcome-file>
    <welcome-file>index.jws</welcome-file>
  </welcome-file-list>

</web-app>

EOF

	
	close WEB;
    }

    foreach $file (@FILES) {

	if ($opt_u) {
	    # restore from backups
	    print STDOUT "${file}.bak -> ${file}\n";
	    rename("${SERVERROOT}${PATH}${file}.bak","${SERVERROOT}${PATH}${file}");
	}
	if ($opt_i) {
	    # make backups
	    print STDOUT "${file} -> ${file}.bak\n";
	    cp("${SERVERROOT}${PATH}${file}","${SERVERROOT}${PATH}${file}.bak");

	    if ( $file eq "server.xml") {
		open SERVER, "${SERVERROOT}${PATH}${file}" || die("Could not open file!");
		@raw_data=<SERVER>;
		close SERVER;

		$i=0;

		while ($line =  $raw_data[$i++]) {
		    #if ($line =~ /CONNECTIONGROUP.*servername=\"([\w.?]+)\"/ ){
			#$SERVERNAME = $1;
		    #}

		    if ($line =~ /\<\/LS/ ) {
			splice @raw_data, $i++,0, 
			("    <LS id=\"dsml-listener\" ip=\"0.0.0.0\" port=\"${DSMLPORT}\" security=\"off\" acceptorthreads=\"1\" blocking=\"no\">\n",
			 "      <CONNECTIONGROUP id=\"dsml_default\" matchingip=\"default\" servername=\"${SERVERNAME}\" defaultvs=\"dsml-serv\"/></LS>\n" );
			$i+=2;
		    }
		    
		    if ($line =~ /\<\/VSCLASS/ ) {
			splice @raw_data, $i, 0, 
			("    <VSCLASS id=\"dsml\" objectfile=\"obj.conf\" rootobject=\"dsmlgw\" acceptlanguage=\"off\">\n" . 
			 "<VARS nice=\"\" docroot=\"${SERVERROOT}{{SEP}}clients{{SEP}}dsmlgw\" webapps_file=\"web-apps.xml\" webapps_enable=\"on\"   />\n" . 
			 "      <VS id=\"dsml-serv\" state=\"on\" connections=\"dsml_default\" urlhosts=\"${SERVERNAME}\" mime=\"mime1\" aclids=\"acl1\">\n" .
			 "        <USERDB id=\"default\" database=\"default\"/>\n      </VS>\n    </VSCLASS>\n");
			$i++;
		    }
		    
		    
		}
		open SERVER, "> ${SERVERROOT}${PATH}${file}" || die("Could not open file!");
		select SERVER;
		print @raw_data;
		close SERVER;
	    }

	    if ( $file eq "web-apps.xml" ) {
		open WEBAPPS, "> ${SERVERROOT}${PATH}${file}";
		select WEBAPPS;
		print STDERR "adding necessary entry to $file.\n";
		print <<EOF;
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE vs  PUBLIC "-//Netscape Communications Corp.; Netscape//DTD Virtual Server Web Applications 6.1//EN" "http://developer.netscape.com/products/servers/enterprise/dtds/nes-webapps_6_1.dtd">
<vs>
 <web-app uri="/axis" dir="${SERVERROOT}{{SEP}}clients{{SEP}}dsmlgw" enable="true"/>
</vs>

EOF

                close WEBAPPS;
		
	    }

	    if ( $file eq "obj.conf" ) {
		open OBJ, ">> ${SERVERROOT}${PATH}${file}";
		select OBJ;
		print STDERR "adding necessary entry to $file.\n";
		print <<EOF;
<Object name="dsmlgw">
ObjectType fn=type-by-extension
ObjectType fn=force-type type=text/plain
Service fn="NSServletService" type="magnus-internal/servlet"
Service method=(GET|HEAD|POST) type=*~magnus-internal/* fn=send-file
Error fn="admin-error" reason="server error"
AddLog fn="admin40_flex_log" name="access"
NameTrans fn="NSServletNameTrans" name="servlet"
PathCheck fn=find-pathinfo
PathCheck fn=find-index index-names="index.html,home.html"
Service type="magnus-internal/jsp" fn="NSServletService"
</Object>

EOF

               close OBJ;
	    

	    }

	    if ( $file eq "jvm12.conf" ) {
		open JVM, ">>  ${SERVERROOT}${PATH}${file}";
		select JVM;
		print STDERR "adding necessary entry to $file.\n";
		print "jvm.option=-Duser.home=${SERVERROOT}{{SEP}}clients{{SEP}}dsmlgw";
		close JVM;
	    }
	}
    }
}
