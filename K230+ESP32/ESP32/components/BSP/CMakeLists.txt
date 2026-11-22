set(src_dirs "")
set(include_dirs "")

file(GLOB led_sources "${CMAKE_CURRENT_LIST_DIR}/LED/*.c")
if(led_sources)
    list(APPEND src_dirs LED)
    list(APPEND include_dirs LED)
endif()

file(GLOB exit_sources "${CMAKE_CURRENT_LIST_DIR}/EXIT/*.c")
if(exit_sources)
    list(APPEND src_dirs EXIT)
    list(APPEND include_dirs EXIT)
endif()

file(GLOB camera_sources "${CMAKE_CURRENT_LIST_DIR}/CAMERA/*.c")
if(camera_sources)
    list(APPEND src_dirs CAMERA)
    list(APPEND include_dirs CAMERA)
endif()

file(GLOB wifi_sources "${CMAKE_CURRENT_LIST_DIR}/WIFI/*.c")
if(wifi_sources)
    list(APPEND src_dirs WIFI)
    list(APPEND include_dirs WIFI)
endif()

file(GLOB web_sources "${CMAKE_CURRENT_LIST_DIR}/WEB/*.c")
if(web_sources)
    list(APPEND src_dirs WEB)
    list(APPEND include_dirs WEB)
endif()

file(GLOB llm_sources "${CMAKE_CURRENT_LIST_DIR}/LLM/*.c")
if(llm_sources)
    list(APPEND src_dirs LLM)
    list(APPEND include_dirs LLM)
endif()

file(GLOB k230_link_sources "${CMAKE_CURRENT_LIST_DIR}/K230_LINK/*.c")
if(k230_link_sources)
    list(APPEND src_dirs K230_LINK)
    list(APPEND include_dirs K230_LINK)
endif()

file(GLOB su03t_link_sources "${CMAKE_CURRENT_LIST_DIR}/SU03T_LINK/*.c")
if(su03t_link_sources)
    list(APPEND src_dirs SU03T_LINK)
    list(APPEND include_dirs SU03T_LINK)
endif()

file(GLOB asrpro_link_sources "${CMAKE_CURRENT_LIST_DIR}/ASRPRO_LINK/*.c")
if(asrpro_link_sources)
    list(APPEND src_dirs ASRPRO_LINK)
    list(APPEND include_dirs ASRPRO_LINK)
endif()

file(GLOB vision_sources "${CMAKE_CURRENT_LIST_DIR}/VISION/*.c")
if(vision_sources)
    list(APPEND src_dirs VISION)
    list(APPEND include_dirs VISION)
endif()

file(GLOB assistant_sources "${CMAKE_CURRENT_LIST_DIR}/ASSISTANT/*.c")
if(assistant_sources)
    list(APPEND src_dirs ASSISTANT)
    list(APPEND include_dirs ASSISTANT)
endif()

file(GLOB face_display_sources "${CMAKE_CURRENT_LIST_DIR}/FACE_DISPLAY/*.c")
if(face_display_sources)
    list(APPEND src_dirs FACE_DISPLAY)
    list(APPEND include_dirs FACE_DISPLAY)
endif()

file(GLOB audio_i2s_sources "${CMAKE_CURRENT_LIST_DIR}/AUDIO_I2S/*.c")
if(audio_i2s_sources)
    list(APPEND src_dirs AUDIO_I2S)
    list(APPEND include_dirs AUDIO_I2S)
endif()

idf_component_register(
    SRC_DIRS ${src_dirs}
    INCLUDE_DIRS ${include_dirs}
    REQUIRES driver esp_driver_spi esp_driver_i2s led_strip esp32-camera esp_wifi esp_event esp_netif esp_http_server nvs_flash esp_timer esp_http_client mbedtls
)
