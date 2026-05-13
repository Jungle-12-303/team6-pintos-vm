# -*- perl -*-
use strict;
use warnings;
require "tests/threads/vm-unit.pm";
check_vm_unit (<<'EOF');
(vm-file-initializer) begin
(vm-file-initializer) file page operations installed
(vm-file-initializer) PASS
(vm-file-initializer) end
EOF
