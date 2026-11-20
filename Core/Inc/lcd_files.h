#ifndef LCD_FILES_H
#define LCD_FILES_H

#include "fatfs.h"
#include "lvgl.h"

#define LCD_FILE_PATH_CAPACITY 515U

typedef enum { LCD_FILE_PHOTO, LCD_FILE_VIDEO } LcdFileMedia;

void LcdFiles_Show(lv_obj_t *home, bool (*open_media)(const TCHAR *, LcdFileMedia));
void LcdFiles_Return(const char *message);

#endif
