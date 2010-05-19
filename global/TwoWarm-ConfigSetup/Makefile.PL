use strict;
use warnings;
use ExtUtils::MakeMaker;

WriteMakefile(
    NAME                => 'TwoWarm::ConfigSetup',
    AUTHOR              => 'Greg Smith <greg@2ndQuadrant.com>',
    VERSION_FROM        => 'lib/TwoWarm/ConfigSetup.pm',
    ABSTRACT_FROM       => 'lib/TwoWarm/ConfigSetup.pm',
    PL_FILES            => {},
    PREREQ_PM => {
        'Test::More' => 0,
    },
    dist                => { COMPRESS => 'gzip -9f', SUFFIX => 'gz', },
    clean               => { FILES => 'TwoWarm-ConfigSetup-*' },
);
