#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "reimpl/path_policy.h"

typedef struct {
    const char *input;
    int write_access;
    int succeeds;
    const char *expected;
} path_case;

int main(void) {
    static const path_case cases[] = {
        {"/sdcard/caustic/songs/a.caustic", 1, 1, "ux0:/data/CAUSTIC3/caustic/songs/a.caustic"},
        {"/storage/emulated/0/caustic/presets/p", 0, 1, "ux0:/data/CAUSTIC3/caustic/presets/p"},
        {"ux0:/data/CAUSTIC3/caustic/config", 1, 1, "ux0:/data/CAUSTIC3/caustic/config"},
        {"ux0:/data/CAUSTIC3", 1, 1, "ux0:/data/CAUSTIC3"},
        {"app0:/assets/demo/test", 0, 1, "app0:/assets/demo/test"},
        {"relative/file", 0, 1, "relative/file"},
        {"/sdcard/../../data/OTHER/file", 1, 0, NULL},
        {"ux0:/data/OTHER/file", 0, 0, NULL},
        {"ux0:/data/CAUSTIC3evil/file", 0, 0, NULL},
        {"app0:/asset", 1, 0, NULL},
        {"uma0:/file", 0, 0, NULL},
        {"/proc/self/maps", 0, 0, NULL},
        {"relative/../escape", 0, 0, NULL},
    };
    char output[128];
    int failures = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        memset(output, 0, sizeof(output));
        errno = 0;
        int result = caustic_map_path(cases[i].input, output, sizeof(output),
                                      cases[i].write_access);
        int succeeded = result == 0;
        if (succeeded != cases[i].succeeds ||
            (succeeded && strcmp(output, cases[i].expected) != 0)) {
            fprintf(stderr, "case %zu failed: input=%s result=%d errno=%d output=%s\n",
                    i, cases[i].input, result, errno, output);
            failures++;
        }
    }
    char tiny[8];
    if (caustic_map_path("/sdcard/caustic/song", tiny, sizeof(tiny), 0) == 0 ||
        errno != ENAMETOOLONG) {
        fprintf(stderr, "truncation case failed\n");
        failures++;
    }
    if (failures) return 1;
    puts("path policy tests passed");
    return 0;
}
