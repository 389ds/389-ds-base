#!/bin/sh
ldapvi -D "cn=Directory Manager" -w secret123 -h localhost:3389 -b "dc=example, dc=com" "(objectclass=posixaccount)"
