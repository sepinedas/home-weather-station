#ifndef __EPAPER_DRIVER_H__
#define __EPAPER_DRIVER_H__

#include <stdint.h>
#include "esp_err.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

// Screen resolution
#ifdef CONFIG_CROWPANEL_EPAPER_4_2_INCH
#define EPD_W 400
#define EPD_H 300
#elif defined(CONFIG_CROWPANEL_EPAPER_2_13_INCH)
// Hardware resolution for 2.13" display in landscape orientation (native)
// Physical dimensions: 250 pixels wide x 122 pixels tall
#define EPD_W 250
#define EPD_H 122
#else
// Fallback
#define EPD_W 400
#define EPD_H 300
#endif

// Colors
#define WHITE 0xFF
#define BLACK 0x00

// Rotation
#define ROTATE_0   0
#define ROTATE_90  90
#define ROTATE_180 180
#define ROTATE_270 270

// Fast Init Modes
#define Fast_Seconds_1_5s 1
#define Fast_Seconds_1_s  2

// Paint Structure
typedef struct {
    uint8_t *Image;
    uint16_t Width;
    uint16_t Height;
    uint16_t WidthMemory;
    uint16_t HeightMemory;
    uint16_t Color;
    uint16_t Rotate;
    uint16_t WidthByte;
    uint16_t HeightByte;
} Paint_t;

extern Paint_t Paint;

// Function Prototypes

// Hardware / GPIO / SPI
void EPD_GPIOInit(void);
void EPD_PowerOn(void); // Toggles pin 7
void EPD_Sleep(void);

// Basic EPD commands
void EPD_Init(void);
void EPD_Init_Fast(uint8_t mode);
void EPD_Clear(void);
void EPD_Clear_R26H(void);
void EPD_Display(const uint8_t *Image);
void EPD_Display_Part(uint16_t x, uint16_t y, uint16_t sizex, uint16_t sizey, const uint8_t *Image);
void EPD_Display_Fast(const uint8_t *Image);

// GUI / Paint
void Paint_NewImage(uint8_t *image, uint16_t Width, uint16_t Height, uint16_t Rotate, uint16_t Color);
void Paint_SetPixel(uint16_t Xpoint, uint16_t Ypoint, uint16_t Color);
void EPD_Full(uint8_t Color);
void EPD_ShowPicture(uint16_t x, uint16_t y, uint16_t sizex, uint16_t sizey, const uint8_t *Image, uint16_t Color);

// Drawing Functions
void EPD_ClearWindows(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye, uint16_t color);
void EPD_DrawLine(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend, uint16_t Color);
void EPD_DrawRectangle(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend, uint16_t Color, uint8_t mode);
void EPD_DrawCircle(uint16_t X_Center, uint16_t Y_Center, uint16_t Radius, uint16_t Color, uint8_t mode);

// Text Rendering Functions
void EPD_ShowChar(uint16_t x, uint16_t y, uint16_t chr, uint16_t size1, uint16_t color);
void EPD_ShowString(uint16_t x, uint16_t y, const char *chr, uint16_t size1, uint16_t color);
void EPD_ShowNum(uint16_t x, uint16_t y, uint32_t num, uint16_t len, uint16_t size1, uint16_t color);
void EPD_ShowFloatNum1(uint16_t x, uint16_t y, float num, uint8_t len, uint8_t pre, uint8_t sizey, uint8_t color);

// Extras from example
void clear_all(void);

#ifdef __cplusplus
}
#endif

#endif // __EPAPER_DRIVER_H__
