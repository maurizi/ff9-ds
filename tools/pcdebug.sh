#!/usr/bin/env bash
# Run the PC host build under gdb and report how it stopped: the fault, its address, the source line and a
# backtrace. Usage (in the container): tools/pcdebug.sh [--frames N]
exec gdb -q -batch -ex run -ex 'printf "\n=== fault address (si_addr): %p\n", $_siginfo._sifields._sigfault.si_addr' \
    -ex 'bt 8' -ex 'info locals' --args build/pc/ff9ds "$@" 2>&1 \
    | grep -v '^\[New Thread\|^\[Thread\|warning: Error disabling\|libthread_db\|^Using host'
