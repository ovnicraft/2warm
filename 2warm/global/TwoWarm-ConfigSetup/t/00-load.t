#!perl -T

use Test::More tests => 1;

BEGIN {
	use_ok( 'TwoWarm::ConfigSetup' );
}

diag( "Testing TwoWarm::ConfigSetup $TwoWarm::ConfigSetup::VERSION, Perl $], $^X" );
