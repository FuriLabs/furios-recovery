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

#ifndef LVM_H
#define LVM_H

#include <stdlib.h>

/* Droidian/FuriOS VG identifiers for functions that take vg_type parameter */
#define VG_AUTO_DETECT 0
#define VG_DROIDIAN    1
#define VG_FURIOS      2

typedef enum {
    LUKS_STATE_ERROR = -1,
    LUKS_STATE_NOT_ENCRYPTED = 0,
    LUKS_STATE_ENCRYPTED_LOCKED = 1,
    LUKS_STATE_ENCRYPTED_UNLOCKED = 2,
} luks_state_t;

/**
 * Check if a volume group exists
 *
 * @param vg_path Path to the volume group
 * @return 1 if exists, 0 if not
 */
int volume_group_exists(const char *vg_path);

/**
 * Check if LV is encrypted with LUKS
 *
 * @param device_path path to the logical volume to check (can be NULL to auto-detect)
 * @param print_bytes number of bytes to read for checking
 * @return 1 if encrypted, 2 if unlocked, 1 if not encrypted, -1 on error
 */
luks_state_t is_lv_encrypted_with_luks(const char *device_path, size_t print_bytes);

/**
 * Helper function for mounting LUKS LVM
 *
 * @param passphrase The passphrase to use for decryption
 * @param vg_type VG type: 0=auto-detect, 1=droidian, 2=furios
 * @return EXIT_SUCCESS on success, EXIT_FAILURE or error code on failure
 */
int mount_luks_lvm_helper(const char *passphrase, int vg_type);

/**
 * Remove encrypted device-mapper nodes
 *
 * @return 0 on success, non-zero on failure
 */
int dmsetup_remove_encrypted(void);

/**
 * Refresh LVM state (deactivate + rescan + reactivate)
 *
 * @return 0 on success, non-zero on failure
 */
int refresh_lvm(void);

#endif // LVM_H
