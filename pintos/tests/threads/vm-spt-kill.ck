# -*- perl -*-
use strict;
use warnings;
require "tests/threads/vm-unit.pm";
check_vm_unit (<<'EOF');
(vm-spt-kill) begin
(vm-spt-kill) pages destroyed
(vm-spt-kill) table reusable
(vm-spt-kill) PASS
(vm-spt-kill) end
EOF
