#include "reimpl/path_policy.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifndef DATA_PATH
#define DATA_PATH "ux0:/data/CAUSTIC3/"
#endif

static int starts_with(const char *value, const char *prefix) {
    return strncmp(value, prefix, strlen(prefix)) == 0;
}

static int has_parent_segment(const char *path) {
    const char *segment = path;
    for (const char *cursor = path; ; ++cursor) {
        if (*cursor == '/' || *cursor == '\0') {
            if ((size_t)(cursor - segment) == 2 &&
                segment[0] == '.' && segment[1] == '.')
                return 1;
            if (*cursor == '\0') break;
            segment = cursor + 1;
        }
    }
    return 0;
}

static int copy_checked(char *output, size_t output_size, const char *value) {
    int written = snprintf(output, output_size, "%s", value);
    if (written < 0 || (size_t)written >= output_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

static int join_data_root(char *output, size_t output_size, const char *suffix) {
    int written = snprintf(output, output_size, DATA_PATH "%s", suffix);
    if (written < 0 || (size_t)written >= output_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

static int is_data_path(const char *path) {
    static const char root[] = DATA_PATH;
    const size_t root_size = sizeof(root) - 1;
    if (strncmp(path, root, root_size) == 0) return 1;
    return root_size > 0 && root[root_size - 1] == '/' &&
           strlen(path) == root_size - 1 &&
           strncmp(path, root, root_size - 1) == 0;
}

int caustic_map_path(const char *input, char *output, size_t output_size,
                     int write_access) {
    if (!input || !*input || !output || output_size == 0) {
        errno = EINVAL;
        return -1;
    }
    if (has_parent_segment(input)) {
        errno = EACCES;
        return -1;
    }

    if (starts_with(input, "/storage/emulated/0/"))
        return join_data_root(output, output_size, input + 20);
    if (starts_with(input, "/sdcard/"))
        return join_data_root(output, output_size, input + 8);

    if (starts_with(input, "app0:")) {
        if (write_access) {
            errno = EROFS;
            return -1;
        }
        return copy_checked(output, output_size, input);
    }
    if (starts_with(input, "ux0:")) {
        if (!is_data_path(input)) {
            errno = EACCES;
            return -1;
        }
        return copy_checked(output, output_size, input);
    }

    /* Other Vita mounts and unrecognized absolute Android/POSIX paths are not
     * part of the app's storage contract. Plain relative names are retained
     * for compatibility after parent-segment rejection. */
    if (strchr(input, ':') || input[0] == '/') {
        errno = EACCES;
        return -1;
    }
    return copy_checked(output, output_size, input);
}
