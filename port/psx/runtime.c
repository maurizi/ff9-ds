/* Runtime services shared by both platforms: logging, fatal errors, stubs. */
#include <stdarg.h>
#include <stdio.h>

#include "platform.h"
#include "psx_runtime.h"

void port_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    plat_vlog(fmt, ap);
    va_end(ap);
}

void port_fatal(const char *fmt, ...) {
    va_list ap;
    port_log("\nFATAL: ");
    va_start(ap, fmt);
    plat_vlog(fmt, ap);
    va_end(ap);
    port_log("\n");
    plat_halt();
}

void port_stub_hit(const char *name) {
    port_fatal("not implemented: %s", name);
}
