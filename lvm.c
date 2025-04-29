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

#include "lvm.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <libcryptsetup.h>

#define LUKS_MAGIC "LUKS\xba\xbe"
#define LUKS_MAGIC_LEN 6

/* Droidian LVM paths */
#define DROIDIAN_DEVICE "/dev/droidian/droidian-rootfs"
#define DROIDIAN_HEADER "/dev/droidian/droidian-reserved"
#define DROIDIAN_DECRYPTED "/dev/mapper/droidian_encrypted"
#define DROIDIAN_NAME "droidian_encrypted"

/* FuriOS LVM paths */
#define FURIOS_DEVICE "/dev/furios/furios-rootfs"
#define FURIOS_HEADER "/dev/furios/furios-reserved"
#define FURIOS_DECRYPTED "/dev/mapper/furios_encrypted"
#define FURIOS_NAME "furios_encrypted"

#define PASSPHRASE_MAX 256

int volume_group_exists(const char *vg_path) {
    struct stat st;
    return (stat(vg_path, &st) == 0);
}

int is_lv_encrypted_with_luks(const char *device_path, size_t print_bytes) {
    int fd, result;
    unsigned char *buffer;
    ssize_t read_bytes;
    struct stat st;
    const char *actual_device = device_path;
    char *temp_device = NULL;
    int vg_type = 0; /* 0 = not determined, 1 = droidian, 2 = furios */

    /* If no device path specified, use header from available VG */
    if (device_path == NULL) {
        if (volume_group_exists("/dev/droidian")) {
            temp_device = strdup(DROIDIAN_HEADER);
            vg_type = 1;
        } else if (volume_group_exists("/dev/furios")) {
            temp_device = strdup(FURIOS_HEADER);
            vg_type = 2;
        } else {
            fprintf(stderr, "No volume group found\n");
            return -1;
        }
        actual_device = temp_device;
    }

    /* Check if either decrypted device already exists */
    if ((vg_type == 0 || vg_type == 1) && stat(DROIDIAN_DECRYPTED, &st) == 0) {
        if (temp_device)
            free(temp_device);
        return 0; /* Already decrypted */
    }

    if ((vg_type == 0 || vg_type == 2) && stat(FURIOS_DECRYPTED, &st) == 0) {
        if (temp_device)
            free(temp_device);
        return 0; /* Already decrypted */
    }

    fd = open(actual_device, O_RDONLY);
    if (fd == -1) {
        perror("Error opening device");
        if (temp_device)
            free(temp_device);
        return -1;
    }

    buffer = malloc(print_bytes);
    if (buffer == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        close(fd);
        if (temp_device)
            free(temp_device);
        return -1;
    }

    read_bytes = read(fd, buffer, print_bytes);
    if (read_bytes == -1) {
        perror("Error reading device");
        close(fd);
        free(buffer);
        if (temp_device)
            free(temp_device);
        return -1;
    }

    result = (memcmp(buffer, LUKS_MAGIC, LUKS_MAGIC_LEN - 1) == 0) ? 1 : 0;

    close(fd);
    free(buffer);
    if (temp_device)
        free(temp_device);

    return result;
}

int mount_luks_lvm(const char *passphrase, int vg_type) {
    struct crypt_device *cd = NULL;
    int result;
    const char *device_path;
    const char *name;

    /* Determine which VG to use */
    if (vg_type == 0) {
        /* Auto-detect which VG to use */
        if (volume_group_exists("/dev/droidian")) {
            device_path = DROIDIAN_DEVICE;
            name = DROIDIAN_NAME;
        } else if (volume_group_exists("/dev/furios")) {
            device_path = FURIOS_DEVICE;
            name = FURIOS_NAME;
        } else {
            fprintf(stderr, "No valid volume group found\n");
            return EXIT_FAILURE;
        }
    } else if (vg_type == 1) {
        device_path = DROIDIAN_DEVICE;
        name = DROIDIAN_NAME;
    } else if (vg_type == 2) {
        device_path = FURIOS_DEVICE;
        name = FURIOS_NAME;
    } else {
        fprintf(stderr, "Invalid volume group selection\n");
        return EXIT_FAILURE;
    }

    result = crypt_init(&cd, device_path);
    if (result < 0) {
        fprintf(stderr, "crypt_init() failed: %s\n", strerror(-result));
        return EXIT_FAILURE;
    }

    /* Load the LUKS header */
    result = crypt_load(cd, CRYPT_LUKS1, NULL);
    if (result < 0) {
        fprintf(stderr, "crypt_load() failed: %s\n", strerror(-result));
        crypt_free(cd);
        return EXIT_FAILURE;
    }

    result = crypt_activate_by_passphrase(cd, name, CRYPT_ANY_SLOT,
                                          passphrase, strlen(passphrase), 0);
    if (result < 0) {
        fprintf(stderr, "Activation failed: Incorrect passphrase or other error.\n");
        crypt_free(cd);
        return 2;
    }

    printf("LUKS device %s activated successfully.\n", name);
    crypt_free(cd);
    return EXIT_SUCCESS;
}


int mount_luks_lvm_helper(const char *passphrase, int vg_type) {
    if (passphrase == NULL || strlen(passphrase) >= PASSPHRASE_MAX)
        return EXIT_FAILURE;

    const char *device_path;
    const char *header_path;
    const char *name;
    char *helper_path = NULL;

    /* Determine which VG to use */
    if (vg_type == 0) {
        /* Auto-detect which VG to use */
        if (volume_group_exists("/dev/droidian")) {
            device_path = DROIDIAN_DEVICE;
            header_path = DROIDIAN_HEADER;
            name = DROIDIAN_NAME;
            vg_type = 1;
        } else if (volume_group_exists("/dev/furios")) {
            device_path = FURIOS_DEVICE;
            header_path = FURIOS_HEADER;
            name = FURIOS_NAME;
            vg_type = 2;
        } else {
            fprintf(stderr, "No valid volume group found\n");
            return EXIT_FAILURE;
        }
    } else if (vg_type == 1) {
        device_path = DROIDIAN_DEVICE;
        header_path = DROIDIAN_HEADER;
        name = DROIDIAN_NAME;
    } else if (vg_type == 2) {
        device_path = FURIOS_DEVICE;
        header_path = FURIOS_HEADER;
        name = FURIOS_NAME;
    } else {
        fprintf(stderr, "Invalid volume group selection\n");
        return EXIT_FAILURE;
    }

    const char *helper_name = NULL;
    helper_path = find_binary("crypted-helper");
    if (helper_path != NULL) {
        helper_name = "crypted-helper";
    } else {
        helper_path = find_binary("droidian-encryption-helper");
        if (helper_path != NULL) {
            helper_name = "droidian-encryption-helper";
        } else {
            fprintf(stderr, "No encryption helper found\n");
            return EXIT_FAILURE;
        }
    }

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return EXIT_FAILURE;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return EXIT_FAILURE;
    }

    if (pid == 0) {
        close(pipefd[1]);
        if (dup2(pipefd[0], STDIN_FILENO) == -1) {
            perror("dup2");
            close(pipefd[0]);
            return EXIT_FAILURE;
        }
        close(pipefd[0]);

        execlp(helper_name, helper_name,
               "--device", device_path,
               "--header", header_path,
               "--name", name,
               "--strip-newlines",
               (char *)NULL);
        perror("execlp");
        return EXIT_FAILURE;
    } else {
        close(pipefd[0]);
        write(pipefd[1], passphrase, strlen(passphrase));
        close(pipefd[1]);

        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            int exit_code = WEXITSTATUS(status);
            if (exit_code == 2)
                return 2;
            return exit_code == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
        } else {
            return EXIT_FAILURE;
        }
    }
}
