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

#ifndef PERSIST_H
#define PERSIST_H

/**
 * Execute a UBports action (such as an update or factory reset)
 */
void execute_ubports_action(void);

/**
 * Check if a UBports action (such as an update or factory reset) should be performed
 *
 * @return 1 if UBports action should be performed, 0 otherwise
 */
int is_ubports_action(void);

/**
 * Mount FuriOS persist partition
 *
 * @param partition Path to the partition
 * @return 1 if mount succeeded, 0 if failed
 */
int mount_furios_persist(const char* partition);

/**
 * Get slot suffix from kernel cmdline
 *
 * @return String containing slot suffix or NULL if not found
 */
char* get_slot_suffix(void);

#endif // PERSIST_H
