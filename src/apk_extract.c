#include "apk_extract.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "log.h"

#define EOCD_SIGNATURE 0x06054b50u
#define CENTRAL_SIGNATURE 0x02014b50u
#define LOCAL_SIGNATURE 0x04034b50u
#define ASSET_PREFIX "assets/demo/"
#define MAX_DEMO_ENTRY_SIZE (16u * 1024u * 1024u)

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int unsafe_relative_path(const char *path) {
    if (!path || !*path || path[0] == '/') return 1;
    const char *segment = path;
    for (const char *cursor = path; ; ++cursor) {
        if (*cursor == '/' || *cursor == '\0') {
            size_t size = (size_t)(cursor - segment);
            if (size == 0 || (size == 1 && segment[0] == '.') ||
                (size == 2 && segment[0] == '.' && segment[1] == '.'))
                return 1;
            if (*cursor == '\0') break;
            segment = cursor + 1;
        }
    }
    return strchr(path, '\\') != NULL || strchr(path, ':') != NULL;
}

static int read_exact(SceUID fd, void *buffer, size_t size) {
    uint8_t *p = buffer;
    while (size) {
        int got = sceIoRead(fd, p, size);
        if (got <= 0) return -1;
        p += got;
        size -= (size_t)got;
    }
    return 0;
}

static void mkdir_parents(const char *path) {
    char work[768];
    snprintf(work, sizeof(work), "%s", path);
    for (char *p = work + 5; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            sceIoMkdir(work, 0777);
            *p = '/';
        }
    }
}

static int write_file(const char *path, const void *data, size_t size) {
    mkdir_parents(path);
    SceUID out = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (out < 0) return out;
    const uint8_t *p = data;
    size_t left = size;
    while (left) {
        int wrote = sceIoWrite(out, p, left);
        if (wrote <= 0) { sceIoClose(out); return -1; }
        p += wrote;
        left -= (size_t)wrote;
    }
    sceIoClose(out);
    return 0;
}

static int existing_size_matches(const char *path, uint32_t size) {
    SceIoStat st;
    memset(&st, 0, sizeof(st));
    return sceIoGetstat(path, &st) >= 0 && (uint64_t)st.st_size == size;
}

static int copy_file_streamed(const char *source, const char *destination) {
    SceUID input = sceIoOpen(source, SCE_O_RDONLY, 0);
    if (input < 0) return input;
    mkdir_parents(destination);
    SceUID output = sceIoOpen(destination,
                              SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (output < 0) {
        sceIoClose(input);
        return output;
    }

    uint8_t buffer[32 * 1024];
    int result = 0;
    for (;;) {
        int received = sceIoRead(input, buffer, sizeof(buffer));
        if (received < 0) {
            result = received;
            break;
        }
        if (received == 0) break;
        int offset = 0;
        while (offset < received) {
            int written = sceIoWrite(output, buffer + offset,
                                     (SceSize)(received - offset));
            if (written <= 0) {
                result = written < 0 ? written : -1;
                break;
            }
            offset += written;
        }
        if (result < 0) break;
    }
    sceIoClose(output);
    sceIoClose(input);
    return result;
}

static int copy_directory_recursive(const char *source, const char *destination,
                                    unsigned *copied, unsigned *skipped,
                                    int preserve_existing) {
    sceIoMkdir(destination, 0777);
    SceUID directory = sceIoDopen(source);
    if (directory < 0) return directory;

    int result = 0;
    SceIoDirent entry;
    while (result >= 0) {
        memset(&entry, 0, sizeof(entry));
        int read_result = sceIoDread(directory, &entry);
        if (read_result < 0) {
            result = read_result;
            break;
        }
        if (read_result == 0) break;
        if (!strcmp(entry.d_name, ".") || !strcmp(entry.d_name, ".."))
            continue;

        char source_path[768];
        char destination_path[768];
        snprintf(source_path, sizeof(source_path), "%s/%s", source, entry.d_name);
        snprintf(destination_path, sizeof(destination_path), "%s/%s",
                 destination, entry.d_name);
        if (SCE_S_ISDIR(entry.d_stat.st_mode)) {
            result = copy_directory_recursive(source_path, destination_path,
                                              copied, skipped,
                                              preserve_existing);
        } else {
            SceIoStat destination_stat;
            if (preserve_existing &&
                sceIoGetstat(destination_path, &destination_stat) >= 0) {
                (*skipped)++;
                continue;
            }
            result = copy_file_streamed(source_path, destination_path);
            if (result >= 0) (*copied)++;
        }
    }
    sceIoDclose(directory);
    return result;
}

int install_caustic_extra_bundle(const char *source, const char *destination,
                                 const char *marker, const char *label,
                                 int preserve_existing) {
    SceIoStat marker_stat;
    if (sceIoGetstat(marker, &marker_stat) >= 0) return 0;

    unsigned copied = 0;
    unsigned skipped = 0;
    int result = copy_directory_recursive(source, destination, &copied,
                                          &skipped, preserve_existing);
    if (result < 0) {
        char line[160];
        snprintf(line, sizeof(line),
                 "Caustic data: %s installation failed (%d)", label, result);
        debug_log(line);
        return result;
    }
    if (write_file(marker, "ok\n", 3) < 0) return -1;

    char line[128];
    snprintf(line, sizeof(line),
             "Caustic data: %s installed (%u copied, %u preserved)",
             label, copied, skipped);
    debug_log(line);
    return 0;
}

int ensure_caustic_config_line(const char *path, const char *key,
                               const char *line) {
    SceUID input = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (input < 0) {
        int result = write_file(path, line, strlen(line));
        if (result >= 0) debug_log("Caustic data: display-fill setting created");
        return result;
    }

    SceOff size = sceIoLseek(input, 0, SCE_SEEK_END);
    if (size < 0 || size > 64 * 1024) {
        sceIoClose(input);
        return -1;
    }
    sceIoLseek(input, 0, SCE_SEEK_SET);
    char *contents = malloc((size_t)size + 1);
    if (!contents) {
        sceIoClose(input);
        return -1;
    }
    if (read_exact(input, contents, (size_t)size) < 0) {
        free(contents);
        sceIoClose(input);
        return -1;
    }
    sceIoClose(input);
    contents[size] = '\0';

    char *match = contents;
    size_t key_size = strlen(key);
    while ((match = strstr(match, key)) != NULL) {
        if (match == contents || match[-1] == '\n') break;
        match += key_size;
    }

    size_t line_size = strlen(line);
    size_t prefix_size;
    const char *suffix;
    size_t suffix_size;
    int add_separator = 0;
    if (match) {
        char *line_end = strchr(match, '\n');
        prefix_size = (size_t)(match - contents);
        suffix = line_end ? line_end + 1 : contents + size;
        suffix_size = (size_t)((contents + size) - suffix);
    } else {
        prefix_size = (size_t)size;
        suffix = contents + size;
        suffix_size = 0;
        add_separator = size > 0 && contents[size - 1] != '\n';
    }

    size_t output_size = prefix_size + (size_t)add_separator +
                         line_size + suffix_size;
    char *output = malloc(output_size);
    if (!output) {
        free(contents);
        return -1;
    }
    memcpy(output, contents, prefix_size);
    size_t cursor = prefix_size;
    if (add_separator) output[cursor++] = '\n';
    memcpy(output + cursor, line, line_size);
    cursor += line_size;
    memcpy(output + cursor, suffix, suffix_size);

    int result = write_file(path, output, output_size);
    free(output);
    free(contents);
    if (result >= 0) debug_log("Caustic data: display-fill setting enabled");
    return result;
}

int extract_caustic_demo_assets(const char *apk_path, const char *destination) {
    const char *marker = "ux0:/data/CAUSTIC3/.demo_assets_322.ready";
    SceIoStat marker_stat;
    if (sceIoGetstat(marker, &marker_stat) >= 0) {
        debug_log("Caustic data: preset extraction already complete");
        return 0;
    }

    debug_log("Caustic data: extracting presets, songs, and skins from APK");
    SceUID fd = sceIoOpen(apk_path, SCE_O_RDONLY, 0);
    if (fd < 0) return fd;
    SceOff file_size = sceIoLseek(fd, 0, SCE_SEEK_END);
    if (file_size < 22) { sceIoClose(fd); return -1; }
    size_t tail_size = file_size < 65557 ? (size_t)file_size : 65557;
    uint8_t *tail = malloc(tail_size);
    if (!tail) { sceIoClose(fd); return -1; }
    sceIoLseek(fd, file_size - tail_size, SCE_SEEK_SET);
    if (read_exact(fd, tail, tail_size) < 0) { free(tail); sceIoClose(fd); return -1; }

    uint8_t *eocd = NULL;
    for (size_t i = tail_size - 22; ; --i) {
        if (le32(tail + i) == EOCD_SIGNATURE) { eocd = tail + i; break; }
        if (i == 0) break;
    }
    if (!eocd) { free(tail); sceIoClose(fd); return -1; }
    uint16_t entries = le16(eocd + 10);
    uint32_t central_offset = le32(eocd + 16);
    free(tail);

    if ((uint64_t)central_offset + 46u > (uint64_t)file_size) {
        sceIoClose(fd);
        return -1;
    }

    if (sceIoLseek(fd, central_offset, SCE_SEEK_SET) < 0) {
        sceIoClose(fd);
        return -1;
    }
    unsigned extracted = 0;
    for (uint16_t index = 0; index < entries; ++index) {
        uint8_t central[46];
        if (read_exact(fd, central, sizeof(central)) < 0 ||
            le32(central) != CENTRAL_SIGNATURE) goto fail;
        uint16_t method = le16(central + 10);
        uint32_t expected_crc = le32(central + 16);
        uint32_t compressed_size = le32(central + 20);
        uint32_t output_size = le32(central + 24);
        uint16_t name_size = le16(central + 28);
        uint16_t extra_size = le16(central + 30);
        uint16_t comment_size = le16(central + 32);
        uint32_t local_offset = le32(central + 42);
        if (name_size == 0 || output_size > MAX_DEMO_ENTRY_SIZE ||
            compressed_size > MAX_DEMO_ENTRY_SIZE ||
            (uint64_t)local_offset + 30u > (uint64_t)file_size)
            goto fail;
        char *name = malloc((size_t)name_size + 1);
        if (!name || read_exact(fd, name, name_size) < 0) { free(name); goto fail; }
        name[name_size] = '\0';
        if (sceIoLseek(fd, extra_size + comment_size, SCE_SEEK_CUR) < 0) {
            free(name);
            goto fail;
        }
        SceOff next_central = sceIoLseek(fd, 0, SCE_SEEK_CUR);
        if (next_central < 0 || next_central > file_size) {
            free(name);
            goto fail;
        }

        size_t prefix_size = strlen(ASSET_PREFIX);
        if (name_size < prefix_size ||
            strncmp(name, ASSET_PREFIX, prefix_size) != 0 ||
            name[name_size - 1] == '/') {
            free(name);
            continue;
        }
        const char *relative = name + prefix_size;
        if (unsafe_relative_path(relative)) { free(name); goto fail; }
        char output_path[768];
        int output_path_size = snprintf(output_path, sizeof(output_path), "%s%s",
                                        destination, relative);
        if (output_path_size < 0 ||
            (size_t)output_path_size >= sizeof(output_path)) {
            free(name);
            goto fail;
        }
        if (existing_size_matches(output_path, output_size)) {
            free(name);
            continue;
        }

        if (sceIoLseek(fd, local_offset, SCE_SEEK_SET) < 0) {
            free(name);
            goto fail;
        }
        uint8_t local[30];
        if (read_exact(fd, local, sizeof(local)) < 0 || le32(local) != LOCAL_SIGNATURE) {
            free(name); goto fail;
        }
        uint32_t data_skip = (uint32_t)le16(local + 26) + le16(local + 28);
        SceOff data_offset = sceIoLseek(fd, data_skip, SCE_SEEK_CUR);
        if (data_offset < 0 ||
            (uint64_t)data_offset + compressed_size > (uint64_t)file_size) {
            free(name);
            goto fail;
        }
        uint8_t *compressed = malloc(compressed_size ? compressed_size : 1);
        uint8_t *output = malloc(output_size ? output_size : 1);
        if (!compressed || !output || read_exact(fd, compressed, compressed_size) < 0) {
            free(compressed); free(output); free(name); goto fail;
        }
        int ok = 0;
        if (method == 0 && compressed_size == output_size) {
            memcpy(output, compressed, output_size);
            ok = 1;
        } else if (method == 8) {
            z_stream stream;
            memset(&stream, 0, sizeof(stream));
            stream.next_in = compressed;
            stream.avail_in = compressed_size;
            stream.next_out = output;
            stream.avail_out = output_size;
            if (inflateInit2(&stream, -MAX_WBITS) == Z_OK) {
                ok = inflate(&stream, Z_FINISH) == Z_STREAM_END &&
                     stream.total_out == output_size;
                inflateEnd(&stream);
            }
        }
        if (!ok || crc32(0, output, output_size) != expected_crc ||
            write_file(output_path, output, output_size) < 0) {
            free(compressed); free(output); free(name); goto fail;
        }
        free(compressed); free(output); free(name);
        extracted++;
        if (extracted % 100 == 0) {
            char line[96];
            snprintf(line, sizeof(line), "Caustic data: extracted %u files", extracted);
            debug_log(line);
        }
        sceIoLseek(fd, next_central, SCE_SEEK_SET);
    }
    sceIoClose(fd);
    if (write_file(marker, "ok\n", 3) < 0) return -1;
    char line[128];
    snprintf(line, sizeof(line), "Caustic data: extraction complete (%u new files)", extracted);
    debug_log(line);
    return 0;

fail:
    sceIoClose(fd);
    debug_log("Caustic data: extraction failed; it will retry next launch");
    return -1;
}
