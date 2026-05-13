# -*- perl -*-
use strict;
use warnings;
require "tests/threads/vm-unit.pm";
check_vm_unit (<<'EOF');
(vm-fault-claim) begin
(vm-fault-claim) lazy page allocated
(vm-fault-claim) fault claimed page
(vm-fault-claim) PASS
(vm-fault-claim) end
EOF
