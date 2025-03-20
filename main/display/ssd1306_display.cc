#include "ssd1306_display.h"
#include "font_awesome_symbols.h"

#include <esp_log.h>
#include <esp_err.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lvgl_port.h>
#include "assets/lang_config.h"

#define TAG "Ssd1306Display"

LV_FONT_DECLARE(font_awesome_30_1); // 声明一个LVGL字体，用于显示图标

// Ssd1306Display类的构造函数，用于初始化SSD1306显示屏及相关配置
// 参数说明：
// i2c_master_handle: I2C主设备句柄，用于与SSD1306显示屏进行通信
// width: 显示屏的宽度
// height: 显示屏的高度
// mirror_x: 是否水平镜像显示
// mirror_y: 是否垂直镜像显示
// text_font: 用于显示文本的字体
// icon_font: 用于显示图标的字体
Ssd1306Display::Ssd1306Display(void *i2c_master_handle, int width, int height, bool mirror_x, bool mirror_y,
                               const lv_font_t *text_font, const lv_font_t *icon_font)
    : text_font_(text_font), icon_font_(icon_font)
{
    // 将传入的宽度和高度赋值给类的成员变量，方便后续使用
    width_ = width;
    height_ = height;

    // 初始化LVGL图形库，这是一个开源的图形库，用于创建用户界面
    // 输出日志信息，表明正在初始化LVGL
    ESP_LOGI(TAG, "Initialize LVGL");
    // 配置LVGL端口初始化参数，使用默认配置
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    // 设置LVGL任务的优先级为1，优先级越高，任务越先执行
    port_cfg.task_priority = 1;
    // 调用初始化函数，传入配置参数，完成LVGL的初始化
    lvgl_port_init(&port_cfg);

    // 配置SSD1306显示屏的I2C通信参数
    esp_lcd_panel_io_i2c_config_t io_config = {
        // SSD1306的I2C设备地址，大多数情况下为0x3C
        .dev_addr = 0x3C,
        // 颜色传输完成时的回调函数，这里不使用，设置为nullptr
        .on_color_trans_done = nullptr,
        // 用户自定义上下文，这里不使用，设置为nullptr
        .user_ctx = nullptr,
        // 控制阶段的字节数，设置为1
        .control_phase_bytes = 1,
        // DC（数据/命令）位的偏移量，设置为6
        .dc_bit_offset = 6,
        // LCD命令的位数，设置为8位
        .lcd_cmd_bits = 8,
        // LCD参数的位数，设置为8位
        .lcd_param_bits = 8,
        .flags = {
            // DC位在数据传输时为低电平，这里不使用，设置为0
            .dc_low_on_data = 0,
            // 禁用控制阶段，这里不使用，设置为0
            .disable_control_phase = 0,
        },
        // I2C时钟频率，设置为400kHz
        .scl_speed_hz = 400 * 1000,
    };

    // 创建I2C面板IO对象，用于与SSD1306显示屏进行通信
    // 传入I2C主设备句柄和配置参数，将创建的面板IO对象指针存储在panel_io_中
    // ESP_ERROR_CHECK用于检查函数执行结果，如果出现错误则会输出错误信息
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2((i2c_master_bus_t *)i2c_master_handle, &io_config, &panel_io_));

    // 安装SSD1306显示屏驱动
    // 输出日志信息，表明正在安装SSD1306驱动
    ESP_LOGI(TAG, "Install SSD1306 driver");
    // 配置SSD1306面板设备参数，使用默认配置
    esp_lcd_panel_dev_config_t panel_config = {};
    // 不使用复位引脚，将复位引脚的GPIO编号设置为-1
    panel_config.reset_gpio_num = -1;
    // 设置显示屏为单色显示，每个像素用1位表示
    panel_config.bits_per_pixel = 1;

    // 配置SSD1306特有的参数
    esp_lcd_panel_ssd1306_config_t ssd1306_config = {
        // 设置显示屏的高度，将类成员变量height_转换为uint8_t类型
        .height = static_cast<uint8_t>(height_),
    };
    // 将SSD1306特有的配置参数指针赋值给面板设备配置的vendor_config字段
    panel_config.vendor_config = &ssd1306_config;

    // 创建SSD1306面板对象，用于控制显示屏
    // 传入面板IO对象指针和面板设备配置参数，将创建的面板对象指针存储在panel_中
    // ESP_ERROR_CHECK用于检查函数执行结果，如果出现错误则会输出错误信息
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
    // 输出日志信息，表明SSD1306驱动已安装完成
    ESP_LOGI(TAG, "SSD1306 driver installed");

    // 复位显示屏，将显示屏恢复到初始状态
    // ESP_ERROR_CHECK用于检查函数执行结果，如果出现错误则会输出错误信息
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
    // 初始化显示屏，如果初始化失败则输出错误信息并返回
    if (esp_lcd_panel_init(panel_) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize display");
        return;
    }

    // 打开显示屏，使其开始显示内容
    // 输出日志信息，表明正在打开显示屏
    ESP_LOGI(TAG, "Turning display on");
    // 调用函数将显示屏打开，ESP_ERROR_CHECK用于检查函数执行结果
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

    // 将LCD屏幕添加到LVGL图形库中，使其可以使用LVGL进行界面绘制
    // 输出日志信息，表明正在添加LCD屏幕到LVGL
    ESP_LOGI(TAG, "Adding LCD screen");
    // 配置LVGL显示参数
    const lvgl_port_display_cfg_t display_cfg = {
        // 面板IO对象指针，用于与显示屏进行通信
        .io_handle = panel_io_,
        // 面板对象指针，用于控制显示屏
        .panel_handle = panel_,
        // 控制对象指针，这里不使用，设置为nullptr
        .control_handle = nullptr,
        // 显示缓冲区的大小，根据显示屏的宽度和高度计算得出
        .buffer_size = static_cast<uint32_t>(width_ * height_),
        // 不使用双缓冲，设置为false
        .double_buffer = false,
        // 传输大小，设置为0
        .trans_size = 0,
        // 水平分辨率，将类成员变量width_转换为uint32_t类型
        .hres = static_cast<uint32_t>(width_),
        // 垂直分辨率，将类成员变量height_转换为uint32_t类型
        .vres = static_cast<uint32_t>(height_),
        // 设置为单色显示，设置为true
        .monochrome = true,
        .rotation = {
            // 是否交换X和Y轴，设置为false
            .swap_xy = false,
            // 是否水平镜像显示，根据传入的参数设置
            .mirror_x = mirror_x,
            // 是否垂直镜像显示，根据传入的参数设置
            .mirror_y = mirror_y,
        },
        .flags = {
            // 使用DMA缓冲区，设置为1
            .buff_dma = 1,
            // 不使用SPIRAM缓冲区，设置为0
            .buff_spiram = 0,
            // 不使用软件旋转，设置为0
            .sw_rotate = 0,
            // 不进行全刷新，设置为0
            .full_refresh = 0,
            // 不使用直接模式，设置为0
            .direct_mode = 0,
        },
    };

    // 将显示配置添加到LVGL中，返回显示对象指针
    // 如果添加失败则输出错误信息并返回
    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr)
    {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    // 根据显示屏的高度设置不同的UI布局
    if (height_ == 64)
    {
        // 如果显示屏高度为64像素，则调用SetupUI_128x64函数进行UI布局
        SetupUI_128x64();
    }
    else
    {
        // 如果显示屏高度不为64像素，则调用SetupUI_128x32函数进行UI布局
        SetupUI_128x32();
    }
}

// Ssd1306Display类的析构函数
// 析构函数的作用是在对象生命周期结束时，负责释放对象所占用的资源，避免内存泄漏和资源浪费。
Ssd1306Display::~Ssd1306Display()
{
    // 释放LVGL对象
    // LVGL（LittlevGL）是一个开源的图形库，用于创建嵌入式系统的用户界面。
    // 在Ssd1306Display类中，使用LVGL创建了多个UI对象，如内容区域、状态栏、侧边栏和容器等。
    // 在对象销毁时，需要释放这些LVGL对象占用的资源。
    if (content_ != nullptr)
    {
        // lv_obj_del是LVGL库提供的函数，用于删除一个LVGL对象。
        // content_是一个指向LVGL对象的指针，代表内容区域。
        // 通过检查content_是否为空指针，可以避免在对象未初始化或已经被释放的情况下调用删除函数。
        lv_obj_del(content_);
    }
    if (status_bar_ != nullptr)
    {
        // status_bar_是指向状态栏LVGL对象的指针。
        // 同样，先检查指针是否为空，然后调用lv_obj_del函数删除该对象。
        lv_obj_del(status_bar_);
    }
    if (side_bar_ != nullptr)
    {
        // side_bar_是指向侧边栏LVGL对象的指针。
        // 进行空指针检查后，使用lv_obj_del函数释放该对象。
        lv_obj_del(side_bar_);
    }
    if (container_ != nullptr)
    {
        // container_是指向容器LVGL对象的指针。
        // 容器通常是其他UI元素的父对象，确保在删除子对象后再删除容器，以避免内存泄漏。
        lv_obj_del(container_);
    }

    // 释放面板和IO对象
    // 在使用ESP-IDF开发时，通过esp_lcd_panel和esp_lcd_panel_io来管理显示屏的面板和IO操作。
    // 在对象销毁时，需要释放这些硬件相关的资源。
    if (panel_ != nullptr)
    {
        // esp_lcd_panel_del是ESP-IDF提供的函数，用于删除一个显示屏面板对象。
        // panel_是指向显示屏面板对象的指针，通过调用该函数释放面板资源。
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_ != nullptr)
    {
        // esp_lcd_panel_io_del是ESP-IDF提供的函数，用于删除一个显示屏IO对象。
        // panel_io_是指向显示屏IO对象的指针，调用该函数释放IO资源。
        esp_lcd_panel_io_del(panel_io_);
    }
    // 反初始化LVGL
    // lvgl_port_deinit是自定义的函数，用于反初始化LVGL库。
    // 在释放所有LVGL对象和硬件资源后，调用该函数来完成LVGL库的清理工作，确保系统资源被正确释放。
    lvgl_port_deinit();
}

// 锁定LVGL端口，防止多线程冲突
// 参数：
// - timeout_ms: 超时时间（毫秒）
// 返回值：
// - 成功返回true，失败返回false
bool Ssd1306Display::Lock(int timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

// 解锁LVGL端口
void Ssd1306Display::Unlock()
{
    lvgl_port_unlock();
}

// 设置聊天消息内容
// 参数：
// - role: 角色（未使用）
// - content: 消息内容
void Ssd1306Display::SetChatMessage(const char *role, const char *content)
{
    DisplayLockGuard lock(this); // 锁定显示
    if (chat_message_label_ == nullptr)
    {
        return;
    }
    if (content_right_ == nullptr)
    {
        lv_label_set_text(chat_message_label_, content); // 设置消息内容
    }
    else
    {
        if (content == nullptr || content[0] == '\0')
        {
            lv_obj_add_flag(content_right_, LV_OBJ_FLAG_HIDDEN); // 隐藏右侧容器
        }
        else
        {
            lv_label_set_text(chat_message_label_, content);       // 设置消息内容
            lv_obj_clear_flag(content_right_, LV_OBJ_FLAG_HIDDEN); // 显示右侧容器
        }
    }
}

// 设置128x64分辨率的UI布局
void Ssd1306Display::SetupUI_128x64()
{
    DisplayLockGuard lock(this); // 锁定显示

    auto screen = lv_screen_active();                         // 获取当前活动的屏幕
    lv_obj_set_style_text_font(screen, text_font_, 0);        // 设置屏幕字体
    lv_obj_set_style_text_color(screen, lv_color_black(), 0); // 设置文本颜色为黑色

    /* 容器 */
    container_ = lv_obj_create(screen);                    // 创建容器
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);   // 设置容器大小
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN); // 设置容器为垂直布局
    lv_obj_set_style_pad_all(container_, 0, 0);            // 设置容器内边距为0
    lv_obj_set_style_border_width(container_, 0, 0);       // 设置容器边框宽度为0
    lv_obj_set_style_pad_row(container_, 0, 0);            // 设置容器行间距为0

    /* 状态栏 */
    status_bar_ = lv_obj_create(container_);          // 创建状态栏
    lv_obj_set_size(status_bar_, LV_HOR_RES, 16);     // 设置状态栏大小
    lv_obj_set_style_border_width(status_bar_, 0, 0); // 设置状态栏边框宽度为0
    lv_obj_set_style_pad_all(status_bar_, 0, 0);      // 设置状态栏内边距为0
    lv_obj_set_style_radius(status_bar_, 0, 0);       // 设置状态栏圆角为0

    /* 内容区域 */
    content_ = lv_obj_create(container_);                                // 创建内容区域
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);          // 关闭滚动条
    lv_obj_set_style_radius(content_, 0, 0);                             // 设置内容区域圆角为0
    lv_obj_set_style_pad_all(content_, 0, 0);                            // 设置内容区域内边距为0
    lv_obj_set_width(content_, LV_HOR_RES);                              // 设置内容区域宽度
    lv_obj_set_flex_grow(content_, 1);                                   // 设置内容区域自动扩展
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_ROW);                    // 设置内容区域为水平布局
    lv_obj_set_style_flex_main_place(content_, LV_FLEX_ALIGN_CENTER, 0); // 设置内容区域居中对齐

    // 创建左侧固定宽度的容器
    content_left_ = lv_obj_create(content_);             // 创建左侧容器
    lv_obj_set_size(content_left_, 32, LV_SIZE_CONTENT); // 设置左侧容器宽度为32像素
    lv_obj_set_style_pad_all(content_left_, 0, 0);       // 设置左侧容器内边距为0
    lv_obj_set_style_border_width(content_left_, 0, 0);  // 设置左侧容器边框宽度为0

    emotion_label_ = lv_label_create(content_left_);                   // 创建表情标签
    lv_obj_set_style_text_font(emotion_label_, &font_awesome_30_1, 0); // 设置表情标签字体
    lv_label_set_text(emotion_label_, FONT_AWESOME_AI_CHIP);           // 设置表情标签文本
    lv_obj_center(emotion_label_);                                     // 居中对齐表情标签
    lv_obj_set_style_pad_top(emotion_label_, 8, 0);                    // 设置表情标签顶部内边距

    // 创建右侧可扩展的容器
    content_right_ = lv_obj_create(content_);                          // 创建右侧容器
    lv_obj_set_size(content_right_, LV_SIZE_CONTENT, LV_SIZE_CONTENT); // 设置右侧容器大小
    lv_obj_set_style_pad_all(content_right_, 0, 0);                    // 设置右侧容器内边距为0
    lv_obj_set_style_border_width(content_right_, 0, 0);               // 设置右侧容器边框宽度为0
    lv_obj_set_flex_grow(content_right_, 1);                           // 设置右侧容器自动扩展
    lv_obj_add_flag(content_right_, LV_OBJ_FLAG_HIDDEN);               // 初始隐藏右侧容器

    chat_message_label_ = lv_label_create(content_right_);                      // 创建聊天消息标签
    lv_label_set_text(chat_message_label_, "");                                 // 设置聊天消息标签文本为空
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR); // 设置聊天消息标签为循环滚动模式
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_LEFT, 0);    // 设置聊天消息标签文本左对齐
    lv_obj_set_width(chat_message_label_, width_ - 32);                         // 设置聊天消息标签宽度
    lv_obj_set_style_pad_top(chat_message_label_, 14, 0);                       // 设置聊天消息标签顶部内边距

    // 延迟一定的时间后开始滚动字幕
    static lv_anim_t a;                                                                                       // 创建动画对象
    lv_anim_init(&a);                                                                                         // 初始化动画
    lv_anim_set_delay(&a, 1000);                                                                              // 设置动画延迟1秒
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);                                                    // 设置动画无限循环
    lv_obj_set_style_anim(chat_message_label_, &a, LV_PART_MAIN);                                             // 将动画应用到聊天消息标签
    lv_obj_set_style_anim_duration(chat_message_label_, lv_anim_speed_clamped(60, 300, 60000), LV_PART_MAIN); // 设置动画持续时间

    /* 状态栏 */
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW); // 设置状态栏为水平布局
    lv_obj_set_style_pad_all(status_bar_, 0, 0);         // 设置状态栏内边距为0
    lv_obj_set_style_border_width(status_bar_, 0, 0);    // 设置状态栏边框宽度为0
    lv_obj_set_style_pad_column(status_bar_, 0, 0);      // 设置状态栏列间距为0

    network_label_ = lv_label_create(status_bar_);             // 创建网络状态标签
    lv_label_set_text(network_label_, "");                     // 设置网络状态标签文本为空
    lv_obj_set_style_text_font(network_label_, icon_font_, 0); // 设置网络状态标签字体

    notification_label_ = lv_label_create(status_bar_);                        // 创建通知标签
    lv_obj_set_flex_grow(notification_label_, 1);                              // 设置通知标签自动扩展
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0); // 设置通知标签文本居中对齐
    lv_label_set_text(notification_label_, "");                                // 设置通知标签文本为空
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);                  // 初始隐藏通知标签

    status_label_ = lv_label_create(status_bar_);                        // 创建状态标签
    lv_obj_set_flex_grow(status_label_, 1);                              // 设置状态标签自动扩展
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);       // 设置状态标签文本为初始化中
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0); // 设置状态标签文本居中对齐

    mute_label_ = lv_label_create(status_bar_);             // 创建静音标签
    lv_label_set_text(mute_label_, "");                     // 设置静音标签文本为空
    lv_obj_set_style_text_font(mute_label_, icon_font_, 0); // 设置静音标签字体

    battery_label_ = lv_label_create(status_bar_);             // 创建电池标签
    lv_label_set_text(battery_label_, "");                     // 设置电池标签文本为空
    lv_obj_set_style_text_font(battery_label_, icon_font_, 0); // 设置电池标签字体
}

// 设置128x32分辨率的UI布局
void Ssd1306Display::SetupUI_128x32()
{
    DisplayLockGuard lock(this); // 锁定显示

    auto screen = lv_screen_active();                  // 获取当前活动的屏幕
    lv_obj_set_style_text_font(screen, text_font_, 0); // 设置屏幕字体

    /* 容器 */
    container_ = lv_obj_create(screen);                  // 创建容器
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES); // 设置容器大小
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_ROW);  // 设置容器为水平布局
    lv_obj_set_style_pad_all(container_, 0, 0);          // 设置容器内边距为0
    lv_obj_set_style_border_width(container_, 0, 0);     // 设置容器边框宽度为0
    lv_obj_set_style_pad_column(container_, 0, 0);       // 设置容器列间距为0

    /* 左侧边栏 */
    side_bar_ = lv_obj_create(container_);                // 创建左侧边栏
    lv_obj_set_flex_grow(side_bar_, 1);                   // 设置左侧边栏自动扩展
    lv_obj_set_flex_flow(side_bar_, LV_FLEX_FLOW_COLUMN); // 设置左侧边栏为垂直布局
    lv_obj_set_style_pad_all(side_bar_, 0, 0);            // 设置左侧边栏内边距为0
    lv_obj_set_style_border_width(side_bar_, 0, 0);       // 设置左侧边栏边框宽度为0
    lv_obj_set_style_radius(side_bar_, 0, 0);             // 设置左侧边栏圆角为0
    lv_obj_set_style_pad_row(side_bar_, 0, 0);            // 设置左侧边栏行间距为0

    /* 右侧表情标签 */
    content_ = lv_obj_create(container_);          // 创建内容区域
    lv_obj_set_size(content_, 32, 32);             // 设置内容区域大小
    lv_obj_set_style_pad_all(content_, 0, 0);      // 设置内容区域内边距为0
    lv_obj_set_style_border_width(content_, 0, 0); // 设置内容区域边框宽度为0
    lv_obj_set_style_radius(content_, 0, 0);       // 设置内容区域圆角为0

    emotion_label_ = lv_label_create(content_);                        // 创建表情标签
    lv_obj_set_style_text_font(emotion_label_, &font_awesome_30_1, 0); // 设置表情标签字体
    lv_label_set_text(emotion_label_, FONT_AWESOME_AI_CHIP);           // 设置表情标签文本
    lv_obj_center(emotion_label_);                                     // 居中对齐表情标签

    /* 状态栏 */
    status_bar_ = lv_obj_create(side_bar_);              // 创建状态栏
    lv_obj_set_size(status_bar_, LV_SIZE_CONTENT, 16);   // 设置状态栏大小
    lv_obj_set_style_radius(status_bar_, 0, 0);          // 设置状态栏圆角为0
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW); // 设置状态栏为水平布局
    lv_obj_set_style_pad_all(status_bar_, 0, 0);         // 设置状态栏内边距为0
    lv_obj_set_style_border_width(status_bar_, 0, 0);    // 设置状态栏边框宽度为0
    lv_obj_set_style_pad_column(status_bar_, 0, 0);      // 设置状态栏列间距为0

    network_label_ = lv_label_create(status_bar_);             // 创建网络状态标签
    lv_label_set_text(network_label_, "");                     // 设置网络状态标签文本为空
    lv_obj_set_style_text_font(network_label_, icon_font_, 0); // 设置网络状态标签字体

    mute_label_ = lv_label_create(status_bar_);             // 创建静音标签
    lv_label_set_text(mute_label_, "");                     // 设置静音标签文本为空
    lv_obj_set_style_text_font(mute_label_, icon_font_, 0); // 设置静音标签字体

    battery_label_ = lv_label_create(status_bar_);             // 创建电池标签
    lv_label_set_text(battery_label_, "");                     // 设置电池标签文本为空
    lv_obj_set_style_text_font(battery_label_, icon_font_, 0); // 设置电池标签字体

    status_label_ = lv_label_create(status_bar_);                  // 创建状态标签
    lv_obj_set_style_pad_left(status_label_, 2, 0);                // 设置状态标签左侧内边距
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING); // 设置状态标签文本为初始化中

    notification_label_ = lv_label_create(status_bar_);       // 创建通知标签
    lv_label_set_text(notification_label_, "");               // 设置通知标签文本为空
    lv_obj_set_style_pad_left(notification_label_, 2, 0);     // 设置通知标签左侧内边距
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN); // 初始隐藏通知标签

    chat_message_label_ = lv_label_create(side_bar_);                           // 创建聊天消息标签
    lv_obj_set_flex_grow(chat_message_label_, 1);                               // 设置聊天消息标签自动扩展
    lv_obj_set_width(chat_message_label_, width_ - 32);                         // 设置聊天消息标签宽度
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR); // 设置聊天消息标签为循环滚动模式
    lv_label_set_text(chat_message_label_, "");                                 // 设置聊天消息标签文本为空

    // 延迟一定的时间后开始滚动字幕
    static lv_anim_t a;                                                                                       // 创建动画对象
    lv_anim_init(&a);                                                                                         // 初始化动画
    lv_anim_set_delay(&a, 1000);                                                                              // 设置动画延迟1秒
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);                                                    // 设置动画无限循环
    lv_obj_set_style_anim(chat_message_label_, &a, LV_PART_MAIN);                                             // 将动画应用到聊天消息标签
    lv_obj_set_style_anim_duration(chat_message_label_, lv_anim_speed_clamped(60, 300, 60000), LV_PART_MAIN); // 设置动画持续时间
}