/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "htu21d.h"
#include <string.h>

// Non-volatile storage (NVS) library is designed to store key-value pairs in flash
// Use : idf.py erase-flash to reset flash
#include "nvs_flash.h"
#include "zb_config_platform.h"
#include "hello_world_main.h"
#include "esp_zigbee_core.h"

static const char *TAG = "DEMO";

void reportAttribute(uint8_t endpoint, uint16_t clusterID, uint16_t attributeID, void *value, uint8_t value_length)
{
    esp_zb_zcl_report_attr_cmd_t cmd = {
        .zcl_basic_cmd = {
            .dst_addr_u.addr_short = 0x0000,
            .dst_endpoint = endpoint,
            .src_endpoint = endpoint,
        },
        .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .clusterID = clusterID,
        .attributeID = attributeID,
        .cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
    };
    esp_zb_zcl_attr_t *value_r = esp_zb_zcl_get_attribute(endpoint, clusterID, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, attributeID);
    memcpy(value_r->data_p, value, value_length);
    esp_zb_zcl_report_attr_cmd_req(&cmd);
}

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(mode_mask));
}

void htu21d_task(void *pvParameters)
{
    int ret = htu21d_init(I2C_NUM_0,12,22,GPIO_PULLUP_ENABLE, GPIO_PULLUP_ENABLE);
    
    while (1)
    {
        if(ret != HTU21D_ERR_OK) {	        
            ESP_LOGE(TAG, "Error %d when initializing HTU21D component\r\n", ret);        
        }
        else {    
            float temp = ht21d_read_temperature();
            float humididy = ht21d_read_humidity();

            ESP_LOGI(TAG, "Hum: %.2f Tmp: %.2f", humididy, temp);

            uint16_t int_temperature = (uint16_t)(temp * 100);
            uint16_t int_humidity = (uint16_t)(humididy * 100);

            reportAttribute(HA_ESP_TEMP_SENSOR_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT, ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID, &int_temperature, 2);
            reportAttribute(HA_ESP_TEMP_SENSOR_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT, ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID, &int_humidity, 2);
            
            vTaskDelay(5000 / portTICK_PERIOD_MS);
        }
    }
}

//
// RTFM => !!! This function has to be defined by user in each example. !!! 
// See https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32/api-reference/esp_zigbee_core.html
//
void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;
    switch (sig_type)
    {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Zigbee stack initialized");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK)
        {
            ESP_LOGI(TAG, "Start network steering");
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        }
        else
        {
            /* commissioning failed */
            ESP_LOGW(TAG, "Failed to initialize Zigbee stack (status: %s)", esp_err_to_name(err_status));
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK)
        {
            esp_zb_ieee_addr_t extended_pan_id;
            esp_zb_get_extended_pan_id(extended_pan_id);
            ESP_LOGI(TAG, "Joined network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, Channel:%d)",
                     extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4],
                     extended_pan_id[3], extended_pan_id[2], extended_pan_id[1], extended_pan_id[0],
                     esp_zb_get_pan_id(), esp_zb_get_current_channel());
            //xTaskCreate(button_task, "button_task", 4096, NULL, 5, NULL);
            xTaskCreate(htu21d_task, "htu21d_task", 4096, NULL, 5, NULL);
        }
        else
        {
            ESP_LOGI(TAG, "Network steering was not successful (status: %s)", esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    default:
        ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s", esp_zb_zdo_signal_to_string(sig_type), sig_type,
                 esp_err_to_name(err_status));
        break;
    }
}


/* initialize Zigbee stack with Zigbee end-device config */
static void esp_zb_task(void *pvParameters)
{
    //See https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32/api-reference/esp_zigbee_core.html?highlight=esp_zb_cfg_t
    //esp_zb_cfg_t: The Zigbee device configuration. See https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32/api-reference/esp_zigbee_core.html?highlight=esp_zb_cfg_t#_CPPv412esp_zb_cfg_s
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZED_CONFIG();

    ESP_LOGI(TAG, "Initializing Zigbee Stack");

    //Initialize Zigbee stack
    esp_zb_init(&zb_nwk_cfg);

    ESP_LOGI(TAG, "Zigbee Stack initialized !!!");

    //See https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32h2/developing.html#data-model

    // ------------------------------ Cluster BASIC ------------------------------

    //See : https://github.com/espressif/esp-zigbee-sdk/blob/9606aa4/components/esp-zigbee-lib/include/zcl/esp_zigbee_zcl_basic.h
    //See : https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32h2/api-reference/zcl/esp_zigbee_zcl_basic.html
    esp_zb_basic_cluster_cfg_t basic_cluster_cfg = {
        .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_DEFAULT_VALUE
    };    

    //uint32_t ApplicationVersion = test. esp_zb_zcl_basic_attr_t. 0x0001;
    uint32_t ApplicationVersion = ESP_ZB_ZCL_BASIC_APPLICATION_VERSION_DEFAULT_VALUE;
    //uint32_t StackVersion = 0x0002;
    uint32_t StackVersion = ESP_ZB_ZCL_BASIC_STACK_VERSION_DEFAULT_VALUE;
    //uint32_t HWVersion = 0x0002;
    uint32_t HWVersion = ESP_ZB_ZCL_BASIC_HW_VERSION_DEFAULT_VALUE;
    uint8_t ManufacturerName[] = {5, 'G', 'U', 'I', 'E', 'T'}; // warning: this is in format {length, 'string'} :
    uint8_t ModelIdentifier[] = {4, 'D', 'e', 'm', 'o'};
    uint8_t DateCode[] = {8, '2', '0', '2', '4', '0', '3', '1', '7'};
    esp_zb_attribute_list_t *esp_zb_basic_cluster = esp_zb_basic_cluster_create(&basic_cluster_cfg);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_APPLICATION_VERSION_ID, &ApplicationVersion);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_STACK_VERSION_ID, &StackVersion);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_HW_VERSION_ID, &HWVersion);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, ManufacturerName);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, ModelIdentifier);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_DATE_CODE_ID, DateCode);

    // ------------------------------ Cluster IDENTIFY ------------------------------

    // See : https://medium.com/@omaslyuchenko/hello-zigbee-part-22-identify-cluster-90cf12680306
    // See : https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32h2/api-reference/zcl/esp_zigbee_zcl_identify.html

    /*esp_zb_identify_cluster_cfg_t identify_cluster_cfg = {
        .identify_time = 0,
    };
    esp_zb_attribute_list_t *esp_zb_identify_cluster = esp_zb_identify_cluster_create(&identify_cluster_cfg);*/

    // ------------------------------ Cluster LIGHT ------------------------------
    /*esp_zb_on_off_cluster_cfg_t on_off_cfg = {
        .on_off = 0,
    };
    esp_zb_attribute_list_t *esp_zb_on_off_cluster = esp_zb_on_off_cluster_create(&on_off_cfg);*/

    // ------------------------------ Cluster BINARY INPUT ------------------------------
    /*esp_zb_binary_input_cluster_cfg_t binary_input_cfg = {
        .out_of_service = 0,
        .status_flags = 0,
    };
    uint8_t present_value = 0;
    esp_zb_attribute_list_t *esp_zb_binary_input_cluster = esp_zb_binary_input_cluster_create(&binary_input_cfg);
    esp_zb_binary_input_cluster_add_attr(esp_zb_binary_input_cluster, ESP_ZB_ZCL_ATTR_BINARY_INPUT_PRESENT_VALUE_ID, &present_value);*/

    // ------------------------------ Cluster Temperature ------------------------------
    esp_zb_temperature_meas_cluster_cfg_t temperature_meas_cfg = {
        .measured_value = ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_UNKNOWN,
        .min_value = -50,
        .max_value = 100,
    };
    esp_zb_attribute_list_t *esp_zb_temperature_meas_cluster = esp_zb_temperature_meas_cluster_create(&temperature_meas_cfg);

    // ------------------------------ Cluster Humidity ------------------------------
    esp_zb_humidity_meas_cluster_cfg_t humidity_meas_cfg = {
        .measured_value = ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_DEFAULT,
        .min_value = 0,
        .max_value = 100,
    };
    esp_zb_attribute_list_t *esp_zb_humidity_meas_cluster = esp_zb_humidity_meas_cluster_create(&humidity_meas_cfg);

    // ------------------------------ Create cluster list ------------------------------
    esp_zb_cluster_list_t *esp_zb_cluster_list = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_basic_cluster(esp_zb_cluster_list, esp_zb_basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    //esp_zb_cluster_list_add_identify_cluster(esp_zb_cluster_list, esp_zb_identify_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    //esp_zb_cluster_list_add_on_off_cluster(esp_zb_cluster_list, esp_zb_on_off_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    //esp_zb_cluster_list_add_binary_input_cluster(esp_zb_cluster_list, esp_zb_binary_input_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_temperature_meas_cluster(esp_zb_cluster_list, esp_zb_temperature_meas_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_humidity_meas_cluster(esp_zb_cluster_list, esp_zb_humidity_meas_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    // ------------------------------ Create endpoint list ------------------------------
    //
    //Within each node are endpoints. Endpoints, identified by a number between 1 and 240, 
    //define each application running in a ZigBee node (yes, a single ZigBee node can run multiple applications). 
    //Endpoints serve three purposes in ZigBee:

    //Endpoints allow for different application profiles to exist within each node.
    //Endpoints allow for separate control points to exist within each node.
    //Endpoints allow for separate devices to exist within each node.

    //HA stands for Home Automation
    esp_zb_ep_list_t *esp_zb_ep_list = esp_zb_ep_list_create();


    //uint8_t endpoint : id du endpoint?

    //See https://github.com/espressif/esp-zigbee-sdk/blob/9606aa4/components/esp-zigbee-lib/include/zcl/esp_zigbee_zcl_common.h
    //uint16_t app_profile_id : application profile identifier (ESP_ZB_AF_HA_PROFILE_ID : Application Framework Home Automation Profile ID)
    //uint16_t app_device_id : application device identifier
    //uint32_t app_device_version : application device version

    esp_zb_endpoint_config_t zb_ep_cfg = ESP_ZB_EP_CONFIG();
    esp_zb_ep_list_add_ep(esp_zb_ep_list, esp_zb_cluster_list, zb_ep_cfg);

    // ------------------------------ Register Device ------------------------------
    esp_zb_device_register(esp_zb_ep_list);
    //esp_zb_core_action_handler_register(zb_action_handler);

    //See https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32/api-reference/esp_zigbee_core.html?highlight=esp_zb_transceiver_all_channels_mask#c.ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK
    esp_zb_set_primary_network_channel_set(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK);

    ESP_ERROR_CHECK(esp_zb_start(false));

    esp_zb_main_loop_iteration();
}

void app_main(void)
{

    esp_zb_platform_config_t config = {
        /*!< Use the native 15.4 radio */
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),

        /*!< Disable host connection */
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };

    //Init Non Volatile Storage (portion of flash memory)
    ESP_ERROR_CHECK(nvs_flash_init());

    /* load Zigbee platform config to initialization */
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));

    /* hardware related and device init */
    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);

    /*printf("Hello world!\n");

    //GPIO_PULLUP_DISABLE
    int ret = htu21d_init(I2C_NUM_0,12,22,GPIO_PULLUP_ENABLE, GPIO_PULLUP_ENABLE);

    if(ret != HTU21D_ERR_OK) {	
      printf("Error %d when initializing HTU21D component\r\n", ret);
      //while(1);
    }

    float temp = ht21d_read_temperature();
    float humididy = ht21d_read_humidity();

    printf("Current temp %.2f.\n",
            temp);
    
    printf("Current humididy %.2f.\n",
            humididy);

    */

    /* Print chip information */
    /*esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        printf("Get flash size failed");
        return;
    }

    printf("%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    printf("Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());

    for (int i = 10; i >= 0; i--) {
        printf("Restarting in %d seconds...\n", i);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    printf("Restarting now.\n");
    fflush(stdout);
    esp_restart();*/
}
