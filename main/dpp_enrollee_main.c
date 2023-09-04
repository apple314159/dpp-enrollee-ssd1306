/* DPP Enrollee Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_dpp.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "qrcode.h"

#include "ssd1306.h"


#ifdef CONFIG_ESP_DPP_LISTEN_CHANNEL
#define EXAMPLE_DPP_LISTEN_CHANNEL_LIST     CONFIG_ESP_DPP_LISTEN_CHANNEL_LIST
#else
#define EXAMPLE_DPP_LISTEN_CHANNEL_LIST     "6"
#endif

#ifdef CONFIG_ESP_DPP_BOOTSTRAPPING_KEY
#define EXAMPLE_DPP_BOOTSTRAPPING_KEY   CONFIG_ESP_DPP_BOOTSTRAPPING_KEY
#else
#define EXAMPLE_DPP_BOOTSTRAPPING_KEY   0
#endif

#ifdef CONFIG_ESP_DPP_DEVICE_INFO
#define EXAMPLE_DPP_DEVICE_INFO      CONFIG_ESP_DPP_DEVICE_INFO
#else
#define EXAMPLE_DPP_DEVICE_INFO      0
#endif

#define CURVE_SEC256R1_PKEY_HEX_DIGITS     64

#define SSID_KEY "ssid"
#define PASSWORD_KEY "password"

static const char *TAG = "wifi dpp-enrollee";
wifi_config_t s_dpp_wifi_config;
nvs_handle_t my_handle = 0;
char ssid[33];
size_t ssid_len = 32;
char password[64];
size_t password_len = 63;

static int s_retry_num = 0;

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_dpp_event_group;

#define DPP_CONNECTED_BIT  BIT0
#define DPP_CONNECT_FAIL_BIT     BIT1
#define DPP_AUTH_FAIL_BIT           BIT2

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your LCD spec /////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define I2C_MASTER_SDA_IO  2
#define I2C_MASTER_SCL_IO  3
#define I2C_MASTER_NUM     0
#define I2C_MASTER_FREQ_HZ 400000

static ssd1306_handle_t ssd1306_dev = NULL;

static void display_init() {
    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = (gpio_num_t)I2C_MASTER_SDA_IO;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_io_num = (gpio_num_t)I2C_MASTER_SCL_IO;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
    conf.clk_flags = I2C_SCLK_SRC_FLAG_FOR_NOMAL;

    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);

    ssd1306_dev = ssd1306_create(I2C_MASTER_NUM, SSD1306_I2C_ADDRESS);
    ssd1306_refresh_gram(ssd1306_dev);
    ssd1306_clear_screen(ssd1306_dev, 0x00);
    ssd1306_refresh_gram(ssd1306_dev);
}

static void display_qr(esp_qrcode_handle_t qrcode) {
    int size = esp_qrcode_get_size(qrcode);
    int border = 2;
    int offset = (SSD1306_HEIGHT-size)/2;

    //unsigned char num = 0;

    printf("display_qr: %i\n", size);
    for (int y = -border; y < size + border; y += 1) {
        for (int x = -border; x < size + border; x += 1) {
	    //ssd1306_fill_point(ssd1306_dev, x+border, y+border, !esp_qrcode_get_module(qrcode, x, y));
	    ssd1306_fill_point(ssd1306_dev, SSD1306_WIDTH/2+offset+x, offset+y, !esp_qrcode_get_module(qrcode, x, y));
        }
    }
    char data_str[10] = "scan QR";
    //sprintf(data_str, "scan QR");
    ssd1306_draw_string(ssd1306_dev, 4, 16, (const uint8_t *)data_str, 16, 1);
    ssd1306_refresh_gram(ssd1306_dev);
}

static void wifi_connected() {
    esp_err_t err = nvs_flash_init();

    ssd1306_clear_screen(ssd1306_dev, 0x00);
    ssd1306_draw_string(ssd1306_dev, 4, 16, (const uint8_t *)"WiFi connected!", 16, 1);
    ssd1306_draw_string(ssd1306_dev, 4, 36, (const uint8_t *)s_dpp_wifi_config.sta.ssid, 16, 1);
    ssd1306_refresh_gram(ssd1306_dev);

    // save SSID and password in nvs
    if (strncmp((char *)s_dpp_wifi_config.sta.ssid, ssid, sizeof(ssid)) == 0) {
	err = nvs_set_str(my_handle, SSID_KEY, (const char *) s_dpp_wifi_config.sta.ssid);
	if (err) {
	    ESP_ERROR_CHECK(err);
	} else
	    strncpy(ssid, (char *) s_dpp_wifi_config.sta.ssid, sizeof(ssid));
    }
    if (strncmp((char *)s_dpp_wifi_config.sta.password, password, sizeof(password)) == 0) {
	err = nvs_set_str(my_handle, PASSWORD_KEY, (const char *) s_dpp_wifi_config.sta.password);
	if (err) {
	    ESP_ERROR_CHECK(err);
	} else
	    strncpy(password, (char *) s_dpp_wifi_config.sta.password, sizeof(password));
    }
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
	if (ssid[0] == 0) {
	    ESP_ERROR_CHECK(esp_supp_dpp_start_listen());
	} else {
	    esp_wifi_set_config(ESP_IF_WIFI_STA, &s_dpp_wifi_config);
	    esp_wifi_connect();
	}
        ESP_LOGI(TAG, "Started listening for DPP Authentication");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 5) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_dpp_event_group, DPP_CONNECT_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_dpp_event_group, DPP_CONNECTED_BIT);
    }
}

void dpp_enrollee_event_cb(esp_supp_dpp_event_t event, void *data)
{
    switch (event) {
    case ESP_SUPP_DPP_URI_READY:
        if (data != NULL) {
            esp_qrcode_config_t cfg = (esp_qrcode_config_t) {
		.display_func = display_qr,
		.max_qrcode_version = 10,
		.qrcode_ecc_level = ESP_QRCODE_ECC_LOW,
	    };

            ESP_LOGI(TAG, "Scan below QR Code to configure the enrollee:");
            esp_qrcode_generate(&cfg, (const char *)data);
        } else {
	    ESP_LOGE(TAG, "QRcode: no data!");
	}
        break;
    case ESP_SUPP_DPP_CFG_RECVD:
        memcpy(&s_dpp_wifi_config, data, sizeof(s_dpp_wifi_config));
        esp_wifi_set_config(ESP_IF_WIFI_STA, &s_dpp_wifi_config);
        ESP_LOGI(TAG, "DPP Authentication successful, connecting to AP : %s",
                 s_dpp_wifi_config.sta.ssid);
        s_retry_num = 0;
        esp_wifi_connect();
        break;
    case ESP_SUPP_DPP_FAIL:
        if (s_retry_num < 5) {
            ESP_LOGI(TAG, "DPP Auth failed (Reason: %s), retry...", esp_err_to_name((int)data));
            ESP_ERROR_CHECK(esp_supp_dpp_start_listen());
            s_retry_num++;
        } else {
            xEventGroupSetBits(s_dpp_event_group, DPP_AUTH_FAIL_BIT);
        }
        break;
    default:
        break;
    }
}

esp_err_t dpp_enrollee_bootstrap(void)
{
    esp_err_t ret;
    char *key = NULL;
#if 0
    size_t pkey_len = strlen(EXAMPLE_DPP_BOOTSTRAPPING_KEY);

    if (pkey_len == 0) {
	return -1;
    }
    /* Currently only NIST P-256 curve is supported, add prefix/postfix accordingly */
    char prefix[] = "30310201010420";
    char postfix[] = "a00a06082a8648ce3d030107";

    if (pkey_len != CURVE_SEC256R1_PKEY_HEX_DIGITS) {
	ESP_LOGI(TAG, "Invalid key length! Private key needs to be 32 bytes (or 64 hex digits) long");
	return ESP_FAIL;
    }

    key = malloc(sizeof(prefix) + pkey_len + sizeof(postfix));
    if (!key) {
	ESP_LOGI(TAG, "Failed to allocate for bootstrapping key");
	return ESP_ERR_NO_MEM;
    }
    sprintf(key, "%s%s%s", prefix, EXAMPLE_DPP_BOOTSTRAPPING_KEY, postfix);
#endif

    /* Currently only supported method is QR Code */
    ret = esp_supp_dpp_bootstrap_gen(EXAMPLE_DPP_LISTEN_CHANNEL_LIST, DPP_BOOTSTRAP_QR_CODE,
                                     key, EXAMPLE_DPP_DEVICE_INFO);

    if (key)
        free(key);

    return ret;
}

void dpp_enrollee_init(void)
{
    s_dpp_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    if (ssid[0] == 0) {
	ESP_ERROR_CHECK(esp_supp_dpp_init(dpp_enrollee_event_cb));
	ESP_ERROR_CHECK(dpp_enrollee_bootstrap());
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_dpp_event_group,
                                           DPP_CONNECTED_BIT | DPP_CONNECT_FAIL_BIT | DPP_AUTH_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & DPP_CONNECTED_BIT) {
	wifi_connected();
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 s_dpp_wifi_config.sta.ssid, s_dpp_wifi_config.sta.password);
    } else if (bits & DPP_CONNECT_FAIL_BIT) {
        ESP_LOGW(TAG, "Failed to connect to SSID:%s, password:%s",
                 s_dpp_wifi_config.sta.ssid, s_dpp_wifi_config.sta.password);
    } else if (bits & DPP_AUTH_FAIL_BIT) {
        ESP_LOGW(TAG, "DPP Authentication failed after %d retries", s_retry_num);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    esp_supp_dpp_deinit();
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler));
    vEventGroupDelete(s_dpp_event_group);
}

void app_main(void)
{
    display_init();

    //Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // check if we have an SSDID and password in storage
    err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
	ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
    } else {
	int allgood = 0;
        // Read
	ESP_LOGI(TAG, "Reading SSID from NVS ... ");
        nvs_get_str(my_handle, SSID_KEY, NULL, &ssid_len);
        err = nvs_get_str(my_handle, SSID_KEY, ssid, &ssid_len);
        switch (err) {
            case ESP_OK:
		ESP_LOGI(TAG, "ssid: %s", ssid);
		allgood++;
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                ESP_LOGI(TAG, "The value is not initialized yet!");
                break;
            default :
                ESP_LOGW(TAG, "Error (%s) reading!\n", esp_err_to_name(err));
        }

        nvs_get_str(my_handle, PASSWORD_KEY, NULL, &password_len);
        err = nvs_get_str(my_handle, PASSWORD_KEY, password, &password_len);
        switch (err) {
            case ESP_OK:
                ESP_LOGI(TAG, "password: %s\n", password);
		allgood++;
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                ESP_LOGI(TAG, "The value is not initialized yet!");
                break;
            default :
                ESP_LOGW(TAG, "Error (%s) reading!", esp_err_to_name(err));
        }

	if (allgood == 2) {
	    memset(&s_dpp_wifi_config, 0, sizeof s_dpp_wifi_config);
	    strcpy((char *) s_dpp_wifi_config.sta.ssid, ssid);
	    strncpy((char *) s_dpp_wifi_config.sta.password, password, sizeof(password));
	    s_retry_num = 0;
	    //esp_wifi_set_config(ESP_IF_WIFI_STA, &s_dpp_wifi_config);
	    //esp_wifi_connect();
	}
    }

    dpp_enrollee_init();
    while (1) {
	vTaskDelay(1000);
    }

}
