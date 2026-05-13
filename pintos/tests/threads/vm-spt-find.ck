# -*- perl -*-
use strict;
use warnings;
require "tests/threads/vm-unit.pm";
check_vm_unit (<<'EOF');
(vm-spt-find) begin
(vm-spt-find) exact lookup hit
(vm-spt-find) offset lookup rounds down
(vm-spt-find) missing lookup null
(vm-spt-find) PASS
(vm-spt-find) end
EOF
