#include "pes.h"
#include <string.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: pes <command> [args]\n");
        fprintf(stderr, "Commands: init, add, status, commit, log\n");
        return 1;
    }

    if      (strcmp(argv[1], "init")   == 0) cmd_init();
    else if (strcmp(argv[1], "add")    == 0) cmd_add(argc, argv);
    else if (strcmp(argv[1], "status") == 0) cmd_status();
    else if (strcmp(argv[1], "commit") == 0) cmd_commit(argc, argv);
    else if (strcmp(argv[1], "log")    == 0) cmd_log();
    else {
        fprintf(stderr, "error: unknown command '%s'\n", argv[1]);
        return 1;
    }
    return 0;
}
