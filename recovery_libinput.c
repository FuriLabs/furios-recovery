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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <sys/ioctl.h>

#include <linux/input.h>
#include <libinput.h>

#include "recovery_libinput.h"

static lv_obj_t **nav_buttons = NULL;
static int nav_button_count = 0;
static int current_button_index = 0;
pthread_t key_thread;
volatile bool key_thread_running = true;

static int open_restricted(const char *path, int flags, void *user_data) {
    (void)user_data;
    int fd = open(path, flags);
    return fd < 0 ? -errno : fd;
}

static void close_restricted(int fd, void *user_data) {
    (void)user_data;
    close(fd);
}

static const struct libinput_interface interface = {
    .open_restricted = open_restricted,
    .close_restricted = close_restricted,
};

void init_button_navigation(int total_buttons) {
    if (nav_buttons != NULL)
        free(nav_buttons);

    nav_buttons = calloc(total_buttons, sizeof(lv_obj_t *));
    nav_button_count = total_buttons;
    current_button_index = 0;

    printf("Initialized navigation for %d buttons\n", total_buttons);
}

void register_nav_button(lv_obj_t *btn, int index) {
    if (index >= 0 && index < nav_button_count)
        nav_buttons[index] = btn;
}

void clear_nav_buttons(void) {
    if (nav_buttons != NULL) {
        free(nav_buttons);
        nav_buttons = NULL;
        nav_button_count = 0;
    }
}

void update_button_highlight(void) {
    /* Remove highlight from all buttons first */
    for (int i = 0; i < nav_button_count; i++) {
        if (nav_buttons[i] != NULL)
            lv_obj_clear_state(nav_buttons[i], LV_STATE_FOCUSED);
    }

    /* Add highlight to current button */
    if (current_button_index >= 0 && current_button_index < nav_button_count &&
        nav_buttons[current_button_index] != NULL) {
        lv_obj_add_state(nav_buttons[current_button_index], LV_STATE_FOCUSED);
        printf("Button %d highlighted\n", current_button_index);
    }
}

int is_input_device(const char *path) {
    int fd;
    char name[256];

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;

    if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) {
        close(fd);
        return 0;
    }

    close(fd);
    return 1;
}

void* key_input_thread(void *arg) {
    (void)arg;
    struct libinput *li;
    struct libinput_event *event;
    int rc;

    li = libinput_path_create_context(&interface, NULL);
    if (!li) {
        fprintf(stderr, "Failed to initialize libinput context\n");
        return NULL;
    }

    DIR *dir;
    struct dirent *entry;
    char path[PATH_MAX];

    dir = opendir("/dev/input");
    if (!dir) {
        fprintf(stderr, "Failed to open /dev/input directory\n");
        libinput_unref(li);
        return NULL;
    }

    int device_count = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) == 0) {
            snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
            if (is_input_device(path)) {
                struct libinput_device *device;
                device = libinput_path_add_device(li, path);
                if (!device) {
                    fprintf(stderr, "Failed to add device: %s\n", path);
                } else {
                    printf("Added input device: %s\n", path);
                    device_count++;
                }
            }
        }
    }

    closedir(dir);

    if (device_count == 0) {
        fprintf(stderr, "No input devices were added\n");
        libinput_unref(li);
        return NULL;
    }

    printf("Monitoring %d input devices for key events\n", device_count);

    libinput_dispatch(li);

    while (key_thread_running) {
        int fd = libinput_get_fd(li);
        fd_set fds;

        FD_ZERO(&fds);
        FD_SET(fd, &fds);

        rc = select(fd + 1, &fds, NULL, NULL, NULL);
        if (rc < 0 && errno != EINTR) {
            fprintf(stderr, "select() failed: %s\n", strerror(errno));
            break;
        }

        if (rc > 0 && FD_ISSET(fd, &fds)) {
            libinput_dispatch(li);

            while ((event = libinput_get_event(li))) {
                if (libinput_event_get_type(event) == LIBINPUT_EVENT_KEYBOARD_KEY) {
                    struct libinput_event_keyboard *key_event;
                    enum libinput_key_state state;
                    uint32_t key;

                    key_event = libinput_event_get_keyboard_event(event);
                    key = libinput_event_keyboard_get_key(key_event);
                    state = libinput_event_keyboard_get_key_state(key_event);

                    struct libinput_device *device = libinput_event_get_device(event);
                    const char *device_name = libinput_device_get_name(device);

                    printf("Key event from '%s': key=%d, state=%d\n",
                           device_name, key, state);
                    if (state == LIBINPUT_KEY_STATE_PRESSED) {
                        switch (key) {
                            case KEY_VOLUMEUP:
                                current_button_index = (current_button_index + nav_button_count - 1) % nav_button_count;
                                update_button_highlight();
                                break;
                            case KEY_VOLUMEDOWN:
                                current_button_index = (current_button_index + 1) % nav_button_count;
                                update_button_highlight();
                                break;
                            case KEY_POWER:
                                if (current_button_index >= 0 && current_button_index < nav_button_count &&
                                    nav_buttons[current_button_index] != NULL) {
                                    lv_event_send(nav_buttons[current_button_index], LV_EVENT_CLICKED, NULL);
                                }
                                break;
                        }
                    }
                }
                libinput_event_destroy(event);
            }
        }
    }

    libinput_unref(li);
    return NULL;
}
