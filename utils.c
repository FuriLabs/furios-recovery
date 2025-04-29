/**
 * Copyright 2025 Bardia Moshiri
 *
 * This file is part of furios-recovery, hereafter referred to as the program.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <mntent.h>

#define DT_COMPATIBLE_PATH "/sys/firmware/devicetree/base/compatible"

int read_int_from_file(const char *path, int default_value) {
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        printf("File not found: %s\n", path);
        return default_value;
    }

    char buffer[20];
    if (fgets(buffer, sizeof(buffer), file) != NULL) {
        fclose(file);
        buffer[strcspn(buffer, "\n")] = 0;

        char *endptr;
        long value = strtol(buffer, &endptr, 10);
        if (*endptr == '\0' && value >= 0)
            return (int)value;
    }

    fclose(file);
    return default_value;
}

int write_int_to_file(const char *path, int value) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        printf("Failed to open file for writing: %s (Error: %s)\n", path, strerror(errno));
        return -1;
    }

    int result = fprintf(file, "%d", value);
    fclose(file);

    if (result < 0) {
        printf("Failed to write to file: %s (Error: %s)\n", path, strerror(errno));
        return -1;
    }

    return 0;
}

int is_mounted(const char* mount_point) {
    FILE* mtab = setmntent("/proc/mounts", "r");
    struct mntent* entry;
    int mounted = 0;

    if (mtab == NULL) {
        printf("Could not open /proc/mounts\n");
        return 0;
    }

    while ((entry = getmntent(mtab)) != NULL) {
        if (strcmp(entry->mnt_dir, mount_point) == 0) {
            mounted = 1;
            break;
        }
    }

    endmntent(mtab);
    return mounted;
}

char* read_dt_compatible() {
    FILE* file = fopen(DT_COMPATIBLE_PATH, "r");
    if (file == NULL) {
        printf("Error opening device tree file: %s\n", DT_COMPATIBLE_PATH);
        return NULL;
    }

    char buffer[512] = {0};
    size_t bytes_read = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);

    if (bytes_read == 0) {
        printf("Error reading device tree file or file is empty\n");
        return NULL;
    }

    /* Device tree compatible strings are null-terminated
     * We need to find the first entry which ends at the first null byte */
    char* first_entry = malloc(bytes_read + 1);
    if (first_entry == NULL) {
        printf("Memory allocation failed\n");
        return NULL;
    }

    /* Copy until first null byte */
    size_t i;
    for (i = 0; i < bytes_read && buffer[i] != '\0'; i++) {
        first_entry[i] = buffer[i];
    }

    first_entry[i] = '\0';

    return first_entry;
}

char* find_binary(const char *binary_name) {
    const char *paths[] = {"/usr/bin", "/usr/sbin", "/bin", "/sbin"};
    static char full_path[256];

    for (int i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        snprintf(full_path, sizeof(full_path), "%s/%s", paths[i], binary_name);

        struct stat st;
        if (stat(full_path, &st) == 0 && (st.st_mode & S_IXUSR))
            return full_path;
    }

    return NULL;
}
