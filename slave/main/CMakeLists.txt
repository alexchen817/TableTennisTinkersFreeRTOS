idf_component_register(SRCS "main.c"
                    PRIV_REQUIRES nvs_flash esp_event esp_netif servo esp_driver_gpio esp_wifi
                    INCLUDE_DIRS ".")
