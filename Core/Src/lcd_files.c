#include "lcd_files.h"

#include "rgb565_file.h"
#include <string.h>

#define ROWS 5U
#define TEXT_BYTES 640U

static lv_obj_t *home_screen;
static lv_obj_t *screen;
static lv_obj_t *status;
static bool (*show_media)(const TCHAR *, LcdFileMedia);
static TCHAR path[LCD_FILE_PATH_CAPACITY] = u"0:/";
static TCHAR names[ROWS][_MAX_LFN + 1];
static BYTE attributes[ROWS];
static FSIZE_t sizes[ROWS];
static unsigned first;
static bool more;
static bool text_view;
static TCHAR text_path[LCD_FILE_PATH_CAPACITY];
static FSIZE_t text_offset;
static FSIZE_t text_next;
static char text[TEXT_BYTES + 1];

static void show_list(void);
static void show_text(void);

static lv_obj_t *button(const char *caption, int x, int y, int width, int height,
                        lv_event_cb_t handler, void *context)
{
    lv_obj_t *item = lv_btn_create(screen);
    lv_obj_set_pos(item, x, y);
    lv_obj_set_size(item, width, height);
    lv_obj_set_style_bg_color(item, lv_color_hex(0x253956), 0);
    lv_obj_set_style_bg_color(item, lv_color_hex(0x39567E), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    lv_obj_t *label = lv_label_create(item);
    lv_obj_set_width(label, width - 8);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, caption);
    lv_obj_center(label);
    lv_obj_add_event_cb(item, handler, LV_EVENT_CLICKED, context);
    return item;
}

static void back(lv_event_t *event)
{
    (void)event;
    if (text_view) {
        show_list();
    } else {
        lv_scr_load(home_screen);
    }
}

static void up(lv_event_t *event)
{
    (void)event;
    size_t length = 3;
    for (size_t i = 3; path[i]; ++i) {
        if (path[i] == u'/') length = i;
    }
    path[length] = 0;
    first = 0;
    show_list();
}

static void choose_volume(lv_event_t *event)
{
    path[0] = lv_event_get_user_data(event) ? u'1' : u'0';
    path[3] = 0;
    first = 0;
    show_list();
}

static void change_page(lv_event_t *event)
{
    if (lv_event_get_user_data(event)) {
        if (!more) return;
        first += ROWS;
    } else {
        if (!first) return;
        first -= ROWS;
    }
    show_list();
}

static void utf8_name(const TCHAR *name, char *output)
{
    for (; *name; ++name) {
        unsigned ch = *name;
        if (ch >= 0xD800 && ch <= 0xDBFF && name[1] >= 0xDC00 && name[1] <= 0xDFFF) {
            ch = 0x10000 + ((ch - 0xD800) << 10) + (*++name - 0xDC00);
        } else if (ch >= 0xD800 && ch <= 0xDFFF) {
            ch = '?';
        }
        if (ch >= 0x10000) *output++ = 0xF0 | (ch >> 18);
        if (ch >= 0x10000) *output++ = 0x80 | ((ch >> 12) & 0x3F);
        else if (ch >= 0x800) *output++ = 0xE0 | (ch >> 12);
        if (ch >= 0x800) *output++ = 0x80 | ((ch >> 6) & 0x3F);
        else if (ch >= 0x80) *output++ = 0xC0 | (ch >> 6);
        *output++ = ch < 0x80 ? ch : 0x80 | (ch & 0x3F);
    }
    *output = 0;
}

static bool has_extension(const TCHAR *name, const TCHAR *extension)
{
    const TCHAR *suffix = name;
    for (; *name; ++name) if (*name == u'.') suffix = name;
    while (*suffix && ff_wtoupper(*suffix) == *extension) {
        ++suffix;
        ++extension;
    }
    return *suffix == 0 && *extension == 0;
}

static void open_entry(lv_event_t *event)
{
    unsigned row = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    TCHAR target[LCD_FILE_PATH_CAPACITY];
    size_t length = 0;
    while (path[length]) ++length;
    if (length + 2 >= LCD_FILE_PATH_CAPACITY) {
        lv_label_set_text_static(status, "Path too long");
        return;
    }
    memcpy(target, path, length * sizeof(TCHAR));
    if (target[length - 1] != u'/') target[length++] = u'/';
    size_t i = 0;
    while (names[row][i] && length + i < LCD_FILE_PATH_CAPACITY - 1U) {
        target[length + i] = names[row][i];
        ++i;
    }
    if (names[row][i]) {
        lv_label_set_text_static(status, "Path too long");
        return;
    }
    target[length + i] = 0;

    if (attributes[row] & AM_DIR) {
        memcpy(path, target, (length + i + 1) * sizeof(TCHAR));
        first = 0;
        show_list();
    } else if (has_extension(target, u".TXT")) {
        memcpy(text_path, target, (length + i + 1) * sizeof(TCHAR));
        text_offset = 0;
        show_text();
    } else if (has_extension(target, u".RGB565") && sizes[row] >= RGB565_FRAME_BYTES &&
               sizes[row] % RGB565_FRAME_BYTES == 0) {
        if (!show_media(target, sizes[row] == RGB565_FRAME_BYTES ? LCD_FILE_PHOTO : LCD_FILE_VIDEO))
            lv_label_set_text_static(status, "Cannot open file");
    } else {
        lv_label_set_text_static(status, "Unsupported file");
    }
}

static void clear_screen(void)
{
    lv_obj_clean(screen);
    lv_obj_scroll_to_y(screen, 0, LV_ANIM_OFF);
}

static void show_list(void)
{
    clear_screen();
    text_view = false;
    more = false;
    button("Back", 6, 6, 68, 38, back, NULL);
    button("SD", 80, 6, 58, 38, choose_volume, NULL);
    button("W25Q16", 144, 6, 90, 38, choose_volume, screen);
    button("Up", 6, 48, 50, 30, up, NULL);
    status = lv_label_create(screen);
    lv_obj_set_pos(status, 62, 56);
    lv_obj_set_width(status, 172);
    lv_label_set_long_mode(status, LV_LABEL_LONG_DOT);
    const TCHAR *folder = path + 3;
    for (const TCHAR *part = folder; *part; ++part) if (*part == u'/') folder = part + 1;
    char caption[(_MAX_LFN + 1) * 3 + 4];
    caption[0] = (char)path[0];
    caption[1] = ':';
    caption[2] = '/';
    utf8_name(folder, caption + 3);
    lv_label_set_text(status, caption);

    DIR dir;
    FRESULT result = f_opendir(&dir, path);
    if (result == FR_NOT_ENABLED) {
        const bool nor = path[0] == u'1';
        if ((nor ? retNOR : retSD) == 0 &&
            f_mount(nor ? &NorFatFS : &SDFatFS, nor ? u"1:" : u"0:", 0) == FR_OK) {
            result = f_opendir(&dir, path);
        }
    }
    if (result != FR_OK) {
        lv_label_set_text_fmt(status, "Cannot read volume (%u)", (unsigned)result);
        return;
    }

    FILINFO info;
    unsigned index = 0;
    unsigned row = 0;
    while ((result = f_readdir(&dir, &info)) == FR_OK && info.fname[0]) {
        if (index++ < first) continue;
        if (row == ROWS) {
            more = true;
            break;
        }
        memcpy(names[row], info.fname, sizeof(names[row]));
        sizes[row] = info.fsize;
        attributes[row] = info.fattrib;
        char label[(_MAX_LFN + 1) * 3 + 1];
        label[0] = '/';
        utf8_name(info.fname, label + 1);
        button(label + ((info.fattrib & AM_DIR) ? 0 : 1), 6, 84 + row * 38,
               228, 34, open_entry, (void *)(uintptr_t)row);
        ++row;
    }
    f_closedir(&dir);
    if (result != FR_OK) lv_label_set_text_fmt(status, "Read error (%u)", (unsigned)result);
    else if (!row) lv_label_set_text_static(status, "Empty folder");
    if (first) button("Previous", 6, 280, 110, 34, change_page, NULL);
    if (more) button("Next", 124, 280, 110, 34, change_page, screen);
}

static void next_text(lv_event_t *event)
{
    (void)event;
    text_offset = text_next;
    show_text();
}

static void show_text(void)
{
    clear_screen();
    text_view = true;
    button("Back", 6, 6, 228, 38, back, NULL);

    FIL file;
    UINT count = 0;
    bool has_next = false;
    FRESULT result = f_open(&file, text_path, FA_READ);
    if (result == FR_OK) {
        result = f_lseek(&file, text_offset);
        if (result == FR_OK) result = f_read(&file, text, TEXT_BYTES, &count);
        has_next = f_tell(&file) < f_size(&file);
        f_close(&file);
    }
    if (has_next) {
        /* Avoid splitting a UTF-8 character between pages. */
        UINT end = count;
        while (end > 0 && ((unsigned char)text[end - 1] & 0xC0) == 0x80) --end;
        if (end > 0) {
            unsigned char lead = text[end - 1];
            unsigned bytes = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
            if (count - (end - 1) < bytes && end > 1) count = end - 1;
        }
    }
    text_next = text_offset + count;
    for (UINT i = 0; i < count; ++i) {
        if ((unsigned char)text[i] < 32 && text[i] != '\n' && text[i] != '\r' && text[i] != '\t') text[i] = ' ';
    }
    text[count] = 0;
    lv_obj_t *body = lv_obj_create(screen);
    lv_obj_set_pos(body, 6, 50);
    lv_obj_set_size(body, 228, has_next ? 224 : 264);
    lv_obj_set_style_pad_all(body, 4, 0);
    lv_obj_add_event_cb(body, back, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(body);
    lv_obj_set_width(label, 220);
    const char *contents = text;
    if (!text_offset && count >= 3 && memcmp(text, "\xEF\xBB\xBF", 3) == 0) contents += 3;
    lv_label_set_text(label, result != FR_OK ? "Cannot read text file" : count ? contents : "Empty file");
    if (has_next && count && result == FR_OK) button("Next page", 6, 280, 228, 34, next_text, NULL);
}

void LcdFiles_Show(lv_obj_t *home, bool (*open_media)(const TCHAR *, LcdFileMedia))
{
    home_screen = home;
    show_media = open_media;
    if (!screen) {
        screen = lv_obj_create(NULL);
        lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x141C30), 0);
        lv_obj_set_style_text_color(screen, lv_color_hex(0xE1E9FA), 0);
    }
    show_list();
    lv_scr_load(screen);
}

void LcdFiles_Return(const char *message)
{
    show_list();
    if (message) lv_label_set_text_static(status, message);
    lv_scr_load(screen);
}
