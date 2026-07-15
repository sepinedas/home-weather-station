#include "epaper_driver.h"
#include "epaper_fonts.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include <string.h>
#include "sdkconfig.h" 
#include "esp_heap_caps.h"

static const char *TAG = "epaper_driver";

// Single instance of Paint structure
Paint_t Paint;

// SPI Device Handle
spi_device_handle_t spi_handle = NULL;

// Macro mappings from Kconfig
#define EPD_SPI_HOST        (CONFIG_CROWPANEL_EPAPER_SPI_HOST + 1)
#define SPI_HOST_ID         (CONFIG_CROWPANEL_EPAPER_SPI_HOST == 1 ? SPI2_HOST : SPI3_HOST)

#define PIN_CS              CONFIG_CROWPANEL_EPAPER_SPI_CS
#define PIN_DC              CONFIG_CROWPANEL_EPAPER_DC_PIN
#define PIN_RST             CONFIG_CROWPANEL_EPAPER_RST_PIN
#define PIN_BUSY            CONFIG_CROWPANEL_EPAPER_BUSY_PIN
#define PIN_MOSI            CONFIG_CROWPANEL_EPAPER_SPI_MOSI
#define PIN_CLK             CONFIG_CROWPANEL_EPAPER_SPI_CLK
#define PIN_PWR             CONFIG_CROWPANEL_EPAPER_POWER_PIN

// GPIO Helper Macros
#define EPD_CS_0()  gpio_set_level(PIN_CS, 0)
#define EPD_CS_1()  gpio_set_level(PIN_CS, 1)
#define EPD_DC_0()  gpio_set_level(PIN_DC, 0)
#define EPD_DC_1()  gpio_set_level(PIN_DC, 1)
#define EPD_RST_0() gpio_set_level(PIN_RST, 0)
#define EPD_RST_1() gpio_set_level(PIN_RST, 1)
#define EPD_ReadBUSY gpio_get_level(PIN_BUSY)

// Delay Helper
#define delay(ms) vTaskDelay(pdMS_TO_TICKS(ms))

// Internal SPI Write Functions
static void EPD_WriteByte(uint8_t data) {
    if (spi_handle == NULL) return;
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &data
    };
    spi_device_transmit(spi_handle, &t);
}

static void EPD_WriteBytes(const uint8_t *data, size_t len) {
    if (spi_handle == NULL || len == 0) return;
    
    spi_transaction_t t = {
        .length = len * 8, // length in bits
        .tx_buffer = data
    };
    spi_device_transmit(spi_handle, &t);
}

static void EPD_WR_REG(uint8_t reg) {
    EPD_DC_0();
    EPD_CS_0();
    EPD_WriteByte(reg);
    EPD_CS_1();
}

static void EPD_WR_DATA8(uint8_t data) {
    EPD_DC_1();
    EPD_CS_0();
    EPD_WriteByte(data);
    EPD_CS_1();
}

static void EPD_WR_DATA_BUFFER(const uint8_t *data, size_t len) {
    if (len == 0) return;
    EPD_DC_1();
    EPD_CS_0();
    EPD_WriteBytes(data, len);
    EPD_CS_1();
}

static void EPD_WR_DATA_REPEAT(uint8_t data, size_t count) {
    if (count == 0 || spi_handle == NULL) return;
    
    #define CHUNK_SIZE 128
    uint8_t buffer[CHUNK_SIZE];
    memset(buffer, data, CHUNK_SIZE);
    
    EPD_DC_1();
    EPD_CS_0();
    
    size_t remaining = count;
    while (remaining > 0) {
        size_t current = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
        spi_transaction_t t = {
           .length = current * 8,
           .tx_buffer = buffer
        };
        spi_device_transmit(spi_handle, &t);
        remaining -= current;
    }
    
    EPD_CS_1();
    #undef CHUNK_SIZE
}

static void EPD_ReadBusy(void) {
    int timeout = 0;
    while (1) {
        int busy_state = EPD_ReadBUSY;
        if (busy_state == 0) {
            break;
        }
        if (timeout++ > 500) { // 5 seconds timeout
            ESP_LOGW(TAG, "BUSY pin timeout! Pin is still HIGH after 5s");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void EPD_RESET(void) {
    EPD_RST_1();
    delay(100);
    EPD_RST_0();
    delay(10);
    EPD_RST_1();
    delay(10);
}

// Power Management
void EPD_PowerOn(void) {
    if (PIN_PWR < 0) {
        ESP_LOGW(TAG, "Power pin not configured (PIN_PWR < 0)");
        return;
    }
    
    // Configure Power Pin if not done
    gpio_config_t power_conf = {
        .pin_bit_mask = (1ULL << PIN_PWR),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&power_conf);
    gpio_set_level(PIN_PWR, 1);
    delay(200); // Increased from 100ms to 200ms
}

void EPD_GPIOInit(void) {
    // Initialize Power Pin
    EPD_PowerOn();
    vTaskDelay(pdMS_TO_TICKS(10)); // Wait for power to stabilize

    // Initialize GPIOs
    gpio_config_t io_conf = {
        .pin_bit_mask = ((1ULL << PIN_DC) | 
                         (1ULL << PIN_RST) | 
                         (1ULL << PIN_CS)),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // Initialize Busy Pin
    io_conf.pin_bit_mask = (1ULL << PIN_BUSY);
    io_conf.mode = GPIO_MODE_INPUT;
    gpio_config(&io_conf);

    // Initialize SPI Helper
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = EPD_W * EPD_H / 8 + 100
    };

    // Check if we can init bus, if not, assume it is already init
    esp_err_t ret = spi_bus_initialize(SPI_HOST_ID, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SPI bus might be already initialized, continuing... (%s)", esp_err_to_name(ret));
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000, // 10 MHz
        .mode = 0,
        .spics_io_num = -1, // We handle CS manually
        .queue_size = 7,
    };

    ret = spi_bus_add_device(SPI_HOST_ID, &devcfg, &spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
    }
}

// Low Level Helpers
static void EPD_Address_Set(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye) {
    EPD_WR_REG(0x44); // SET_RAM_X_ADDRESS_START_END_POSITION
    EPD_WR_DATA8((xs >> 3) & 0xFF);
    EPD_WR_DATA8((xe >> 3) & 0xFF);

    EPD_WR_REG(0x45); // SET_RAM_Y_ADDRESS_START_END_POSITION
    EPD_WR_DATA8(ys & 0xFF);
    EPD_WR_DATA8((ys >> 8) & 0xFF);
    EPD_WR_DATA8(ye & 0xFF);
    EPD_WR_DATA8((ye >> 8) & 0xFF);
}

static void EPD_SetCursor(uint16_t xs, uint16_t ys) {
    EPD_WR_REG(0x4E); // SET_RAM_X_ADDRESS_COUNTER
    EPD_WR_DATA8((xs >> 3) & 0xFF); // X address is in units of 8 pixels

    EPD_WR_REG(0x4F); // SET_RAM_Y_ADDRESS_COUNTER
    EPD_WR_DATA8(ys & 0xFF);
    EPD_WR_DATA8((ys >> 8) & 0xFF);
}

static void EPD_Update(void) {
    EPD_WR_REG(0x22);
#if defined(CONFIG_CROWPANEL_EPAPER_4_2_INCH)
    EPD_WR_DATA8(0xF7);
#elif defined(CONFIG_CROWPANEL_EPAPER_2_13_INCH)
    EPD_WR_DATA8(0xF7); // Changed to 0xF7 to match working Arduino example
#else 
    EPD_WR_DATA8(0xF7); // Default
#endif
    EPD_WR_REG(0x20);
    EPD_ReadBusy();
}

static void EPD_Update_Fast(void) {
    EPD_WR_REG(0x22);
    EPD_WR_DATA8(0xC7);
    EPD_WR_REG(0x20);
    EPD_ReadBusy();
}

static void EPD_Update_Part(void) {
    EPD_WR_REG(0x22);
#if defined(CONFIG_CROWPANEL_EPAPER_4_2_INCH)
    EPD_WR_DATA8(0xFF);
#elif defined(CONFIG_CROWPANEL_EPAPER_2_13_INCH)
    EPD_WR_DATA8(0xFC); // Example says FC
#else
    EPD_WR_DATA8(0xFF);
#endif
    EPD_WR_REG(0x20);
    EPD_ReadBusy();
}

// Driver Implementation

#if defined(CONFIG_CROWPANEL_EPAPER_4_2_INCH)
// ==========================================
// 4.2 Inch Initialization (SSD1683)
// ==========================================
void EPD_Init(void) {
    EPD_RESET();
    EPD_ReadBusy();
    EPD_WR_REG(0x12);   // soft reset
    EPD_ReadBusy();
    EPD_WR_REG(0x21); // Display update control
    EPD_WR_DATA8(0x40);
    EPD_WR_DATA8(0x00);
    EPD_WR_REG(0x3C); // BorderWavefrom
    EPD_WR_DATA8(0x05);
    EPD_WR_REG(0x11); // data entry mode
    EPD_WR_DATA8(0x03);   // X-mode
    EPD_Address_Set(0, 0, EPD_W - 1, EPD_H - 1);
    EPD_SetCursor(0, 0);
    EPD_ReadBusy();
}

void EPD_Init_Fast(uint8_t mode) {
    EPD_RESET();
    EPD_ReadBusy();
    EPD_WR_REG(0x12);   // soft reset
    EPD_ReadBusy();
    EPD_WR_REG(0x21);
    EPD_WR_DATA8(0x40);
    EPD_WR_DATA8(0x00);
    EPD_WR_REG(0x3C);
    EPD_WR_DATA8(0x05);
    
    if (mode == Fast_Seconds_1_5s) {
        EPD_WR_REG(0x1A); 
        EPD_WR_DATA8(0x6E);
    } else if (mode == Fast_Seconds_1_s) {
        EPD_WR_REG(0x1A);
        EPD_WR_DATA8(0x5A);
    }
    
    EPD_WR_REG(0x22); // Load temperature value
    EPD_WR_DATA8(0x91);
    EPD_WR_REG(0x20);
    EPD_ReadBusy();
    EPD_WR_REG(0x11); // data entry mode
    EPD_WR_DATA8(0x03);   // X-mode
    EPD_Address_Set(0, 0, EPD_W - 1, EPD_H - 1);
    EPD_SetCursor(0, 0);
    EPD_ReadBusy();
}

#elif defined(CONFIG_CROWPANEL_EPAPER_2_13_INCH)
// ==========================================
// 2.13 Inch Initialization (Corrected for Portrait Controller 122x250)
// ==========================================
void EPD_Init(void) {
    EPD_RESET();
    EPD_ReadBusy();
    
    EPD_WR_REG(0x12); // SW Reset
    EPD_ReadBusy();

    // Driver output control (Gate MUX)
    EPD_WR_REG(0x01);
    EPD_WR_DATA8(0xF9); // (250-1) & 0xFF = 0xF9 -> MUX lines = 250
    EPD_WR_DATA8(0x00);
    EPD_WR_DATA8(0x00);

    // Data entry mode
    EPD_WR_REG(0x11);
    EPD_WR_DATA8(0x03); // X+ Y+

    // Set RAM address window - 122(W) x 250(H) Physical
    EPD_WR_REG(0x44); 
    EPD_WR_DATA8(0x00);
    EPD_WR_DATA8(0x0F); // 15 * 8 = 120 (approx 122)

    EPD_WR_REG(0x45); 
    EPD_WR_DATA8(0x00); // Start Y Low
    EPD_WR_DATA8(0x00); // Start Y High
    EPD_WR_DATA8(0xF9); // End Y Low (249)
    EPD_WR_DATA8(0x00); // End Y High

    // Border
    EPD_WR_REG(0x3C);
    EPD_WR_DATA8(0x01);
    
    EPD_ReadBusy();

    // Temp sensor
    EPD_WR_REG(0x18);
    EPD_WR_DATA8(0x80);

    // Reset Cursor
    EPD_WR_REG(0x4E);
    EPD_WR_DATA8(0x00);
    EPD_WR_REG(0x4F);
    EPD_WR_DATA8(0x00);
    EPD_WR_DATA8(0x00);
    
    EPD_ReadBusy();
}

void EPD_Init_Fast(uint8_t mode) {
    EPD_RESET();
    
    EPD_WR_REG(0x12);  // SW Reset
    EPD_ReadBusy();   
    
    EPD_WR_REG(0x18); // Temperature sensor control
    EPD_WR_DATA8(0x80); // Internal sensor
        
    EPD_WR_REG(0x22); // Display Update Control 2
    EPD_WR_DATA8(0xB1); // Load temperature value    
    EPD_WR_REG(0x20); // Master Activation
    EPD_ReadBusy();   

    EPD_WR_REG(0x1A); // Write temperature register
    EPD_WR_DATA8(0x64); // Temperature value    
    EPD_WR_DATA8(0x00);  
            
    EPD_WR_REG(0x22); // Display Update Control 2
    EPD_WR_DATA8(0x91); // Load temperature value    
    EPD_WR_REG(0x20); // Master Activation
    EPD_ReadBusy();
    
    // Configure data entry mode
    EPD_WR_REG(0x11); // Data entry mode
    EPD_WR_DATA8(0x03); // X+ Y+
    
    // Set RAM address window - 250x122 landscape
    EPD_WR_REG(0x44); // Set RAM X address
    EPD_WR_DATA8(0x00);
    EPD_WR_DATA8(0x1F); // 31 (32*8=256, covers 250)
    
    EPD_WR_REG(0x45); // Set RAM Y address
    EPD_WR_DATA8(0x00);
    EPD_WR_DATA8(0x00);
    EPD_WR_DATA8(0x79); // 121 (122-1)
    EPD_WR_DATA8(0x00);
    
    // Set initial cursor
    EPD_WR_REG(0x4E);
    EPD_WR_DATA8(0x00);
    EPD_WR_REG(0x4F);
    EPD_WR_DATA8(0x00);
    EPD_WR_DATA8(0x00);
    
    EPD_ReadBusy();
}
#endif

void EPD_Clear(void) {
    uint32_t size;
#if defined(CONFIG_CROWPANEL_EPAPER_4_2_INCH)
    uint16_t Width = (EPD_W % 8 == 0) ? (EPD_W / 8) : (EPD_W / 8 + 1);
    size = Width * EPD_H;
#elif defined(CONFIG_CROWPANEL_EPAPER_2_13_INCH)
    // Physical: 122(W) x 250(H). 122 requires 16 bytes (128 bits).
    size = 16 * 250; // 4000 bytes
#else
    uint16_t Width = (EPD_W % 8 == 0) ? (EPD_W / 8) : (EPD_W / 8 + 1);
    size = Width * EPD_H;
#endif

    EPD_Init();
    
    // Write to both NEW (0x24) and OLD (0x26) data buffers
    EPD_WR_REG(0x24); // Write RAM (NEW data)
    EPD_WR_DATA_REPEAT(0xFF, size);

    EPD_WR_REG(0x26); // Write RAM (OLD data)
    EPD_WR_DATA_REPEAT(0xFF, size);
    
    EPD_Update();
}

void EPD_Clear_R26H(void) {
    uint32_t size;
#if defined(CONFIG_CROWPANEL_EPAPER_4_2_INCH)
    uint16_t Width = (EPD_W % 8 == 0) ? (EPD_W / 8) : (EPD_W / 8 + 1);
    size = Width * EPD_H;
#elif defined(CONFIG_CROWPANEL_EPAPER_2_13_INCH)
    size = 16 * 250; // 4000 bytes
#else
    uint16_t Width = (EPD_W % 8 == 0) ? (EPD_W / 8) : (EPD_W / 8 + 1);
    size = Width * EPD_H;
#endif
    
    EPD_WR_REG(0x26); // Write RAM (OLD data)
    EPD_WR_DATA_REPEAT(0xFF, size);
}

void EPD_Display(const uint8_t *Image) {
    uint16_t Width, Height;
    Width = (EPD_W % 8 == 0) ? (EPD_W / 8) : (EPD_W / 8 + 1);
    Height = EPD_H;
    
#if defined(CONFIG_CROWPANEL_EPAPER_2_13_INCH)
    // 2.13" display requires 90 degree rotation and coordinate mapping
    // Logical: 250(W) x 122(H). Physical: 122(W) x 250(H).
    
    // Allocate buffer for physical frame (122x250 pixels)
    // 122 pixels wide -> 16 bytes per line. 250 lines. = 4000 bytes.
    size_t phys_buf_size = 4000;
    uint8_t *phys_buf = heap_caps_malloc(phys_buf_size, MALLOC_CAP_DMA);
    if (!phys_buf) {
        ESP_LOGE(TAG, "Failed to allocate rotation buffer");
        return;
    }
    memset(phys_buf, 0xFF, phys_buf_size); // Initialize to White

    uint16_t log_stride = (EPD_W + 7) / 8; // 32 bytes for 250 pixels

    // Determine physical buffer dimensions
    // Physical Width (Gate) = 122 (16 bytes)
    // Physical Height (Source) = 250
    
    for (uint16_t y_phys = 0; y_phys < 250; y_phys++) {
        for (uint16_t x_phys_byte = 0; x_phys_byte < 16; x_phys_byte++) {
            uint8_t byte_to_send = 0x00;
            
            for (uint8_t bit = 0; bit < 8; bit++) {
                uint16_t x_phys = x_phys_byte * 8 + bit;
                
                if (x_phys >= 122) { // Out of physical bounds
                    byte_to_send |= (1 << (7 - bit)); // White filler
                    continue;
                }
                
                // Rotation Mapping
                // x_phys correlates to y_log
                // y_phys correlates to (249 - x_log)
                // Therefore: x_log = 249 - y_phys
                
                uint16_t y_log = x_phys;
                uint16_t x_log = 249 - y_phys;
                
                // Read from Logical Image Buffer
                uint32_t log_idx = y_log * log_stride + (x_log / 8);
                uint8_t log_bit = 7 - (x_log % 8);
                
                if (Image[log_idx] & (1 << log_bit)) {
                    // Pixel is White
                    byte_to_send |= (1 << (7 - bit));
                }
            }
            phys_buf[y_phys * 16 + x_phys_byte] = byte_to_send;
        }
    }

    EPD_WR_REG(0x24);
    EPD_WR_DATA_BUFFER(phys_buf, phys_buf_size);
    
    free(phys_buf);
    EPD_Update();
    // EPD_Clear_R26H() was redundant in simple driver, skipping for speed unless needed
#else
    // 4.2" display uses normal pixel data
    EPD_WR_REG(0x24);
    EPD_WR_DATA_BUFFER(Image, Width * Height);
    EPD_Update();
#endif
}

void EPD_Display_Fast(const uint8_t *Image) {
    uint16_t Width, Height;
    Width = (EPD_W % 8 == 0) ? (EPD_W / 8) : (EPD_W / 8 + 1);
    Height = EPD_H;
    
#if defined(CONFIG_CROWPANEL_EPAPER_2_13_INCH)
    // 2.13" display requires inverted pixel data
    EPD_WR_REG(0x24);
    for (uint32_t i = 0; i < Width * Height; i++) {
        EPD_WR_DATA8(~Image[i]);
    }
#else
    // 4.2" display uses normal pixel data
    EPD_WR_REG(0x24);
    EPD_WR_DATA_BUFFER(Image, Width * Height);
#endif
    
    EPD_Update_Fast();
}

void EPD_Display_Part(uint16_t x, uint16_t y, uint16_t sizex, uint16_t sizey, const uint8_t *Image) {
    uint16_t Width, Height;
    Width = (sizex % 8 == 0) ? (sizex / 8) : (sizex / 8 + 1);
    Height = sizey;
    
    // Configure border and display update control
    EPD_WR_REG(0x3C); // BorderWavefrom
    EPD_WR_DATA8(0x80);
    
    EPD_WR_REG(0x21); // Display update control
    EPD_WR_DATA8(0x00);
    EPD_WR_DATA8(0x00);
    
    EPD_WR_REG(0x3C); // BorderWavefrom
    EPD_WR_DATA8(0x80);
    
    // Set data entry mode
    EPD_WR_REG(0x11); // Data entry mode
    EPD_WR_DATA8(0x03); // X+ Y+
    
    // Set address window
    EPD_Address_Set(x, y, x + sizex - 1, y + sizey - 1);
    EPD_SetCursor(x, y);
    
    // Write image data
    EPD_WR_REG(0x24); // Write RAM (BW)
    
#if defined(CONFIG_CROWPANEL_EPAPER_2_13_INCH)
    // 2.13" display requires inverted pixel data
    for (uint32_t i = 0; i < Width * Height; i++) {
        EPD_WR_DATA8(~Image[i]);
    }
#else
    // 4.2" display uses normal pixel data
    EPD_WR_DATA_BUFFER(Image, Width * Height);
#endif
    
    EPD_Update_Part();
    
#if defined(CONFIG_CROWPANEL_EPAPER_2_13_INCH)
    // After partial update on 2.13, restore border setting
    EPD_WR_REG(0x3C);
    EPD_WR_DATA8(0x01);
#endif
}

void EPD_Sleep(void) {
    EPD_WR_REG(0x10);
    EPD_WR_DATA8(0x01);
#if defined(CONFIG_CROWPANEL_EPAPER_2_13_INCH)
    EPD_WR_REG(0x3C);
    EPD_WR_DATA8(0x01);
#endif
    delay(50);
}

// GUI Implementation

void Paint_NewImage(uint8_t *image, uint16_t Width, uint16_t Height, uint16_t Rotate, uint16_t Color) {
    Paint.Image = 0x00;
    Paint.Image = image;

    Paint.WidthMemory = Width;
    Paint.HeightMemory = Height;
    Paint.Color = Color;
    Paint.WidthByte = (Width % 8 == 0) ? (Width / 8 ) : (Width / 8 + 1);
    Paint.HeightByte = Height;
    Paint.Rotate = Rotate;
    if (Rotate == ROTATE_0 || Rotate == ROTATE_180) {
        Paint.Width = Width;
        Paint.Height = Height;
    } else {
        Paint.Width = Height;
        Paint.Height = Width;
    }
}

void Paint_SetPixel(uint16_t Xpoint, uint16_t Ypoint, uint16_t Color) {
    uint16_t X, Y;
    uint32_t Addr;
    uint8_t Rdata;
    switch (Paint.Rotate) {
        case 0:
            X = Xpoint;
            Y = Ypoint;
            break;
        case 90:
            X = Paint.WidthMemory - Ypoint - 1;
            Y = Xpoint;
            break;
        case 180:
            X = Paint.WidthMemory - Xpoint - 1;
            Y = Paint.HeightMemory - Ypoint - 1;
            break;
        case 270:
            X = Ypoint;
            Y = Paint.HeightMemory - Xpoint - 1;
            break;
        default:
            return;
    }
    if (X >= Paint.WidthMemory || Y >= Paint.HeightMemory) return;

    Addr = X / 8 + Y * Paint.WidthByte;
    Rdata = Paint.Image[Addr];
    if (Color == BLACK) {
        Paint.Image[Addr] = Rdata & ~(0x80 >> (X % 8)); // clear bit
    } else {
        Paint.Image[Addr] = Rdata | (0x80 >> (X % 8));  // set bit
    }
}

void EPD_Full(uint8_t Color) {
    uint16_t X, Y;
    uint32_t Addr;
    for (Y = 0; Y < Paint.HeightByte; Y++) {
        for (X = 0; X < Paint.WidthByte; X++) {
            Addr = X + Y * Paint.WidthByte;
            Paint.Image[Addr] = Color;
        }
    }
}

void EPD_ShowPicture(uint16_t x, uint16_t y, uint16_t sizex, uint16_t sizey, const uint8_t *BMP, uint16_t Color) {
    uint16_t j = 0, t;
    uint16_t i, n, temp;
    uint16_t x0, width = 0;
    x += 1; y += 1; x0 = x;
    width = sizex;
    sizex = sizex / 8 + ((sizex % 8) ? 1 : 0);
    for (n = 0; n < sizey; n++) {
        for (i = 0; i < sizex; i++) {
            temp = BMP[j];
            for (t = 0; t < 8; t++) {
                if (temp & 0x80) {
                    Paint_SetPixel(x - 1, y - 1, (Color == WHITE) ? BLACK : WHITE); // Inverse color check?
                } else {
                    Paint_SetPixel(x - 1, y - 1, Color);
                }
                x++;
                temp <<= 1;
            }
            if ((x - x0) == width) {
                x = x0;
                y++;
            }
            j++;
        }
    }
}

void clear_all(void) {
    uint16_t Width, Height;
    Width = (EPD_W % 8 == 0) ? (EPD_W / 8) : (EPD_W / 8 + 1);
    Height = EPD_H;
    
    // Standard clear sequence
    EPD_Clear();
    
    // Reinitialize paint buffer
    Paint_NewImage(Paint.Image, EPD_W, EPD_H, 0, WHITE);
    EPD_Full(WHITE);
    
    // Partial display to complete the clear
    EPD_Display_Part(0, 0, EPD_W, EPD_H, Paint.Image);
}

// Clear a rectangular window
void EPD_ClearWindows(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye, uint16_t color) {
    uint16_t i, j;
    for (i = ys; i < ye; i++) {
        for (j = xs; j < xe; j++) {
            Paint_SetPixel(j, i, color);
        }
    }
}

// Draw a line using Bresenham algorithm
void EPD_DrawLine(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend, uint16_t Color) {
    uint16_t Xpoint, Ypoint;
    int dx, dy;
    int XAddway, YAddway;
    int Esp;
    Xpoint = Xstart;
    Ypoint = Ystart;
    dx = (int)Xend - (int)Xstart >= 0 ? Xend - Xstart : Xstart - Xend;
    dy = (int)Yend - (int)Ystart <= 0 ? Yend - Ystart : Ystart - Yend;
    XAddway = Xstart < Xend ? 1 : -1;
    YAddway = Ystart < Yend ? 1 : -1;
    Esp = dx + dy;
    
    for (;;) {
        Paint_SetPixel(Xpoint, Ypoint, Color);
        if (2 * Esp >= dy) {
            if (Xpoint == Xend)
                break;
            Esp += dy;
            Xpoint += XAddway;
        }
        if (2 * Esp <= dx) {
            if (Ypoint == Yend)
                break;
            Esp += dx;
            Ypoint += YAddway;
        }
    }
}

// Draw a rectangle
void EPD_DrawRectangle(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend, uint16_t Color, uint8_t mode) {
    uint16_t i;
    if (mode) {
        // Filled rectangle
        for (i = Ystart; i < Yend; i++) {
            EPD_DrawLine(Xstart, i, Xend, i, Color);
        }
    } else {
        // Outline only
        EPD_DrawLine(Xstart, Ystart, Xend, Ystart, Color);
        EPD_DrawLine(Xstart, Ystart, Xstart, Yend, Color);
        EPD_DrawLine(Xend, Yend, Xend, Ystart, Color);
        EPD_DrawLine(Xend, Yend, Xstart, Yend, Color);
    }
}

// Draw a circle using Bresenham algorithm
void EPD_DrawCircle(uint16_t X_Center, uint16_t Y_Center, uint16_t Radius, uint16_t Color, uint8_t mode) {
    int Esp, sCountY;
    uint16_t XCurrent, YCurrent;
    XCurrent = 0;
    YCurrent = Radius;
    Esp = 3 - (Radius << 1);
    
    if (mode) {
        // Filled circle
        while (XCurrent <= YCurrent) {
            for (sCountY = XCurrent; sCountY <= YCurrent; sCountY++) {
                Paint_SetPixel(X_Center + XCurrent, Y_Center + sCountY, Color);
                Paint_SetPixel(X_Center - XCurrent, Y_Center + sCountY, Color);
                Paint_SetPixel(X_Center - sCountY, Y_Center + XCurrent, Color);
                Paint_SetPixel(X_Center - sCountY, Y_Center - XCurrent, Color);
                Paint_SetPixel(X_Center - XCurrent, Y_Center - sCountY, Color);
                Paint_SetPixel(X_Center + XCurrent, Y_Center - sCountY, Color);
                Paint_SetPixel(X_Center + sCountY, Y_Center - XCurrent, Color);
                Paint_SetPixel(X_Center + sCountY, Y_Center + XCurrent, Color);
            }
            if ((int)Esp < 0)
                Esp += 4 * XCurrent + 6;
            else {
                Esp += 10 + 4 * (XCurrent - YCurrent);
                YCurrent--;
            }
            XCurrent++;
        }
    } else {
        // Hollow circle
        while (XCurrent <= YCurrent) {
            Paint_SetPixel(X_Center + XCurrent, Y_Center + YCurrent, Color);
            Paint_SetPixel(X_Center - XCurrent, Y_Center + YCurrent, Color);
            Paint_SetPixel(X_Center - YCurrent, Y_Center + XCurrent, Color);
            Paint_SetPixel(X_Center - YCurrent, Y_Center - XCurrent, Color);
            Paint_SetPixel(X_Center - XCurrent, Y_Center - YCurrent, Color);
            Paint_SetPixel(X_Center + XCurrent, Y_Center - YCurrent, Color);
            Paint_SetPixel(X_Center + YCurrent, Y_Center - XCurrent, Color);
            Paint_SetPixel(X_Center + YCurrent, Y_Center + XCurrent, Color);
            
            if ((int)Esp < 0)
                Esp += 4 * XCurrent + 6;
            else {
                Esp += 10 + 4 * (XCurrent - YCurrent);
                YCurrent--;
            }
            XCurrent++;
        }
    }
}

// Text Rendering Functions

// Helper function for exponentiation
static uint32_t EPD_Pow(uint16_t m, uint16_t n) {
    uint32_t result = 1;
    while (n--) {
        result *= m;
    }
    return result;
}

// Display a single character
void EPD_ShowChar(uint16_t x, uint16_t y, uint16_t chr, uint16_t size1, uint16_t color) {
    uint16_t i, m, temp, size2, chr1;
    uint16_t x0, y0;
    x += 1; y += 1; x0 = x; y0 = y;
    
    if (size1 == 8) size2 = 6;
    else size2 = (size1 / 8 + ((size1 % 8) ? 1 : 0)) * (size1 / 2);
    
    chr1 = chr - ' '; // Calculate offset from space character
    
    for (i = 0; i < size2; i++) {
        if (size1 == 8) {
            temp = ascii_0806[chr1][i];
        } else if (size1 == 12) {
            temp = ascii_1206[chr1][i];
        } else if (size1 == 16) {
            temp = ascii_1608[chr1][i];
        } else if (size1 == 24) {
            temp = ascii_2412[chr1][i];
        } else {
            return; // Unsupported size
        }
        
        for (m = 0; m < 8; m++) {
            if (temp & 0x01) {
                Paint_SetPixel(x, y, color);
            } else {
                Paint_SetPixel(x, y, !color);
            }
            temp >>= 1;
            y++;
        }
        x++;
        if ((size1 != 8) && ((x - x0) == size1 / 2)) {
            x = x0;
            y0 = y0 + 8;
        }
        y = y0;
    }
}

// Display a string
void EPD_ShowString(uint16_t x, uint16_t y, const char *chr, uint16_t size1, uint16_t color) {
    while (*chr != '\0') {
        EPD_ShowChar(x, y, *chr, size1, color);
        chr++;
        x += size1 / 2;
    }
}

// Display an integer number
void EPD_ShowNum(uint16_t x, uint16_t y, uint32_t num, uint16_t len, uint16_t size1, uint16_t color) {
    uint8_t t, temp, m = 0;
    if (size1 == 8) m = 2;
    
    for (t = 0; t < len; t++) {
        temp = (num / EPD_Pow(10, len - t - 1)) % 10;
        if (temp == 0) {
            EPD_ShowChar(x + (size1 / 2 + m) * t, y, '0', size1, color);
        } else {
            EPD_ShowChar(x + (size1 / 2 + m) * t, y, temp + '0', size1, color);
        }
    }
}

// Display a floating point number
void EPD_ShowFloatNum1(uint16_t x, uint16_t y, float num, uint8_t len, uint8_t pre, uint8_t sizey, uint8_t color) {
    uint8_t t, temp, sizex;
    uint16_t num1;
    sizex = sizey / 2;
    num1 = num * EPD_Pow(10, pre);
    
    for (t = 0; t < len; t++) {
        temp = (num1 / EPD_Pow(10, len - t - 1)) % 10;
        if (t == (len - pre)) {
            EPD_ShowChar(x + (len - pre) * sizex, y, '.', sizey, color);
            t++;
            len += 1;
        }
        EPD_ShowChar(x + t * sizex, y, temp + 48, sizey, color);
    }
}
