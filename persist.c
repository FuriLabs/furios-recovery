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

#include "persist.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/reboot.h>

void execute_ubports_action(void) {
    printf("Executing UBports action\n");

    struct stat st;

    if (stat("/etc/plymouth", &st) != 0) {
        printf("Creating /etc/plymouth directory\n");
        if (mkdir("/etc/plymouth", 0755) != 0)
            printf("Failed to create /etc/plymouth directory: %s\n", strerror(errno));
    }

    FILE* conf_file = fopen("/etc/plymouth/plymouthd.conf", "w");
    if (conf_file != NULL) {
        printf("Writing Plymouth configuration\n");
        fprintf(conf_file, "[Daemon]\nTheme=ubports\n");
        fclose(conf_file);
    } else {
        printf("Failed to write Plymouth configuration: %s\n", strerror(errno));
    }

    if (stat("/run/plymouth", &st) != 0) {
        printf("Creating /run/plymouth directory\n");
        if (mkdir("/run/plymouth", 0755) != 0)
            printf("Failed to create /run/plymouth directory\n");
    }

    if (access("/usr/sbin/plymouthd", X_OK) == 0) {
        printf("Starting plymouth daemon\n");
        setenv("PLYMOUTH_FORCE_SCALE", "1", 1);
        system("/usr/sbin/plymouthd --mode=boot --attach-to-session --pid-file=/run/plymouth/pid --ignore-serial-consoles --kernel-command-line \"splash plymouth.ignore-udev\"");
    } else {
        printf("/usr/sbin/plymouthd not found\n");
    }

    if (access("/usr/bin/plymouth", X_OK) == 0) {
        printf("Showing plymouth splash\n");
        setenv("PLYMOUTH_FORCE_SCALE", "1", 1);
        system("/usr/bin/plymouth --show-splash");
    } else {
        printf("/usr/bin/plymouth not found\n");
    }

    printf("Creating symbolic link for cache\n");
    unlink("/cache");
    if (symlink("/ubuntu-userdata/cache", "/cache") != 0)
        printf("Failed to create symbolic link to /cache: %s\n", strerror(errno));

    if (access("/scripts/system-image-upgrader", X_OK) == 0) {
        printf("Running system-image-upgrader\n");
        system("/scripts/system-image-upgrader /cache/recovery/ubuntu_command");
    } else {
        printf("/scripts/system-image-upgrader not found\n");
    }

    sync();
    reboot(RB_AUTOBOOT);
}

int is_ubports_action(void) {
    if (access("/furios-persist/bootman/ubuntu-userdata", F_OK) != 0) {
        printf("UBports user data config does not exist\n");
        return 0;
    }

    printf("Found UBports user data config file\n");

    struct stat st;
    if (stat("/dev/droidian/ubuntu-userdata", &st) != 0 &&
        stat("/dev/furios/ubuntu-userdata", &st) != 0) {
        printf("Partition path for ubuntu-userdata does not exist\n");
        return 0;
    }

    if (mkdir("/ubuntu-userdata", 0755) != 0 && errno != EEXIST) {
        printf("Failed to create /ubuntu-userdata directory\n");
        return 0;
    }

    if (is_mounted("/ubuntu-userdata")) {
        printf("/ubuntu-userdata is already mounted\n");
    } else {
        /* Try both volume groups */
        if (stat("/dev/droidian/ubuntu-userdata", &st) == 0) {
            if (mount("/dev/droidian/ubuntu-userdata", "/ubuntu-userdata", "ext4", 0, NULL) != 0) {
                printf("Failed to mount droidian ubuntu-userdata to /ubuntu-userdata\n");
                /* Try furios VG if droidian fails */
                if (stat("/dev/furios/ubuntu-userdata", &st) == 0) {
                    if (mount("/dev/furios/ubuntu-userdata", "/ubuntu-userdata", "ext4", 0, NULL) != 0) {
                        printf("Failed to mount furios ubuntu-userdata to /ubuntu-userdata\n");
                        return 0;
                    }
                } else {
                    return 0;
                }
            }
        } else if (stat("/dev/furios/ubuntu-userdata", &st) == 0) {
            if (mount("/dev/furios/ubuntu-userdata", "/ubuntu-userdata", "ext4", 0, NULL) != 0) {
                printf("Failed to mount furios ubuntu-userdata to /ubuntu-userdata\n");
                return 0;
            }
        } else {
            return 0;
        }

        printf("Successfully mounted ubuntu-userdata to /ubuntu-userdata\n");
    }

    if (access("/ubuntu-userdata/cache/recovery/ubuntu_command", F_OK) != 0) {
        printf("Ubuntu command file does not exist\n");
        return 0;
    }

    printf("UBports action detected\n");
    return 1;
}

int mount_furios_persist(const char* partition) {
    if (mkdir("/furios-persist", 0755) != 0 && errno != EEXIST) {
        printf("Failed to create /furios-persist directory\n");
        return 0;
    }

    if (is_mounted("/furios-persist")) {
        printf("/furios-persist is already mounted\n");
        return 1;
    }

    if (mount(partition, "/furios-persist", "ext4", 0, NULL) != 0) {
        printf("Failed to mount %s to /furios-persist\n", partition);
        return 0;
    }

    printf("Successfully mounted %s to /furios-persist\n", partition);
    return 1;
}

char* get_slot_suffix() {
    FILE* cmdline = fopen("/proc/cmdline", "r");
    if (cmdline == NULL) {
        perror("Error opening /proc/cmdline");
        return NULL;
    }

    char buffer[1024];
    char* result = NULL;
    if (fgets(buffer, sizeof(buffer), cmdline) != NULL) {
        char* token = strstr(buffer, "androidboot.slot_suffix=");
        if (token != NULL) {
            token += strlen("androidboot.slot_suffix=");
            result = malloc(3 * sizeof(char));
            if (result != NULL) {
                strncpy(result, token, 2);
                result[2] = '\0';
            }
        }
    }

    fclose(cmdline);
    return result;
}
