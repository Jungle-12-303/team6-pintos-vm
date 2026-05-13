# -*- perl -*-
use strict;
use warnings;
use tests::tests;

sub check_vm_unit {
    my ($expected) = @_;
    our ($test);
    my (@output) = read_text_file ("$test.output");

    common_checks ("run", @output);
    @output = get_core_output ("run", @output);
    @output = grep (!/^\[page_hash\]/, @output);

    my ($actual) = join ("\n", @output) . "\n";
    $expected .= "\n" if $expected !~ /\n\z/;

    fail "Test output failed to match expected output.\n\n"
      . "Expected output:\n$expected\nActual output:\n$actual"
      if $actual ne $expected;

    pass;
}

1;
