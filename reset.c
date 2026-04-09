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

#include "reset.h"
#include "persist.h"
#include "utils.h"
#include "lvm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <dirent.h>

int drop_caches() {
    int fd = open("/proc/sys/vm/drop_caches", O_WRONLY);
    if (fd == -1) {
        perror("Failed to open /proc/sys/vm/drop_caches");
        return -1;
    }

    if (write(fd, "1", 1) != 1) {
        perror("Failed to write to /proc/sys/vm/drop_caches");
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

int factory_reset(void) {
    /* the reason most things here are system calls is because our ramdisk must be small and more libraries we link against the bigger the binary will get
     * here, we're using pre existing binaries in the ramdisk to not take too much storage in the ramdisk */
    struct stat buffer;
    int result;
    char cmd[1024];
    char* slot_suffix = get_slot_suffix();

    /* If no slot suffix is found, default to an empty string so that single slot devices can work */
    if (slot_suffix == NULL)
        slot_suffix = strdup("");

    drop_caches(); /* tar will fill up cache, has to be cleared before writing */

    if (stat("/dev/disk/by-partlabel/super", &buffer) == 0) {
        /* if system_a doesn't exist */
        if (stat("/dev/mapper/dynpart-system_a", &buffer) != 0) {
            /* if system_b doesn't exist */
            if (stat("/dev/mapper/dynpart-system_b", &buffer) != 0) {
                snprintf(cmd, sizeof(cmd), "dmsetup create --concise \"$(parse-android-dynparts /dev/disk/by-partlabel/super)\"");
                system(cmd);
            }
        }
    }

    mkdir("/system_mnt", 0755);
    if (stat("/dev/mapper/dynpart-system_a", &buffer) == 0) {
        result = mount("/dev/mapper/dynpart-system_a", "/system_mnt", "ext4", 0, NULL);
        if (result != 0) {
            printf("Failed to mount dynpart-system_a\n");
            free(slot_suffix);
            return -1;
        }
    } else if (stat("/dev/mapper/dynpart-system_b", &buffer) == 0) {
        result = mount("/dev/mapper/dynpart-system_b", "/system_mnt", "ext4", 0, NULL);
        if (result != 0) {
            printf("Failed to mount dynpart-system_b\n");
            free(slot_suffix);
            return -1;
        }
    } else {
        printf("Failed to mount dynpart-system, block device doesn't not exist\n");
        free(slot_suffix);
        return -1;
    }

    if (stat("/system_mnt/userdata.img.tar.gz", &buffer) == 0) {
        snprintf(cmd, sizeof(cmd), "tar -xzOf /system_mnt/userdata.img.tar.gz | dd of=/dev/disk/by-partlabel/userdata bs=4M");
    } else if (stat("/system_mnt/userdata-raw.img.tar.gz", &buffer) == 0) {
        snprintf(cmd, sizeof(cmd), "tar -xzOf /system_mnt/userdata-raw.img.tar.gz | dd of=/dev/disk/by-partlabel/userdata bs=4M");
    } else {
        printf("Failed to find userdata archive\n");
        umount("/system_mnt");
        free(slot_suffix);
        return -1;
    }

    result = system(cmd);
    if (result != 0) {
        printf("Failed to extract and write userdata\n");
        umount("/system_mnt");
        free(slot_suffix);
        return -1;
    }

    sync();
    dmsetup_remove_encrypted();
    refresh_lvm();

    if (stat("/system_mnt/boot.img", &buffer) == 0) {
        snprintf(cmd, sizeof(cmd),
                 "dd if=/system_mnt/boot.img of=/dev/disk/by-partlabel/boot%s bs=4M",
                 slot_suffix);
        result = system(cmd);
        if (result != 0) {
            printf("Failed to flash boot image%s%s\n",
                   *slot_suffix ? " to slot suffix " : "",
                   *slot_suffix ? slot_suffix : "");
        } else {
            printf("Flashed boot.img from /system_mnt\n");
        }
    } else {
        printf("No /system_mnt/boot.img found.\n");
    }

    if (stat("/system_mnt/dtbo.img", &buffer) == 0) {
        snprintf(cmd, sizeof(cmd),
                 "dd if=/system_mnt/dtbo.img of=/dev/disk/by-partlabel/dtbo%s bs=4M",
                 slot_suffix);
        result = system(cmd);
        if (result != 0) {
            printf("Failed to flash dtbo image%s%s\n",
                   *slot_suffix ? " to slot suffix " : "",
                   *slot_suffix ? slot_suffix : "");
        } else {
            printf("Flashed dtbo.img from /system_mnt\n");
        }
    } else {
        printf("No /system_mnt/dtbo.img found.\n");
    }

    if (stat("/system_mnt/boot.img", &buffer) != 0 ||
        stat("/system_mnt/dtbo.img", &buffer) != 0) {
        /* Try to find boot images in LVM volume groups */
        if (volume_group_exists("/dev/droidian") &&
            stat("/dev/mapper/droidian-droidian--rootfs", &buffer) == 0) {
            mkdir("/rootfs_mnt", 0755);

            result = mount("/dev/mapper/droidian-droidian--rootfs", "/rootfs_mnt", "ext4", 0, NULL);
            if (result != 0) {
                printf("Failed to mount droidian-droidian--rootfs\n");
                umount("/system_mnt");
                free(slot_suffix);
                return -1;
            }

            flash_images("/rootfs_mnt", slot_suffix);
            umount("/rootfs_mnt");
        } else if (volume_group_exists("/dev/furios") &&
                   stat("/dev/mapper/furios-furios--rootfs", &buffer) == 0) {
            mkdir("/rootfs_mnt", 0755);

            result = mount("/dev/mapper/furios-furios--rootfs", "/rootfs_mnt", "ext4", 0, NULL);
            if (result != 0) {
                printf("Failed to mount furios-furios--rootfs\n");
                umount("/system_mnt");
                free(slot_suffix);
                return -1;
            }

            flash_images("/rootfs_mnt", slot_suffix);
            umount("/rootfs_mnt");
        } else {
            printf("No LVM volume groups found to flash images\n");
        }
    }

    umount("/system_mnt");

    /* Wipe bootman configuration files */
    char* dt_compatible = read_dt_compatible();

    if (dt_compatible != NULL) {
        printf("DT compatible %s\n", dt_compatible);

        if (strcmp(dt_compatible, "furilabs,flx1") == 0 || strcmp(dt_compatible, "furilabs,flx1s") == 0) {
            printf("Detected supported furilabs device (%s), formatting vendor_boot_a partition\n", dt_compatible);

            snprintf(cmd, sizeof(cmd), "mke2fs -b 4096 /dev/disk/by-partlabel/vendor_boot_a");
            result = system(cmd);

            if (result != 0)
                printf("Failed to format vendor_boot_a partition\n");
            else
                printf("Successfully formatted vendor_boot_a partition\n");
        }

        free(dt_compatible);
    }

    drop_caches();
    free(slot_suffix);
    return 0;
}

void flash_images(const char *mount_path, const char *slot_suffix) {
    char bootimg_file[256] = "";
    char dtboimg_file[256] = "";
    char vendorimg_path[PATH_MAX];
    char boot_dir_path[PATH_MAX];
    char cmd[5120];
    int result;
    struct stat buffer;

    snprintf(boot_dir_path, sizeof(boot_dir_path), "%s/boot", mount_path);
    DIR *dir = opendir(boot_dir_path);
    if (dir == NULL) {
        printf("Failed to opendir %s/boot\n", mount_path);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "boot.img", strlen("boot.img")) == 0) {
            strncpy(bootimg_file, entry->d_name, sizeof(bootimg_file) - 1);
            bootimg_file[sizeof(bootimg_file) - 1] = '\0';
            break;
        }
    }

    rewinddir(dir);
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "dtbo.img", strlen("dtbo.img")) == 0) {
            strncpy(dtboimg_file, entry->d_name, sizeof(dtboimg_file) - 1);
            dtboimg_file[sizeof(dtboimg_file) - 1] = '\0';
            break;
        }
    }

    closedir(dir);

    if (bootimg_file[0] != '\0') {
        char boot_path[512];
        snprintf(boot_path, sizeof(boot_path), "%s/boot/%s", mount_path, bootimg_file);

        snprintf(cmd, sizeof(cmd),
                 "dd if=\"%s\" of=\"/dev/disk/by-partlabel/boot%s\" bs=4M",
                 boot_path, slot_suffix);

        result = system(cmd);
        if (result != 0) {
            printf("Failed to flash boot image%s%s\n",
                   *slot_suffix ? " to slot suffix " : "",
                   *slot_suffix ? slot_suffix : "");
        } else {
            printf("Flashed boot.img from %s\n", mount_path);
        }
    } else {
        printf("Failed to find boot image in %s\n", mount_path);
    }

    if (dtboimg_file[0] != '\0') {
        char dtbo_path[512];
        snprintf(dtbo_path, sizeof(dtbo_path), "%s/boot/%s", mount_path, dtboimg_file);

        snprintf(cmd, sizeof(cmd),
                 "dd if=\"%s\" of=\"/dev/disk/by-partlabel/dtbo%s\" bs=4M",
                 dtbo_path, slot_suffix);

        result = system(cmd);
        if (result != 0) {
            printf("Failed to flash dtbo image%s%s\n",
                   *slot_suffix ? " to slot suffix " : "",
                   *slot_suffix ? slot_suffix : "");
        } else {
            printf("Flashed dtbo.img from %s\n", mount_path);
        }
    } else {
        printf("Failed to find dtbo image in %s\n", mount_path);
    }

    snprintf(vendorimg_path, sizeof(vendorimg_path), "%s/usr/share/vendor-image/vendor.img", mount_path);
    if (stat(vendorimg_path, &buffer) == 0) {
        printf("Found vendor.img, attempting to flash\n");
        int flashed = 0;

        if (stat("/dev/mapper/dynpart-vendor_a", &buffer) == 0) {
            snprintf(cmd, sizeof(cmd),
                     "dd if=\"%s\" of=/dev/mapper/dynpart-vendor_a bs=4M",
                     vendorimg_path);
            result = system(cmd);
            if (result != 0) {
                printf("Failed to flash vendor.img to dynpart-vendor_a\n");
            } else {
                printf("Flashed vendor.img to dynpart-vendor_a\n");
                flashed = 1;
            }
        }

        if (stat("/dev/mapper/dynpart-vendor_b", &buffer) == 0) {
            snprintf(cmd, sizeof(cmd),
                     "dd if=\"%s\" of=/dev/mapper/dynpart-vendor_b bs=4M",
                     vendorimg_path);
            result = system(cmd);
            if (result != 0) {
                printf("Failed to flash vendor.img to dynpart-vendor_b\n");
            } else {
                printf("Flashed vendor.img to dynpart-vendor_b\n");
                flashed = 1;
            }
        }

        if (!flashed)
            printf("vendor.img found but no dynpart-vendor_a or dynpart-vendor_b exists\n");
    } else {
        printf("No vendor.img found under %s, skipping vendor flash\n", mount_path);
    }
}
