/**
 * Copyright 2021 Johannes Marbach
 * Copyright 2024 David Badiei
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


#include "backends.h"
#include "command_line.h"
#include "config.h"
#include "indev.h"
#include "furios-recovery.h"
#include "terminal.h"
#include "theme.h"
#include "themes.h"
#include "lvm.h"
#include "utils.h"
#include "recovery_libinput.h"
#include "reset.h"
#include "persist.h"

#include "lv_drv_conf.h"

#if USE_FBDEV
#include "lv_drivers/display/fbdev.h"
#endif /* USE_FBDEV */
#if USE_DRM
#include "lv_drivers/display/drm.h"
#endif /* USE_DRM */
#if USE_MINUI
#include "lv_drivers/display/minui.h"
#endif /* USE_MINUI */

#include "lvgl/lvgl.h"

#include "squeek2lvgl/sq2lv.h"

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <mntent.h>
#include <errno.h>

#include <sys/reboot.h>
#include <sys/wait.h>
#include <sys/time.h>

#define NUM_IMAGES 1
#define MIN_BRIGHTNESS 5
#define BRIGHTNESS_PATH "/sys/class/leds/lcd-backlight/brightness"
#define MAX_BRIGHTNESS_PATH "/sys/class/leds/lcd-backlight/max_brightness"

/**
 * Static variables
 */

cli_opts cli_options;
config_opts conf_opts;

static lv_color_t *buf = NULL;
static lv_disp_draw_buf_t disp_buf;

bool is_alternate_theme = true;
bool is_password_obscured = true;
bool is_keyboard_hidden = true;

bool enabling_ssh = false;
bool mounting_rootfs = false;

/* Main page */
lv_obj_t *container = NULL;
lv_obj_t *label_container = NULL;
lv_obj_t *furios_label_container = NULL;
lv_obj_t *furios_label = NULL;
lv_obj_t *keyboard = NULL;
lv_obj_t *ip_label_container = NULL;
lv_obj_t *ip_label = NULL;
lv_obj_t *reboot_btn = NULL;
lv_obj_t *shutdown_btn = NULL;
lv_obj_t *factory_reset_btn = NULL;
lv_obj_t *theme_btn = NULL;
lv_obj_t *ssh_btn = NULL;
lv_obj_t *ssh_btn_label = NULL;
lv_obj_t *mount_rootfs_btn = NULL;
lv_obj_t *mount_rootfs_btn_label = NULL;
lv_obj_t *terminal_btn = NULL;
lv_obj_t *brightness_slider = NULL;
lv_obj_t *brightness_label = NULL;

/* Decryption page */
lv_obj_t *decrypt_container = NULL;
lv_obj_t *spangroup = NULL;
lv_obj_t *textarea_container = NULL;
lv_obj_t *textarea = NULL;
lv_obj_t *toggle_pw_btn = NULL;
lv_obj_t *toggle_kb_btn = NULL;

/* Images */
LV_IMG_DECLARE(furilabs_white)
LV_IMG_DECLARE(furilabs_black)

const void *darkmode_imgs[] = {&furilabs_white};
const void *lightmode_imgs[] = {&furilabs_black};
lv_obj_t* images[1];

/**
 * Static prototypes
 */

/**
 * Handle LV_EVENT_CLICKED events from the theme toggle button.
 *
 * @param event the event object
 */
static void toggle_theme_btn_clicked_cb(lv_event_t *event);

/**
 * Handle LV_EVENT_CLICKED events from the ssh toggle button.
 *
 * @param event the event object
 */
static void toggle_ssh_btn_clicked_cb(lv_event_t *event);

/**
 * Handle LV_EVENT_CLICKED events from the mount rootfs toggle button.
 *
 * @param event the event object
 */
static void toggle_mount_rootfs_btn_clicked_cb(lv_event_t *event);

/**
 * Enable or disable SSH access
 */
static void enable_ssh(void);

/**
 * Mount or unmount the rootfs
 */
static void toggle_mount_rootfs(void);

/**
 * Toggle between the light and dark theme.
 */
static void toggle_theme(void);

/**
 * Set the UI theme.
 *
 * @param is_dark true if the dark theme should be applied, false if the light theme should be applied
 */
static void set_theme(bool is_dark);

/**
 * Set the image mode
 *
 * @param is_dark true if the dark theme should be applied, false if the light theme should be applied
 */
static void update_image_mode(bool is_dark);

/**
 * Handle LV_EVENT_CLICKED events from the show/hide password toggle button.
 *
 * @param event the event object
 */
static void toggle_pw_btn_clicked_cb(lv_event_t *event);

/**
 * Toggle between showing and hiding the password.
 */
static void toggle_password_obscured(void);

/**
 * Show / hide the password.
 *
 * @param is_hidden true if the password should be hidden, false if it should be shown
 */
static void set_password_obscured(bool is_obscured);

/**
 * Handle LV_EVENT_CLICKED events from the show/hide keyboard toggle button.
 *
 * @param event the event object
 */
static void toggle_kb_btn_clicked_cb(lv_event_t *event);

/**
 * Toggle between showing and hiding the keyboard.
 */
static void toggle_keyboard_hidden(void);

/**
 * Show / hide the keyboard
 *
 * @param is_hidden true if the keyboard should be hidden, false if it should be shown
 */
static void set_keyboard_hidden(bool is_hidden);

/**
 * Callback for the keyboard's vertical slide in / out animation.
 *
 * @param obj keyboard widget
 * @param value y position
 */
static void keyboard_anim_y_cb(void *obj, int32_t value);

/**
 * Callback for the brightness slider
 *
 * @param event the event object
 */
static void brightness_slider_changed_cb(lv_event_t *event);

/**
 * Handle LV_EVENT_CLICKED events from the shutdown button.
 *
 * @param event the event object
 */
static void shutdown_btn_clicked_cb(lv_event_t *event);

/**
 * Handle LV_EVENT_VALUE_CHANGED events from the shutdown message box.
 *
 * @param event the event object
 */
static void shutdown_mbox_value_changed_cb(lv_event_t *event);

/**
 * Handle LV_EVENT_CLICKED events from the terminal button.
 *
 * @param event the event object
 */
static void terminal_btn_clicked_cb(lv_event_t *event);

/**
 * Handle LV_EVENT_VALUE_CHANGED events from the terminal message box.
 *
 * @param event the event object
 */
static void terminal_mbox_value_changed_cb(lv_event_t *event);

/**
 * Handle LV_EVENT_CLICKED events from the reboot button.
 *
 * @param event the event object
 */
static void reboot_btn_clicked_cb(lv_event_t *event);

/**
 * Handle LV_EVENT_VALUE_CHANGED events from the reboot message box.
 *
 * @param event the event object
 */
static void reboot_mbox_value_changed_cb(lv_event_t *event);

/**
 * Handle LV_EVENT_CLICKED events from the factory reset button.
 */
static void factory_reset_btn_clicked_cb(lv_event_t *event);

/**
 * Handle LV_EVENT_CLICKED events from the factory reset confirmation button.
 */
static void perform_factory_reset(lv_timer_t *timer);

/**
 * Handle LV_EVENT_VALUE_CHANGED events from the factory reset message box.
 *
 * @param event the event object
 */
static void factory_reset_mbox_value_changed_cb(lv_event_t *event);

/**
 * Handle LV_EVENT_CLICKED events from encryption failed messsage box
 */
static void close_mbox_cb(lv_event_t *event);

/**
 * Handle LV_EVENT_CLICKED events from encryption failed messsage box without rebooting
 */
static void close_only_mbox_cb(lv_event_t *event);

/**
 * Handle LV_EVENT_CLICKED events from encryption maximum attempts messsage box and restore to main screen
 */
static void close_mbox_and_restore_main_screen_cb(lv_event_t *event);

/**
 * Handle LV_EVENT_VALUE_CHANGED events from the keyboard widget.
 *
 * @param event the event object
 */
static void keyboard_value_changed_cb(lv_event_t *event);

/**
 * Handle LV_EVENT_READY events from the textarea widget.
 *
 * @param event the event object
 */
static void textarea_ready_cb(lv_event_t *event);

/**
 * Check password against LVM for SSH access
 *
 * @param textarea the textarea widget
 */
static void check_password_enable_ssh(lv_obj_t *textarea);

/**
 * Check password against LVM for mounting the rootfs
 *
 * @param textarea the textarea widget
 */
static void check_password_mount_rootfs(lv_obj_t *textarea);

/**
 * Check password against LVM for factory reset
 *
 * @param textarea the textarea widget
 */
static void check_password_factory_reset(lv_obj_t *textarea);

/**
 * Handle LV_EVENT_CLICKED events from the password check
 */
static void factory_reset_password(lv_timer_t *timer);

/**
 * Restores the screen from the decryption page
 */
static void restore_main_screen(void);

/**
 * Returns rootfs luks encryption status
 */
static luks_state_t get_rootfs_luks_state(void);

/**
 * Decrypts the device
 */
static void decrypt(void);

/**
 * Reboots the device.
 */
static void reboot_device(void);

/**
 * Shuts down the device.
 */
static void shutdown(void);

/**
 * Launch another instance of recovery before exiting in open_terminal
 *
 * @param unused
 */
static void* run_recovery(void* arg);

/**
 * Opens furios-terminal
 */
static void open_terminal(void);

/**
 * Handle termination signals sent to the process.
 *
 * @param signum the signal's number
 */
static void sigaction_handler(int signum);

/**
 * Create all buttons in the label container
 *
 * @param label container to create buttons in
 */
static void create_buttons(lv_obj_t *label_container);

/**
 * Create main UI
 *
 * @param horizontal resolution
 * @param vertical resolution
 */
static void create_ui(uint32_t hor_res, uint32_t ver_res);

/**
 * Initialize recovery UI
 */
static void initialize_recovery_ui(void);

/**
 * Static functions
 */

static void toggle_theme_btn_clicked_cb(lv_event_t *event) {
    LV_UNUSED(event);
    toggle_theme();
}

static void toggle_theme(void) {
    is_alternate_theme = !is_alternate_theme;

    update_image_mode(is_alternate_theme);
    set_theme(is_alternate_theme);
}

static void update_image_mode(bool is_alternate) {
    for (int i = 0; i < NUM_IMAGES; i++)
        lv_img_set_src(images[i], is_alternate ? lightmode_imgs[i] : darkmode_imgs[i]);
}

static void set_theme(bool is_alternate) {
    theme_apply(&(themes_themes[is_alternate ? conf_opts.theme.alternate_id : conf_opts.theme.default_id]));
}

static void toggle_pw_btn_clicked_cb(lv_event_t *event) {
    LV_UNUSED(event);
    toggle_password_obscured();
}

static void toggle_password_obscured(void) {
    is_password_obscured = !is_password_obscured;
    set_password_obscured(is_password_obscured);
}

static void set_password_obscured(bool is_obscured) {
    lv_obj_t *textarea = lv_keyboard_get_textarea(keyboard);
    lv_textarea_set_password_mode(textarea, is_obscured);
}

static void toggle_kb_btn_clicked_cb(lv_event_t *event) {
    LV_UNUSED(event);
    toggle_keyboard_hidden();
}

static void toggle_keyboard_hidden(void) {
    is_keyboard_hidden = !is_keyboard_hidden;
    set_keyboard_hidden(is_keyboard_hidden);
}

static void set_keyboard_hidden(bool is_hidden) {
    if (!conf_opts.general.animations) {
        lv_obj_set_y(keyboard, is_hidden ? lv_obj_get_height(keyboard) : 0);
        return;
    }

    lv_anim_t keyboard_anim;
    lv_anim_init(&keyboard_anim);
    lv_anim_set_var(&keyboard_anim, keyboard);
    lv_anim_set_values(&keyboard_anim, is_hidden ? 0 : lv_obj_get_height(keyboard), is_hidden ? lv_obj_get_y(keyboard) : 0);
    lv_anim_set_path_cb(&keyboard_anim, lv_anim_path_ease_out);
    lv_anim_set_time(&keyboard_anim, 500);
    lv_anim_set_exec_cb(&keyboard_anim, keyboard_anim_y_cb);
    lv_anim_start(&keyboard_anim);
}

static void keyboard_anim_y_cb(void *obj, int32_t value) {
    lv_obj_set_y(obj, value);
}

static void brightness_slider_changed_cb(lv_event_t *event) {
    lv_obj_t *slider = lv_event_get_target(event);
    int32_t value = lv_slider_get_value(slider);

    if (value < MIN_BRIGHTNESS) {
        value = MIN_BRIGHTNESS;
        lv_slider_set_value(slider, value, LV_ANIM_OFF);
    }

    write_int_to_file(BRIGHTNESS_PATH, value);
}

static void shutdown_btn_clicked_cb(lv_event_t *event) {
    LV_UNUSED(event);
    static const char *btns[] = { "Yes", "No", "" };
    lv_obj_t *mbox = lv_msgbox_create(NULL, NULL, "Shutdown device?", btns, false);
    lv_obj_set_size(mbox, 400, LV_SIZE_CONTENT);
    lv_obj_add_event_cb(mbox, shutdown_mbox_value_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_center(mbox);
}

static void shutdown_mbox_value_changed_cb(lv_event_t *event) {
    lv_obj_t *mbox = lv_event_get_current_target(event);
    if (lv_msgbox_get_active_btn(mbox) == 0)
        shutdown();
    lv_msgbox_close(mbox);
}

static void terminal_btn_clicked_cb(lv_event_t *event) {
    LV_UNUSED(event);
    static const char *btns[] = { "Yes", "No", "" };
    lv_obj_t *mbox = lv_msgbox_create(NULL, NULL, "Open terminal?", btns, false);
    lv_obj_set_size(mbox, 400, LV_SIZE_CONTENT);
    lv_obj_add_event_cb(mbox, terminal_mbox_value_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_center(mbox);
}

static void terminal_mbox_value_changed_cb(lv_event_t *event) {
    lv_obj_t *mbox = lv_event_get_current_target(event);
    if (lv_msgbox_get_active_btn(mbox) == 0)
        open_terminal();
    lv_msgbox_close(mbox);
}

static void reboot_btn_clicked_cb(lv_event_t *event) {
    LV_UNUSED(event);
    static const char *btns[] = { "Yes", "No", "" };
    lv_obj_t *mbox = lv_msgbox_create(NULL, NULL, "Reboot device?", btns, false);
    lv_obj_set_size(mbox, 400, LV_SIZE_CONTENT);
    lv_obj_add_event_cb(mbox, reboot_mbox_value_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_center(mbox);
}

static void reboot_mbox_value_changed_cb(lv_event_t *event) {
    lv_obj_t *mbox = lv_event_get_current_target(event);
    if (lv_msgbox_get_active_btn(mbox) == 0)
        reboot_device();
    lv_msgbox_close(mbox);
}

static void factory_reset_btn_clicked_cb(lv_event_t *event) {
    LV_UNUSED(event);
    static const char *btns[] = {"Yes", "No", ""};
    lv_obj_t *mbox = lv_msgbox_create(NULL, NULL, "Factory reset device?", btns, false);
    lv_obj_set_size(mbox, 400, LV_SIZE_CONTENT);
    lv_obj_add_event_cb(mbox, factory_reset_mbox_value_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_center(mbox);
}

static void factory_reset_mbox_value_changed_cb(lv_event_t *event) {
    lv_obj_t *mbox = lv_event_get_current_target(event);
    if (lv_msgbox_get_active_btn(mbox) == 0) {
        lv_msgbox_close(mbox);

        lv_obj_t *resetting_mbox = lv_msgbox_create(NULL, NULL, "Resetting device...", NULL, false);
        lv_obj_set_size(resetting_mbox, 400, LV_SIZE_CONTENT);
        lv_obj_center(resetting_mbox);

        lv_timer_t *timer = lv_timer_create(perform_factory_reset, 500, resetting_mbox);
        lv_timer_set_repeat_count(timer, 1);
    } else {
        lv_msgbox_close(mbox);
    }
}

static void perform_factory_reset(lv_timer_t *timer) {
    lv_obj_t *resetting_mbox = (lv_obj_t *)timer->user_data;
    luks_state_t state = get_rootfs_luks_state();

    switch (state) {
        case LUKS_STATE_ENCRYPTED_LOCKED:
            enabling_ssh = false;
            mounting_rootfs = false;
            lv_msgbox_close(resetting_mbox);
            decrypt();
            return;

        case LUKS_STATE_ENCRYPTED_UNLOCKED:
        case LUKS_STATE_NOT_ENCRYPTED: {
            int factory_reset_result = factory_reset();

            lv_msgbox_close(resetting_mbox);

            static const char *btns[] = {"OK", ""};
            lv_obj_t *mbox = lv_msgbox_create(
                NULL,
                NULL,
                factory_reset_result == 0
                    ? "Successfully reset to factory settings"
                    : "Failed to factory reset",
                btns,
                false
            );
            lv_obj_set_size(mbox, 400, LV_SIZE_CONTENT);
            lv_obj_add_event_cb(mbox, close_mbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
            lv_obj_center(mbox);
            return;
        }

        case LUKS_STATE_ERROR:
        default: {
            lv_msgbox_close(resetting_mbox);
            static const char *btns[] = {"OK", ""};
            lv_obj_t *fail_mbox = lv_msgbox_create(
                NULL,
                NULL,
                "Failed to factory reset - unable to determine encryption state",
                btns,
                false
            );
            lv_obj_set_size(fail_mbox, 400, LV_SIZE_CONTENT);
            lv_obj_add_event_cb(fail_mbox, close_only_mbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
            lv_obj_center(fail_mbox);
            return;
        }
    }
}

static void close_mbox_cb(lv_event_t *event) {
    lv_obj_t *mbox = lv_event_get_current_target(event);

    /* maybe do something instead of sleep? */
    sleep(3);
    reboot_device();
    lv_msgbox_close(mbox);
}

static void close_only_mbox_cb(lv_event_t *event) {
    lv_obj_t *mbox = lv_event_get_current_target(event);
    lv_msgbox_close(mbox);
}

static void close_mbox_and_restore_main_screen_cb(lv_event_t *event) {
    lv_obj_t *mbox = lv_event_get_current_target(event);
    lv_msgbox_close(mbox);
    restore_main_screen();
}

static void keyboard_value_changed_cb(lv_event_t *event) {
    lv_obj_t *kb = lv_event_get_target(event);

    uint16_t btn_id = lv_btnmatrix_get_selected_btn(kb);
    if (btn_id == LV_BTNMATRIX_BTN_NONE) {
        return;
    }

    if (sq2lv_is_layer_switcher(kb, btn_id)) {
        sq2lv_switch_layer(kb, btn_id);
        return;
    }

    lv_keyboard_def_event_cb(event);
}

static void textarea_ready_cb(lv_event_t *event) {
    if (enabling_ssh)
        check_password_enable_ssh(lv_event_get_target(event));
    else if (mounting_rootfs)
        check_password_mount_rootfs(lv_event_get_target(event));
    else
        check_password_factory_reset(lv_event_get_target(event));
}

static void check_password_enable_ssh(lv_obj_t *textarea) {
    const char *password = lv_textarea_get_text(textarea);
    static int attempt_count = 0;

    int result = mount_luks_lvm_helper(password, VG_AUTO_DETECT);

    if (result == EXIT_SUCCESS) {
        attempt_count = 0;
        enable_ssh();
        restore_main_screen();
    } else if (result == 2) {
        attempt_count++;

        static const char *btns[] = {"OK", ""};
        lv_obj_t *error_mbox;

        if (attempt_count >= 3) {
            error_mbox = lv_msgbox_create(NULL, NULL, "Maximum password attempts reached.", btns, false);
            lv_obj_set_size(error_mbox, 400, LV_SIZE_CONTENT);
            lv_obj_add_event_cb(error_mbox, close_mbox_and_restore_main_screen_cb, LV_EVENT_VALUE_CHANGED, NULL);
            lv_obj_center(error_mbox);

            lv_textarea_set_text(textarea, "");
            attempt_count = 0;
        } else {
            error_mbox = lv_msgbox_create(NULL, NULL, "Incorrect password.", btns, false);
            lv_obj_set_size(error_mbox, 400, LV_SIZE_CONTENT);
            lv_obj_add_event_cb(error_mbox, close_only_mbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
            lv_obj_center(error_mbox);

            lv_textarea_set_text(textarea, "");
            lv_obj_add_state(textarea, LV_STATE_FOCUSED);
        }
    } else {
        static const char *btns[] = {"OK", ""};
        lv_obj_t *error_mbox = lv_msgbox_create(NULL, NULL, "Failed to unlock encrypted volume.", btns, false);
        lv_obj_set_size(error_mbox, 400, LV_SIZE_CONTENT);
        lv_obj_add_event_cb(error_mbox, close_only_mbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_center(error_mbox);

        lv_textarea_set_text(textarea, "");
        lv_obj_add_state(textarea, LV_STATE_FOCUSED);
    }
}

static void check_password_mount_rootfs(lv_obj_t *textarea) {
    const char *password = lv_textarea_get_text(textarea);
    static int attempt_count = 0;

    int result = mount_luks_lvm_helper(password, VG_AUTO_DETECT);

    if (result == EXIT_SUCCESS) {
        attempt_count = 0;
        toggle_mount_rootfs();
        restore_main_screen();
    } else if (result == 2) {
        attempt_count++;

        static const char *btns[] = {"OK", ""};
        lv_obj_t *error_mbox;

        if (attempt_count >= 3) {
            error_mbox = lv_msgbox_create(NULL, NULL, "Maximum password attempts reached.", btns, false);
            lv_obj_set_size(error_mbox, 400, LV_SIZE_CONTENT);
            lv_obj_add_event_cb(error_mbox, close_mbox_and_restore_main_screen_cb, LV_EVENT_VALUE_CHANGED, NULL);
            lv_obj_center(error_mbox);

            lv_textarea_set_text(textarea, "");
            attempt_count = 0;
        } else {
            error_mbox = lv_msgbox_create(NULL, NULL, "Incorrect password.", btns, false);
            lv_obj_set_size(error_mbox, 400, LV_SIZE_CONTENT);
            lv_obj_add_event_cb(error_mbox, close_only_mbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
            lv_obj_center(error_mbox);

            lv_textarea_set_text(textarea, "");
            lv_obj_add_state(textarea, LV_STATE_FOCUSED);
        }
    } else {
        static const char *btns[] = {"OK", ""};
        lv_obj_t *error_mbox = lv_msgbox_create(NULL, NULL, "Failed to unlock encrypted volume.", btns, false);
        lv_obj_set_size(error_mbox, 400, LV_SIZE_CONTENT);
        lv_obj_add_event_cb(error_mbox, close_only_mbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_center(error_mbox);

        lv_textarea_set_text(textarea, "");
        lv_obj_add_state(textarea, LV_STATE_FOCUSED);
    }
}

static void check_password_factory_reset(lv_obj_t *textarea) {
    const char *password = lv_textarea_get_text(textarea);
    static int attempt_count = 0;

    int result = mount_luks_lvm_helper(password, VG_AUTO_DETECT);

    if (result == EXIT_SUCCESS) {
        attempt_count = 0;

        lv_obj_t *resetting_mbox = lv_msgbox_create(NULL, NULL, "Resetting device...", NULL, false);
        lv_obj_set_size(resetting_mbox, 400, LV_SIZE_CONTENT);
        lv_obj_center(resetting_mbox);

        lv_timer_t *timer = lv_timer_create(factory_reset_password, 500, resetting_mbox);
        lv_timer_set_repeat_count(timer, 1);
    } else if (result == 2) {
        attempt_count++;

        static const char *btns[] = {"OK", ""};
        lv_obj_t *error_mbox;

        if (attempt_count >= 3) {
            error_mbox = lv_msgbox_create(NULL, NULL, "Maximum password attempts reached.", btns, false);
            lv_obj_set_size(error_mbox, 400, LV_SIZE_CONTENT);
            lv_obj_add_event_cb(error_mbox, close_mbox_and_restore_main_screen_cb, LV_EVENT_VALUE_CHANGED, NULL);
            lv_obj_center(error_mbox);

            lv_textarea_set_text(textarea, "");
            attempt_count = 0;
        } else {
            error_mbox = lv_msgbox_create(NULL, NULL, "Incorrect password.", btns, false);
            lv_obj_set_size(error_mbox, 400, LV_SIZE_CONTENT);
            lv_obj_add_event_cb(error_mbox, close_only_mbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
            lv_obj_center(error_mbox);

            lv_textarea_set_text(textarea, "");
            lv_obj_add_state(textarea, LV_STATE_FOCUSED);
        }
    } else {
        static const char *btns[] = {"OK", ""};
        lv_obj_t *error_mbox = lv_msgbox_create(NULL, NULL, "Failed to unlock encrypted volume.", btns, false);
        lv_obj_set_size(error_mbox, 400, LV_SIZE_CONTENT);
        lv_obj_add_event_cb(error_mbox, close_only_mbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_center(error_mbox);

        lv_textarea_set_text(textarea, "");
        lv_obj_add_state(textarea, LV_STATE_FOCUSED);
    }
}

static void factory_reset_password(lv_timer_t *timer) {
    lv_obj_t *resetting_mbox = (lv_obj_t *)timer->user_data;

    int factory_reset_result = factory_reset();

    lv_msgbox_close(resetting_mbox);

    static const char *btns[] = {"OK", ""};
    lv_obj_t *mbox = lv_msgbox_create(
        NULL,
        NULL,
        factory_reset_result == 0
            ? "Successfully reset to factory settings"
            : "Failed to factory reset",
        btns,
        false
    );
    lv_obj_set_size(mbox, 400, LV_SIZE_CONTENT);
    lv_obj_add_event_cb(mbox, close_mbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_center(mbox);
}

static void toggle_mount_rootfs(void) {
    struct stat buffer;
    const char *mount_point = "/rootfs";
    int mount_result;
    int current_mount = is_mounted(mount_point);

    if (current_mount) {
        if (umount(mount_point) == 0)
            lv_label_set_text(mount_rootfs_btn_label, "Mount rootfs");
        return;
    }

    mkdir(mount_point, 0755);

    /* First try droidian LVM devices */
    if (volume_group_exists("/dev/droidian")) {
        /* Try droidian-droidian--rootfs */
        if (stat("/dev/mapper/droidian-droidian--rootfs", &buffer) == 0) {
            mount_result = mount("/dev/mapper/droidian-droidian--rootfs", mount_point, "ext4", 0, NULL);
            if (mount_result == 0) {
                lv_label_set_text(mount_rootfs_btn_label, "Unmount rootfs");
                return;
            }
        }

        /* Try droidian_encrypted */
        if (stat("/dev/mapper/droidian_encrypted", &buffer) == 0) {
            mount_result = mount("/dev/mapper/droidian_encrypted", mount_point, "ext4", 0, NULL);
            if (mount_result == 0) {
                lv_label_set_text(mount_rootfs_btn_label, "Unmount rootfs");
                return;
            }
        }
    }

    /* Then try furios LVM devices */
    if (volume_group_exists("/dev/furios")) {
        /* Try furios-furios--rootfs */
        if (stat("/dev/mapper/furios-furios--rootfs", &buffer) == 0) {
            mount_result = mount("/dev/mapper/furios-furios--rootfs", mount_point, "ext4", 0, NULL);
            if (mount_result == 0) {
                lv_label_set_text(mount_rootfs_btn_label, "Unmount rootfs");
                return;
            }
        }

        /* Try furios_encrypted */
        if (stat("/dev/mapper/furios_encrypted", &buffer) == 0) {
            mount_result = mount("/dev/mapper/furios_encrypted", mount_point, "ext4", 0, NULL);
            if (mount_result == 0) {
                lv_label_set_text(mount_rootfs_btn_label, "Unmount rootfs");
                return;
            }
        }
    }

    /* If we get here, all mounts failed */
    lv_label_set_text(mount_rootfs_btn_label, "Mount rootfs");
}

static void toggle_ssh_btn_clicked_cb(lv_event_t *event) {
    LV_UNUSED(event);

    luks_state_t state = get_rootfs_luks_state();

    switch (state) {
        case LUKS_STATE_ENCRYPTED_LOCKED:
            mounting_rootfs = false;
            enabling_ssh = true;
            decrypt();
            break;

        case LUKS_STATE_ENCRYPTED_UNLOCKED:
        case LUKS_STATE_NOT_ENCRYPTED:
            enabling_ssh = false;
            mounting_rootfs = false;
            enable_ssh();
            break;

        case LUKS_STATE_ERROR:
        default: {
            static const char *btns[] = {"OK", ""};
            lv_obj_t *mbox = lv_msgbox_create(NULL, NULL,
                                              "Unable to determine encryption state. SSH was not changed.",
                                              btns, false);
            lv_obj_set_size(mbox, 400, LV_SIZE_CONTENT);
            lv_obj_add_event_cb(mbox, close_only_mbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
            lv_obj_center(mbox);
            break;
        }
    }
}

static void enable_ssh() {
    struct stat buffer;

    if (stat("/tmp/dropbear-enabled", &buffer) == 0) {
        if (stat("/scripts/enable-ssh.sh", &buffer) == 0) {
            system("/scripts/enable-ssh.sh 0");
            if (ip_label_container != NULL)
                lv_obj_add_flag(ip_label_container, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(ssh_btn_label, "Enable SSH");
        }
    } else {
        if (stat("/scripts/enable-ssh.sh", &buffer) == 0) {
            system("/scripts/enable-ssh.sh 1");

            if (ip_label_container == NULL) {
                /* IP Address label container */
                ip_label_container = lv_obj_create(lv_scr_act());
                lv_obj_set_width(ip_label_container, LV_PCT(100));
                lv_obj_set_height(ip_label_container, LV_SIZE_CONTENT);
                lv_obj_set_align(ip_label_container, LV_ALIGN_BOTTOM_MID);
                lv_obj_set_style_pad_bottom(ip_label_container, 50, LV_PART_MAIN);

                /* IP Address label text */
                ip_label = lv_label_create(ip_label_container);
                lv_label_set_text(ip_label, "IP Address: 192.168.2.15");
                lv_obj_align(ip_label, LV_ALIGN_BOTTOM_MID, 0, 0);
            } else {
                lv_obj_clear_flag(ip_label_container, LV_OBJ_FLAG_HIDDEN);
            }
            lv_label_set_text(ssh_btn_label, "Disable SSH");
        }
    }
}

static void toggle_mount_rootfs_btn_clicked_cb(lv_event_t *event) {
    LV_UNUSED(event);

    luks_state_t state = get_rootfs_luks_state();

    switch (state) {
        case LUKS_STATE_ENCRYPTED_LOCKED:
            enabling_ssh = false;
            mounting_rootfs = true;
            decrypt();
            break;

        case LUKS_STATE_ENCRYPTED_UNLOCKED:
        case LUKS_STATE_NOT_ENCRYPTED:
            enabling_ssh = false;
            mounting_rootfs = false;
            toggle_mount_rootfs();
            break;

        case LUKS_STATE_ERROR:
        default: {
            static const char *btns[] = {"OK", ""};
            lv_obj_t *mbox = lv_msgbox_create(NULL, NULL,
                                              "Unable to determine encryption state. Rootfs was not mounted.",
                                              btns, false);
            lv_obj_set_size(mbox, 400, LV_SIZE_CONTENT);
            lv_obj_add_event_cb(mbox, close_only_mbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
            lv_obj_center(mbox);
            break;
        }
    }
}

static void restore_main_screen(void) {
    /* Hide all decrypt page widgets */
    lv_obj_add_flag(decrypt_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);

    /* Show all main window widgets */
    lv_obj_clear_flag(container, LV_OBJ_FLAG_HIDDEN);
}

static luks_state_t get_rootfs_luks_state(void) {
    luks_state_t state = LUKS_STATE_ERROR;

    if (volume_group_exists("/dev/droidian")) {
        state = is_lv_encrypted_with_luks("/dev/droidian/droidian-reserved", 64);
        if (state == LUKS_STATE_ENCRYPTED_LOCKED ||
            state == LUKS_STATE_ENCRYPTED_UNLOCKED ||
            state == LUKS_STATE_NOT_ENCRYPTED) {
            return state;
        }
    }

    if (volume_group_exists("/dev/furios")) {
        state = is_lv_encrypted_with_luks("/dev/furios/furios-reserved", 64);
        if (state == LUKS_STATE_ENCRYPTED_LOCKED ||
            state == LUKS_STATE_ENCRYPTED_UNLOCKED ||
            state == LUKS_STATE_NOT_ENCRYPTED) {
            return state;
        }
    }

    return LUKS_STATE_ERROR;
}

static void decrypt(void) {
    uint32_t hor_res = 0;
    uint32_t ver_res = 0;
    uint32_t dpi = 0;

    switch (conf_opts.general.backend) {
#if USE_FBDEV
    case BACKENDS_BACKEND_FBDEV:
        fbdev_get_sizes(&hor_res, &ver_res, &dpi);
        break;
#endif /* USE_FBDEV */
#if USE_DRM
    case BACKENDS_BACKEND_DRM:
        drm_get_sizes((lv_coord_t *)&hor_res, (lv_coord_t *)&ver_res, &dpi);
        break;
#endif /* USE_DRM */
#if USE_MINUI
    case BACKENDS_BACKEND_MINUI:
        minui_get_sizes(&hor_res, &ver_res, &dpi);
        break;
#endif /* USE_MINUI */
    default:
        printf("Unable to find suitable backend\n");
        exit(EXIT_FAILURE);
    }

    is_keyboard_hidden = false;

    /* Figure out a few numbers for sizing and positioning */
    const int keyboard_height = ver_res > hor_res ? ver_res / 3 : ver_res / 2;
    const int padding = keyboard_height / 8;
    const int label_width = hor_res - 2 * padding;
    const int textarea_container_max_width = LV_MIN(hor_res, ver_res);

    /* Hide everything from the main window */
    lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);

    /* Main flexbox */
    decrypt_container = lv_obj_create(lv_scr_act());
    lv_obj_set_flex_flow(decrypt_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(decrypt_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(decrypt_container, LV_PCT(100), ver_res - keyboard_height);
    lv_obj_set_pos(decrypt_container, 0, 0);
    lv_obj_set_style_pad_row(decrypt_container, padding, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(decrypt_container, padding, LV_PART_MAIN);

    /* Label container */
    label_container = lv_obj_create(decrypt_container);
    lv_obj_set_size(label_container, label_width, LV_PCT(100));
    lv_obj_set_flex_grow(label_container, 1);

    /* Label */
    spangroup = lv_spangroup_create(label_container);
    lv_spangroup_set_align(spangroup, LV_TEXT_ALIGN_CENTER);
    lv_spangroup_set_mode(spangroup, LV_SPAN_MODE_BREAK);
    lv_spangroup_set_overflow(spangroup, LV_SPAN_OVERFLOW_ELLIPSIS);
    lv_span_t *span1 = lv_spangroup_new_span(spangroup);

    const char *label_text = "";

    /* Label text */
    if (enabling_ssh)
        label_text = "Password required for SSH access";
    else if (mounting_rootfs)
        label_text = "Password required for mounting the rootfs";
    else
        label_text = "Password required for factory reset";

    lv_span_set_text(span1, label_text);

    /* Size label to content */
    const lv_coord_t label_height = lv_spangroup_get_expand_height(spangroup, label_width);
    lv_obj_set_style_max_height(spangroup, LV_PCT(100), LV_PART_MAIN);
    lv_obj_set_size(spangroup, label_width, label_height);
    lv_obj_set_align(spangroup, LV_ALIGN_BOTTOM_MID);

    /* Textarea flexbox */
    textarea_container = lv_obj_create(decrypt_container);
    lv_obj_set_size(textarea_container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(textarea_container, textarea_container_max_width, LV_PART_MAIN);
    lv_obj_set_flex_flow(textarea_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(textarea_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(textarea_container, padding, LV_PART_MAIN);
    lv_obj_set_style_pad_right(textarea_container, padding, LV_PART_MAIN);

    /* Textarea */
    textarea = lv_textarea_create(textarea_container);
    lv_textarea_set_one_line(textarea, true);
    lv_textarea_set_password_mode(textarea, true);
    lv_textarea_set_password_bullet(textarea, conf_opts.textarea.bullet);
    lv_textarea_set_placeholder_text(textarea, "Enter password...");
    lv_obj_add_event_cb(textarea, textarea_ready_cb, LV_EVENT_READY, NULL);
    lv_obj_set_flex_grow(textarea, 1);
    lv_obj_add_state(textarea, LV_STATE_FOCUSED);

    /* Route physical keyboard input into textarea */
    indev_set_up_textarea_for_keyboard_input(textarea);

    /* Reveal / obscure password button */
    toggle_pw_btn = lv_btn_create(textarea_container);
    const int textarea_height = lv_obj_get_height(textarea);
    lv_obj_set_size(toggle_pw_btn, textarea_height * 0.70f, textarea_height * 0.70f);
    lv_obj_t *toggle_pw_btn_label = lv_label_create(toggle_pw_btn);
    lv_obj_center(toggle_pw_btn_label);
    lv_label_set_text(toggle_pw_btn_label, LV_SYMBOL_EYE_OPEN);
    lv_obj_add_event_cb(toggle_pw_btn, toggle_pw_btn_clicked_cb, LV_EVENT_CLICKED, NULL);

    /* Show / hide keyboard button */
    toggle_kb_btn = lv_btn_create(textarea_container);
    lv_obj_set_size(toggle_kb_btn, textarea_height * 0.70f, textarea_height * 0.70f);
    lv_obj_add_event_cb(toggle_kb_btn, toggle_kb_btn_clicked_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *toggle_kb_btn_label = lv_label_create(toggle_kb_btn);
    lv_label_set_text(toggle_kb_btn_label, LV_SYMBOL_KEYBOARD);
    lv_obj_center(toggle_kb_btn_label);

    /* Hide label if it clips vertically */
    if (label_height > lv_obj_get_height(label_container)) {
        lv_obj_set_height(spangroup, 0);
    }

    /* Keyboard (after textarea / label so that key popovers are not drawn over) */
    keyboard = lv_keyboard_create(lv_scr_act());
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(keyboard, textarea);
    lv_obj_remove_event_cb(keyboard, lv_keyboard_def_event_cb);
    lv_obj_add_event_cb(keyboard, keyboard_value_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_pos(keyboard, 0, is_keyboard_hidden ? keyboard_height : 0);
    lv_obj_set_size(keyboard, hor_res, keyboard_height);
    theme_prepare_keyboard(keyboard);

    /* Apply textarea options */
    set_password_obscured(conf_opts.textarea.obscured);
}

static void reboot_device(void) {
    sync();
    reboot(RB_AUTOBOOT);
}

static void shutdown(void) {
    sync();
    reboot(RB_POWER_OFF);
}

static void* run_recovery(void* arg) {
    LV_UNUSED(arg);

    char *recovery_args[] = {"/usr/bin/furios-recovery", NULL};
    execv(recovery_args[0], recovery_args);
    perror("execv");
    return NULL;
}

static void open_terminal(void) {
    lv_obj_clean(lv_scr_act());
    lv_deinit();
    if (buf)
        free(buf);

    switch (conf_opts.general.backend) {
#if USE_FBDEV
    case BACKENDS_BACKEND_FBDEV:
        fbdev_exit();
        break;
#endif /* USE_FBDEV */
#if USE_DRM
    case BACKENDS_BACKEND_DRM:
        drm_exit();
        break;
#endif /* USE_DRM */
#if USE_MINUI
    case BACKENDS_BACKEND_MINUI:
        minui_exit();
        break;
#endif /* USE_MINUI */
    }

    terminal_reset_current_terminal();

    pid_t pid = fork();
    if (pid == 0) {
        char *args[] = {"/usr/bin/furios-terminal", NULL};
        execv(args[0], args);
    } else {
        waitpid(pid, NULL, 0);
        printf("Terminal exited, reinitializing recovery\n");

        pthread_t recovery_thread;
        int thread_result = pthread_create(&recovery_thread, NULL, run_recovery, NULL);
        if (thread_result != 0) {
            perror("pthread_create");
            exit(1);
        }

        thread_result = pthread_detach(recovery_thread);
        if (thread_result != 0) {
            perror("pthread_detach");
            exit(1);
        }

        sleep(1); /* wait for the other instance to start */
        exit(0);
    }
}

static void sigaction_handler(int signum) {
    LV_UNUSED(signum);
    key_thread_running = false;
    pthread_join(key_thread, NULL);
    terminal_reset_current_terminal();

    if (buf) {
        free(buf);
        buf = NULL;
    }

    exit(0);
}

static void create_buttons(lv_obj_t *label_container) {
    uint32_t screen_height = lv_obj_get_height(lv_scr_act());
    uint32_t screen_width = lv_obj_get_width(lv_scr_act());

    int button_height = (int)(screen_height / 16);  /* About 6% of screen height */
    button_height = LV_MAX(60, LV_MIN(button_height, 100));  /* Between 60-100px */

    int button_width_pct = 100;  /* Default to full width */
    int max_button_width = (int)(screen_width * 0.9);  /* 90% of screen width max */

    int slider_top_margin;
    /* Get logo's bottom position */
    lv_coord_t logo_y = lv_obj_get_y(images[0]);
    lv_coord_t logo_height = lv_obj_get_height(images[0]);
    slider_top_margin = logo_y + logo_height + 40;  /* 40px gap below logo */

    int button_spacing = LV_MAX(8, (int)(screen_height / 160));

    /* Brightness slider */
    brightness_slider = lv_slider_create(label_container);
    lv_obj_set_width(brightness_slider, LV_PCT(100));
    lv_obj_set_height(brightness_slider, 20);

    int max_brightness = read_int_from_file(MAX_BRIGHTNESS_PATH, 255);
    lv_slider_set_range(brightness_slider, 0, max_brightness);

    int current_brightness = read_int_from_file(BRIGHTNESS_PATH, max_brightness);
    lv_slider_set_value(brightness_slider, current_brightness, LV_ANIM_OFF);

    lv_obj_add_event_cb(brightness_slider, brightness_slider_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_align(brightness_slider, LV_ALIGN_TOP_MID, 0, slider_top_margin);

    static lv_style_t style_slider;
    lv_style_init(&style_slider);
    lv_style_set_bg_color(&style_slider, lv_color_hex(0x888888));
    lv_style_set_bg_opa(&style_slider, LV_OPA_100);
    lv_obj_add_style(brightness_slider, &style_slider, LV_PART_MAIN);

    static lv_style_t style_indicator;
    lv_style_init(&style_indicator);
    lv_style_set_bg_color(&style_indicator, lv_color_hex(0x00ff00));
    lv_style_set_bg_opa(&style_indicator, LV_OPA_100);
    lv_obj_add_style(brightness_slider, &style_indicator, LV_PART_INDICATOR);

    static lv_style_t style_knob;
    lv_style_init(&style_knob);
    lv_style_set_bg_color(&style_knob, lv_color_hex(0xffffff));
    lv_style_set_bg_opa(&style_knob, LV_OPA_100);
    lv_style_set_border_color(&style_knob, lv_color_hex(0x000000));
    lv_style_set_border_width(&style_knob, 2);
    lv_style_set_radius(&style_knob, LV_RADIUS_CIRCLE);
    lv_style_set_pad_all(&style_knob, 5);
    lv_obj_add_style(brightness_slider, &style_knob, LV_PART_KNOB);

    brightness_label = lv_label_create(label_container);
    lv_label_set_text(brightness_label, "Brightness control");
    lv_obj_align_to(brightness_label, brightness_slider, LV_ALIGN_OUT_TOP_MID, 0, -10);

    /* Initialize navigation array for 7 buttons */
    init_button_navigation(7);
    int btn_index = 0;

    /* Calculate starting position for buttons */
    int button_start_y = slider_top_margin + 80;  /* Start below slider */

    /* Reboot button */
    reboot_btn = lv_btn_create(label_container);
    lv_obj_set_width(reboot_btn, LV_PCT(button_width_pct));
    lv_obj_set_style_max_width(reboot_btn, max_button_width, LV_PART_MAIN);
    lv_obj_set_height(reboot_btn, button_height);
    lv_obj_t *reboot_btn_label = lv_label_create(reboot_btn);
    lv_label_set_text(reboot_btn_label, "Reboot");
    lv_obj_add_event_cb(reboot_btn, reboot_btn_clicked_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(reboot_btn, LV_ALIGN_TOP_MID, 0, button_start_y);
    lv_obj_set_flex_flow(reboot_btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(reboot_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    register_nav_button(reboot_btn, btn_index++);

    /* Shutdown button */
    shutdown_btn = lv_btn_create(label_container);
    lv_obj_set_width(shutdown_btn, LV_PCT(button_width_pct));
    lv_obj_set_style_max_width(shutdown_btn, max_button_width, LV_PART_MAIN);
    lv_obj_set_height(shutdown_btn, button_height);
    lv_obj_t *shutdown_btn_label = lv_label_create(shutdown_btn);
    lv_label_set_text(shutdown_btn_label, "Shutdown");
    lv_obj_add_event_cb(shutdown_btn, shutdown_btn_clicked_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align_to(shutdown_btn, reboot_btn, LV_ALIGN_OUT_BOTTOM_MID, 0, button_spacing);
    lv_obj_set_flex_flow(shutdown_btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(shutdown_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    register_nav_button(shutdown_btn, btn_index++);

    /* Factory reset button */
    factory_reset_btn = lv_btn_create(label_container);
    lv_obj_set_width(factory_reset_btn, LV_PCT(button_width_pct));
    lv_obj_set_style_max_width(factory_reset_btn, max_button_width, LV_PART_MAIN);
    lv_obj_set_height(factory_reset_btn, button_height);
    lv_obj_t *factory_reset_btn_label = lv_label_create(factory_reset_btn);
    lv_label_set_text(factory_reset_btn_label, "Factory Reset");
    lv_obj_add_event_cb(factory_reset_btn, factory_reset_btn_clicked_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align_to(factory_reset_btn, shutdown_btn, LV_ALIGN_OUT_BOTTOM_MID, 0, button_spacing);
    lv_obj_set_flex_flow(factory_reset_btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(factory_reset_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    register_nav_button(factory_reset_btn, btn_index++);

    /* Theme toggle button */
    theme_btn = lv_btn_create(label_container);
    lv_obj_set_width(theme_btn, LV_PCT(button_width_pct));
    lv_obj_set_style_max_width(theme_btn, max_button_width, LV_PART_MAIN);
    lv_obj_set_height(theme_btn, button_height);
    lv_obj_t *theme_btn_label = lv_label_create(theme_btn);
    lv_label_set_text(theme_btn_label, "Toggle Theme");
    lv_obj_add_event_cb(theme_btn, toggle_theme_btn_clicked_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align_to(theme_btn, factory_reset_btn, LV_ALIGN_OUT_BOTTOM_MID, 0, button_spacing);
    lv_obj_set_flex_flow(theme_btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(theme_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    register_nav_button(theme_btn, btn_index++);

    /* Terminal button */
    terminal_btn = lv_btn_create(label_container);
    lv_obj_set_width(terminal_btn, LV_PCT(button_width_pct));
    lv_obj_set_style_max_width(terminal_btn, max_button_width, LV_PART_MAIN);
    lv_obj_set_height(terminal_btn, button_height);
    lv_obj_t *terminal_btn_label = lv_label_create(terminal_btn);
    lv_label_set_text(terminal_btn_label, "Terminal");
    lv_obj_add_event_cb(terminal_btn, terminal_btn_clicked_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align_to(terminal_btn, theme_btn, LV_ALIGN_OUT_BOTTOM_MID, 0, button_spacing);
    lv_obj_set_flex_flow(terminal_btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(terminal_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    register_nav_button(terminal_btn, btn_index++);

    /* SSH toggle button */
    ssh_btn = lv_btn_create(label_container);
    lv_obj_set_width(ssh_btn, LV_PCT(button_width_pct));
    lv_obj_set_style_max_width(ssh_btn, max_button_width, LV_PART_MAIN);
    lv_obj_set_height(ssh_btn, button_height);
    ssh_btn_label = lv_label_create(ssh_btn);

    struct stat buffer;
    if (stat("/tmp/dropbear-enabled", &buffer) == 0)
        lv_label_set_text(ssh_btn_label, "Disable SSH");
    else
        lv_label_set_text(ssh_btn_label, "Enable SSH");

    lv_obj_add_event_cb(ssh_btn, toggle_ssh_btn_clicked_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align_to(ssh_btn, terminal_btn, LV_ALIGN_OUT_BOTTOM_MID, 0, button_spacing);
    lv_obj_set_flex_flow(ssh_btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ssh_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    register_nav_button(ssh_btn, btn_index++);

    /* Mount rootfs toggle button */
    mount_rootfs_btn = lv_btn_create(label_container);
    lv_obj_set_width(mount_rootfs_btn, LV_PCT(button_width_pct));
    lv_obj_set_style_max_width(mount_rootfs_btn, max_button_width, LV_PART_MAIN);
    lv_obj_set_height(mount_rootfs_btn, button_height);
    mount_rootfs_btn_label = lv_label_create(mount_rootfs_btn);

    if (is_mounted("/rootfs"))
        lv_label_set_text(mount_rootfs_btn_label, "Unmount rootfs");
    else
        lv_label_set_text(mount_rootfs_btn_label, "Mount rootfs");

    lv_obj_add_event_cb(mount_rootfs_btn, toggle_mount_rootfs_btn_clicked_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align_to(mount_rootfs_btn, ssh_btn, LV_ALIGN_OUT_BOTTOM_MID, 0, button_spacing);
    lv_obj_set_flex_flow(mount_rootfs_btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(mount_rootfs_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    register_nav_button(mount_rootfs_btn, btn_index++);

    /* Highlight the first button by default */
    update_button_highlight();
}

static void create_ui(uint32_t hor_res, uint32_t ver_res) {
    /* Clear the screen */
    lv_obj_clean(lv_scr_act());

    /* Disallow scrolling */
    lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);

    const int keyboard_height = ver_res > hor_res ? ver_res / 3 : ver_res / 2;

    /* Use smaller, responsive padding */
    int padding_calc1 = (int)(keyboard_height / 8);
    int padding_calc2 = (int)(hor_res / 20);
    const int padding = LV_MIN(padding_calc1, padding_calc2);
    const int label_width = (int)hor_res - 2 * padding;

    /* Main flexbox */
    container = lv_obj_create(lv_scr_act());
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_pos(container, 0, 0);
    lv_obj_set_style_pad_row(container, padding, LV_PART_MAIN);
    lv_obj_set_style_pad_top(container, padding, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(container, padding, LV_PART_MAIN);

    /* Label container */
    label_container = lv_obj_create(container);
    lv_obj_set_size(label_container, label_width, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(label_container, padding, LV_PART_MAIN);

    /* FuriOS label container */
    furios_label_container = lv_obj_create(lv_scr_act());
    lv_obj_set_width(furios_label_container, LV_PCT(100));
    lv_obj_set_height(furios_label_container, LV_SIZE_CONTENT);
    lv_obj_set_align(furios_label_container, LV_ALIGN_BOTTOM_MID);

    /* FuriOS label text */
    furios_label = lv_label_create(furios_label_container);
    lv_label_set_text(furios_label, "FuriOS Recovery");
    lv_obj_align(furios_label, LV_ALIGN_BOTTOM_MID, 0, 0);

    /* Initialize images */
    for (int i = 0; i < NUM_IMAGES; i++)
        images[i] = lv_img_create(lv_scr_act());

    /* Set image mode */
    update_image_mode(is_alternate_theme);

    /* Scale image based on screen width */
    if (hor_res <= 800)
        lv_img_set_zoom(images[0], 166);  /* 65% for screens ≤ 800px wide */
    else if (hor_res <= 1080)
        lv_img_set_zoom(images[0], 230);  /* 90% for screens ≤ 1080px wide */
    else
        lv_img_set_zoom(images[0], 320);  /* 125% for larger screens */

    /* Position logo */
    int logo_top_margin = LV_MIN(100, (int)(ver_res / 16));

    /* Furi Labs logo */
    lv_obj_align(images[0], LV_ALIGN_TOP_MID, 0, logo_top_margin);

    /* Create buttons */
    create_buttons(label_container);
}

static void initialize_recovery_ui(void) {
    /* Initialise LVGL and set up logging callback */
    lv_init();

    /* Initialise display driver */
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);

    /* Initialise framebuffer driver and query display size */
    uint32_t hor_res = 0;
    uint32_t ver_res = 0;
    uint32_t dpi = 0;

    switch (conf_opts.general.backend) {
#if USE_FBDEV
    case BACKENDS_BACKEND_FBDEV:
        fbdev_init();
        fbdev_get_sizes(&hor_res, &ver_res, &dpi);
        disp_drv.flush_cb = fbdev_flush;
        break;
#endif /* USE_FBDEV */
#if USE_DRM
    case BACKENDS_BACKEND_DRM:
        drm_init();
        drm_get_sizes((lv_coord_t *)&hor_res, (lv_coord_t *)&ver_res, &dpi);
        disp_drv.flush_cb = drm_flush;
        break;
#endif /* USE_DRM */
#if USE_MINUI
    case BACKENDS_BACKEND_MINUI:
        minui_init();
        minui_get_sizes(&hor_res, &ver_res, &dpi);
        disp_drv.flush_cb = minui_flush;
        break;
#endif /* USE_MINUI */
    default:
        printf("Unable to find suitable backend\n");
        exit(EXIT_FAILURE);
    }

    /* Override display parameters with command line options if necessary */
    if (cli_options.hor_res > 0)
        hor_res = cli_options.hor_res;
    if (cli_options.ver_res > 0)
        ver_res = cli_options.ver_res;
    if (cli_options.dpi > 0)
        dpi = cli_options.dpi;

    /* Prepare display buffer */
    const size_t buf_size = hor_res * ver_res / 10; /* At least 1/10 of the display size is recommended */
    if (buf == NULL)
        buf = (lv_color_t *)malloc(buf_size * sizeof(lv_color_t));

    lv_disp_draw_buf_init(&disp_buf, buf, NULL, buf_size);

    /* Register display driver */
    disp_drv.draw_buf = &disp_buf;
    disp_drv.hor_res = hor_res;
    disp_drv.ver_res = ver_res;
    disp_drv.offset_x = cli_options.x_offset;
    disp_drv.offset_y = cli_options.y_offset;
    disp_drv.dpi = dpi;
    lv_disp_drv_register(&disp_drv);

    printf("Display resolution: %dx%d, DPI: %d, Offset: (%d, %d)\n",
           hor_res, ver_res, dpi, cli_options.x_offset, cli_options.y_offset);

    /* Connect input devices */
    indev_auto_connect(conf_opts.input.keyboard, conf_opts.input.pointer, conf_opts.input.touchscreen);
    indev_set_up_mouse_cursor();

    /* Initialise theme */
    set_theme(is_alternate_theme);

    /* Create UI elements */
    create_ui(hor_res, ver_res);

    /* Add a focus style for navigation highlighting */
    static lv_style_t style_focus;
    lv_style_init(&style_focus);
    lv_style_set_border_width(&style_focus, 3);
    lv_style_set_border_color(&style_focus, lv_palette_main(LV_PALETTE_YELLOW));
    lv_style_set_border_opa(&style_focus, LV_OPA_COVER);

    for (int i = 0; i < 7; i++) { /* 7 is the number of navigation buttons */
        lv_obj_t *nav_btn = NULL;
        switch (i) {
            case 0: nav_btn = reboot_btn; break;
            case 1: nav_btn = shutdown_btn; break;
            case 2: nav_btn = factory_reset_btn; break;
            case 3: nav_btn = theme_btn; break;
            case 4: nav_btn = terminal_btn; break;
            case 5: nav_btn = ssh_btn; break;
            case 6: nav_btn = mount_rootfs_btn; break;
        }

        if (nav_btn)
            lv_obj_add_style(nav_btn, &style_focus, LV_STATE_FOCUSED);
    }
}

int main(int argc, char *argv[]) {
    int furios_mounted = 0;
    int ubuntu_mounted = 0;

    char* dt_compatible = read_dt_compatible();
    if (dt_compatible != NULL) {
        printf("DT compatible %s\n", dt_compatible);

        if (strcmp(dt_compatible, "furilabs,flx1") == 0) {
            furios_mounted = mount_furios_persist("/dev/disk/by-partlabel/vendor_boot_a");
            if (furios_mounted) {
                ubuntu_mounted = is_ubports_action();
                if (ubuntu_mounted)
                    execute_ubports_action();
            }
        }

        free(dt_compatible);
    }

    if (ubuntu_mounted)
        umount("/ubuntu-userdata");
    if (furios_mounted)
        umount("/furios-persist");

    /* Parse command line options */
    cli_parse_opts(argc, argv, &cli_options);

    /* Parse config files */
    config_parse(cli_options.config_files, cli_options.num_config_files, &conf_opts);

    /* Prepare current TTY and clean up on termination */
    terminal_prepare_current_terminal();
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = sigaction_handler;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    initialize_recovery_ui();

    key_thread_running = true;
    if (pthread_create(&key_thread, NULL, key_input_thread, NULL) != 0)
        printf("Failed to start key input thread: %s\n", strerror(errno));
    else
        printf("Key input thread started successfully\n");

    /* Run lvgl in "tickless" mode */
    while (1) {
        lv_task_handler();
        usleep(5000);
    }

    return 0;
}

/**
 * Generate tick for LVGL.
 *
 * @return tick in ms
 */
uint32_t get_tick(void) {
    static uint64_t start_ms = 0;
    if (start_ms == 0) {
        struct timeval tv_start;
        gettimeofday(&tv_start, NULL);
        start_ms = (tv_start.tv_sec * 1000000 + tv_start.tv_usec) / 1000;
    }

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    uint64_t now_ms;
    now_ms = (tv_now.tv_sec * 1000000 + tv_now.tv_usec) / 1000;

    uint32_t time_ms = now_ms - start_ms;
    return time_ms;
}
