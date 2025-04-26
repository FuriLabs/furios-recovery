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

#ifndef RESET_H
#define RESET_H

/**
 * Drop all caches on device
 *
 * @return 0 on success, -1 on failure
 */
int drop_caches(void);

/**
 * Factory resets the device
 *
 * @return 0 on success, -1 on failure
 */
int factory_reset(void);

/**
 * Find and flash boot/dtbo images from a mounted rootfs
 *
 * @param mount_path Path where the rootfs is mounted
 * @param slot_suffix Current slot suffix for A/B devices
 */
void find_boot_images(const char *mount_path, const char *slot_suffix);

#endif // RESET_H
