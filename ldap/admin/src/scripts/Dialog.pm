# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2007 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
# END COPYRIGHT BLOCK
#

package Dialog;

use DialogManager;

#require    Exporter;
#@ISA       = qw(Exporter);
#@EXPORT    = qw();

# NOTE: This "class" is an "abstract" class.  There are two methods which
# must be provided by subclasses:
# $ans = $dialog->defaultAns($promptindex);
# where $promptindex is the index into the array of prompts given when
# constructing the Dialog object
# The dialog will typically use a default answer either hardcoded in
# or from some key in the setup cache (.inf) file
# 
# $resp = $dialog->handleResponse($ans, $index);
# The dialog uses this method to perform validation of the input, set the value
# in the setup cache, display errors or warnings, and tell the dialog manager
# if the prompt needs to be redisplayed, or if there was an unrecoverable error
# $resp should be $SAME to reprompt, $ERR to abort, or $NEXT to continue
# the $ans and defaultAns should be in the native charset, so the dialog
# may have to convert to/from utf8 as needed.

# a dialog consists of a title, some explanatory text, and one or more prompts
# each prompt has a default value.  An example of a dialog with more than
# one prompt would be a dialog asking the user for the new root DN and password -
# in that case, there would be 3 prompts - one for the DN, one for the password,
# and one to verify the password
# The text and prompts are given as resource keys.  Usually the resource value
# will be a simple string, in which case the resource key is passed in as a simple
# string.  However, if the resource string contains replaceable parameters, the
# resource key is passed as an array ref consisting of the resource key as the
# first element and the parameters to use for replacement as the subsequent
# array elements e.g.
# $foo = new Dialog(['RESOURCE_KEY_CONFIG_LDAP_URL', $secure, $host, $port, $suffix], ...);
# but usually for simple cases like this:
# $foo = new Dialog('RESOURCE_KEY_WELCOME', ...);
# The manager contains the context for all of the dialogs - the setup type, the resource
# file, setup log, other context shared among the dialogs
# the type is the setup type - 1, 2, or 3 for express, typical, or custom
# type is used to say which types use this dialog
sub new {
    my $type = shift;
    my $self = {};

    $self->{type} = shift;
    $self->{text} = shift;
    $self->{defaultAns} = shift;
    $self->{handleResp} = shift;
    $self->{prompts} = \@_;

    $self = bless $self, $type;

    return $self;
}

sub setManager {
    my $self = shift;
    $self->{"manager"} = shift;
}

# returns true if this dialog is to be displayed for the current setup type
# false otherwise
sub isDisplayed {
    my $self = shift;

    return $self->{type} <= $self->{"manager"}->{type};
}

sub isEnabled {
    my $self = shift;
    return !defined($self->{disabled});
}

sub enable {
    my $self = shift;
    delete $self->{disabled};
}

sub disable {
    my $self = shift;
    $self->{disabled} = 1;
}

# each prompt looks like this:
# [ 'resource key', is pwd, hide ]
# The resource key is the string key of the resource
# is pwd is optional - if present, the prompt is for a password
# and should not echo the answer
# hide is optional - if present and true, the prompt will not be displayed - this
# is useful in cases where you may want to display or hide a subprompt depending
# on the response to a main prompt
# e.g.
# ['RESOURCE_USERNAME'], ['RESOURCE_PASSWORD', 1], ['RESOURCE_PASSWORD_AGAIN', 1]
# e.g.
# ['USE_SECURITY'], ['CA_CERTIFICATE', 0, 0]
# you can set the 0 to a 1 if the user has chosen to use security
sub run {
    my $self = shift;
    my $direction = shift;
    my $resp = $DialogManager::SAME;

    # display the dialog text
    if ($self->isDisplayed()) {
        $self->{manager}->showText($self->{text});
    }

    # display each prompt for this dialog
    my $index = 0;
    my @prompts = @{$self->{prompts}};
    for (my $index = 0; $index < @prompts; ++$index) {
        my $prompt = $prompts[$index];
        my $defaultans = $self->{defaultAns}($self, $index);
        my $ans;
        if ($self->isDisplayed() && !$prompt->[2]) {
            $ans = $self->{manager}->showPrompt($prompt->[0], $defaultans, $prompt->[1]);
        } else {
            $ans = $defaultans;
        }

        # see if this is the special BACK response, and finish if so
        if ($self->{"manager"}->isBack($ans)) {
            $resp = $DialogManager::BACK;
            last;
        }

        # figure out what action to take based on the users response
        # this will set values in the setup info file
        # this will also validate input, and display errors if the
        # input is not correct - in that case, the resp will be
        # SAME to reprompt, or ERR if unrecoverable
        # NOTE: user cannot BACK from prompt to prompt - BACK
        # always means BACK to the previous dialog
        $resp = $self->{handleResp}($self, $ans, $index);
        if (($resp == $DialogManager::SAME) or ($resp == $DialogManager::FIRST)) {
            if (!$self->isDisplayed()) {
                $self->{manager}->alert('dialog_use_different_type');
                $resp = $DialogManager::ERR;
            } elsif ($resp == $DialogManager::SAME) {
                $index--; # reprompt
            } else {
                $index = -1; # reshow first prompt on dialog
            }
        } elsif ($resp == $DialogManager::ERR) {
            last;
        } elsif (!$self->isDisplayed() && ($direction < 0) &&
                 ($resp == $DialogManager::NEXT)) {
            # we did not display this dialog, and the current navigation
            # direction is BACK, so we should return BACK, to allow
            # the user to go back through several dialogs
            $resp = $DialogManager::BACK;
        }
    }

    return $resp;
}

package DialogYesNo;

@ISA       = qw(Dialog);

sub new {
    my $type = shift;
    my $setuptype = shift;
    my $text = shift;
    my $defaultIsYes = shift;
    my $handler = shift || \&handleResponse;
    my $prompt = shift || ['prompt_yes_no'];
    my $self = Dialog->new($setuptype, $text,
                           \&defaultAns, $handler, $prompt);

    $self->{defaultIsYes} = $defaultIsYes;
    
    $self = bless $self, $type;

    return $self;
}

sub setDefaultYes {
    my $self = shift;
    $self->{default} = $self->{"manager"}->getText("yes");
}

sub setDefaultNo {
    my $self = shift;
    $self->{default} = $self->{"manager"}->getText("no");
}

sub defaultAns {
    my $self = shift;
    if (exists($self->{ans})) {
        return $self->{ans};
    }
    if (!exists($self->{default})) {
        my $isyes;
        if (ref($self->{defaultIsYes}) eq 'CODE') {
            $isyes = &{$self->{defaultIsYes}}($self);
        } else {
            $isyes = $self->{defaultIsYes};
        }
        if ($isyes) {
            $self->{default} = $self->{"manager"}->getText("yes");
        } else {
            $self->{default} = $self->{"manager"}->getText("no");
        }
    }
    return $self->{default};
}

sub isYes {
    my $self = shift;
    return $self->{ans} eq $self->{"manager"}->getText("yes");
}

sub handleResponse {
    my $self = shift;
    my $ans = shift;
    my $resp = $DialogManager::SAME;
    my $yes = $self->{"manager"}->getText("yes");
    my $nno = $self->{"manager"}->getText("no");

    # the regexp allows us to use y or ye or yes for "yes"
    if ($nno =~ /^$ans/i) {
        $resp = $DialogManager::NEXT;
        $self->{ans} = $nno;
    } elsif ($yes =~ /^$ans/i) {
        $resp = $DialogManager::NEXT;
        $self->{ans} = $yes;
    } else {
        $self->{"manager"}->alert("yes_no_error");
    }

    return $resp;
}

#############################################################################
# Mandatory TRUE return value.
#
1;
