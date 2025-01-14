set(requires "")
idf_build_get_property(target IDF_TARGET)

if(${target} STREQUAL "linux")
    list(APPEND requires esp_stubs esp_event protocol_examples_common nvs_flash)
endif()

idf_component_register(SRCS "bt_app_av.c"
                            "bt_app_core.c"
                            "myuart.c"
                            "get_time_and_weather.c"
                            "main.c"
                    PRIV_REQUIRES esp_driver_i2s bt nvs_flash esp_ringbuf esp_driver_dac driver esp-tls esp_http_client json
                    INCLUDE_DIRS ".")
