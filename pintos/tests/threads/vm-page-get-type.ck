# -*- perl -*-
use strict;
use warnings;
require "tests/threads/vm-unit.pm";
check_vm_unit (<<'EOF');
(vm-page-get-type) begin
(vm-page-get-type) uninit types resolve
(vm-page-get-type) initialized types resolve
(vm-page-get-type) PASS
(vm-page-get-type) end
EOF
