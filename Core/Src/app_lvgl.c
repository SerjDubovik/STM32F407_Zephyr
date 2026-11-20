#include "app_lvgl.h"

#include "ili9341.h"
#include "lcd_files.h"
#include "lvgl.h"
#include "lvgl_tick.h"
#include "main.h"
#include "rgb565_file.h"
#include "slideshow.h"
#include "touchscreen.h"
#include "video_player.h"

#define DRAW_ROWS 10U

typedef enum { DISPLAY_MENU, DISPLAY_VIDEO, DISPLAY_SLIDESHOW, DISPLAY_FILE_VIDEO, DISPLAY_FILE_PHOTO } DisplayMode;

typedef struct
{
    DisplayMode mode;
    const char *caption;
    const TCHAR *folder;
    const char *empty_message;
} MediaChoice;

static const MediaChoice video = {DISPLAY_VIDEO, "Video", u"0:/video", "No videos in /video"};
static const MediaChoice slideshow = {DISPLAY_SLIDESHOW, "Slideshow", u"0:/image", "No images in /image"};

static lv_disp_draw_buf_t draw_buffer;
static lv_disp_drv_t display_driver;
static lv_indev_drv_t touch_driver;
static lv_obj_t *home_screen;
static lv_obj_t *media_status;
static _Alignas(4) lv_color_t pixels[ILI9341_WIDTH * DRAW_ROWS];
static DisplayMode mode;
static bool panel_needs_redraw;
static bool wait_for_release;
static TCHAR photo_path[LCD_FILE_PATH_CAPACITY];
static bool photo_pending;

_Static_assert(sizeof(lv_color_t) == sizeof(uint16_t), "The LCD expects RGB565 pixels");

uint32_t AppLvgl_Tick(void)
{
    return HAL_GetTick();
}

static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *colors)
{
    uint16_t width = lv_area_get_width(area);
    uint16_t height = lv_area_get_height(area);
    uint16_t count = width * height;
    ILI9341_BeginWindow(area->x1, area->y1, width, height);
    ILI9341_StartPixels((const uint16_t *)colors, count);
    if (!ILI9341_WaitPixels()) {
        ILI9341_BeginWindow(area->x1, area->y1, width, height);
        ILI9341_WritePixels((const uint16_t *)colors, count);
    }
    lv_disp_flush_ready(driver);
}

static void read_touch(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    (void)driver;
    TouchscreenSample sample;
    data->state = LV_INDEV_STATE_RELEASED;
    if (!Touchscreen_ReadPosition(&sample)) return;
    if (!sample.pressed) {
        wait_for_release = false;
    } else if (!wait_for_release) {
        data->point.x = sample.x;
        data->point.y = sample.y;
        data->state = LV_INDEV_STATE_PRESSED;
    }
}

void AppLvgl_StartVideo(void)
{
    mode = DISPLAY_VIDEO;
    wait_for_release = true;
}

static void play_media(lv_event_t *event)
{
    const MediaChoice *choice = lv_event_get_user_data(event);
    FIL file;
    TCHAR name[_MAX_LFN + 1] = {0};
    if (!Rgb565_OpenNext(&file, choice->folder, name, choice->mode == DISPLAY_VIDEO)) {
        lv_label_set_text_static(media_status, choice->empty_message);
        return;
    }
    if (f_close(&file) != FR_OK) {
        lv_label_set_text_static(media_status, "SD card read error");
        return;
    }
    lv_label_set_text_static(media_status, "");
    if (choice->mode == DISPLAY_VIDEO) VideoPlayer_SetPaused(false);
    mode = choice->mode;
    wait_for_release = true;
}

static bool open_browser_media(const TCHAR *path, LcdFileMedia media)
{
    if (media == LCD_FILE_VIDEO) {
        if (!VideoPlayer_OpenFile(path)) return false;
        mode = DISPLAY_FILE_VIDEO;
    } else {
        TCHAR *destination = photo_path;
        while ((*destination++ = *path++) != 0) {}
        photo_pending = true;
        mode = DISPLAY_FILE_PHOTO;
    }
    wait_for_release = true;
    return true;
}

static void open_browser(lv_event_t *event)
{
    (void)event;
    LcdFiles_Show(home_screen, open_browser_media);
}

static void make_button(const char *text, lv_coord_t y, lv_event_cb_t handler, void *context)
{
    lv_obj_t *button = lv_btn_create(home_screen);
    lv_obj_set_size(button, 190, 46);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x253956), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x39567E), LV_STATE_PRESSED);
    lv_obj_align(button, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_add_event_cb(button, handler, LV_EVENT_CLICKED, context);
    lv_obj_t *caption = lv_label_create(button);
    lv_label_set_text_static(caption, text);
    lv_obj_center(caption);
}

void AppLvgl_Init(void)
{
    lv_init();
    lv_disp_draw_buf_init(&draw_buffer, pixels, NULL, ILI9341_WIDTH * DRAW_ROWS);
    lv_disp_drv_init(&display_driver);
    display_driver.hor_res = ILI9341_WIDTH;
    display_driver.ver_res = ILI9341_HEIGHT;
    display_driver.draw_buf = &draw_buffer;
    display_driver.flush_cb = flush;
    lv_disp_drv_register(&display_driver);

    lv_indev_drv_init(&touch_driver);
    touch_driver.type = LV_INDEV_TYPE_POINTER;
    touch_driver.read_cb = read_touch;
    lv_indev_drv_register(&touch_driver);

    home_screen = lv_scr_act();
    lv_obj_clear_flag(home_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(home_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(home_screen, lv_color_hex(0x141C30), 0);
    lv_obj_set_style_text_color(home_screen, lv_color_hex(0xE1E9FA), 0);

    lv_obj_t *title = lv_label_create(home_screen);
    lv_label_set_text_static(title, "STM32F407");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 32);

    make_button(video.caption, 76, play_media, (void *)&video);
    make_button(slideshow.caption, 130, play_media, (void *)&slideshow);
    make_button("File Browser", 184, open_browser, NULL);

    media_status = lv_label_create(home_screen);
    lv_obj_set_width(media_status, ILI9341_WIDTH - 16);
    lv_obj_set_style_text_align(media_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_static(media_status, "");
    lv_obj_align(media_status, LV_ALIGN_TOP_MID, 0, 238);

    lv_obj_t *hint = lv_label_create(home_screen);
    lv_label_set_text_static(hint, "Tap during playback to return");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -16);
    mode = DISPLAY_MENU;
}

void AppLvgl_Poll(bool touch_test_active)
{
    if (touch_test_active) {
        panel_needs_redraw = true;
        wait_for_release = true;
        if (mode == DISPLAY_FILE_PHOTO) photo_pending = true;
        return;
    }

    if (mode != DISPLAY_MENU) {
        if (HAL_GPIO_ReadPin(T_IRQ_GPIO_Port, T_IRQ_Pin) == GPIO_PIN_SET) {
            wait_for_release = false;
        } else if (!wait_for_release) {
            bool from_browser = mode == DISPLAY_FILE_VIDEO || mode == DISPLAY_FILE_PHOTO;
            if (mode == DISPLAY_FILE_VIDEO || mode == DISPLAY_VIDEO) VideoPlayer_Stop();
            if (from_browser) LcdFiles_Return(NULL);
            else lv_scr_load(home_screen);
            mode = DISPLAY_MENU;
            panel_needs_redraw = true;
            wait_for_release = true;
        }
    }

    if (mode == DISPLAY_MENU) {
        if (panel_needs_redraw) {
            lv_obj_invalidate(lv_scr_act());
            panel_needs_redraw = false;
        }
        lv_timer_handler();
    } else if (mode == DISPLAY_VIDEO) {
        VideoPlayer_Poll();
    } else if (mode == DISPLAY_SLIDESHOW) {
        Slideshow_Poll();
    } else if (mode == DISPLAY_FILE_VIDEO) {
        if (!VideoPlayer_Poll()) {
            mode = DISPLAY_MENU;
            LcdFiles_Return(NULL);
        }
    } else if (photo_pending) {
        photo_pending = false;
        FIL file;
        bool shown = false;
        if (f_open(&file, photo_path, FA_READ) == FR_OK) {
            shown = Rgb565_ReadFrame(&file, false);
            f_close(&file);
        }
        if (!shown) {
            mode = DISPLAY_MENU;
            LcdFiles_Return("Cannot display image");
        }
    }
}
