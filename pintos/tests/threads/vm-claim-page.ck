# -*- perl -*-
use strict;
use warnings;
require "tests/threads/vm-unit.pm";
check_vm_unit (<<'EOF');
(vm-claim-page) begin
(vm-claim-page) lazy page allocated
(vm-claim-page) claim mapped frame
(vm-claim-page) PASS
(vm-claim-page) end
EOF
