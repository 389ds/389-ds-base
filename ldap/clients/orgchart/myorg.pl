#!../../../bin/slapd/admin/bin/perl
#
#set ts=4

$|=1;
print "Content-type: text/html;charset=UTF-8\n\n";
#print "Content-type: text/html\n\n";

#
#  Read config.txt settings for MyOrgChart-specific items
#
&read_config_file();

#-------------------------------------
print "
<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 Transitional//EN\">
<html>
<head>
<title>Customize: Netscape Directory Server Org Chart</title>
<LINK REL=stylesheet TYPE=\"text/css\" HREF=\"../html/styles.css\">
";
#-------------------------------------

&print_javascript();

print "</head>";

&print_body(); 

print "</html>";

exit(0);


#==============================================================================

sub read_config_file()
{

	if (!open (FILE, "../config.txt") )
	{
		print "\n\n<BR><BR>Can't open configuration file: config.txt\n\n<BR><BR>Error from OS: $!\n\n";
		exit;
	}

	#
	#  let's set some default values, so in case a setting
	#  does not exist both in the config.txt file, as well
	#  as does not exist via a user's MyOrgChart cookie,
	#  we at least have some type of valid value present.
	#
	%config_tokens = ( 
			"icons-aim-visible","disabled",
			"icons-email-visible","disabled",
			"icons-phonebook-visible","disabled",
			"icons-locator-visible","disabled",
			"max-levels-drawn", "3",
	);

	#
	#  read in the config.txt file
	#
	while(<FILE>)
	{
		chop;

		foreach $f (keys %config_tokens)
		{
			$config_tokens{$f} = $1 if ($_ =~ /^$f[ \t]+(.+)/);
		}
	}
	close (FILE);

	#
	#  check the "max-levels-drawn" setting for numeric, and to
	#  make sure it is a number greater than zero.
	#
	#  If a bad setting, let's set it to 3 so that at least it 
	#  is set to a valid number, but then a user's MyOrgChart
	#  preferences can override it (if their setting is 1, 2,
	#  or 3 only).
	#

	# check for non-numeric first

	$temp = $config_tokens{"max-levels-drawn"};
	$temp =~ s/[\d]//g;

	if (  length($temp) != 0 )
	{
		#  a non-numeric setting
		$config_tokens{"max-levels-drawn"} = 3;
	}
	else
	{
		# a numeric setting, but:  check for less than value of 1
		if ( $config_tokens{"max-levels-drawn"} < 1 )
		{
			$config_tokens{"max-levels-drawn"} = 3;
		}
	}


	#
	#  if every icon has been disabled, set a state so that later on
	#  we don't draw the header and the footer text for the icons.
	#  
	if (  ($config_tokens{"icons-email-visible"} eq "disabled") && ($config_tokens{"icons-phonebook-visible"} eq "disabled") && ($config_tokens{"icons-aim-visible"} eq "disabled")  && ($config_tokens{"icons-locator-visible"} eq "disabled") )
	{
		$all_icons_disabled = "yes";
	}
	else
	{
		$all_icons_disabled = "no";
	}
}

#==============================================================================

sub print_body()
{

print "

<body bgcolor=\"#FFFFFF\" leftmargin=0 topmargin=0 marginwidth=0 marginheight=0 onLoad=\"initValues()\">
<FORM name=\"customize\">


<table width=\"500\" border=\"0\" cellpadding=\"0\" cellspacing=\"0\" align=\"center\">
	<tr>
	<td height=\"20\">&nbsp;</td></tr>
</table>

<table width=\"500\" border=\"1\" cellpadding=\"0\" cellspacing=\"0\" align=\"center\">
<tr>
    <td width=\"500\" height=\"22\" valign=\"top\" bgcolor=\"#cccccc\" class=\"pageHeader\">Customize View</td>
</tr>

<tr>
    <td height=\"236\" valign=\"top\">
	  <table width=\"100%\" border=\"0\" cellpadding=\"2\" cellspacing=\"0\">
	    <tr height=\"7\"></tr>
";


#
#  If all icons are "disabled" by the admin, we better not display the
#  window dressing (header and footer) text that normally surrounds the
#  icon options.  This is the header.
#
if ( "$all_icons_disabled" eq "no" )
{
	print "

        <tr>
          <td width=\"28\" height=\"21\" valign=\"top\">&nbsp;</td>
          <td valign=\"top\" colspan=\"4\" class=\"prefsPageHead\">Icon Settings</td>
        </tr>
		<tr height =\"7\"></tr>
        <tr>
		  <td width=\"35\" height=\"21\"></td>
          <td width=\"35\" valign=\"top\" class=\"prefsPageData\">Icon:</td>
          <td width=\"25\"></td>
          <td width=\"105\" valign=\"top\" class=\"prefsPageData\">Description:</td>
          <td width=\"21\"></td>
          <td width=\"205\" valign=\"top\" class=\"prefsPageData\">Location:</td>
		</tr>
	";
}

#
# don't draw the email option if admin has disabled it !
#
if ( $config_tokens{"icons-email-visible"} ne "disabled" )
{

	print "

		<tr height=\"6\"></tr>
		  <tr>
          <td width=\"33\" height=\"21\"></td>
          <td width=\"20\" valign=\"top\" class=\"prefsPageData\">&nbsp;<img src=\"../html/mail.gif\" alt=\"\" width=\"14\" height=\"16\" border=\"0\"></td>
          <td width=\"25\"></td>
          <td width=\"105\" valign=\"top\" class=\"prefsPageData\">EMail</td>
          <td width=\"21\"></td>
          <td width=\"205\" valign=\"center\" class=\"prefsPageData\"><select NAME=\"email\">
";

# --------------------------------------------

$selected1 = $selected2 = $selected3 = "";
if ($config_tokens{"icons-email-visible"} eq "no")		{ $selected1 = " SELECTED"; }
if ($config_tokens{"icons-email-visible"} eq "forefront")	{ $selected2 = " SELECTED"; }
if ($config_tokens{"icons-email-visible"} eq "layer")		{ $selected3 = " SELECTED"; }

# --------------------------------------------

print "
			<option value=\"no\"$selected1>Never display this icon</option>
  			<option value=\"forefront\"$selected2>Next to name</option>
 			<option value=\"layer\"$selected3>In floating layer</option>
			</select></td>

   		  </tr>
	";
}

#
# don't draw the phonebook option if admin has disabled it !
#
if ( $config_tokens{"icons-phonebook-visible"} ne "disabled" )
{
	print "
		<tr height=\"6\"></tr>
		  <tr>
          <td width=\"33\" height=\"21\"></td>
          <td width=\"20\" valign=\"top\" class=\"prefsPageData\">&nbsp;<img src=\"../html/ldap-person.gif\" alt=\"\" width=\"12\" height=\"16\" border=\"0\"></td>
          <td width=\"25\"></td>
          <td width=\"125\" valign=\"top\" class=\"prefsPageData\" nowrap>Phonebook Entry</td>
          <td width=\"21\"></td>
          <td width=\"205\" valign=\"center\" class=\"prefsPageData\"><select NAME=\"phonebook\">
";

# --------------------------------------------

$selected1 = $selected2 = $selected3 = "";
if ($config_tokens{"icons-phonebook-visible"} eq "no")		{ $selected1 = " SELECTED"; }
if ($config_tokens{"icons-phonebook-visible"} eq "forefront")	{ $selected2 = " SELECTED"; }
if ($config_tokens{"icons-phonebook-visible"} eq "layer")	{ $selected3 = " SELECTED"; }

# --------------------------------------------

print "
			<option value=\"no\"$selected1>Never display this icon</option>
  			<option value=\"forefront\"$selected2>Next to name</option>
 			<option value=\"layer\"$selected3>In floating layer</option>
			</select></td>
   		  </tr>
	";
}

#
# don't draw the locator option if admin has disabled it !
#
if ( $config_tokens{"icons-locator-visible"} ne "disabled" )
{

	print "

		<tr height=\"6\"></tr>
		  <tr>
          <td width=\"33\" height=\"21\"></td>
          <td width=\"20\" valign=\"top\" class=\"prefsPageData\">&nbsp;<img src=\"../html/mag.gif\" alt=\"\" width=\"15\" height=\"15\" border=\"0\"></td>
          <td width=\"25\"></td>
          <td width=\"125\" valign=\"top\" class=\"prefsPageData\" nowrap>Locate User</td>
          <td width=\"21\"></td>
          <td width=\"205\" valign=\"top\" class=\"prefsPageData\"><select NAME=\"locate\">

";

# --------------------------------------------

$selected1 = $selected2 = $selected3 = "";
if ($config_tokens{"icons-locator-visible"} eq "no")		{ $selected1 = " SELECTED"; }
if ($config_tokens{"icons-locator-visible"} eq "forefront")	{ $selected2 = " SELECTED"; }
if ($config_tokens{"icons-locator-visible"} eq "layer")		{ $selected3 = " SELECTED"; }

# --------------------------------------------

print "
			<option value=\"no\"$selected1>Never display this icon</option>
  			<option value=\"forefront\"$selected2>Next to name</option>
 			<option value=\"layer\"$selected3>In floating layer</option>
			</select></td>
   		  </tr>
	";
}

#
# don't draw the AIM option if admin has disabled it !
#
if ( $config_tokens{"icons-aim-visible"} ne "disabled" )
{

	print "
		  <tr height=\"6\"></tr>
		  <tr>
          <td width=\"33\" height=\"21\"></td>
          <td width=\"20\" valign=\"top\" class=\"prefsPageData\">&nbsp;<img src=\"../html/aim-online.gif\" alt=\"\" width=\"15\" height=\"15\" border=\"0\"></td>
          <td width=\"25\"></td>
          <td width=\"125\" valign=\"top\" class=\"prefsPageData\" nowrap>AIM Presence</td>
          <td width=\"21\"></td>
          <td width=\"205\" valign=\"top\" class=\"prefsPageData\"><select NAME=\"aim\">
";

# --------------------------------------------

$selected1 = $selected2 = $selected3 = "";
if ($config_tokens{"icons-aim-visible"} eq "no")		{ $selected1 = " SELECTED"; }
if ($config_tokens{"icons-aim-visible"} eq "forefront")		{ $selected2 = " SELECTED"; }
if ($config_tokens{"icons-aim-visible"} eq "layer")		{ $selected3 = " SELECTED"; }

# --------------------------------------------

print "
			<option value=\"no\"$selected1>Never display this icon</option>
  			<option value=\"forefront\"$selected2>Next to name</option>
 			<option value=\"layer\"$selected3>In floating layer</option>
			</select></td>
   		  </tr>
	";
}

#
#  If all icons are "disabled" by the admin, we better not display the
#  window dressing (header and footer) text that normally surrounds the
#  icon options.  This is the footer.
#
if ( "$all_icons_disabled" eq "no" )
{
	print "
		<tr height=\"15\">
		</tr>
		<tr>
		<td width=\"28\" height=\"21\" valign=\"top\">&nbsp;</td>
		<td valign=\"middle\" colspan=\"4\"><hr></td>
		</tr>
	";
}

print "
        <tr>
          <td width=\"28\" height=\"21\" valign=\"top\">&nbsp;</td>
          <td valign=\"top\" colspan=\"4\" class=\"prefsPageHead\">Organization Chart Depth</td>
	    </tr>
		<tr height=\"10\">
		</tr>
		<tr>
		  <td width=\"35\" height=\"28\"></td>
          <td width=\"10\" valign=\"top\" class=\"prefsPageData\">Show&nbsp;&nbsp;</td>
		   <td width=\"20\" valign=\"top\" class=\"prefsPageData\"><select NAME=\"leveldepth\">
";

for ( $num = 1 ; $num <= $config_tokens{"max-levels-drawn"} ; $num++ )
{
	if ( $num < $config_tokens{"max-levels-drawn"} )
	{
		print "<option value=\"$num\">$num</option>";
	}
	else
	{
		print "<option value=\"$num\" SELECTED>$num</option>";
	}
}

print "
			</select></td>
			<td width=\"350\" colspan=\"3\" class=\"prefsPageData\">&nbsp;&nbsp;levels of organization depth</td>
		  </tr>
		  <td height=\"30\"></td>
      </table>
    </td>
  </tr>

</table>

<table width=\"500\" border=\"0\" cellpadding=\"0\" cellspacing=\"0\" align=\"center\">
	<tr>
	<td height=\"20\">&nbsp;</td></tr>
	<tr>
	<td align=\"right\"><input type=\"button\" name=\"save\" value=\"       Finished       \" onClick=\"saveSettings();\"></td>
	<td width=\"20\"</td>
	<td><input type=\"button\" name=\"del_cookie\" value=\"Restore Defaults\" onClick=\"deleteCookie();\"></td>
	</tr>
	</table>
</form>
</body>
";

}

#==============================================================================

sub print_javascript()
{

print "

<SCRIPT language=\"javascript\">

var today = new Date();
var expires = new Date();
var expired = new Date(today.getTime() - 1000 * 24 * 60 * 60 * 1000);

function initValues()
{
	var myorgsettings = getCookie(\"MyOrgChart\");
	var possvalues = new Array(\"no\",\"forefront\",\"layer\");
";


# --------------------------------------------------------------

#
#	let's build up a string like the contents below of Array
#	(if max-levels-drawn was 3):
#
#		var posslevelvalues = new Array("1","2","3");
#
$finalstring = "var posslevelvalues = new Array(";

for ( $num = 1 ; $num <= $config_tokens{"max-levels-drawn"} ; $num++ )
{
	$finalstring = "$finalstring\"$num\"";

	if ( $num != $config_tokens{"max-levels-drawn"} )
	{
		$finalstring = "$finalstring,";
	}
}
$finalstring = "$finalstring);";

# --------------------------------------------------------------

print "

	$finalstring

	//  If there is a cookie already set, let's correct the
	//  values of the HTML form to be based on their personal 
	//  settings, for easier editing and also lack of confusion
	if ( myorgsettings != \"\" )
	{
		var splitorgvalues = myorgsettings.split(\"&\");
		var tempstr;

		// alert(myorgsettings);

		for (var loop=0; loop < splitorgvalues.length; loop++)
		{
			tempstr = splitorgvalues[loop].split(\"=\");
";

# --start---------------------------------------------------------

if ( $config_tokens{"icons-email-visible"} ne "disabled" )
{

print "
			if ( tempstr[0] == \"email\" )
			{
				for (var innerloop=0; innerloop < possvalues.length; innerloop++) 
				{
					if ( tempstr[1] == possvalues[innerloop] )
						document.customize.email.options[innerloop].selected = true;
				}
			}
";
}

# --end---------------------------------------------------------

# --start---------------------------------------------------------

if ( $config_tokens{"icons-phonebook-visible"} ne "disabled" )
{

print "

			if ( tempstr[0] == \"pb\" )
			{
				for (var innerloop=0; innerloop < possvalues.length; innerloop++) 
				{
					if ( tempstr[1] == possvalues[innerloop] )
						document.customize.phonebook[innerloop].selected = true;
				}
			}
";
}

# --end---------------------------------------------------------

# --start---------------------------------------------------------

if ( $config_tokens{"icons-locator-visible"} ne "disabled" )
{

print "

			if ( tempstr[0] == \"maps\" )
			{
				for (var innerloop=0; innerloop < possvalues.length; innerloop++) 
				{
					if ( tempstr[1] == possvalues[innerloop] )
						document.customize.locate[innerloop].selected = true;
				}
			}
";
}

# --end---------------------------------------------------------

# --start---------------------------------------------------------

if ( $config_tokens{"icons-aim-visible"} ne "disabled" )
{

print "

			if ( tempstr[0] == \"aim\" )
			{
				for (var innerloop=0; innerloop < possvalues.length; innerloop++) 
				{
					if ( tempstr[1] == possvalues[innerloop] )
						document.customize.aim[innerloop].selected = true;
				}
			}
";
}

# --end---------------------------------------------------------

print "

			if ( tempstr[0] == \"maxlevels\" )
			{
				for (var innerloop=0; innerloop < $config_tokens{\"max-levels-drawn\"}; innerloop++) 
				{
					if ( tempstr[1] == posslevelvalues[innerloop] )
						document.customize.leveldepth.options[innerloop].selected=true;
				}
			}
		}

	}

	return;
}

function getCookie(Name) 
{
	var search = Name + \"=\"
	if (document.cookie.length > 0) 
	{ // if there are any cookies
		offset = document.cookie.indexOf(search) 
		if (offset != -1) 
		{ // if cookie exists 
			offset += search.length // set index of beginning of value
			end = document.cookie.indexOf(\";\", offset) // set index of end of cookie value
			if (end == -1) 
				end = document.cookie.length
			return unescape(document.cookie.substring(offset, end))
		} 
	}

	return (\"\");
}

function deleteCookie()
{
	document.cookie=\"MyOrgChart\" + \"=null; expires=\" + expired.toGMTString();
	alert(\"Your preferences have been deleted from your browser.\");
	document.location.href = \"myorg\";  
}

function saveSettings()
{
	var i;
	var finalString;

	finalString = \"\";

	// alert(document.customize.email.options[document.customize.email.selectedIndex].value);

";

if (  $config_tokens{"icons-email-visible"} ne "disabled"  )
{
print"
	finalString += \"&email=\" + document.customize.email.options[document.customize.email.selectedIndex].value;
";
}

if (  $config_tokens{"icons-phonebook-visible"} ne "disabled"  )
{
print"
	finalString += \"&pb=\" + document.customize.phonebook.options[document.customize.phonebook.selectedIndex].value;
";
}

if (  $config_tokens{"icons-locator-visible"} ne "disabled"  )
{
print"
	finalString += \"&maps=\" + document.customize.locate.options[document.customize.locate.selectedIndex].value;
";
}

if (  $config_tokens{"icons-aim-visible"} ne "disabled"  )
{
print"
	finalString += \"&aim=\" + document.customize.aim.options[document.customize.aim.selectedIndex].value;
";
}

print "

	finalString += \"&maxlevels=\" + document.customize.leveldepth.options[document.customize.leveldepth.selectedIndex].value;

	expires.setTime(today.getTime() + 1000*60*60*24*365);
	setCookie(\"MyOrgChart\", finalString, expires);

	alert(\"Your preferences have been saved in your browser.\");
	// alert(\"Your preferences have been saved in your browser as:\\n\" + finalString);
	return;
}

function setCookie(name, value, expire) 
{
   document.cookie = name + \"=\" + escape(value)
   + ((expire == null) ? \"\" : (\"; expires=\" + expire.toGMTString()));
}

</SCRIPT>

";

}

#==============================================================================



