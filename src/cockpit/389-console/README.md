---
title: "Cockpit UI Design"
---

# Layout
--------------------------

The main layout consists of as tabbed panel for the following categories:

- Server Configuration
- Security
- Database
- Replication
- Schema
- Plugins
- Monitoring


<br>


# Panels
-------------------------------

This briefly describes what each panel covers

## Server

This is the welcome page(panel).  You select which local instance you want to configure from auto-filled drop down list.  This instance is then carried over to the other panels.  These are the configuration areas that are covered under this panel:

- General Configuration: port, hostname, LDAPI, various config settings under cn=config
- SASL:  SSL Settings and Mappings
- Password Policy: all things password related
- Logs:  access, error, audit/auditfail settings
- Tasks: backups/restore, Create/delete instances, etc
- Tuning and resource limits: size/time limits, max threads per conn, etc

## Security

- Enable Security and configureation settings
- Manage Certificate Database
- Manage ciphers

## Database

- Mapping tree and backend are "linked".  They are seen as a single object in the UI.  If you create a new suffix, it creates the mapping tree entry and the backend entry.  Delete removes both as well.

- Global database settings
    - General settings
    - Chaining
- Suffix settings
    - General settings
    - Referrals
    - Indexes
    - Attr encryption
- Suffix Tree Node - right click: 
    - Import/Export
    - Backup/Restore
    - Reindex
    - Create database link
    - Add sub-suffix
    - Delete


## Replication

Setup replication configuration, changelog, and agreements.  Keep agmt setup wizard simple.  Build in some simple monitoring and agreement status info.  Save the real replication monitoring for the monitor tab panel. 

- Build in cleanallruv task (and the abort task), and cleanallruv task monitoring!
- "Reinit all agmts" option??
- "test replication" operation (already exists in lib389) - monitoring page, or agreement dropdown(+1)?

## Schema

Manage Standard and Custom Schema and Matching Rules (also has schema-reload task "button")

## Plugins

Manage all plugins  For common/known plugins like: RI, MEP, MemberOf, DNA, Automember, provide a nice customized configuration form in UI.  Eventually do this for IPA plugins too.  For generic/unknown plugins provide basic UI form(enable/disable, and dependencies).


- Sort server core plugins (with customized config):  RI, mep, memeberOf, etc



## Monitoring

- Replication Monitoring
    - Latency monitoring (already exists in lib389)
    - repl-monitor.pl equivalent.  There are complaints this is too complicated, so we need to find a simpler way to express the replication state.
- cn=monitor/Perf Counters/snmp (all monitors - organize content into database monitoring and server monitoring)
- Log viewing (live tailing - feature of cockpit)
- Logconv stats (either from perl script or new internal stats)?
- Database State:
   - index checking/validation (customer request).  Task that performs a search(es) that are expected to be indexed or not indexed.
   - dbverify



# Documentation Via Mouse Hover
----------------------------------

The idea here is provide some built-in documentation that is accessible on the fly when looking at a particular setting or tree node.  Hover the mouse pointer over a config setting and it gives a full description of the setting, value range, and the real attribute name.  This is good for novices and experts.

This is really a simple idea, but it could have a huge impact for admins.  We can add "titles" to almost anything in HTML, lets use them for everything that could use an explanation.


# Working with Cockpit
-----------------------------

## Install cockpit on all servers in topology

Cockpit offers some great built-in functionality for things like authentication and managing many systems from a single location.

-  No HTTP admin server to manage
-  This allows our UI to be very simple with little overhead.
-  Currently there is no need for things like a o=netscaperoot backend (but in the future if the features continue to grow we might still need this).
-  UI would be easy to customize


## Cockpit and DS interaction

The cockpit UI runs shell commands on the actual system using lib389's CLI tools.  Authentication must use LDAPI.  No LDAPI, no cockpit UI.  Using the cockpit API we can run CLI tools and retrieve their output.  The output should be in a JSON format for easy parsing in the UI/javascript:

    cockpit.spawn('/usr/bin/dsconf', '-z', '-n')


# Setup Installation
-----------------------------

## Prerequisites

LDAPI Support (no go for Solaris/HPUX)
cockpit

## Installation

- Enable LDAPI in local ds instances
- Drop in 389's cockpit plugin bundle under **/usr/share/cockpit/**  -->  **/usr/share/cockpit/389-console/**
- Done

## setup-ds-cockpit

-Install script, see "man setup-ds-cockpit"


# Misc
---------------------

No RESET buttons?  Not yet at least...


LIB389 Requirements:

- Consistent JSON representation of entries
- Retrieve schema: standard and custom (user-defined)
- Retrieve plugins
- Get attr syntaxes
- retreive indexes
- Retreive config (cn=config and friends)
- SASL mapping/priority
- Retrieve cleanAllRUV(and abort) tasks
- Get all user/subtree password policies
- Get SSL ciphers
- Get certificates:
    - Get Trust Attrs
    - Get CA certs: expireation dates
    - Get Server Certs, exp dates, issue to, issued by
    - Get Revoked Certs: isssued by, effective date, Next Update, Type
    



# To Do
-------------------------------------

## wizard forms(modals)

- Edit plugin (generic)
- Edit plugin (RI plugin)
- Edit plugin (Member Of)
- Edit plugin ...
- Add/edit SASL Mapiing
- Import/Export Certification (file location)

- Add/edit schema (attrs & objectclasses)
- Add/edit local password policy


## Monitoring page

- Nothing has been done yet
- Some monitoring features need RFE that have yet to be coded

## Nice to have/fix

- Get "Cipher" Datatable to sort on checkbox (other wise we need a "yes/no" 'Enabled" column and a button (in its own column) to toggle it.
- Figure out why the ds-flex page changes width between sever configuraton buttons (Server Configuration vs SASL)
- Remove panel tab highlighting after slection








