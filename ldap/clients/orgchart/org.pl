#!../../../bin/slapd/admin/bin/perl
#
#set ts=4

# ------------
#
#  Notes for anybody reading the code below:
#
#  [1]	The concept of the $uid variable throughout the code 
#		is whatever the leftmost RDN value is for a given user DN,
#		and this relates to the "attrib-farleft-rdn" setting in
#		config.txt, of what the attribute name will always be.
#
# ------------


use Mozilla::LDAP::Conn;
use Mozilla::LDAP::Utils qw(:all);

use CGI;
$cg = new CGI;

$|=1;
print "Content-type: text/html;charset=UTF-8\n\n";  

##########################################
#
#  Let's find out what browswer they are using
#
##########################################

$agentstring = $ENV{'HTTP_USER_AGENT'}; 

# IE 6.0  :   ---Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0)---
# Comm478 :   ---Mozilla/4.78 [en] (Windows NT 5.0; U)---
# Nscp622 :   ---Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:0.9.4.1) Gecko/20020314 Netscape6/6.2.2---

$browser_is_msie = "MSIE" if $agentstring =~ /MSIE/;

# is this Windows?
$isWindows = -d '\\';

##########################################
#
#  Read config.txt settings, set by the administrator
#
##########################################

&read_config_file();

##########################################
#
#  Let's look at what is being passed in, from the user.
#
##########################################


#
#  "data" is a generic FORM variable name from
#  the topframe.html document that we receive our incoming query
#  from.  
#
#  (See comment at start of this file about "$uid" variable.)
#
if (  defined  $cg->param("data")   )
{
	$uid = $cg->param("data");
}

#
# For coexistence with the DSGW, when we crosslink, we need to
# make sure that the user is taken back to the correct dsgw
# context
#
$contextParamString = "";
if (  defined  $cg->param("context")   )
{
    $context = $cg->param("context");
    $contextParamString = "context=${context}&";
    $config_tokens{"url-phonebook-base"} =~ s/context=.*?&/$contextParamString/g;
}

#
#  But they may have entered this code from clicking on an org
#  chart icon from an already-drawn org chart, in which case
#  we know what the RDN attribute name is (cn, uid, etc.), so i
#  that has priority, if present.
#
if (  defined  $cg->param("$config_tokens{'attrib-farleft-rdn'}")  )
{
	$uid = $cg->param("$config_tokens{'attrib-farleft-rdn'}")
}

if ($uid eq "") 
{
	&output_html_header("no-javascript");
	print "No username selected...</BODY></HTML>";
	#print "\n</BODY></HTML>";
	exit(0);
}

##########################################
#
#  If the user has asked this org chart to be prepared for printing
#
##########################################

if (  (defined  $cg->param("print")) && ( $cg->param("print") eq "yes" ) )
{
	$print_mode = 1;
}
else
{
	$print_mode = 0;
}

if ( !($print_mode) )
{
	$fontstring="<font face=\"verdana, Arial, Helvetica, sans-serif\" style=\"font-size: 11px\">";
}
else 
{
	# if printing, let's make the font smaller, to fit more org chart on one page
	#
	$fontstring="<font face=\"verdana, Arial, Helvetica, sans-serif\" style=\"font-size: 8px\">";
}

##########################################
#
#  See if the user has their own preferences to use.
#
#
##########################################

&check_myorgchart_settings();


##########################################
#
#  Let's configure which attributes to request from LDAP,
#  based on preferences read above...
#
##########################################

&config_ldap_return_attrib_list();


##########################################
#
#  global variable descriptions:
#
#       $total          :  stores the displayed statistic of "Total # of people" that is printed under org chart
#       $display_indent :  helps track how deeply "indented" in the org chart hierarchy a given person is, to help
#                          draw an internal data structure of the hierarchy.  See details in get_org_data() function.
#       $tempnum        :  just generic variable used for different reasons, always within a very small (controllable)
#                          scope within a given function only, since a generic all-purpose variable
#       $anothertempnum :  same idea as $tempnum, just another variable for the same generic purpose
#       $tempstr        :  same idea as $tempnum, just another variable for the same generic purpose
#
#
##########################################

$total = 0;
$display_indent = 0;
$tempnum = 0;
$anothertempnum = 0;
$tempstr = "";

##########################################
#
#  The $incomplete variable tracks whether an org chart cannot
#  be fully drawn because the "max number of levels to draw"
#  setting has been exceeded.  We'll use this variable value
#  to:  [1] store this fact during initial LDAP data gathering,
#  [2] to make sure we draw org chart icons (hyperlinks) next
#  to people that have people below them purposely 
#  not displayed (purposely chopped off)
#
##########################################

$incomplete = 0;

##########################################
#
#  Let's take what the end-user entered and search on it.
#  If not found as an exact uid=XXX match, then let's
#  broader their search to try to give them some results to
#  pick from.
#
##########################################

&search_for_enduser_query();

##########################################
#
#  Before we draw any part of the org chart,
#  let's send the javascript code needed back
#  to the browser first, as well as open BODY tag
#
##########################################

&output_html_header("with-javascript");


##########################################
#
#  This single DIV layer HTML code will be used as the only layer in the final output.
#  It is dynamically changed, as far as its content, based on which person that the
#  end-user hovers the mouse cursor over.  This is much faster (WAY less code to send
#  to the browser) compared to sending a unique hardcoded DIV for each and every
#  person that appears on the org chart.
#
##########################################

&print_single_div_html();

##########################################
#
#  For some reason, Nav4 browsers ignore onMouseOver and onMouseOut
#  event handlers if they are inside the DIV HTML tag itself, so
#  you have to assign them to the DIV after declaring the DIV above.
#
##########################################

&nav4_specific_event_handlers();

##########################################
#
#  Start drawing the org chart to the browser.
#
##############
#
# Let's first put the full name of the person submitted to us in a box,
# along with their manager listed underneath them.
#
##########################################

&print_topmost_box();

##########################################
#
#  See if the "manager-DN-location" config.txt setting is 
#  either "search" or else assume "same".
#
#  See config.txt file for detailed description.
#
##########################################

if ( $config_tokens{"manager-DN-location"} eq "search" )
{
	$tempstr = "*"
}
else
{
	# if we are assuming "same", then strip the leftmost RDN component
	# out of the entered LDAP user's DN, and assume that location
	# for all user entry locations we will be dealing with.be dealing with.
	#
	($tempstr) = ($entry->{dn} =~ /[^,]+=[^,]+,(.+)/ );
}

##########################################
#
#  This is where the heavy lifting is done.  Generate an 2D array
#  that stores all the org chart people data we need, to later draw it.
#
##########################################

&get_org_data($entry->{$config_tokens{'attrib-farleft-rdn'}}[0], $config_tokens{'attrib-farleft-rdn'} , $tempstr);


##########################################
#
# Let's sort the return results array, mainly to help put
# the hierarchy in order, to help us draw the final result.
#
##########################################

@sortedPeople = sort {  $a->[0] cmp $b->[0]  } @people;


##########################################
#
#  If they exceeded max depth allowed, let's still figure out
#  which people are managers of some type and make sure we
#  still put an org chart icon next to their name, even if the
#  people below them will not be shown because of being on the
#  side that was chopped off (past the max depth), so that at
#  least the end user knows people in reality report to that 
#  person, even though not displayed on purpose.
#
##########################################

&detect_nonleaf_depth_exceeded();

##########################################
#
#  This function will print just the tree branch that is below the
#  topmost box.
#
##########################################

&print_toplevel_tree_branch();

##########################################
#
#  Now let's analyze the remaining branch structure to draw,
#  and pre-markup some details about how to draw this branch structure,
#  as it can get quite complex for some org charts.
#
#  we need to scan up and down several times certain parts of the 
#  org chart data in certain areas to learn things about how to draw 
#  the structure to the screen.   This just makes life easier at the
#  final drawing stage.
#
##########################################

&pre_markup_remaining_branches();

##########################################
#
#  Draw the rest of the org chart.
#  (with "rest" meaning non-leaf entries below the uid that the end-user entered,
#  so this means both the 2nd layer of boxes that have a single name in each
#  box, as well as then the tree branches under each of those boxes)
#
##########################################

&draw_remaining_branches();

##########################################
#
#  The org chart is basically drawn now.  We just need
#  to close some tables that we have open, and print the
#  total number of reports shown on the entire org chart.
#
##########################################

print "</TD></TR></TABLE></center>";
print "\n\n";
print "<BR><BR><HR>\n$fontstring";
print "Total number of reports shown above: " . $total . "\n</font><BR><BR><BR><BR><BR><BR><BR><BR>";
print "</TD><TD NOWRAP>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</TD></TABLE></CENTER></BODY></HTML>\n";

exit;


#==============================================================================
#
#  End of "main()" part of script.   All the subroutines are below, that were
#  used above.
#
#==============================================================================


##########################################
#
#  get_org_data():  A recursive function that gets all the needed LDAP data on all people
#                   necessary to later draw the resulting org chart.
#
##########################################

sub get_org_data
{
	local ($attrib_value, $attrib_name, $managerDNlocation) = @_;
    local ($manager)="$attrib_name=" . $attrib_value . ",$managerDNlocation";
    local ($search) = "$config_tokens{'attrib-manager'}=$manager";
    local ($entry);
    local ($conn);

    $conn = new Mozilla::LDAP::Conn($config_tokens{"ldap-host"}, $config_tokens{"ldap-port"}, $config_tokens{"ldap-bind-dn"}, $config_tokens{"ldap-bind-pass"});
    die "Could't connect to LDAP server $config_tokens{\"ldap-host\"}" unless $conn;
    $entry = $conn->search($config_tokens{"ldap-search-base"}, "subtree", $search, 0, @return_attribs);

    $display_indent += 1;
		
    while ($entry) 
	{
		if (not_terminated($entry) && not_own_manager($entry))
		{
		    $total++;

			$indentname[$display_indent] = $entry->{cn}[0];

			$people[$total-1][0] = "/";
			for ( $tempnum = 1 ; $tempnum < $display_indent+1 ; $tempnum++ ) 
			{
				$people[$total-1][0] = "$people[$total-1][0]$indentname[$tempnum]/"; 
			}
			$people[$total-1][1] = $entry->{$config_tokens{'attrib-farleft-rdn'}}[0];
			$people[$total-1][2] = url_encode($entry->{dn});
			$people[$total-1][3] = $entry->{mail}[0];
			$people[$total-1][4] = $entry->{$config_tokens{"attrib-job-title"}}[0];

			# AIM
			$people[$total-1][5] = "(none)";

			if ( $config_tokens{"icons-aim-visible"} ne "no" ) 
			{
				if ( "$entry->{nsAIMStatusText}[0]" eq "ONLINE" )
				{
					$people[$total-1][5] = $entry->{nsaimid}[0];
				}
				if ( "$entry->{nsAIMStatusText}[0]" eq "OFFLINE" )
				{
					$people[$total-1][5] = "OFFLINE";
				}
			}

			# locator
			$people[$total-1][6] = url_encode($entry->{cn}[0]);


			if ( $display_indent < $config_tokens{"max-levels-drawn"}+1 )
			{
				get_org_data($entry->{$config_tokens{'attrib-farleft-rdn'}}[0], $config_tokens{'attrib-farleft-rdn'} , $managerDNlocation);
			}
			else
			{
				$incomplete = 1;
			}
	    
		}

		$entry = $conn->nextEntry();
    }


    $display_indent -= 1;
}

##########################################
#
#  not_terminated():    Should we leave this in the shipping version, since most companies
#                       may want to modify it for how they mark LDAP entries as inactive?
#
#                       Can't do any harm technically to leave it here, but may just look
#                       like loose ends to the customer and gives away part of our internal
#                       way of doing things. I'll leave it up to you, the code reviewer,
#                       to make the call. (I see pros and cons both ways.)
#
##########################################

sub not_terminated
{
    my($person) = @_;

    for ($j=0; $person->{objectclass}[$j] ; $j++)
	{
		if ($person->{objectclass}[$j] eq "nscphidethis")
		{
		    return(0);
		}
    }
    return(1);
}

##########################################
#
#  not_own_manager():   See if person reports to himself, and if so then
#						we need to tell the calling function that, so we
#						don't get caught in an infinite loop while discovering
#						the reporting chain.
#
##########################################

sub not_own_manager
{
	my ($entry) = @_;

	@manager= split (/,/ , $entry->{$config_tokens{'attrib-manager'}}[0]);
	@splitagain = split (/=/, @manager[0] );
	$manageruid = @splitagain[1];

	if ( $entry->{$config_tokens{'attrib-farleft-rdn'}}[0]  eq  $manageruid)
	{
		print "ATTENTION: $entry->{cn}[0] is his own manager!<BR>\n";
		return(0);
	}
	else
	{
		return(1);
	}
}


##########################################
#
#  Print the locator icon icon next to the person's name,
#  if that's what we are configured to do, and only if not in
#  print mode  ("print mode": if the page is not being generated
#  in a stripped-down way [sans icons] for printing)
#
##########################################

sub print_locator_icon_if_outside_layer
{
	my ($visible, $locator) = @_;

	if ( ($visible eq "forefront") && (!($print_mode)) )
	{
		print " <a href=\"$config_tokens{\"url-locator-base\"}";
		print "$locator\"><img src=\"../html/mag.gif\" border=0 align=TEXTTOP></a>";
	}

	return;
}

##########################################
#
#  Print the phonebook icon icon next to the person's name,
#  if that's what we are configured to do, and only if not in
#  print mode  ("print mode": if the page is not being generated
#  in a stripped-down way [sans icons] for printing)
#
##########################################

sub print_pb_icon_if_outside_layer
{
	my ($visible, $dn) = @_;

	if ( ($visible eq "forefront") && (!($print_mode)) ) 
	{

		print " <a href=\"$config_tokens{'url-phonebook-base'}";
		print "$dn\">";
		print "<img src=\"../html/ldap-person.gif \" border=0 align=TEXTTOP>";
		print "</a>";
	}

	return;
}

##########################################
#
#  Print the email icon icon next to the person's name,
#  if that's what we are configured to do, and only if not in
#  print mode  ("print mode": if the page is not being generated
#  in a stripped-down way [sans icons] for printing)
#
##########################################

sub print_email_icon_if_outside_layer
{
	my ($visible, $email) = @_;

	if ( ($visible eq "forefront") &&  ( $email =~ /@/ ) && (!($print_mode)) )
	{
		print " <a href=\"mailto:$email\">";
		print "<img src=\"../html/mail.gif \" border=0 align=TEXTTOP>";
		print "</a>";
	}

	return;
}

##########################################
#
#  Print the AIM icon icon next to the person's name,
#  if that's what we are configured to do, and only if not in
#  print mode  ("print mode": if the page is not being generated
#  in a stripped-down way [sans icons] for printing), and if the person is ONLINE
#
##########################################

sub print_aim_icon_if_outside_layer
{
	my ($visible, $status, $screenname) = @_;

	if ( ($visible eq "forefront") && (!($print_mode)) ) 
	{
		if ( $status eq "discover" )
		{
			if (  ($screenname eq "(none)") || ($screenname eq "OFFLINE") )
			{
				$status = "OFFLINE";
			}
			else
			{
				$status = "ONLINE";
			}
		}

		if ( $status eq "ONLINE" )
		{
			$screenname =~ tr/ /+/;
			print " <a href=\"aim:goim?Screenname=$screenname\">";
			print "<img src=\"../html/aim-online.gif\" border=0 align=TEXTTOP></a>";
		}
	}

	return;
}

##########################################
#
#  Figure out if we are supposed to be putting the locator icon
#  inside the floating layer, if that's what we are configured to do.
#  If not, then return "(none)", which client-side javascript then
#  knows how to react off of (to not display anything).
#
##########################################

sub is_locator_in_layer
{
	my ($visible, $locator) = @_;
	my ($returnvalue) = "(none)";

	if ( $visible eq "layer" ) 
	{
		$returnvalue = $locator;
	}

	return ( $returnvalue );
}

##########################################
#
#  Figure out if we are supposed to be putting the phonebook icon
#  inside the floating layer, if that's what we are configured to do.
#  If not, then return "(none)", which client-side javascript then
#  knows how to react off of (to not display anything).
#
##########################################

sub is_pb_in_layer
{
	my ($visible, $pb) = @_;
	my ($returnvalue) = "(none)";

	if ( $visible eq "layer" ) 
	{
		$returnvalue = $pb;
	}

	return ( $returnvalue );
}

##########################################
#
#  Figure out if we are supposed to be putting the email icon
#  inside the floating layer, if that's what we are configured to do.
#  If not, then return "(none)", which client-side javascript then
#  knows how to react off of (to not display anything).
#
##########################################

sub is_email_in_layer
{
	my ($visible, $email) = @_;
	my ($returnvalue) = "(none)";

	if ( ($visible eq "layer") &&  ( $email =~ /@/ )  )
	{
		$returnvalue = $email;
	}

	return ( $returnvalue );
}

##########################################
#
#  Figure out if we are supposed to be putting the AIM icon
#  inside the floating layer, if that's what we are configured to do.
#  If not, then return "(none)", which client-side javascript then
#  knows how to react off of (to not display anything).
#  knows how to react off of (to not display anything).
#
##########################################

sub is_aimid_in_layer
{
	my ($visible, $status, $screenname) = @_;

	my ($returnvalue) = "(none)";

	if ( $status eq "discover" )
	{
		if (  ($screenname eq "(none)") || ($screenname eq "OFFLINE") )
		{
			$status = "OFFLINE";
		}
		else
		{
			$status = "ONLINE";
		}
	}


	if (  ($visible eq "layer") &&  ($status eq "ONLINE") )
	{
		$screenname =~ tr/ /+/;
		$returnvalue = $screenname;
	}

	return ( $returnvalue );
}

##########################################
#
#  Generic encoder function, used in several places for building
#  correct URL's for the user to click on.
#
##########################################

sub url_encode
{
	my ($tempstr) = @_;

	$tempstr =~ s/([\W])/"%" . uc(sprintf("%2.2x",ord($1)))/eg;

	return($tempstr);
}

##########################################
#   
#  This javascript below is needed for whenever an org chart of any
#  nature is drawn.  It contains the DHTML-related javascript to 
#  dynamically construct and display (and then hide) a given floating
#  layer of information and links for a given employee that is being
#
##########################################

sub print_javascript
{

print "<SCRIPT>


var left = 0;
var top = 0;

var W3C = document.getElementById? true : false;
var NN4 = document.layers? true : false;
var IE4 = document.all? true : false;
var MOZ5 = ((navigator.userAgent.toLowerCase().indexOf(\"mozilla\")==0) && (navigator.userAgent.toLowerCase().charAt(8) >= 5) && (navigator.userAgent.toLowerCase().indexOf(\"compatible\")<0));
var OP = navigator.userAgent.toLowerCase().indexOf(\"opera\")>=0;

var isOver = false;
var timer = null;

function OverLayer() 
{
	clearTimeout(timer); 
	isOver = true; 
}

function OutLayer() 
{
	clearTimeout(timer);
	isOver = false;
	timer = setTimeout(\"hideLayer()\",500);
}


function hideLayer()
{
	if (!isOver)
	{
		if ( W3C )
		{
			document.getElementById(\"test\").style.visibility = \"hidden\";
		}

		if ( NN4 )
		{
			document.layers[\"test\"].visibility = \"hidden\";
		}

		if ( IE4 )
		{
			document.all[\"test\"].style.visibility = \"hidden\";
		}

	}


}


function showLayer(cn,title,mail,dn,locator,aimid)
{
	var finalhtml;
	var num = 0;

	clearTimeout(timer);
	hideLayer();

	finalhtml  =  '<TABLE border=1 CELLPADDING=15 BGCOLOR=\"#CCCCCC\"><TR><TD><TABLE BORDER=0>';
	finalhtml +=  '<TR><TD COLSPAN=2 NOWRAP>$fontstring<B>' + unescape(cn) + '</B></font></TD></TR>';
	finalhtml +=  '<TR><TD COLSPAN=2 NOWRAP>$fontstring' + title + '</font></TD></TR>';
	finalhtml +=  '<TR><TD COLSPAN=2 NOWRAP>';

	if (  (mail == '(none)') && (dn == '(none)') && (locator == '(none)') && (aimid == '(none)')  )
	{
		//  don't draw HR line 
	}
	else
	{
		finalhtml +=  '<HR>';
	}

	finalhtml +=  '</TD></TR>';

	if ( mail != '(none)' )
	{
		finalhtml +=  '<TR><TD align=center><a href=\"mailto:' + mail + '\">';
		finalhtml +=  '<img src=\"../html/mail.gif\" border=0 align=TEXTTOP></a></TD>';
		finalhtml +=  '<TD NOWRAP>$fontstring &nbsp;&nbsp;&nbsp;';
		finalhtml +=  '<a href=\"mailto:' + mail + '\">Email</a></font></TD></TR>';
	}

	if ( dn != '(none)' )
	{
		finalhtml +=  '<TR><TD align=center>';
		finalhtml +=  '<a href=\"$config_tokens{\"url-phonebook-base\"}';
		finalhtml +=  dn + '\"><img src=\"../html/ldap-person.gif\" border=0 align=TEXTTOP></a></TD>';
		finalhtml +=  '<TD NOWRAP>$fontstring &nbsp;&nbsp;&nbsp;';
		finalhtml +=  '<a href=\"$config_tokens{\"url-phonebook-base\"}' + dn + '\">';
		finalhtml +=  'Phonebook</a></font></TD></TR>';
	}

	if ( locator != '(none)' )
	{
		finalhtml +=  '<TR><TD align=center>';
		finalhtml +=  '<a href=\"$config_tokens{\"url-locator-base\"}';
		finalhtml +=  locator + '\"><img src=\"../html/mag.gif\" border=0 align=TEXTTOP></a></TD>';
		finalhtml +=  '<TD NOWRAP>$fontstring &nbsp;&nbsp;&nbsp;';
		finalhtml +=  '<a href=\"$config_tokens{\"url-locator-base\"}' + locator + '\">';
		finalhtml +=  'Locator</a></font></TD></TR>';
	}

	if ( aimid != '(none)' )
	{
		finalhtml +=  '<TR><TD align=center>';
		finalhtml +=  '<a href=\"aim:goim?Screenname=' + aimid + '\">';
		finalhtml +=  '<img src=\"../html/aim-online.gif\" border=0 align=TEXTTOP></a></TD>';
		finalhtml +=  '<TD NOWRAP>$fontstring &nbsp;&nbsp;&nbsp;';
		finalhtml +=  '<a href=\"aim:goim?Screenname=' + aimid + '\">';
		finalhtml +=  'Currently online</a></font></TD></TR>';
	}

	finalhtml +=   '</TABLE></TD></TR></TABLE>';


	if ( W3C )
	{
		document.getElementById(\"test\").innerHTML = finalhtml;

		if (navigator.userAgent.toLowerCase().indexOf('opera')>-1)
		{
			// Opera bug - don't use the units
			document.getElementById(\"test\").style.left = left + 25;
			document.getElementById(\"test\").style.top = top + 5;
		}
		else
		{
			document.getElementById(\"test\").style.left = left + 25 + \"px\";
			document.getElementById(\"test\").style.top = top + 5 + \"px\";
		}

		document.getElementById(\"test\").style.visibility = \"visible\";
	}


	if ( IE4 )
	{
		test.innerHTML = finalhtml;

		document.all[\"test\"].style.pixelLeft = left + 25;
		document.all[\"test\"].style.pixelTop = top + 5;
		document.all[\"test\"].style.visibility = \"visible\";
	}

	
	if ( NN4 )
	{
		document.test.document.write(finalhtml);
		document.test.document.close();

		document.layers[\"test\"].left = left + 25;
		document.layers[\"test\"].top = top + 5;
		document.layers[\"test\"].visibility = \"show\";
	}

}

function setMouseCoordinate(e)
{
	if (MOZ5 || NN4)
	{
		left = e.pageX;
		top = e.pageY;
	}
	else if (IE4 || OP)
	{
		left = document.body.scrollLeft + event.clientX;
		top = document.body.scrollTop + event.clientY;
	}
}


if ( NN4 )
{
	document.captureEvents(Event.MOUSEMOVE);
}
document.onmousemove = setMouseCoordinate;



</SCRIPT>

";

} 

##########################################
#
#	Read the "config.txt" file for admin's desired settings.
#
#
#	See the file itself for details on what each setting
#	represents, and what the possible values are.
#
##########################################

sub read_config_file()
{
my $curdir;
if ($isWindows) {
	$curdir = `cd`; chop($curdir);
} else {
	$curdir = `pwd`; chop($curdir);
}
if (!open (FILE, "../config.txt") )
{
	&output_html_header("no-javascript");
	print "\n\n<BR><BR>Can't open configuration file: $curdir/config.txt\n\n<BR><BR>Error from OS: $!\n\n";
	print "\n</BODY></HTML>";
	exit;
}

%config_tokens = ( 	"ldap-host","none", 
					"ldap-port","none",
					"ldap-search-base","none",
					"ldap-bind-dn","",
					"ldap-bind-pass","",
					"icons-aim-visible","no",
					"icons-email-visible","no",
					"icons-phonebook-visible","no",
					"icons-locator-visible","no",
					"url-phonebook-base", "none",
					"url-locator-base", "none",
					"attrib-job-title", "title",
					"attrib-manager", "manager",
					"attrib-farleft-rdn", "uid",
					"max-levels-drawn", "3",
					"manager-DN-location", "same",
					"min-chars-searchstring", "4",
					"allowed-filter-chars", "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-"
				);

while(<FILE>)
{
	chop;

	foreach $f (keys %config_tokens)
	{
		$config_tokens{$f} = $1 if ($_ =~ /^$f[ \t]+(.+)/);
	}
}

close (FILE);


if ( $config_tokens{"ldap-host"} eq "none" )
{
	&output_html_header("no-javascript");
	print "<BR><BR>The administrator of this application needs to configure an LDAP host to use.<BR><BR>";
	print "\n</BODY></HTML>";
	exit(0);
}
if ( $config_tokens{"ldap-port"} eq "none" )
{
	&output_html_header("no-javascript");
	print "<BR><BR>The administrator of this application needs to configure an LDAP port number to use.<BR><BR>";
	print "\n</BODY></HTML>";
	exit(0);
}
if ( $config_tokens{"ldap-search-base"} eq "none" )
{
	&output_html_header("no-javascript");
	print "<BR><BR>The administrator of this application needs to configure an LDAP search base.<BR><BR>";
	print "\n</BODY></HTML>";
	exit(0);
}
if (  ($config_tokens{"url-phonebook-base"} eq "none") && ( $config_tokens{"icons-phonebook-visible"} ne "disabled")  )
{
	&output_html_header("no-javascript");
	print "<BR><BR>The administrator of this application has configured phonebook icons to be enabled, but has not yet configured a phonebook partial base URL to use for those phonebook icons.<BR><BR>";
	print "\n</BODY></HTML>";
	exit(0);
}
if (  ($config_tokens{"url-locator-base"} eq "none") && ( $config_tokens{"icons-locator-visible"} ne "disabled")  )
{
	&output_html_header("no-javascript");
	print "<BR><BR>The administrator of this application has configured locator icons to be enabled, but has not configured a locator partial base URL to use for those locator icons.<BR><BR>";
	print "\n</BODY></HTML>";
	exit(0);
}
}


##########################################
#
#	Let's read in (and validate) any personal settings
#	that the user has, which they can set from clicking
#	the "Customize" link.
#
##########################################


sub check_myorgchart_settings()
{

	my $query = new CGI;
	my $cookie_in = $query->cookie("MyOrgChart");

	#
	#  if client-side browser cookie was found...
	#
	if ($cookie_in)
	{
		@cookiedata = split (/&/ , $cookie_in);

		foreach $f (@cookiedata)
		{
			if (  $f  =~  /=/    )
			{
				@individ = split (/=/ , $f);
				$cookie_tokens{$individ[0]} = $individ[1];
			}
		}
	}

# =========================================================
#
#  begin  ---> 	Check for MyOrgChart overriding settings
#				(that may override settings the admin set
#				in config.txt)
#
# =========================================================

if (  (defined  $cookie_tokens{"email"}) && ($config_tokens{"icons-email-visible"} ne "disabled")  )
{
	$config_tokens{"icons-email-visible"} = $cookie_tokens{"email"};
}
if ( (defined  $cookie_tokens{"pb"}) && ($config_tokens{"icons-phonebook-visible"} ne "disabled")  )
{
    $config_tokens{"icons-phonebook-visible"} = $cookie_tokens{"pb"};
}
if (   (defined  $cookie_tokens{"maps"}) &&  ( $config_tokens{"icons-locator-visible"} ne "disabled" )  )
{
    $config_tokens{"icons-locator-visible"} = $cookie_tokens{"maps"};
}
if (   (defined  $cookie_tokens{"aim"}) && ($config_tokens{"icons-aim-visible"} ne "disabled")  )
{
    $config_tokens{"icons-aim-visible"} = $cookie_tokens{"aim"};
}
if ( defined  $cookie_tokens{"maxlevels"}    )
{
	if ( $cookie_tokens{"maxlevels"} < $config_tokens{"max-levels-drawn"}  )
	{
		#
		#  Just to make life easier (coding-wise), if the user specified a
		#  a personal preference of having a smaller number of "maxlevels"
		#  (how many levels drawn for any org chart they generate) drawn than
		#  the admin-configured value, let's just set the admin-config'ed value
		#  (just in memory, so just for a few seconds) to the user's value
		#
		$config_tokens{"max-levels-drawn"} = $cookie_tokens{"maxlevels"};
	}
}


# =========================================================
#
#  end    ---> Check for MyOrgChart overriding settings
#
# =========================================================

#  Hold on, one final important step before we leave this function....
#
#  Below (as far as just this Perl CGI file is concerned only) it is a lot less code
#  to just treat "disabled" settings as "no" for the icons, to accomplish the
#  same end result in both cases, of not showing the given icon(s).
#
#  But in the MyOrgChart.cgi, we do care about this distinction, because for the
#  "disable" setting we don't want the user to have the option listed to enable
#  that icon to now be displayed in some way.  This is why we need to have the
#  below code right after the MyOrgChart overrides above, to make sure the
#  below has the final say, for the icon-related settings.
#
if ( $config_tokens{"icons-aim-visible"} eq "disabled" )  { $config_tokens{"icons-aim-visible"} eq "no"; }
if ( $config_tokens{"icons-email-visible"} eq "disabled" )  { $config_tokens{"icons-email-visible"} eq "no"; }
if ( $config_tokens{"icons-phonebook-visible"} eq "disabled" )  { $config_tokens{"icons-phonebook-visible"} eq "no"; }
if ( $config_tokens{"icons-locator-visible"} eq "disabled" )  { $config_tokens{"icons-locator-visible"} = "no"; }

}

##########################################
#
#  See location this function is called from for comments on purpose.
#
##########################################

sub print_single_div_html()
{

print "\n<DIV id=\"test\" onMouseOver=\"OverLayer();\" onMouseOut=\"OutLayer();\" style=\"LEFT:0px;POSITION:absolute;TOP:0px;VISIBILITY:visible;Z-INDEX:0\">";
print "</DIV>";

print "\n\n";

}

##########################################
#
#  See location this function is called from for comments on purpose.
#
##########################################

sub nav4_specific_event_handlers()
{
	print "<script type=\"text/javascript\">\n"; 
	print "\n";
	print "if ( NN4 ) \n";
	print "{\n";
	print "		document.layers['test'].onmouseover=OverLayer; \n";
	print "		document.layers['test'].onmouseout=OutLayer; \n";
	print "}\n";
	print "</script>\n\n";
}

##########################################
#
#  See location this function is called from for comments on purpose.
#
##########################################

sub config_ldap_return_attrib_list()
{

	@return_attribs = ("businesscategory", "cn", "dn", "mail", "$config_tokens{'attrib-manager'}", "objectclass", "ou", "telephonenumber", "$config_tokens{'attrib-job-title'}", "uid");


	$found = 0;
	foreach $f (@return_attribs)
	{
		if (  $f  eq  $config_tokens{'attrib-farleft-rdn'}   )
		{
			$found = 1;
		}
	}
	if ( $found == 0 )
	{
		#
		#  If the RDN attribute name defined in config.txt is not already
		#  listed in the above @return_attribs array, then we need to add
		#  it to the array, so that we get the value back from searches.
		#
		push @return_attribs, $config_tokens{'attrib-farleft-rdn'};
	}



	#
	#  It is really expensive currently, per design of the AIM Presence plugin
	#  in DS 6.0x, to ask LDAP for AIM status, so let's only request this for each
	#  and every user in the org chart if we absolutely have to (per MyOrgChart 
	#  preferences having AIM icons turned on)
	#
	if ( $config_tokens{"icons-aim-visible"} ne "no" )
	{
		push @return_attribs, "nsAIMStatusText";
		push @return_attribs, "nsaimid";
	}
}

##########################################
#
#  See location this function is called from for comments on purpose.
#
##########################################

sub search_for_enduser_query()
{
	#
	#  Check that filter contains only allowed characters by comparing to
	#  allowed-filter-chars in config.txt.

	$allowedlist = $config_tokens{"allowed-filter-chars"};
	for($i=0; $i < length($uid); $i++) {
		if(substr($uid,$i,1) !~ /[$allowedlist]/) {
			&output_html_header("no-javascript");
			print "<BR><BR>\"";
			print substr($uid,$i,1) . "\" is not allowed in search filters.<BR><BR>";
			print "Please modify your search and try again.<BR>";
			print "\n</BODY></HTML>";
			exit (0);
		}
	}

	#
	# Get the full user entry of the uid entered by the end-user
	#
	# ...so if end user enters "steveh", then the below $search = "uid=steveh"

	$search = "$config_tokens{'attrib-farleft-rdn'}=" . $uid;

	$conn = new Mozilla::LDAP::Conn($config_tokens{"ldap-host"}, $config_tokens{"ldap-port"}, $config_tokens{"ldap-bind-dn"}, $config_tokens{"ldap-bind-pass"});
	die	"Couldn't connect to LDAP server $config_tokens{\"ldap-host\"}" unless $conn;
	$entry = $conn->search($config_tokens{"ldap-search-base"}, "subtree", $search, 0 , @return_attribs);

	#
	#  If no entries found for the above exact UID match, before we
	#  broaden the search filter to help the user out, let's first check how
	#  many characters they submitted as compared to the "min-chars-searchstring"
	#  setting in config.txt, to avoid potential heavy loads on the LDAP server.

	if (! $entry)
	{ 
		if (  length($uid) < $config_tokens{"min-chars-searchstring"}  )
		{
			&output_html_header("no-javascript");
			print "<BR><BR>I did not find an exact userid match for what you entered.<BR><BR>";
			print "Please enter at least $config_tokens{\"min-chars-searchstring\"} characters to broaden the search more.<BR>";
			print "\n</BODY></HTML>";
			exit (0);
		}
	}

	# if (no entries found), let's try broading the search, to give them some
	# search results to pick from  (I guess they did not enter an exact uid)
	#
	if (! $entry)
	{
		$uid =~ tr/ /*/;

		$search = "(|(cn=*$uid*)(mail=*$uid*))";

		$conn = new Mozilla::LDAP::Conn($config_tokens{"ldap-host"}, $config_tokens{"ldap-port"}, $config_tokens{"ldap-bind-dn"}, $config_tokens{"ldap-bind-pass"});
		die	"Couldn't connect to LDAP server $config_tokens{\"ldap-host\"}" unless $conn;
		$entry = $conn->search($config_tokens{"ldap-search-base"}, "subtree", $search, 0, @return_attribs);

		$anothertempnum = 0;
		while ($entry)
		{

#print "Entry Count: ".Mozilla::LDAP::API::ldap_count_entries($conn->getLD(), $conn->getRes())."\n";
			$results[$anothertempnum][0] = "<a href=\"org?${contextParamString}" . $config_tokens{'attrib-farleft-rdn'} . "=" . url_encode( $entry->{$config_tokens{'attrib-farleft-rdn'}}[0] ) . "\">";

			$results[$anothertempnum][1] = $entry->{cn}[0];

			if ( $entry->{telephonenumber}[0] ne "" )
			{
				$results[$anothertempnum][2] = $entry->{telephonenumber}[0];
			}
			else
			{
				$results[$anothertempnum][2] = "&nbsp";
			}
	
			if ( $entry->{mail}[0] ne "" )
			{
				if ( $entry->{mail}[0] =~ /@/ )
				{
					$results[$anothertempnum][3] = "<a href=\"mailto:$entry->{mail}[0]\">$entry->{mail}[0]</a>";
				}
			}
			else
			{
				$results[$anothertempnum][3] = "&nbsp";
			}

			if ( $entry->{businesscategory}[0] ne "" )
			{
				$results[$anothertempnum][4] = $entry->{businesscategory}[0];
			}
			else
			{
				$results[$anothertempnum][4] = "&nbsp";
			}


			if ( $entry->{ou}[0] ne "" )
			{
				$results[$anothertempnum][5] = $entry->{ou}[0];
			}
			else
			{
				$results[$anothertempnum][5] = "&nbsp";
			}

			$lastentry = $entry;
			$entry = $conn->nextEntry();
			++$anothertempnum;
		}

		if ( $anothertempnum == 0 )
		{
			&output_html_header("no-javascript");
			print "<BR><BR>No search results found!<BR>";
		}
		elsif ( $anothertempnum == 1)
		{
			#
			#  If we only have one match, let's display the org chart
			#  for that person, as opposed to just showing a single search result.
			#
			$entry = $lastentry;
			$uid = $entry->{$config_tokens{'attrib-farleft-rdn'}}[0];
		}
		else
		{

#---------------------------------------------
#
#  Let's print the LDAP entries found, that match the string entered.
#
#---------------------------------------------

&output_html_header("no-javascript");
print "

<br>
<table cellspacing=\"-1\" cellpadding=\"2\" border=\"0\" width=\"100%\">
<tr>
<td align=\"left\" class=\"pageHeader\">Search Results: $anothertempnum users</td>
<td align=\"right\" class=\"searchHelp\"><img src=\"../html/orgicon.gif\" width=\"16\" height=\"14\" border=\"0\"> = view organization chart</td>
</tr>
<tr><td>&nbsp;</td></tr>
</table>
<table bgcolor=\"#FFFFFF\" cellspacing=\"-1\" cellpadding=\"3\" border=\"1\" width=\"100%\">
<tr>
    <th align=\"left\" class=\"resultsHeader\">Name</th>
    <th align=\"left\" class=\"resultsHeader\">Phone</th>
    <th align=\"left\" class=\"resultsHeader\">EMail</th>
    <th align=\"left\" class=\"resultsHeader\">Group</th>
    <th align=\"left\" class=\"resultsHeader\">Business Category</th>
</tr>
";


for ( $num = 0 ; $num < $anothertempnum ; $num++ )
{

print "
<tr>
    <td align=\"left\" nowrap>$results[$num][0]<img src=\"../html/orgicon.gif\" width=\"16\" height=\"14\" border=\"0\" alt=\"View Organization Chart\"></a>&nbsp;&nbsp;$results[$num][1]</td>
    <td align=\"left\" nowrap>$results[$num][2]</td>
    <td align=\"left\">$results[$num][3]</td>
    <td align=\"left\">$results[$num][4]</td>
    <td align=\"left\">$results[$num][5]</td>
</tr>

";

}
print "</table>";
#---------------------------------------------

		}

		#  if there was only one search result (which we purposely
		#  did not print to the browser above), then let's draw the
		#  org chart for that single search result
		#
		#  if zero or more than one search result, let's end things
		#  here, as there isn't anything else to do, code-wise.
		if ( $anothertempnum !=  1 )
		{
			print "\n</BODY></HTML>";
			exit(0);
		}
	}
}

##########################################
#
#  See location this function is called from for comments on purpose.
#
##########################################

sub print_topmost_box() 
{
	if ( !($print_mode) )
	{
		# let's print the "Prepare for Printing" link if not already doing so
		#
		print "<font face=\"verdana, Arial, Helvetica, sans-serif\" style=\"font-size: 14px\">";

		print "<a href=\"org?${contextParamString}" . $config_tokens{'attrib-farleft-rdn'}  . "=" . url_encode($uid) .  "&print=yes\" target=\"org_print_window\">Prepare this page for printing</A><BR>";
		print "</font>";
	}

	print "<CENTER><table border=0><tr><td NOWRAP>";

	print "<center>";

	#
	# special exception:  seems like when hardcopy printing org chart from IE browser,
	#					  the boxes that people are in are not printed, so by making
	#					  border=1, at least you can see the box on the hardcopy version
	#
	if (  ( "$browser_is_msie" ) &&  ( $print_mode ) )
	{
		print "<table border=1  CELLSPACING=1 > \n";
	}
	else
	{
		print "<table border=0  CELLSPACING=1 > \n";
	}

	print "<tr>\n";
	print "<td ALIGN=CENTER BGCOLOR=\"#000000\" NOWRAP>\n"; 
	print "<table border=0 CELLSPACING=0 CELLPADDING=6 >\n"; 
	print "<tr>\n";
	print "<td BGCOLOR=\"#CCCCCC\" ALIGN=CENTER VALIGN=CENTER NOWRAP>\n"; 
	print "<table cellspacing=0 border=0><tr><td NOWRAP>";
	print "$fontstring<center>";

	$tempstr = url_encode($entry->{dn});
	$tempstr2  = url_encode($entry->{cn}[0]);

	$aimid = is_aimid_in_layer ( $config_tokens{"icons-aim-visible"} , $entry->{nsAIMStatusText}[0] , $entry->{nsaimid}[0] );
	$emailstr = is_email_in_layer ( $config_tokens{"icons-email-visible"}, $entry->{mail}[0] );
	$pbstr = is_pb_in_layer ( $config_tokens{"icons-phonebook-visible"}, $tempstr );
	$locatorstr = is_locator_in_layer ( $config_tokens{"icons-locator-visible"}, $tempstr2 );

	if ( !($print_mode) )
	{
		print "\n\n <A HREF='javascript:return false;' target=_top onMouseOver=\"showLayer('$tempstr2','$entry->{$config_tokens{\"attrib-job-title\"}}[0]','$emailstr','$pbstr','$locatorstr','$aimid');\" onMouseOut=\"OutLayer();\">";
		print "<img src=\"../html/arrow.gif\" border=0 align=TEXTTOP>";
		print "</A> \n";
	}

	print "<B>$entry->{cn}[0]</B>";

	print_aim_icon_if_outside_layer( $config_tokens{"icons-aim-visible"}, $entry->{nsAIMStatusText}[0], $entry->{nsaimid}[0] );
	print_email_icon_if_outside_layer( $config_tokens{"icons-email-visible"}, $entry->{mail}[0] );
	print_pb_icon_if_outside_layer( $config_tokens{"icons-phonebook-visible"}, $tempstr );
	print_locator_icon_if_outside_layer( $config_tokens{"icons-locator-visible"}, $tempstr2 );

	print "<BR>\n";
	print "$entry->{$config_tokens{\"attrib-job-title\"}}[0]<BR>\n</font>";

	#
	# Get the full name of the manager of the uid entered by the end-user
	#

	@manager= split (/,/ , $entry->{$config_tokens{'attrib-manager'}}[0]);
	@splitagain = split (/=/, @manager[0] );
	$manager = @splitagain[1];
	$managerSearch = $config_tokens{'attrib-farleft-rdn'} . "=" . $manager;
	$managerEntry = $conn->search($config_tokens{"ldap-search-base"},"subtree", $managerSearch, 0, @return_attribs);

	print "$fontstring";
	print "Manager: ";

	if ($managerEntry)
	{
		$tempstr = url_encode($managerEntry->{dn});
		$tempstr2  = url_encode($managerEntry->{cn}[0]);
		$managertitle = $managerEntry->{$config_tokens{"attrib-job-title"}}[0];

		$aimid = is_aimid_in_layer ( $config_tokens{"icons-aim-visible"} , $managerEntry->{nsAIMStatusText}[0] , $managerEntry->{nsaimid}[0] );
		$emailstr = is_email_in_layer ( $config_tokens{"icons-email-visible"}, $managerEntry->{mail}[0] );
		$pbstr = is_pb_in_layer ( $config_tokens{"icons-phonebook-visible"}, $tempstr );
		$locatorstr = is_locator_in_layer ( $config_tokens{"icons-locator-visible"}, $tempstr2 );

		if ( !($print_mode) )
		{
			print "\n\n <A HREF='javascript:return false;' target=_top onMouseOver=\"showLayer('$tempstr2','$managertitle','$emailstr','$pbstr','$locatorstr','$aimid');\" onMouseOut=\"OutLayer();\">";
			print "<img src=\"../html/arrow.gif\" border=0 align=TEXTTOP>";
			print "</A> \n";
		}

		print $managerEntry->{cn}[0];

		if ( !($print_mode) )
		{
			print " <A HREF=org?${contextParamString}" . $config_tokens{'attrib-farleft-rdn'} . "=" . url_encode($manager) . "><img src=\"../html/orgicon.gif\" border=0 height=15 width=17 align=TEXTTOP></a>";
		}

		print_aim_icon_if_outside_layer( $config_tokens{"icons-aim-visible"}, $managerEntry->{nsAIMStatusText}[0], $managerEntry->{nsaimid}[0] );
		print_email_icon_if_outside_layer( $config_tokens{"icons-email-visible"}, $managerEntry->{mail}[0] );
		print_pb_icon_if_outside_layer( $config_tokens{"icons-phonebook-visible"}, $tempstr );
		print_locator_icon_if_outside_layer( $config_tokens{"icons-locator-visible"}, $tempstr2 );

		print "</font>";
	}

	if (!$managerEntry)
	{
		print "<B>(no manager listed)</B>";
	}

	print"</center></td> </tr> </table> </td> </tr> </table> </td> </tr> </table> <BR>";
}

##########################################
#
#  See location this function is called from for comments on purpose.
#
##########################################

sub print_toplevel_tree_branch()
{
	#
	#  Are there any leaf entries directly under top level person?
	#
	#  If yes, then don't put them in their own boxes, but instead
	#  list them in a tree branch underneath the top level person's box.
	#

	print "\n<center><table border=0><tr><td NOWRAP>";
	$anothertempnum = @sortedPeople;
	for ( $tempnum = 0 ; $tempnum < $anothertempnum ; $tempnum++ )
	{
		$f = $sortedPeople[$tempnum][0];
		$count = ($f =~ tr/\///);

		if ( $count == 2 )
		{
			@tempdata = split(/\//, $f );
			$entry = $tempdata[1];

			#
			# if we are at the end of the array, we want to avoid
			# the else block below, because we don't want to add one
			# more blank element to the array with the "+1", or that
			# will make our @sortedPeople value be a fake one element higher
			#
			if ( $tempnum == $anothertempnum-1 )
			{
				$nextentry = "";
			}
			else
			{
				$info = $sortedPeople[$tempnum+1][0];
				@tempdata = split(/\//, $info);
				$nextentry = @tempdata[1];
			}

			if ( "$entry" ne "$nextentry" )
			{
				print "$fontstring";
				print "\n<img SRC=\"../html/new-branch-first.gif\" align=TEXTTOP>";

				$aimid = is_aimid_in_layer ( $config_tokens{"icons-aim-visible"} , "discover" , $sortedPeople[$tempnum][5] );
				$emailstr = is_email_in_layer ( $config_tokens{"icons-email-visible"}, $sortedPeople[$tempnum][3] );
				$pbstr = is_pb_in_layer ( $config_tokens{"icons-phonebook-visible"}, $sortedPeople[$tempnum][2] ); 
				$locatorstr = is_locator_in_layer ( $config_tokens{"icons-locator-visible"}, $sortedPeople[$tempnum][6] );

				if ( !($print_mode) )
				{
					print "\n\n <A HREF='javascript:return false;' target=_top onMouseOver=\"showLayer('$sortedPeople[$tempnum][6]','$sortedPeople[$tempnum][4]','$emailstr','$pbstr','$locatorstr','$aimid');\" onMouseOut=\"OutLayer();\">";
					print "<img src=\"../html/arrow.gif\" border=0 align=TEXTTOP>";
					print "</A> \n";
				}

				print "\n $entry ";

				if (  $sortedPeople[$tempnum][7] =~ /nonleaf/  )
				{
					#
					#  If we are only supposed to draw one level for the org chart,
					#  and there are nonleaf entries, display org chart icon next
					#  to the person's name, to indicate they have people below them.
					#
					print "<a href=org?${contextParamString}" . $config_tokens{'attrib-farleft-rdn'} . "=" . url_encode($sortedPeople[$tempnum][1]) . ">";
					print "<img src=\"../html/orgicon.gif\" border=0 height=15 width=17 align=TEXTTOP></a>";
				}

				print_aim_icon_if_outside_layer( $config_tokens{"icons-aim-visible"}, "discover", $sortedPeople[$tempnum][5] );
				print_email_icon_if_outside_layer( $config_tokens{"icons-email-visible"}, $sortedPeople[$tempnum][3] );
				print_pb_icon_if_outside_layer( $config_tokens{"icons-phonebook-visible"}, $sortedPeople[$tempnum][2] );
				print_locator_icon_if_outside_layer( $config_tokens{"icons-locator-visible"}, $sortedPeople[$tempnum][6] );

				print "</font><BR>\n";
				$sortedPeople[$tempnum][0] = "--skip--"; 
			}
		}
	}

	print "\n</td></tr></table></center>\n\n";
}

##########################################
#
#  See location this function is called from for comments on purpose.
#
##########################################

sub pre_markup_remaining_branches()
{

	#
	#  Below let's scan the org chart entries and record
	#  some notes next to some of the entries (to be used later on
	#  in drawing the final display) on which tree branch pieces 
	#  need to be rounded, which need to be draw in a special way, etc.
	#
	#  cc1 below ("corner case 1") is the condition where the last leaf
	#  user entry under a given boxed user entry (manager entry) is at
	#  the farthest left justification/indent level.
	#
	#  cc2 is the trickier condition, and is anything other than cc1 above.
	#  Meaning there are user entries at the end of the branch that are
	#  indented one or more times to the right, so we need to draw
	#  blank space in the areas where we indent (we cannot have lines there)
	#
	#  Below, we iterate throught the org chart data in reverse order
	#  (reverse order to make things easiest to program, for the marking up)
	#  and we save details in the same org chart array, to help the org chart
	#  drawing code later on know what to draw when there is indenting
	#  (whether to draw lines or no lines, or combination of the two)
	#

	$inside_cc1 = "no"; 
	$inside_cc2 = "no"; 
	$deeper_inside_cc2 = "no";
	$last_count = 0;

	$last_manager = "";

	$anothertempnum = @sortedPeople;
	for ( $tempnum = @sortedPeople - 1 ; $tempnum >= 0  ; $tempnum-- )
	{
		if ( "$sortedPeople[$tempnum][0]" ne "--skip--" )
		{
			$f = $sortedPeople[$tempnum][0];
			$count = ($f =~ tr/\///) - 1;

			@tempdata = split(/\//, $sortedPeople[$tempnum][0] );
			$specvalue = $tempdata[1];

			if (  ( $count == 2 ) && ( "$last_manager" ne "$specvalue" )  )
			{
				$sortedPeople[$tempnum][8]="cc1";
				$inside_cc1 = "yes";
				$inside_cc2 = "no";
				$deeper_inside_cc2 = "no";
			}
			else
			{
				if (  ( "$inside_cc1" eq "yes"  )  &&  (  "$last_manager" ne "$specvalue" ) )
				{
					$inside_cc1 = "no";
				}

			}

			if (  "$inside_cc1" eq "yes" )
			{
				if (  ( $count >= 3 )  &&  ( $last_count != $count )  )
				{
					$sortedPeople[$tempnum][8]="cc1";
				}
			}


			if (  ( $count > 2 )  &&  ( "$last_manager" ne "$specvalue" )  )
			{
				$inside_cc2 = "yes";
				$deeper_inside_cc2 = "no";
				$inside_cc1 = "no";
			}

			if (  "$inside_cc2" eq "yes" )
			{
				if (  ($count == 2 )  &&  ( "$deeper_inside_cc2" eq "no" ) )
				{
					$deeper_inside_cc2 = "yes";
					$tempstr = "rounded";
				}
				elsif (  ( $count >= 3 )  &&  ( $last_count != $count )  )
				{
					$tempstr = "rounded";
				}
				else
				{
					$tempstr = "tee";
				}


				if ( "$deeper_inside_cc2" eq "no" )
				{
					$sortedPeople[$tempnum][8]="cc2-bottom-$tempstr";
				}
				else
				{
					$sortedPeople[$tempnum][8]="cc2-upper-$tempstr";
				}

			}

			$last_count = $count;
			$last_manager = "$specvalue"; 
		}
	}
}

##########################################
#
#  See location this function is called from for comments on purpose.
#
##########################################

sub draw_remaining_branches()
{

	print "\n"; 
	print "<center><table border=0 cellpadding=10><tr VALIGN=top>";

	$current_indent = 1;
	$one_time_td = 0;

	$maxitems = @sortedPeople;

	for ( $tempnum = 0 ; $tempnum < $maxitems ; $tempnum++ )
	{
		if ( "$sortedPeople[$tempnum][0]" ne "--skip--" )
		{
			$count = ($sortedPeople[$tempnum][0] =~ tr/\///) - 1;

			while ( $count > $current_indent )
			{
				$current_indent = $current_indent + 1;
			}

			while ( $count < $current_indent )
			{
				$current_indent = $current_indent - 1;
			}

			@tempdata = split(/\//, $sortedPeople[$tempnum][0] );

			if ( $current_indent == 1 )
			{
				if ( $one_time_td == 0 )
				{
					print "<TD NOWRAP>\n"; 
					$one_time_td = 1;
				}
				else
				{
					print "</TD><TD NOWRAP>\n";
				}
			}


			if ( $current_indent == 1 )
			{
				# special exception:  seems like when printing org chart from IE browser,
				#                     the boxes that people are in are not printed, so by making
				#                     border=1, at least you can see the box on the hardcopy version
				#
				if (  ( "$browser_is_msie" ) &&  ( $print_mode ) )           
				{
					print "<table border=1  CELLSPACING=1 > \n"; 
				}
				else
				{
					print "<table border=0  CELLSPACING=1 > \n";
				}

				print "<tr>\n";
				print "<td ALIGN=CENTER BGCOLOR=\"#000000\">\n"; 
				print "<table border=0 CELLSPACING=0 CELLPADDING=6 >\n"; 
				print "<tr>\n";
				print "<td BGCOLOR=\"#CCCCCC\" ALIGN=CENTER VALIGN=CENTER>\n"; 
				print "<table cellspacing=0 border=0><tr><td NOWRAP><CENTER>";

				#
				#  See comment just a few lines below about being careful on 
				#  not moving this font tag past the IMG SRC tags
				#
				print "$fontstring";
			}
			else
			{
				# 
				# Be careful on moving this font tag after the below IMG SRC
				# tags for drawing branch pieces ---> to have the branch pieces 
				# stay connected on Netscape 6.x, the open FONT tag needs to be
				# BEFORE the IMG SRC tags for the branch pieces....
				#
				print "$fontstring";

				if ( $sortedPeople[$tempnum][8] =~ /^cc2-bottom/ )
				{
					for ( $anothertempnum = 0 ; $anothertempnum < $current_indent - 2 ; $anothertempnum++ )
					{
						print "<img SRC=\"../html/new-branch-blank.gif\" align=TEXTTOP>";
					}
				}
				else
				{
					for ( $anothertempnum = 0 ; $anothertempnum < $current_indent - 2 ; $anothertempnum++ )
					{
						print "<img SRC=\"../html/new-branch-straight.gif\" align=TEXTTOP>";
					}
				}


				if ( ("$sortedPeople[$tempnum][8]" eq "cc1")  ||  ( $sortedPeople[$tempnum][8] =~ /rounded/  )  )
				{
					print "<img SRC=\"../html/branch-cc1.gif\" align=TEXTTOP>";
				}
				else
				{
					print "<img SRC=\"../html/new-branch-first.gif\" align=TEXTTOP>";
				}
			}


			$aimid = is_aimid_in_layer ( $config_tokens{"icons-aim-visible"} , "discover" , $sortedPeople[$tempnum][5] );
			$emailstr = is_email_in_layer ( $config_tokens{"icons-email-visible"}, $sortedPeople[$tempnum][3] );
			$pbstr = is_pb_in_layer ( $config_tokens{"icons-phonebook-visible"}, $sortedPeople[$tempnum][2] ); 
			$locatorstr = is_locator_in_layer ( $config_tokens{"icons-locator-visible"}, $sortedPeople[$tempnum][6] );

			if ( !($print_mode) )
			{
				print "\n\n <A HREF='javascript:return false;' target=_top onMouseOver=\"showLayer('$sortedPeople[$tempnum][6]','$sortedPeople[$tempnum][4]','$emailstr','$pbstr','$locatorstr','$aimid');\" onMouseOut=\"OutLayer();\">";
				print "<img src=\"../html/arrow.gif\" border=0 align=TEXTTOP>";
				print "</A> \n";
			}

			print "$tempdata[@tempdata-1] \n";

			#
			#  If they are a nonleaf entry based on the next person being below them, or if they
			#  are a nonleaf person based on "nonleaf" value which happens when max depth is exceeded
			#  such that all people below them were chopped off (were on the next level that was chopped
			#  off, hence why we needed to previously record "nonleaf" before the chop happened) 
			#
			#  then print the org chart icon
			#
			if (  ( $sortedPeople[$tempnum+1][0] =~ /$tempdata[@tempdata-1]/  ) ||  ( $sortedPeople[$tempnum][7] =~ /nonleaf/ )  )
			{
				if ( ($print_mode) &&  ($current_indent == 1 )  )
				{
					# special exception #1 of 2: 
					# if we are in "prepare this page for printing" mode, and drawing a user in
					# a box, then let's not print the org icon next to their name ---> not needed
					# in the hardcopy printout (not helpful)
				}
				else
				{
					if (  ($print_mode)  &&  ( $sortedPeople[$tempnum+1][0] =~ /$tempdata[@tempdata-1]/  ) )
					{
						#  special exception #2 of 2:  if we are preparing this org chart for printing,
						#  and if the org icon we are about to draw is for a group of people that are
						#  already being printed on this same org chart under that person, there is 
						#  no point in hardcopy printing this icon next to the person's name
						#
						#  but in the "else" block below, we do want to print the icon next to their name
						#  (both for print and non-print org charts) because it signifies people underneath
						#  that person when we CANNOT/WON'T see those people listed under that person
					}
					else
					{
						if  (  !(     $sortedPeople[$tempnum+1][0] =~ /$tempdata[@tempdata-1]\/$/    )    )
						{
							print "<a href=org?${contextParamString}" . $config_tokens{'attrib-farleft-rdn'} . "=" . url_encode($sortedPeople[$tempnum][1]) . ">";
							print "<img src=\"../html/orgicon.gif\" border=0 height=15 width=17 align=TEXTTOP></a>";
						}
					}
				}
			}


			print_aim_icon_if_outside_layer( $config_tokens{"icons-aim-visible"}, "discover", $sortedPeople[$tempnum][5] );
			print_email_icon_if_outside_layer( $config_tokens{"icons-email-visible"}, $sortedPeople[$tempnum][3] );
			print_pb_icon_if_outside_layer( $config_tokens{"icons-phonebook-visible"}, $sortedPeople[$tempnum][2] );
			print_locator_icon_if_outside_layer( $config_tokens{"icons-locator-visible"}, $sortedPeople[$tempnum][6] );

			#
			#  if the person's name is being printed within a box,
			#  then also print their title below their name
			#
			if ( $current_indent == 1 )
			{

				print "<BR>$sortedPeople[$tempnum][4]";
			}

			print "</font>";

			if ( $current_indent == 1 )
			{
				print" </CENTER></td></tr> </table> </td> </tr> </table> </td> </tr> </table> ";
			}

			print "<BR>";

		}
	}

}

##########################################
#
# If they exceeded max depth allowed, let's still figure out
# which people are managers of some type and make sure we
# still put an org chart icon next to their name, so that the
# user can tell that there is extra org chart branches that were
# chopped off.
#
# We do this by over-filling the array of the org chart structure,
# and then make sure that when we chop off the extra level below,
# we record for the manager-types that have now chopped-off people
# that they are a non-leaf item (which needs an org chart icon next
# to their name
#
##########################################

sub detect_nonleaf_depth_exceeded()
{

	if ( $incomplete == 1  )
	{
		$indelete = 0;
		$anothertempnum = @sortedPeople;
		for ( $tempnum = $anothertempnum-1 ; $tempnum >= 0 ; $tempnum-- )  
		{  
			#  number of levels in current array element
			#
			$num = ($sortedPeople[$tempnum][0] =~ tr/\//\//) - 1;

			if (  $num > $config_tokens{"max-levels-drawn"}  )
			{
				splice(@sortedPeople,$tempnum,1);
				$indelete = 1;
				#  $total is the total number of people we read in from LDAP
				#  as reporting to the person entered.  But now that we are
				#  chopping people off that exceed the max depth, we better
				#  adjust the $total accordingly as well, or else the 
				#  "Total Reports: XXX" summary info at bottom of org chart
				#  will be too high/inaccurate.
				#
				--$total;
			}
			else
			{
				if ( $indelete == 1 )
				{
					$indelete = 0;
					$sortedPeople[$tempnum][7] = "nonleaf";
				}
				else
				{
					$sortedPeople[$tempnum][7] = "leaf";
				}
			}
		}
	}

}

##########################################
#
#  See location this function is called from for comments on purpose.
#
##########################################

sub output_html_header()
{
	my ($js_output) = @_;

	print "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 Transitional//EN\">\n";
	print "<HTML>\n";
	print "<HEAD>\n";
	print "     <title>Directory Server Org Chart</title>\n";

	if ( $js_output ne "with-javascript" )
	{
		print "     <LINK REL=stylesheet TYPE=\"text/css\" HREF=\"../html/styles.css\">\n";
	}
	if ( $js_output eq "with-javascript" )
	{
		&print_javascript();
	}

	print "</HEAD>\n";
	print "<BODY BGCOLOR=\"#FFFFFF\">\n";

}

#===  end   ===================================================================


