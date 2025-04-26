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

#ifndef UTILS_H
#define UTILS_H

/**
 * Read a value from given path and return the value
 *
 * @param path is the path of the file
 * @param default_value is the default value if there was an error
 * @return integer value read from file or default value on error
 */
int read_int_from_file(const char *path, int default_value);

/**
 * Write a value to a given path
 *
 * @param path is the path of the file
 * @param value is the value requested for writing
 * @return 0 on success, -1 on error
 */
int write_int_to_file(const char *path, int value);

/**
 * Check if a path is a mount point
 *
 * @param mount_point path to check against /proc/mounts
 * @return 1 if mounted, 0 if not mounted
 */
int is_mounted(const char* mount_point);

/**
 * Read the first DT compatible entry
 *
 * @return string containing the first DT compatible entry or NULL on error
 */
char* read_dt_compatible(void);

#endif // UTILS_H
