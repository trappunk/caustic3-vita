#ifndef CAUSTIC_PATH_POLICY_H
#define CAUSTIC_PATH_POLICY_H

#include <stddef.h>

/* Translate a Caustic/Android path into the Vita namespace while enforcing
 * the wrapper's filesystem boundary. Returns 0 on success and -1 with errno
 * set when the path is malformed, truncated, or outside an allowed root. */
int caustic_map_path(const char *input, char *output, size_t output_size,
                     int write_access);

#endif
