# -*- perl -*-
use strict;
use warnings;
require "tests/threads/vm-unit.pm";
check_vm_unit (<<'EOF');
(vm-fault-invalid) begin
(vm-fault-invalid) rejects invalid faults
(vm-fault-invalid) rejects write to read-only page
(vm-fault-invalid) PASS
(vm-fault-invalid) end
EOF
