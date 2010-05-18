package TwoWarm::ConfigSetup;

use warnings;
use strict;

=head1 NAME

TwoWarm::ConfigSetup

Confirm environment is setup the way we expect before running
anything

Copyright (c) 2ndQuadrant, 2009-2010

=head1 VERSION

Version 2.06

=cut

our $VERSION = '2.06';
use base 'Exporter';
our @EXPORT=qw(find_base,config_setup,require_pgdata,primary_only,
    standby_only,require_other,require_dr,require_archive,
    exit_on_error,exit_if_primary,dump_config)

my %CONFIG = {}
my $rc;

=head1 SYNOPSIS

Example usage:

    use TwoWarm::ConfigSetup;
    my $foo = TwoWarm::ConfigSetup->new();
    TwoWarm:ConfigSetup::config_setup;
    my %cfg=TwoWarm::ConfigSetup::cfg;

=head1 EXPORT

find_base,config_setup,require_pgdata,primary_only,
standby_only,require_other,require_dr,require_archive,
exit_on_error,exit_if_primary,dump_config

=head1 FUNCTIONS

=head2 cfg

=cut
sub cfg
{
    return %CONFIG
}

=head2 find_base

=cut
sub find_base
{
    use FindBin;
    use File::Spec;
    use Cwd;
    $PGROOT="";
    $PGROOT=$ENV{'PGROOT'} if (defined $ENV{'PGROOT'});
	# Validate if a hard-coded PGROOT points somewhere useful
	if ( ! -d $PGROOT)
    {
        # Figure out where this script is running from and guess
        # where PGROOT is at from there.  All of the major scripts
        # we run are 3 directory levels up from the PGROOT base.        
        my $SCRIPTDIR=File::Spec->rel2abs($FindBin::Bin);
        my $UPPATH=File::Spec->updir()."/".File::Spec->updir()."/".File::Spec->updir();
        $PGROOT = Cwd::realpath(File::Spec->rel2abs($UPPATH,$SCRIPTDIR));
    }
	# Basic validation that PGROOT has expected scripts
    my $CONFIGSCRIPT=$PGROOT."/2warm/global/replication/configSetup";
    if ( ! -f $CONFIGSCRIPT )
    {
      print "ERROR:  PGROOT does not point to a complete 2warm installation.";
      exit(1);
    }
    $config{'PGROOT'}=$PGROOT
}

=head2 config_setup

=cut
sub config_setup
{
    my $PGDATA;
    my $PGROOT;
    my $ARCHIVE;
    my $GLOBAL;
    my $LOCAL;
    my $OTHERNODE;
    my $DRNODE;

	find_base;
    if (defined $ENV{'PGDATA'})
    {
        $PGDATA=$ENV{'PGDATA'};
    }

	# Basic directory tree
	my $GLOBAL=$PGROOT."/2warm/global/";
	my $LOCAL=$PGROOT."/2warm/local/";
    
	# Lookup information about our configuration
	my $OTHERNODE=`cat $LOCAL/replication/othernode`;
    chomp $OTHERNODE;
	undef $DRNODE;
	if ( -f $LOCAL."/replication/drnode" )
    {
		$DRNODE=`cat $LOCAL/replication/drnode`;
        chomp $OTHERNODE;
    }
	# Load local settings
	if ( -f $LOCAL."/environment.sh" )
    {
        # TODO
        #source $LOCAL/environment.sh
    }
    
	# And finally set the archive directory (after the local settings have applied)
    if (defined $PGDATA)
    {
        $ARCHIVE=$PGDATA."/archive"
    }
    else
    {
        $ARCHIVE=""
    }
    $cfg{'ARCHIVE'}=$ARCHIVE
    $cfg{'PGDATA'}=$PGDATA;
    $cfg{'PGROOT'}=$PGROOT;
    $cfg{'ARCHIVE'}=$ARCHIVE;
    $cfg{'GLOBAL'}=$GLOBAL;
    $cfg{'LOCAL'}=$LOCAL;
    $cfg{'OTHERNODE'}=$OTHERNODE;
    $cfg{'DRNODE'}=$DRNODE;
}

=head2 require_pgdata

=cut
sub require_pgdata
{
	if ( ! -d $cfg('PGDATA') ) 
    {
		print "ERROR:  PGDATA undefined or doesn't point to a valid directory";
		exit(1);
    }
}

=head2 primary_only

=cut
sub primary_only
{
  if ( -f $cfg('PGDATA')"recovery.conf" )
    {
    print "ERROR:  recovery.conf exists: we are the Standby node.  Run this on Primary node only.";
    exit(1);
    }
}

=head2 standby_only

=cut
sub standby_only
{
  if ( ! -f $cfg('PGDATA')."recovery.conf" )
    {
    print "ERROR:  recovery.conf does not exist: we aren't the standby node";
    exit(1);
    }
}

=head2

=cut
sub require_other
{
  if (!defined $cfg('OTHERNODE')
    {
    print "ERROR:  a secondary other node is required for this script to operate.";
    exit(1);
    }
}

=head2 require_dr

=cut
sub require_dr
{
  if (!defined $DRNODE) 
    {
    print "ERROR:  a DR relay is required for this script to operate.";
    exit(1);
    }
}

=head2 require_archive

=cut
sub require_archive
{
  require_pgdata;
  mkdir -p $ARCHIVE;
  if ( ! -d $ARCHIVE ) 
    {
    print "ERROR:  Do not have and could not create archive directory".$ARCHIVE;
    exit(1);
    }
}

=head2 exit_on_error

=cut
sub exit_on_error
{
    my $ERROR;
    if ( $rc != 0 ) 
    {
        $ERROR="";  ## TODO Error message passed as argument
        if ( -z $ERROR )
        {
            $ERROR="Unexpected problem";
        }
    print "ERROR:  ".$ERROR;
    exit(1);
    }
}

=head2 exit_if_primary

=cut
sub exit_if_primary
{
  # Can I logon to Postgres? If I can then I must be Primary, so exit
  $rc=system("psql -c \"select version();\" 2>&1 >> /dev/null");
  if ($rc == 0)
    {
    print "ERROR:  This node is running a database and appears to be the primary, not the standby or DR relay.\n";
    exit(1);
    }
}

=head2 dump_config

=cut
sub dump_config
{
  print "PGROOT=".$PGROOT."\n";
  print "GLOBAL=".$GLOBAL."\n";
  print "LOCAL=".$LOCAL."\n";
  print "ARCHIVE=".$ARCHIVE."\n";
  print "OTHERNODE=".$OTHERNODE."\n";
  print "DRNODE=".$DRNODE."\n";
}

=head2 test

=cut
sub test
{
config_setup;

if ( $ARGV[0] eq "debug")
    {
    dump_config;
    print "Exiting after debug config dump\n";
    exit(1);
    }
}

=head1 AUTHOR

Greg Smith, C<< <greg at 2ndQuadrant.com> >>

=head1 BUGS

This is currently prototype quality code

=head1 SUPPORT

You can find documentation for this module with the perldoc command.

    perldoc TwoWarm::ConfigSetup

You can also look for information at:

=over 4
=item * 2ndQuadrant Projects
L<http://projects.2ndQuadrant.com>
=back

=head1 ACKNOWLEDGEMENTS

2warm originally by Simon Riggs, expanded by Greg Smith and Gabriele
Bartolini

=head1 COPYRIGHT & LICENSE

Copyright 2010 2ndQuadrant
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions 
are met:

=over 4
=item * Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
=item * Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the following disclaimer
in the documentation and/or other materials provided with the
distribution.
=item * Neither the name of the author nor the names of contributors
may be used to endorse or promote products derived from this software
without specific prior written permission.
=back

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
=cut

1; # End of TwoWarm::ConfigSetup
