# 设置源文件列表
# 这些源文件包含了项目中各个模块的实现代码，如音频编解码器、LED 控制、显示模块、协议处理等
set(SOURCES "audio_codecs/audio_codec.cc"
            "audio_codecs/no_audio_codec.cc"
            "audio_codecs/box_audio_codec.cc"
            "audio_codecs/es8311_audio_codec.cc"
            "audio_codecs/es8388_audio_codec.cc"
            "audio_codecs/cores3_audio_codec.cc"
            "led/single_led.cc"
            "led/circular_strip.cc"
            "display/display.cc"
            "display/no_display.cc"
            "display/lcd_display.cc"
            "display/ssd1306_display.cc"
            "protocols/protocol.cc"
            "iot/thing.cc"
            "iot/thing_manager.cc"
            "system_info.cc"
            "application.cc"
            "ota.cc"
            "settings.cc"
            "background_task.cc"
            "main.cc"
            )

# 设置包含目录列表
# 这些目录包含了项目中各个模块的头文件，编译器在编译源文件时会在这些目录中查找头文件
set(INCLUDE_DIRS "." "display" "audio_codecs" "protocols" "audio_processing")

# 添加 IOT 相关文件
# 使用 file(GLOB) 命令查找指定目录下所有以 .cc 结尾的文件，并将其存储在 IOT_SOURCES 变量中
file(GLOB IOT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/iot/things/*.cc)
# 将 IOT_SOURCES 中的文件添加到 SOURCES 列表中
list(APPEND SOURCES ${IOT_SOURCES})

# 添加板级公共文件
# 使用 file(GLOB) 命令查找指定目录下所有以 .cc 结尾的文件，并将其存储在 BOARD_COMMON_SOURCES 变量中
file(GLOB BOARD_COMMON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/boards/common/*.cc)
# 将 BOARD_COMMON_SOURCES 中的文件添加到 SOURCES 列表中
list(APPEND SOURCES ${BOARD_COMMON_SOURCES})
# 将板级公共文件所在目录添加到 INCLUDE_DIRS 列表中
list(APPEND INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/boards/common)

# 根据 BOARD_TYPE 配置添加对应的板级文件
# CONFIG_BOARD_TYPE_* 是通过 Kconfig 配置系统设置的选项，用于选择不同的开发板类型
if(CONFIG_BOARD_TYPE_BREAD_COMPACT_WIFI)
    set(BOARD_TYPE "bread-compact-wifi")
elseif(CONFIG_BOARD_TYPE_BREAD_COMPACT_ML307)
    set(BOARD_TYPE "bread-compact-ml307")
elseif(CONFIG_BOARD_TYPE_BREAD_COMPACT_ESP32)
    set(BOARD_TYPE "bread-compact-esp32")    
elseif(CONFIG_BOARD_TYPE_ESP_BOX_3)
    set(BOARD_TYPE "esp-box-3")
elseif(CONFIG_BOARD_TYPE_ESP_BOX)
    set(BOARD_TYPE "esp-box")
elseif(CONFIG_BOARD_TYPE_KEVIN_BOX_1)
    set(BOARD_TYPE "kevin-box-1")
elseif(CONFIG_BOARD_TYPE_KEVIN_BOX_2)
    set(BOARD_TYPE "kevin-box-2")
elseif(CONFIG_BOARD_TYPE_KEVIN_C3)
    set(BOARD_TYPE "kevin-c3") 
elseif(CONFIG_BOARD_TYPE_KEVIN_SP_V3_DEV)
    set(BOARD_TYPE "kevin-sp-v3-dev")
elseif(CONFIG_BOARD_TYPE_KEVIN_YUYING_313LCD)
    set(BOARD_TYPE "kevin-yuying-313lcd")
elseif(CONFIG_BOARD_TYPE_LICHUANG_DEV)
    set(BOARD_TYPE "lichuang-dev")
elseif(CONFIG_BOARD_TYPE_LICHUANG_C3_DEV)
    set(BOARD_TYPE "lichuang-c3-dev")
elseif(CONFIG_BOARD_TYPE_MAGICLICK_2P4)
    set(BOARD_TYPE "magiclick-2p4")
elseif(CONFIG_BOARD_TYPE_MAGICLICK_2P5)
    set(BOARD_TYPE "magiclick-2p5")
elseif(CONFIG_BOARD_TYPE_MAGICLICK_C3)
    set(BOARD_TYPE "magiclick-c3")
elseif(CONFIG_BOARD_TYPE_MAGICLICK_C3_V2)
    set(BOARD_TYPE "magiclick-c3-v2")
elseif(CONFIG_BOARD_TYPE_M5STACK_CORE_S3)
    set(BOARD_TYPE "m5stack-core-s3")
elseif(CONFIG_BOARD_TYPE_ATOMS3_ECHO_BASE)
    set(BOARD_TYPE "atoms3-echo-base")
elseif(CONFIG_BOARD_TYPE_ATOMS3R_ECHO_BASE)
    set(BOARD_TYPE "atoms3r-echo-base")
elseif(CONFIG_BOARD_TYPE_ATOMMATRIX_ECHO_BASE)
    set(BOARD_TYPE "atommatrix-echo-base") 
elseif(CONFIG_BOARD_TYPE_XMINI_C3)
    set(BOARD_TYPE "xmini-c3")
elseif(CONFIG_BOARD_TYPE_ESP32S3_KORVO2_V3)
    set(BOARD_TYPE "esp32s3-korvo2-v3")
elseif(CONFIG_BOARD_TYPE_ESP_SPARKBOT)
    set(BOARD_TYPE "esp-sparkbot")    
elseif(CONFIG_BOARD_TYPE_ESP32S3_Touch_AMOLED_1_8)
    set(BOARD_TYPE "esp32-s3-touch-amoled-1.8")
elseif(CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_1_85C)
    set(BOARD_TYPE "esp32-s3-touch-lcd-1.85c")
elseif(CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_1_85)
    set(BOARD_TYPE "esp32-s3-touch-lcd-1.85")
elseif(CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_1_46)
    set(BOARD_TYPE "esp32-s3-touch-lcd-1.46")
elseif(CONFIG_BOARD_TYPE_BREAD_COMPACT_WIFI_LCD)
    set(BOARD_TYPE "bread-compact-wifi-lcd")    
elseif(CONFIG_BOARD_TYPE_TUDOUZI)
    set(BOARD_TYPE "tudouzi") 
elseif(CONFIG_BOARD_TYPE_LILYGO_T_CIRCLE_S3)
    set(BOARD_TYPE "lilygo-t-circle-s3") 
elseif(CONFIG_BOARD_TYPE_LILYGO_T_CAMERAPLUS_S3)
    set(BOARD_TYPE "lilygo-t-cameraplus-s3") 
elseif(CONFIG_BOARD_TYPE_MOVECALL_MOJI_ESP32S3)
    set(BOARD_TYPE "movecall-moji-esp32s3") 
elseif(CONFIG_BOARD_TYPE_ATK_DNESP32S3)
    set(BOARD_TYPE "atk-dnesp32s3")
elseif(CONFIG_BOARD_TYPE_ATK_DNESP32S3_BOX)
    set(BOARD_TYPE "atk-dnesp32s3-box") 
elseif(CONFIG_BOARD_TYPE_DU_CHATX)
    set(BOARD_TYPE "du-chatx")     
elseif(CONFIG_BOARD_TYPE_ESP32S3_Taiji_Pi)
    set(BOARD_TYPE "taiji-pi-s3")
elseif(CONFIG_BOARD_TYPE_XINGZHI_Cube_0_96OLED_WIFI)
    set(BOARD_TYPE "xingzhi-cube-0.96oled-wifi")
elseif(CONFIG_BOARD_TYPE_XINGZHI_Cube_0_96OLED_ML307)
    set(BOARD_TYPE "xingzhi-cube-0.96oled-ml307")
elseif(CONFIG_BOARD_TYPE_XINGZHI_Cube_1_54TFT_WIFI)
    set(BOARD_TYPE "xingzhi-cube-1.54tft-wifi")
elseif(CONFIG_BOARD_TYPE_XINGZHI_Cube_1_54TFT_ML307)
    set(BOARD_TYPE "xingzhi-cube-1.54tft-ml307")
elseif(CONFIG_BOARD_TYPE_SENSECAP_WATCHER)
    set(BOARD_TYPE "sensecap-watcher")
endif()
# 使用 file(GLOB) 命令查找指定开发板类型目录下所有以 .cc 和 .c 结尾的文件，并将其存储在 BOARD_SOURCES 变量中
file(GLOB BOARD_SOURCES 
    ${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD_TYPE}/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD_TYPE}/*.c
)
# 将 BOARD_SOURCES 中的文件添加到 SOURCES 列表中
list(APPEND SOURCES ${BOARD_SOURCES})

# 根据连接类型配置添加对应的协议源文件
# CONFIG_CONNECTION_TYPE_* 是通过 Kconfig 配置系统设置的选项，用于选择不同的连接协议
if(CONFIG_CONNECTION_TYPE_MQTT_UDP)
    list(APPEND SOURCES "protocols/mqtt_protocol.cc")
elseif(CONFIG_CONNECTION_TYPE_WEBSOCKET)
    list(APPEND SOURCES "protocols/websocket_protocol.cc")
endif()

# 根据是否使用音频处理器配置添加对应的源文件
# CONFIG_USE_AUDIO_PROCESSOR 是通过 Kconfig 配置系统设置的选项，用于决定是否使用音频处理器
if(CONFIG_USE_AUDIO_PROCESSOR)
    list(APPEND SOURCES "audio_processing/audio_processor.cc")
endif()
# 根据是否使用唤醒词检测配置添加对应的源文件
# CONFIG_USE_WAKE_WORD_DETECT 是通过 Kconfig 配置系统设置的选项，用于决定是否使用唤醒词检测
if(CONFIG_USE_WAKE_WORD_DETECT)
    list(APPEND SOURCES "audio_processing/wake_word_detect.cc")
endif()

# 根据Kconfig选择语言目录
# CONFIG_LANGUAGE_* 是通过 Kconfig 配置系统设置的选项，用于选择不同的语言
if(CONFIG_LANGUAGE_ZH_CN)
    set(LANG_DIR "zh-CN")
elseif(CONFIG_LANGUAGE_EN_US)
    set(LANG_DIR "en-US")
endif()

# 定义生成路径
# LANG_JSON 是语言配置的 JSON 文件路径
set(LANG_JSON "${CMAKE_CURRENT_SOURCE_DIR}/assets/${LANG_DIR}/language.json")
# LANG_HEADER 是生成的语言配置头文件路径
set(LANG_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/assets/lang_config.h")
# 使用 file(GLOB) 命令查找指定语言目录下所有以 .p3 结尾的文件，并将其存储在 LANG_SOUNDS 变量中
file(GLOB LANG_SOUNDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/${LANG_DIR}/*.p3)
# 使用 file(GLOB) 命令查找公共声音目录下所有以 .p3 结尾的文件，并将其存储在 COMMON_SOUNDS 变量中
file(GLOB COMMON_SOUNDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/common/*.p3)

# 如果目标芯片是 ESP32，则排除特定文件
# CONFIG_IDF_TARGET_ESP32 是通过 Kconfig 配置系统设置的选项，用于指定目标芯片为 ESP32
if(CONFIG_IDF_TARGET_ESP32)
    # 从 SOURCES 列表中移除不适合 ESP32 的源文件
    list(REMOVE_ITEM SOURCES "audio_codecs/box_audio_codec.cc"
                             "audio_codecs/cores3_audio_codec.cc"
                             "audio_codecs/es8388_audio_codec.cc")
endif()

# 注册 ESP-IDF 组件
# SRCS 指定源文件列表
# EMBED_FILES 指定要嵌入到组件中的文件，这里是语言声音文件
# INCLUDE_DIRS 指定包含目录列表
# WHOLE_ARCHIVE 表示链接时包含整个静态库
idf_component_register(SRCS ${SOURCES}
                    EMBED_FILES ${LANG_SOUNDS} ${COMMON_SOUNDS}
                    INCLUDE_DIRS ${INCLUDE_DIRS}
                    WHOLE_ARCHIVE
                    )

# 使用 target_compile_definitions 来定义编译时的宏
# PRIVATE 表示这些宏只对当前组件有效
# 定义 BOARD_TYPE 和 BOARD_NAME 宏，方便在代码中使用
target_compile_definitions(${COMPONENT_LIB}
                    PRIVATE BOARD_TYPE=\"${BOARD_TYPE}\" BOARD_NAME=\"${BOARD_NAME}\"
                    )

# 添加自定义命令，用于生成语言配置头文件
# OUTPUT 指定生成的文件
# COMMAND 指定要执行的命令，这里是调用 Python 脚本生成头文件
# DEPENDS 指定依赖文件，当这些文件发生变化时，会重新执行命令
# COMMENT 是命令的描述信息
add_custom_command(
    OUTPUT ${LANG_HEADER}
    COMMAND python ${PROJECT_DIR}/scripts/gen_lang.py
            --input "${LANG_JSON}"
            --output "${LANG_HEADER}"
    DEPENDS 
        ${LANG_JSON}
        ${PROJECT_DIR}/scripts/gen_lang.py
    COMMENT "Generating ${LANG_DIR} language config"
)

# 强制建立生成依赖
# 添加一个自定义目标，确保在构建过程中生成语言配置头文件
add_custom_target(lang_header ALL
    DEPENDS ${LANG_HEADER}
)