# -*- perl -*-
use strict;
use warnings;
require "tests/threads/vm-unit.pm";
check_vm_unit (<<'EOF');
(vm-spt-init) begin
(vm-spt-init) init returned true
(vm-spt-init) missing lookup null
(vm-spt-init) PASS
(vm-spt-init) end
EOF
