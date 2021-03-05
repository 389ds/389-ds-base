.. lib389 documentation main file, created by
   sphinx-quickstart on Wed Aug 23 18:43:30 2017.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

Welcome to lib389's documentation!
==================================

Lib389 is a python base library for managing Directory servers.
It was initially created to help writing tests of DS. It can also used
to create new administration CLIs. This document is focusing on writing
tests for the Directory Server. Lib389 can be used to generate individual
testcases(tickets), and suites that test a range of functionality/feature. 


Contents
========

.. toctree::
   :maxdepth: 2

   guidelines.rst
   Replication <replication.rst>
   Configuring Databases <databases.rst>
   Access Control <accesscontrol.rst>
   Identity Management <identitymanagement.rst>
   DSE ldif <dseldif.rst>
   Indexes <indexes.rst>

Work in progress
-----------------
.. toctree::
   :maxdepth: 2

   need_to_be_triaged.rst


Contact us
==========

If you have any issue or a question, you have a few options:

+ write an email to 389-devel@lists.fedoraproject.org
+ ask on irc.freenode.net channel #389
+ check (or create a new) issues on https://pagure.io/lib389

Indices and tables
==================

* :ref:`genindex`
* :ref:`modindex`
* :ref:`search`

