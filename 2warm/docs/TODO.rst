Known hazards
=============

These are all not real bugs, just things to be careful about during
the install that could be handled better.

* When running distrib2warm, the result will go to the same directory
  structure as the primary--not necessarily into $PGDATA/../2warm where it
  need to go

* copyToStandby should confirm the existence of the archive/ directory
  on the standby before doing pg_start_backup, because it will
  fail if archiving isn't working right on there before it starts
 
Known Bugs
==========
 
* The DR node capability has not been tested recently, and is expected
  to have issues in its current form.  Its associated documentation is
  not yet included either.

Desired Features
================

* Examples showing how to monitor a warm standby environment would
  improve usability.

Roadmap
=======

Several larger changes are either already under development or planned
as part of the long-term development on 2warm:

* Improved integration with the streaming replication features in
  PostgreSQL 9.0.

* Currently rsync is the only implemented mechanism for transporting
  data between master/standby/relay.  A way to configure each node
  connection with a transport type would allow using alternate methods
  when available.  And example is adding "zfs send" as an alternate
  to using rsync for making a base backup.

* A prototype of a straight Perl port of the existing bash code may be
  included in some 2warm packages.  The Perl implementation is not in
  any way functional yet.

* Full Windows support will be available using the Perl implementation.

* An experimental installer for 2warm that uses the standard autotools
  configure/make mechanism is available from 2ndQuadrant.  Due to code
  complexity and integration issues it's not currently being released.

* Installation into a standard system directory tree (such as putting
  the scripts into /usr/local/bin) may be preferrable for some
  installations to the suggested implementation, which uses paths
  relative to PGDATA.  Making it easier to use that scheme is being
  considered, both with and without autotools support.

* Ready to use packages of 2warm are planned for RPM (RHEL/CentOS) and
  deb (Debian/Ubuntu) platforms.

