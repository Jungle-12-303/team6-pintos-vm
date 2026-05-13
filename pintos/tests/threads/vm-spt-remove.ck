# -*- perl -*-
use strict;
use warnings;
require "tests/threads/vm-unit.pm";
check_vm_unit (<<'EOF');
(vm-spt-remove) begin
(vm-spt-remove) page removed
(vm-spt-remove) PASS
(vm-spt-remove) end
EOF
