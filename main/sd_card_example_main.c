#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#define EXAMPLE_MAX_CHAR_SIZE    64
#define READ_INTERVAL            5000  // 5秒
#define READ_BUFFER_SIZE         512  // 每次读取缓冲区大小

static const char *TAG = "example";

#define MOUNT_POINT "/sdcard"

#define PIN_NUM_MISO  CONFIG_EXAMPLE_PIN_MISO
#define PIN_NUM_MOSI  CONFIG_EXAMPLE_PIN_MOSI
#define PIN_NUM_CLK   CONFIG_EXAMPLE_PIN_CLK
#define PIN_NUM_CS    CONFIG_EXAMPLE_PIN_CS

static bool first_write = true;

// 分块读取并写入文件内容（每次读写512字节）
static esp_err_t s_example_read_and_write_file(const char *input_path, const char *output_path) {
    ESP_LOGI(TAG, "Reading from file: %s", input_path);

    FILE *f_in = fopen(input_path, "rb");
    if (f_in == NULL) {
        ESP_LOGE(TAG, "Failed to open input file for reading");
        return ESP_FAIL;
    }

    FILE *f_out = fopen(output_path, first_write ? "wb" : "ab"); // 第一次写入用覆盖模式，其余追加模式
    if (f_out == NULL) {
        ESP_LOGE(TAG, "Failed to open output file for writing");
        fclose(f_in);
        return ESP_FAIL;
    }

    uint8_t buffer[READ_BUFFER_SIZE];
    size_t bytes_read, bytes_written;

    // 每次读取512字节，直到文件结束
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), f_in)) > 0) {
        ESP_LOGI(TAG, "Read %d bytes", bytes_read);
        
        bytes_written = fwrite(buffer, 1, bytes_read, f_out);
        ESP_LOGI(TAG, "Wrote %d bytes", bytes_written);
        
        if (bytes_written != bytes_read) {
            ESP_LOGE(TAG, "Failed to write all bytes to output file");
            fclose(f_in);
            fclose(f_out);
            return ESP_FAIL;
        }
    }

    fclose(f_in);
    fclose(f_out);

    first_write = false; // 后续写入切换到追加模式
    ESP_LOGI(TAG, "Finished writing to file: %s", output_path);
    return ESP_OK;
}

// 任务函数：读取并写入完整文件内容
void read_and_write_task(void *pvParameter) {
    const char *input_file = MOUNT_POINT"/songs/song.mp3";
    const char *output_file = MOUNT_POINT"/songs/song.txt";

    esp_err_t ret = s_example_read_and_write_file(input_file, output_file);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error reading and writing file");
    }

    vTaskDelete(NULL);
}

void app_main(void) {
    esp_err_t ret;

    // 文件系统挂载配置
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
#ifdef CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = false,
#endif
        .max_files = 5,
        .allocation_unit_size = 4 * 1024
    };

    sdmmc_card_t *card;
    const char mount_point[] = MOUNT_POINT;

    ESP_LOGI(TAG, "Initializing SD card");

    // SPI配置
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .max_transfer_sz = 4000,
    };

    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bus.");
        return;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                     "If you want the card to be formatted, set the CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        return;
    }
    ESP_LOGI(TAG, "Filesystem mounted");

    sdmmc_card_print_info(stdout, card);

    // 创建并启动任务
    xTaskCreate(read_and_write_task, "read_and_write_task", 16384, NULL, 5, NULL);
}
