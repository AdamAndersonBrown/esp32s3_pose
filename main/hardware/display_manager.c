#include "display_manager.h"
#include "esp_lcd_ili9341.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LCD_HOST       SPI2_HOST
#define LCD_MOSI       37
#define LCD_SCLK       36
#define LCD_CS         3
#define LCD_DC         35
#define LCD_WIDTH      320
#define LCD_HEIGHT     240

static const char *TAG = "DISPLAY";
static esp_lcd_panel_handle_t panel_handle = NULL;
static uint16_t *framebuffer;

void cores3_power_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = 12,
        .scl_io_num = 11,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    i2c_param_config(I2C_NUM_0, &conf);
    i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);

    uint8_t data[2];
    
    // 1. AXP2101: Enable ALDO2 (LCD Logic), ALDO3 (IMU), ALDO4 (Camera), and DLDO1 (LCD Backlight)
    // Register 0x90 controls these. Bit 7 is DLDO1. Bits 1-3 are ALDO2-4. 
    data[0] = 0x90; data[1] = 0x8E; 
    i2c_master_write_to_device(I2C_NUM_0, 0x34, data, 2, 100);

    // 2. AXP2101: Set Voltages to 3.3V
    data[0] = 0x92; data[1] = 0x1D; i2c_master_write_to_device(I2C_NUM_0, 0x34, data, 2, 100); // ALDO2
    data[0] = 0x93; data[1] = 0x1D; i2c_master_write_to_device(I2C_NUM_0, 0x34, data, 2, 100); // ALDO3
    data[0] = 0x99; data[1] = 0x1D; i2c_master_write_to_device(I2C_NUM_0, 0x34, data, 2, 100); // DLDO1 (Backlight Power)

    vTaskDelay(pdMS_TO_TICKS(50));

    // 3. AW9523B GPIO Expander: Release LCD Hardware Reset
    data[0] = 0x12; data[1] = 0x00; // Reset global Push-Pull config
    i2c_master_write_to_device(I2C_NUM_0, 0x58, data, 2, 100);
    data[0] = 0x04; data[1] = 0x00; // Port 0: All Outputs
    i2c_master_write_to_device(I2C_NUM_0, 0x58, data, 2, 100);
    data[0] = 0x05; data[1] = 0x00; // Port 1: All Outputs
    i2c_master_write_to_device(I2C_NUM_0, 0x58, data, 2, 100);
    
    data[0] = 0x02; data[1] = 0xFF; // Port 0: Pull ALL HIGH
    i2c_master_write_to_device(I2C_NUM_0, 0x58, data, 2, 100);
    data[0] = 0x03; data[1] = 0xFF; // Port 1: Pull ALL HIGH
    i2c_master_write_to_device(I2C_NUM_0, 0x58, data, 2, 100);
}

void display_manager_init(void) {
    ESP_LOGI(TAG, "Waking up CoreS3 Hardware Display...");
    cores3_power_init();

    spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_SCLK,
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * 2 + 8
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_DC,
        .cs_gpio_num = LCD_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .color_space = ESP_LCD_COLOR_SPACE_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel_handle));

    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    esp_lcd_panel_invert_color(panel_handle, true);
    esp_lcd_panel_disp_on_off(panel_handle, true);

    framebuffer = heap_caps_malloc(LCD_WIDTH * LCD_HEIGHT * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if(!framebuffer) {
        framebuffer = heap_caps_malloc(LCD_WIDTH * LCD_HEIGHT * 2, MALLOC_CAP_SPIRAM);
    }
}

void display_manager_fill_screen(uint16_t color) {
    if(!framebuffer) return;
    uint16_t swapped = (color >> 8) | (color << 8);
    for(int i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++) framebuffer[i] = swapped; 
}

void display_draw_pixel(int x, int y, uint16_t color) {
    if(x >= 0 && x < LCD_WIDTH && y >= 0 && y < LCD_HEIGHT && framebuffer) {
        framebuffer[y * LCD_WIDTH + x] = (color >> 8) | (color << 8);
    }
}

void display_manager_flush(void) {
    if(panel_handle && framebuffer) {
        esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, LCD_WIDTH, LCD_HEIGHT, framebuffer);
    }
}
