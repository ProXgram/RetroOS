#include "shell.h"
#include "terminal.h"

void kmain(void) {
    terminal_initialize();
    shell_run();
}
