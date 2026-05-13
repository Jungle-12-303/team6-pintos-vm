# -*- perl -*-
use strict;
use warnings;
require "tests/threads/vm-unit.pm";
check_vm_unit (<<'EOF');
(vm-spt-copy) begin
(vm-spt-copy) uninit pages copied
(vm-spt-copy) metadata preserved
(vm-spt-copy) PASS
(vm-spt-copy) end
EOF
