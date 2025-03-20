#include "oled_display.h"
#include "font_awesome_symbols.h"
#include "assets/lang_config.h"

#include <string>
#include <algorithm>

#include <esp_log.h>
#include <esp_err.h>
#include <esp_lvgl_port.h>

#define TAG "OledDisplay"

LV_FONT_DECLARE(font_awesome_30_1);
// OledDisplay 类的构造函数
// 参数：
// panel_io: ESP-IDF 显示面板IO句柄
// panel: ESP-IDF 显示面板句柄
// width: 屏幕宽度
// height: 屏幕高度
// mirror_x: X轴镜像标志
// mirror_y: Y轴镜像标志
// fonts: 显示字体配置结构体
OledDisplay::OledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
    int width, int height, bool mirror_x, bool mirror_y, DisplayFonts fonts)
    : panel_io_(panel_io), panel_(panel), fonts_(fonts) {
    width_ = width;
    height_ = height;

    // 初始化LVGL图形库
    ESP_LOGI(TAG, "Initialize LVGL");
    // 配置LVGL端口参数
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;  // 设置LVGL任务优先级
    lvgl_port_init(&port_cfg);  // 初始化LVGL端口

    // 添加LCD屏幕显示
    ESP_LOGI(TAG, "Adding LCD screen");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,          // 显示面板IO句柄
        .panel_handle = panel_,          // 显示面板句柄
        .control_handle = nullptr,       // 控制句柄（未使用）
        .buffer_size = static_cast<uint32_t>(width_ * height_),  // 显示缓冲区大小
        .double_buffer = false,          // 禁用双缓冲
        .trans_size = 0,                 // 传输大小（未使用）
        .hres = static_cast<uint32_t>(width_),  // 水平分辨率
        .vres = static_cast<uint32_t>(height_), // 垂直分辨率
        .monochrome = true,              // 单色显示模式
        .rotation = {
            .swap_xy = false,           // 不交换XY轴
            .mirror_x = mirror_x,       // X轴镜像配置
            .mirror_y = mirror_y,       // Y轴镜像配置
        },
        .flags = {
            .buff_dma = 1,              // 使用DMA传输缓冲区
            .buff_spiram = 0,           // 不使用SPIRAM
            .sw_rotate = 0,             // 禁用软件旋转
            .full_refresh = 0,          // 禁用全屏刷新
            .direct_mode = 0,           // 禁用直接模式
        },
    };

    // 添加LVGL显示设备
    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    // 根据屏幕高度初始化不同UI布局
    if (height_ == 64) {
        SetupUI_128x64();  // 128x64分辨率UI布局
    } else {
        SetupUI_128x32();  // 128x32分辨率UI布局
    }
}

// OledDisplay 类的析构函数
OledDisplay::~OledDisplay() {
    // 销毁UI元素
    if (content_ != nullptr) lv_obj_del(content_);
    if (status_bar_ != nullptr) lv_obj_del(status_bar_);
    if (side_bar_ != nullptr) lv_obj_del(side_bar_);
    if (container_ != nullptr) lv_obj_del(container_);

    // 释放显示硬件资源
    if (panel_ != nullptr) esp_lcd_panel_del(panel_);
    if (panel_io_ != nullptr) esp_lcd_panel_io_del(panel_io_);
    
    // 反初始化LVGL库
    lvgl_port_deinit();
}

// 锁定显示资源（线程安全）
// 参数：timeout_ms 超时时间（毫秒）
// 返回值：成功锁定返回true，超时返回false
bool OledDisplay::Lock(int timeout_ms) {
    return lvgl_port_lock(timeout_ms);
}

// 解锁显示资源
void OledDisplay::Unlock() {
    lvgl_port_unlock();
}

// 设置聊天消息显示
// 参数：
// role: 消息角色（未使用）
// content: 消息内容字符串
void OledDisplay::SetChatMessage(const char* role, const char* content) {
    // 使用RAII风格锁保护显示操作
    DisplayLockGuard lock(this);
    
    if (chat_message_label_ == nullptr) return;

    // 处理消息内容（替换换行符为空格）
    std::string content_str = content;
    std::replace(content_str.begin(), content_str.end(), '\n', ' ');

    // 更新消息显示
    if (content_right_ == nullptr) {
        lv_label_set_text(chat_message_label_, content_str.c_str());
    } else {
        if (content == nullptr || content[0] == '\0') {
            // 隐藏消息区域
            lv_obj_add_flag(content_right_, LV_OBJ_FLAG_HIDDEN);
        } else {
            // 显示消息内容
            lv_label_set_text(chat_message_label_, content_str.c_str());
            lv_obj_clear_flag(content_right_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
// 定义 OledDisplay 类的成员函数 SetupUI_128x64
// 该函数的作用是为 128x64 分辨率的 OLED 显示屏设置用户界面
void OledDisplay::SetupUI_128x64() {
    // 创建一个显示锁保护对象，用于确保在设置 UI 期间对显示的操作是线程安全的
    // 原理：在多线程环境中，防止多个线程同时对显示进行操作，避免界面显示混乱
    DisplayLockGuard lock(this);

    // 获取当前活动的屏幕对象
    // 原理：LVGL（LittlevGL）是一个开源的图形库，通过 lv_screen_active() 函数获取当前正在显示的屏幕，后续的 UI 元素将基于此屏幕创建
    auto screen = lv_screen_active();

    // 设置屏幕的文本字体为预先定义的字体
    // 原理：lv_obj_set_style_text_font 函数用于设置对象（这里是屏幕）的文本字体，0 表示默认状态
    lv_obj_set_style_text_font(screen, fonts_.text_font, 0);
    // 设置屏幕的文本颜色为黑色
    // 原理：lv_obj_set_style_text_color 函数用于设置对象的文本颜色，通过 lv_color_black() 函数获取黑色颜色值
    lv_obj_set_style_text_color(screen, lv_color_black(), 0);

    /* Container */
    // 创建一个容器对象，作为整个 UI 的根容器
    // 原理：容器可以作为其他 UI 元素的父对象，方便对多个元素进行布局和管理
    container_ = lv_obj_create(screen);
    // 设置容器的大小为屏幕的宽度和高度
    // 原理：LV_HOR_RES 和 LV_VER_RES 分别表示屏幕的水平和垂直分辨率，确保容器充满整个屏幕
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    // 设置容器的布局方式为垂直列布局
    // 原理：LV_FLEX_FLOW_COLUMN 表示子元素将按照垂直方向排列
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    // 设置容器的内边距为 0
    // 原理：内边距是指容器内部元素与容器边界之间的距离，设置为 0 可以使元素紧贴容器边界
    lv_obj_set_style_pad_all(container_, 0, 0);
    // 设置容器的边框宽度为 0
    // 原理：去除容器的边框，使界面更加简洁
    lv_obj_set_style_border_width(container_, 0, 0);
    // 设置容器内元素的行间距为 0
    // 原理：在垂直布局中，行间距控制着子元素之间的垂直距离
    lv_obj_set_style_pad_row(container_, 0, 0);

    /* Status bar */
    // 创建一个状态栏对象，作为容器的子对象
    // 原理：状态栏通常用于显示一些系统状态信息，如网络状态、电池电量等
    status_bar_ = lv_obj_create(container_);
    // 设置状态栏的宽度为屏幕宽度，高度为 16 像素
    // 原理：状态栏通常位于屏幕顶部，宽度与屏幕相同，高度根据设计需求设置
    lv_obj_set_size(status_bar_, LV_HOR_RES, 16);
    // 设置状态栏的边框宽度为 0
    // 原理：去除状态栏的边框，使界面更加简洁
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    // 设置状态栏的内边距为 0
    // 原理：使状态栏内的元素紧贴边界
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    // 设置状态栏的圆角半径为 0
    // 原理：去除状态栏的圆角效果，使其为直角
    lv_obj_set_style_radius(status_bar_, 0, 0);

    /* Content */
    // 创建一个内容容器对象，作为容器的子对象
    // 原理：内容容器用于显示主要的用户界面内容，如聊天消息、图标等
    content_ = lv_obj_create(container_);
    // 设置内容容器的滚动条模式为关闭
    // 原理：避免在不需要滚动时显示滚动条，使界面更加简洁
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    // 设置内容容器的圆角半径为 0
    // 原理：去除内容容器的圆角效果
    lv_obj_set_style_radius(content_, 0, 0);
    // 设置内容容器的内边距为 0
    // 原理：使内容容器内的元素紧贴边界
    lv_obj_set_style_pad_all(content_, 0, 0);
    // 设置内容容器的宽度为屏幕宽度
    // 原理：确保内容容器充满屏幕宽度
    lv_obj_set_width(content_, LV_HOR_RES);
    // 设置内容容器在垂直布局中可以扩展
    // 原理：LV_FLEX_GROW 表示元素在布局中可以占用剩余的空间
    lv_obj_set_flex_grow(content_, 1);
    // 设置内容容器的布局方式为水平行布局
    // 原理：子元素将按照水平方向排列
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_ROW);
    // 设置内容容器内元素在水平方向上居中对齐
    // 原理：LV_FLEX_ALIGN_CENTER 表示元素在主轴方向上居中对齐
    lv_obj_set_style_flex_main_place(content_, LV_FLEX_ALIGN_CENTER, 0);

    // 创建左侧固定宽度的容器
    // 原理：将内容区域分为左右两部分，左侧容器用于显示固定的图标等信息
    content_left_ = lv_obj_create(content_);
    // 设置左侧容器的宽度为 32 像素，高度根据内容自动调整
    // 原理：固定左侧容器的宽度，方便布局
    lv_obj_set_size(content_left_, 32, LV_SIZE_CONTENT);
    // 设置左侧容器的内边距为 0
    // 原理：使左侧容器内的元素紧贴边界
    lv_obj_set_style_pad_all(content_left_, 0, 0);
    // 设置左侧容器的边框宽度为 0
    // 原理：去除左侧容器的边框
    lv_obj_set_style_border_width(content_left_, 0, 0);

    // 在左侧容器内创建一个标签，用于显示表情图标
    // 原理：标签是一种用于显示文本或图标的 UI 元素
    emotion_label_ = lv_label_create(content_left_);
    // 设置表情标签的文本字体为预先定义的字体
    // 原理：确保表情图标能够正确显示
    lv_obj_set_style_text_font(emotion_label_, &font_awesome_30_1, 0);
    // 设置表情标签的文本为一个图标字符
    // 原理：通过 FONT_AWESOME_AI_CHIP 定义的图标字符显示相应的图标
    lv_label_set_text(emotion_label_, FONT_AWESOME_AI_CHIP);
    // 将表情标签在左侧容器内居中显示
    // 原理：使图标在左侧容器中垂直和水平居中
    lv_obj_center(emotion_label_);
    // 设置表情标签的顶部内边距为 8 像素
    // 原理：调整图标在垂直方向上的位置
    lv_obj_set_style_pad_top(emotion_label_, 8, 0);

    // 创建右侧可扩展的容器
    // 原理：右侧容器用于显示动态的内容，如聊天消息，并且可以根据内容扩展
    content_right_ = lv_obj_create(content_);
    // 设置右侧容器的宽度和高度根据内容自动调整
    // 原理：初始时根据内容大小显示，后续可以根据内容扩展
    lv_obj_set_size(content_right_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    // 设置右侧容器的内边距为 0
    // 原理：使右侧容器内的元素紧贴边界
    lv_obj_set_style_pad_all(content_right_, 0, 0);
    // 设置右侧容器的边框宽度为 0
    // 原理：去除右侧容器的边框
    lv_obj_set_style_border_width(content_right_, 0, 0);
    // 设置右侧容器在水平布局中可以扩展
    // 原理：占用剩余的水平空间
    lv_obj_set_flex_grow(content_right_, 1);
    // 初始时隐藏右侧容器
    // 原理：在需要显示聊天消息时再显示该容器
    lv_obj_add_flag(content_right_, LV_OBJ_FLAG_HIDDEN);

    // 在右侧容器内创建一个标签，用于显示聊天消息
    // 原理：标签用于显示文本内容
    chat_message_label_ = lv_label_create(content_right_);
    // 初始时将聊天消息标签的文本设置为空
    // 原理：后续根据实际情况更新聊天消息
    lv_label_set_text(chat_message_label_, "");
    // 设置聊天消息标签的文本显示模式为循环滚动
    // 原理：当消息内容过长时，自动循环滚动显示
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    // 设置聊天消息标签的文本对齐方式为左对齐
    // 原理：使消息文本从左侧开始显示
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_LEFT, 0);
    // 设置聊天消息标签的宽度为屏幕宽度减去左侧容器的宽度
    // 原理：确保消息标签能够充分利用右侧容器的空间
    lv_obj_set_width(chat_message_label_, width_ - 32);
    // 设置聊天消息标签的顶部内边距为 14 像素
    // 原理：调整消息文本在垂直方向上的位置
    lv_obj_set_style_pad_top(chat_message_label_, 14, 0);

    // 延迟一定的时间后开始滚动字幕
    // 原理：创建一个动画对象，用于控制聊天消息的滚动
    static lv_anim_t a;
    // 初始化动画对象
    lv_anim_init(&a);
    // 设置动画的延迟时间为 1000 毫秒
    // 原理：在 1 秒后开始滚动消息
    lv_anim_set_delay(&a, 1000);
    // 设置动画的重复次数为无限次
    // 原理：使消息一直循环滚动
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    // 将动画对象应用到聊天消息标签上
    // 原理：通过动画控制标签的滚动效果
    lv_obj_set_style_anim(chat_message_label_, &a, LV_PART_MAIN);
    // 设置动画的持续时间，根据速度计算
    // 原理：通过 lv_anim_speed_clamped 函数根据速度计算动画持续时间，确保滚动速度在合理范围内
    lv_obj_set_style_anim_duration(chat_message_label_, lv_anim_speed_clamped(60, 300, 60000), LV_PART_MAIN);

    /* Status bar */
    // 设置状态栏的布局方式为水平行布局
    // 原理：状态栏内的元素将按照水平方向排列
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    // 设置状态栏的内边距为 0
    // 原理：使状态栏内的元素紧贴边界
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    // 设置状态栏的边框宽度为 0
    // 原理：去除状态栏的边框
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    // 设置状态栏内元素的列间距为 0
    // 原理：在水平布局中，列间距控制着子元素之间的水平距离
    lv_obj_set_style_pad_column(status_bar_, 0, 0);

    // 在状态栏内创建一个标签，用于显示网络状态图标
    // 原理：通过标签显示网络状态信息
    network_label_ = lv_label_create(status_bar_);
    // 初始时将网络状态标签的文本设置为空
    // 原理：后续根据实际网络状态更新标签文本
    lv_label_set_text(network_label_, "");
    // 设置网络状态标签的文本字体为预先定义的图标字体
    // 原理：确保网络状态图标能够正确显示
    lv_obj_set_style_text_font(network_label_, fonts_.icon_font, 0);

    // 在状态栏内创建一个标签，用于显示通知信息
    // 原理：显示系统通知等信息
    notification_label_ = lv_label_create(status_bar_);
    // 设置通知标签在水平布局中可以扩展
    // 原理：占用剩余的水平空间
    lv_obj_set_flex_grow(notification_label_, 1);
    // 设置通知标签的文本对齐方式为居中对齐
    // 原理：使通知信息在状态栏中居中显示
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    // 初始时将通知标签的文本设置为空
    // 原理：后续根据实际通知情况更新标签文本
    lv_label_set_text(notification_label_, "");
    // 初始时隐藏通知标签
    // 原理：在没有通知时不显示该标签
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    // 在状态栏内创建一个标签，用于显示系统状态信息
    // 原理：显示系统的当前状态，如初始化、就绪等
    status_label_ = lv_label_create(status_bar_);
    // 设置状态标签在水平布局中可以扩展
    // 原理：占用剩余的水平空间
    lv_obj_set_flex_grow(status_label_, 1);
    // 初始时将状态标签的文本设置为初始化提示信息
    // 原理：在系统启动时显示初始化状态
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    // 设置状态标签的文本对齐方式为居中对齐
    // 原理：使状态信息在状态栏中居中显示
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);

    // 在状态栏内创建一个标签，用于显示静音状态图标
    // 原理：通过标签显示系统的静音状态
    mute_label_ = lv_label_create(status_bar_);
    // 初始时将静音状态标签的文本设置为空
    // 原理：后续根据实际静音状态更新标签文本
    lv_label_set_text(mute_label_, "");
    // 设置静音状态标签的文本字体为预先定义的图标字体
    // 原理：确保静音状态图标能够正确显示
    lv_obj_set_style_text_font(mute_label_, fonts_.icon_font, 0);

    // 在状态栏内创建一个标签，用于显示电池电量图标
    // 原理：通过标签显示电池电量信息
    battery_label_ = lv_label_create(status_bar_);
    // 初始时将电池电量标签的文本设置为空
    // 原理：后续根据实际电池电量更新标签文本
    lv_label_set_text(battery_label_, "");
    // 设置电池电量标签的文本字体为预先定义的图标字体
    // 原理：确保电池电量图标能够正确显示
    lv_obj_set_style_text_font(battery_label_, fonts_.icon_font, 0);

    // 创建一个低电量弹出窗口对象
    // 原理：当电池电量低时，弹出窗口提示用户充电
    lv_obj_t * low_battery_popup_ = lv_obj_create(screen);
    // 设置低电量弹出窗口的滚动条模式为关闭
    // 原理：避免在不需要滚动时显示滚动条
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    // 设置低电量弹出窗口的大小为屏幕宽度的 0.9 倍，高度为文本行高的 2 倍
    // 原理：根据设计需求设置弹出窗口的大小
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, fonts_.text_font->line_height * 2);
    // 将低电量弹出窗口对齐到屏幕底部中间位置
    // 原理：确保弹出窗口在屏幕底部居中显示
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, 0);
    // 设置低电量弹出窗口的背景颜色为黑色
    // 原理：根据设计需求设置弹出窗口的背景颜色
    lv_obj_set_style_bg_color(low_battery_popup_, lv_color_black(), 0);
    // 设置低电量弹出窗口的圆角半径为 10 像素
    // 原理：使弹出窗口具有圆角效果
    lv_obj_set_style_radius(low_battery_popup_, 10, 0);
    // 在低电量弹出窗口内创建一个标签，用于显示低电量提示信息
    // 原理：通过标签显示提示用户充电的信息
    lv_obj_t* low_battery_label = lv_label_create(low_battery_popup_);
    // 设置低电量标签的文本为低电量提示信息
    // 原理：显示预先定义的低电量提示文本
    lv_label_set_text(low_battery_label, Lang::Strings::BATTERY_LOW);
    // 设置低电量标签的文本颜色为白色
    // 原理：确保提示信息在黑色背景下清晰可见
    lv_obj_set_style_text_color(low_battery_label, lv_color_white(), 0);
    // 将低电量标签在弹出窗口内居中显示
    // 原理：使提示信息在弹出窗口中垂直和水平居中
    lv_obj_center(low_battery_label);
    // 初始时隐藏低电量弹出窗口
    // 原理：在电池电量正常时不显示该窗口
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
}
// 定义 OledDisplay 类的成员函数 SetupUI_128x32
// 该函数的作用是为 128x32 分辨率的 OLED 显示屏设置用户界面
void OledDisplay::SetupUI_128x32() {
    // 创建一个显示锁保护对象，用于确保在设置 UI 期间对显示的操作是线程安全的
    // 原理：在多线程环境中，防止多个线程同时对显示进行操作，避免界面显示混乱
    DisplayLockGuard lock(this);

    // 获取当前活动的屏幕对象
    // 原理：LVGL（LittlevGL）是一个开源的图形库，通过 lv_screen_active() 函数获取当前正在显示的屏幕，后续的 UI 元素将基于此屏幕创建
    auto screen = lv_screen_active();
    // 设置屏幕的文本字体为预先定义的字体
    // 原理：lv_obj_set_style_text_font 函数用于设置对象（这里是屏幕）的文本字体，0 表示默认状态
    lv_obj_set_style_text_font(screen, fonts_.text_font, 0);

    /* Container */
    // 创建一个容器对象，作为整个 UI 的根容器
    // 原理：容器可以作为其他 UI 元素的父对象，方便对多个元素进行布局和管理
    container_ = lv_obj_create(screen);
    // 设置容器的大小为屏幕的宽度和高度
    // 原理：LV_HOR_RES 和 LV_VER_RES 分别表示屏幕的水平和垂直分辨率，确保容器充满整个屏幕
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    // 设置容器的布局方式为水平行布局
    // 原理：LV_FLEX_FLOW_ROW 表示子元素将按照水平方向排列
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_ROW);
    // 设置容器的内边距为 0
    // 原理：内边距是指容器内部元素与容器边界之间的距离，设置为 0 可以使元素紧贴容器边界
    lv_obj_set_style_pad_all(container_, 0, 0);
    // 设置容器的边框宽度为 0
    // 原理：去除容器的边框，使界面更加简洁
    lv_obj_set_style_border_width(container_, 0, 0);
    // 设置容器内元素的列间距为 0
    // 原理：在水平布局中，列间距控制着子元素之间的水平距离
    lv_obj_set_style_pad_column(container_, 0, 0);

    /* Emotion label on the left side */
    // 创建一个内容容器对象，作为根容器的子对象
    // 原理：内容容器用于显示主要的用户界面内容，如表情图标、聊天消息等
    content_ = lv_obj_create(container_);
    // 设置内容容器的宽度和高度为 32 像素
    // 原理：左侧容器固定大小，用于显示表情图标
    lv_obj_set_size(content_, 32, 32);
    // 设置内容容器的内边距为 0
    // 原理：使内容容器内的元素紧贴边界
    lv_obj_set_style_pad_all(content_, 0, 0);
    // 设置内容容器的边框宽度为 0
    // 原理：去除内容容器的边框
    lv_obj_set_style_border_width(content_, 0, 0);
    // 设置内容容器的圆角半径为 0
    // 原理：去除内容容器的圆角效果
    lv_obj_set_style_radius(content_, 0, 0);

    // 在内容容器内创建一个标签，用于显示表情图标
    // 原理：标签是一种用于显示文本或图标的 UI 元素
    emotion_label_ = lv_label_create(content_);
    // 设置表情标签的文本字体为预先定义的字体
    // 原理：确保表情图标能够正确显示
    lv_obj_set_style_text_font(emotion_label_, &font_awesome_30_1, 0);
    // 设置表情标签的文本为一个图标字符
    // 原理：通过 FONT_AWESOME_AI_CHIP 定义的图标字符显示相应的图标
    lv_label_set_text(emotion_label_, FONT_AWESOME_AI_CHIP);
    // 将表情标签在内容容器内居中显示
    // 原理：使图标在内容容器中垂直和水平居中
    lv_obj_center(emotion_label_);

    /* Right side */
    // 创建一个侧边栏容器对象，作为根容器的子对象
    // 原理：侧边栏容器用于显示右侧的动态内容，如状态信息、聊天消息等
    side_bar_ = lv_obj_create(container_);
    // 设置侧边栏容器的宽度为屏幕宽度减去左侧容器的宽度，高度为 32 像素
    // 原理：确保侧边栏容器占据右侧剩余空间
    lv_obj_set_size(side_bar_, width_ - 32, 32);
    // 设置侧边栏容器的布局方式为垂直列布局
    // 原理：LV_FLEX_FLOW_COLUMN 表示子元素将按照垂直方向排列
    lv_obj_set_flex_flow(side_bar_, LV_FLEX_FLOW_COLUMN);
    // 设置侧边栏容器的内边距为 0
    // 原理：使侧边栏容器内的元素紧贴边界
    lv_obj_set_style_pad_all(side_bar_, 0, 0);
    // 设置侧边栏容器的边框宽度为 0
    // 原理：去除侧边栏容器的边框
    lv_obj_set_style_border_width(side_bar_, 0, 0);
    // 设置侧边栏容器的圆角半径为 0
    // 原理：去除侧边栏容器的圆角效果
    lv_obj_set_style_radius(side_bar_, 0, 0);
    // 设置侧边栏容器内元素的行间距为 0
    // 原理：在垂直布局中，行间距控制着子元素之间的垂直距离
    lv_obj_set_style_pad_row(side_bar_, 0, 0);

    /* Status bar */
    // 创建一个状态栏对象，作为侧边栏容器的子对象
    // 原理：状态栏通常用于显示一些系统状态信息，如网络状态、电池电量等
    status_bar_ = lv_obj_create(side_bar_);
    // 设置状态栏的宽度为侧边栏容器的宽度，高度为 16 像素
    // 原理：状态栏通常位于屏幕顶部，宽度与屏幕相同，高度根据设计需求设置
    lv_obj_set_size(status_bar_, width_ - 32, 16);
    // 设置状态栏的圆角半径为 0
    // 原理：去除状态栏的圆角效果
    lv_obj_set_style_radius(status_bar_, 0, 0);
    // 设置状态栏的布局方式为水平行布局
    // 原理：状态栏内的元素将按照水平方向排列
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    // 设置状态栏的内边距为 0
    // 原理：使状态栏内的元素紧贴边界
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    // 设置状态栏的边框宽度为 0
    // 原理：去除状态栏的边框
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    // 设置状态栏内元素的列间距为 0
    // 原理：在水平布局中，列间距控制着子元素之间的水平距离
    lv_obj_set_style_pad_column(status_bar_, 0, 0);

    // 在状态栏内创建一个标签，用于显示系统状态信息
    // 原理：显示系统的当前状态，如初始化、就绪等
    status_label_ = lv_label_create(status_bar_);
    // 设置状态标签在水平布局中可以扩展
    // 原理：占用剩余的水平空间
    lv_obj_set_flex_grow(status_label_, 1);
    // 设置状态标签的左侧内边距为 2 像素
    // 原理：调整状态信息在水平方向上的位置
    lv_obj_set_style_pad_left(status_label_, 2, 0);
    // 初始时将状态标签的文本设置为初始化提示信息
    // 原理：在系统启动时显示初始化状态
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);

    // 在状态栏内创建一个标签，用于显示通知信息
    // 原理：显示系统通知等信息
    notification_label_ = lv_label_create(status_bar_);
    // 设置通知标签在水平布局中可以扩展
    // 原理：占用剩余的水平空间
    lv_obj_set_flex_grow(notification_label_, 1);
    // 设置通知标签的左侧内边距为 2 像素
    // 原理：调整通知信息在水平方向上的位置
    lv_obj_set_style_pad_left(notification_label_, 2, 0);
    // 初始时将通知标签的文本设置为空
    // 原理：后续根据实际通知情况更新标签文本
    lv_label_set_text(notification_label_, "");
    // 初始时隐藏通知标签
    // 原理：在没有通知时不显示该标签
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    // 在状态栏内创建一个标签，用于显示静音状态图标
    // 原理：通过标签显示系统的静音状态
    mute_label_ = lv_label_create(status_bar_);
    // 初始时将静音状态标签的文本设置为空
    // 原理：后续根据实际静音状态更新标签文本
    lv_label_set_text(mute_label_, "");
    // 设置静音状态标签的文本字体为预先定义的图标字体
    // 原理：确保静音状态图标能够正确显示
    lv_obj_set_style_text_font(mute_label_, fonts_.icon_font, 0);

    // 在状态栏内创建一个标签，用于显示网络状态图标
    // 原理：通过标签显示网络状态信息
    network_label_ = lv_label_create(status_bar_);
    // 初始时将网络状态标签的文本设置为空
    // 原理：后续根据实际网络状态更新标签文本
    lv_label_set_text(network_label_, "");
    // 设置网络状态标签的文本字体为预先定义的图标字体
    // 原理：确保网络状态图标能够正确显示
    lv_obj_set_style_text_font(network_label_, fonts_.icon_font, 0);

    // 在状态栏内创建一个标签，用于显示电池电量图标
    // 原理：通过标签显示电池电量信息
    battery_label_ = lv_label_create(status_bar_);
    // 初始时将电池电量标签的文本设置为空
    // 原理：后续根据实际电池电量更新标签文本
    lv_label_set_text(battery_label_, "");
    // 设置电池电量标签的文本字体为预先定义的图标字体
    // 原理：确保电池电量图标能够正确显示
    lv_obj_set_style_text_font(battery_label_, fonts_.icon_font, 0);

    // 在侧边栏容器内创建一个标签，用于显示聊天消息
    // 原理：标签用于显示文本内容
    chat_message_label_ = lv_label_create(side_bar_);
    // 设置聊天消息标签的宽度为侧边栏容器的宽度，高度根据内容自动调整
    // 原理：确保消息标签能够充分利用右侧容器的空间
    lv_obj_set_size(chat_message_label_, width_ - 32, LV_SIZE_CONTENT);
    // 设置聊天消息标签的左侧内边距为 2 像素
    // 原理：调整消息文本在水平方向上的位置
    lv_obj_set_style_pad_left(chat_message_label_, 2, 0);
    // 设置聊天消息标签的文本显示模式为循环滚动
    // 原理：当消息内容过长时，自动循环滚动显示
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    // 初始时将聊天消息标签的文本设置为空
    // 原理：后续根据实际情况更新聊天消息
    lv_label_set_text(chat_message_label_, "");

    // 延迟一定的时间后开始滚动字幕
    // 原理：创建一个动画对象，用于控制聊天消息的滚动
    static lv_anim_t a;
    // 初始化动画对象
    lv_anim_init(&a);
    // 设置动画的延迟时间为 1000 毫秒
    // 原理：在 1 秒后开始滚动消息
    lv_anim_set_delay(&a, 1000);
    // 设置动画的重复次数为无限次
    // 原理：使消息一直循环滚动
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    // 将动画对象应用到聊天消息标签上
    // 原理：通过动画控制标签的滚动效果
    lv_obj_set_style_anim(chat_message_label_, &a, LV_PART_MAIN);
    // 设置动画的持续时间，根据速度计算
    // 原理：通过 lv_anim_speed_clamped 函数根据速度计算动画持续时间，确保滚动速度在合理范围内
    lv_obj_set_style_anim_duration(chat_message_label_, lv_anim_speed_clamped(60, 300, 60000), LV_PART_MAIN);
}

