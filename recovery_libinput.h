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

#ifndef RECOVERY_LIBINPUT_H
#define RECOVERY_LIBINPUT_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "lvgl/lvgl.h"

extern pthread_t key_thread;
extern volatile bool key_thread_running;

/**
 * Initialize button navigation
 *
 * @param total_buttons Total number of buttons for navigation
 */
void init_button_navigation(int total_buttons);

/**
 * Update button highlighting
 */
void update_button_highlight(void);

/**
 * Check if a file is an input device
 *
 * @param path Path to the input device
 * @return 1 if it's an input device, 0 otherwise
 */
int is_input_device(const char *path);

/**
 * Initialize libinput and monitor for key events
 *
 * @param arg *arg is unused
 */
void* key_input_thread(void *arg);

/**
 * Register a navigation button
 *
 * @param btn Button to register
 * @param index Index in the navigation array
 */
void register_nav_button(lv_obj_t *btn, int index);

/**
 * Clear all navigation buttons
 */
void clear_nav_buttons(void);

#endif // RECOVERY_LIBINPUT_H
