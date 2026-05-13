# -*- perl -*-
use strict;
use warnings;
require "tests/threads/vm-unit.pm";
check_vm_unit (<<'EOF');
(vm-spt-insert) begin
(vm-spt-insert) aligned user page inserted
(vm-spt-insert) duplicates rejected
(vm-spt-insert) invalid addresses rejected
(vm-spt-insert) PASS
(vm-spt-insert) end
EOF
