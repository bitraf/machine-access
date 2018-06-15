#include <esp_system.h>
#include "esp_spiffs.h"
#include "sdkconfig.h"

#include "main-priv.h"

/******************************************************************************
 * FunctionName : user_rf_cal_sector_set
 * Description  : SDK just reversed 4 sectors, used for rf init data and paramters.
 *                We add this function to force users to set rf cal sector, since
 *                we don't know which sector is free in user's application.
 *                sector map for last several sectors : ABCCC
 *                A : rf cal
 *                B : rf init data
 *                C : sdk parameters
 * Parameters   : none
 * Returns      : rf cal sector
*******************************************************************************/
uint32_t user_rf_cal_sector_set()
{
    flash_size_map size_map = system_get_flash_size_map();
    uint32_t rf_cal_sec = 0;

    switch (size_map) {
    case FLASH_SIZE_4M_MAP_256_256:
        rf_cal_sec = 128 - 5;
        break;

    case FLASH_SIZE_8M_MAP_512_512:
        rf_cal_sec = 256 - 5;
        break;

    case FLASH_SIZE_16M_MAP_512_512:
    case FLASH_SIZE_16M_MAP_1024_1024:
        rf_cal_sec = 512 - 5;
        break;

    case FLASH_SIZE_32M_MAP_512_512:
    case FLASH_SIZE_32M_MAP_1024_1024:
        rf_cal_sec = 1024 - 5;
        break;
    case FLASH_SIZE_64M_MAP_1024_1024:
        rf_cal_sec = 2048 - 5;
        break;
    case FLASH_SIZE_128M_MAP_1024_1024:
        rf_cal_sec = 4096 - 5;
        break;
    default:
        rf_cal_sec = 0;
        break;
    }

    return rf_cal_sec;
}

int fs_init()
{
    struct esp_spiffs_config config;

    uint32_t log_page_size = CONFIG_MAIN_FLASH_LOG_PAGE_SIZE;

    config.phys_size = CONFIG_MAIN_FLASH_SIZE_KB * 1024;
    config.phys_addr = CONFIG_MAIN_FLASH_FLASH_ADDR_KB * 1024;
    config.phys_erase_block = CONFIG_MAIN_FLASH_SECTOR_SIZE_KB * 1024;
    config.log_block_size = CONFIG_MAIN_FLASH_LOG_BLOCK_SIZE_KB * 1024;
    config.log_page_size = log_page_size;
    config.fd_buf_size = CONFIG_MAIN_FLASH_FD_BUF_SIZE * 2;
    config.cache_buf_size = (log_page_size + 32) * 8;

    return esp_spiffs_init(&config);
}
