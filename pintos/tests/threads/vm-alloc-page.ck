# -*- perl -*-
use strict;
use warnings;
require "tests/threads/vm-unit.pm";
check_vm_unit (<<'EOF');
(vm-alloc-page) begin
(vm-alloc-page) anon lazy page allocated
(vm-alloc-page) initializer metadata stored
(vm-alloc-page) duplicates rejected
(vm-alloc-page) file lazy page allocated
(vm-alloc-page) PASS
(vm-alloc-page) end
EOF
