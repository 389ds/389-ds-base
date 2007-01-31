#!/bin/sh
ldapsearch -x -D "cn=Directory Manager" -w secret123 -h localhost -p 3389 -b "cn=Distributed Numeric Assignment Plugin,cn=plugins,cn=config" "(objectclass=*)"
