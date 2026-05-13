# -*- perl -*-
use strict;
use warnings;
require "tests/threads/vm-unit.pm";
check_vm_unit (<<'EOF');
(vm-anon-initializer) begin
(vm-anon-initializer) anon page operations installed
(vm-anon-initializer) PASS
(vm-anon-initializer) end
EOF
