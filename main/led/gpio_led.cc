#include "gpio_led.h"
#include "application.h"
#include <esp_log.h>

// 定义日志标签，方便在日志输出中识别相关信息
#define TAG "GpioLed"

// 定义不同亮度级别，以百分比表示
// 定义默认亮度，以百分比表示，默认情况下LED的亮度为50%
#define DEFAULT_BRIGHTNESS 50
// 定义高亮度，以百分比表示，当需要LED最亮时，使用此亮度值，即100%亮度
#define HIGH_BRIGHTNESS 100
// 定义低亮度，以百分比表示，在需要较低亮度时，使用此亮度值，即10%亮度
#define LOW_BRIGHTNESS 10

// 定义空闲状态下LED的亮度，以百分比表示，设备处于空闲状态时，LED亮度为5%
#define IDLE_BRIGHTNESS 5
// 定义说话状态下LED的亮度，以百分比表示，当设备处于说话状态时，LED亮度为75%
#define SPEAKING_BRIGHTNESS 75
// 定义升级状态下LED的亮度，以百分比表示，设备进行升级操作时，LED亮度为25%
#define UPGRADING_BRIGHTNESS 25
// 定义激活状态下LED的亮度，以百分比表示，设备处于激活过程中，LED亮度为35%
#define ACTIVATING_BRIGHTNESS 35

// 定义无限闪烁的次数，用 -1 表示
#define BLINK_INFINITE -1

// GPIO_LED 相关配置
// 选择 LEDC 定时器
#define LEDC_LS_TIMER          LEDC_TIMER_1
// 选择 LEDC 速度模式
#define LEDC_LS_MODE           LEDC_LOW_SPEED_MODE
// 选择 LEDC 通道
#define LEDC_LS_CH0_CHANNEL    LEDC_CHANNEL_0

// 定义 PWM 占空比的最大值
#define LEDC_DUTY              (4096)
// 定义渐变时间，单位为毫秒
#define LEDC_FADE_TIME    (1000)
// GPIO_LED

// 构造函数，初始化 GpioLed 对象
GpioLed::GpioLed(gpio_num_t gpio, int output_invert) {
    // 断言检查传入的 GPIO 引脚是否有效，如果无效则终止程序
    assert(gpio != GPIO_NUM_NC);

    /*
     * 准备并设置 LED 控制器将使用的定时器配置
     */
    // 定义 LEDC 定时器配置结构体
    ledc_timer_config_t ledc_timer = {};
    // 设置 PWM 占空比的分辨率为 13 位
    ledc_timer.duty_resolution = LEDC_TIMER_13_BIT; 
    // 设置 PWM 信号的频率为 4000 Hz
    ledc_timer.freq_hz = 4000;                      
    // 设置定时器模式为低速模式
    ledc_timer.speed_mode = LEDC_LS_MODE;           
    // 设置定时器编号
    ledc_timer.timer_num = LEDC_LS_TIMER;            
    // 自动选择时钟源
    ledc_timer.clk_cfg = LEDC_AUTO_CLK;              

    // 配置 LEDC 定时器，并检查是否成功
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // 配置 LEDC 通道
    ledc_channel_.channel    = LEDC_LS_CH0_CHANNEL;
    // 初始占空比为 0
    ledc_channel_.duty       = 0;
    // 指定 GPIO 引脚
    ledc_channel_.gpio_num   = gpio;
    // 设置通道速度模式
    ledc_channel_.speed_mode = LEDC_LS_MODE;
    // 设置 hpoint 值
    ledc_channel_.hpoint     = 0;
    // 指定使用的定时器
    ledc_channel_.timer_sel  = LEDC_LS_TIMER;
    // 设置输出反转标志
    ledc_channel_.flags.output_invert = output_invert & 0x01;

    // 使用之前准备好的配置设置 LED 控制器通道
    ledc_channel_config(&ledc_channel_);

    // 初始化渐变服务
    ledc_fade_func_install(0);

    // 当 ledc_cb_degister 注册的回调函数被调用时，运行 led ->OnFadeEnd()
    ledc_cbs_t ledc_callbacks = {
        .fade_cb = FadeCallback
    };
    // 注册 LEDC 回调函数
    ledc_cb_register(ledc_channel_.speed_mode, ledc_channel_.channel, &ledc_callbacks, this);

    // 定义闪烁定时器的创建参数
    esp_timer_create_args_t blink_timer_args = {
        // 定时器回调函数
        .callback = [](void *arg) {
            // 将参数转换为 GpioLed 指针
            auto led = static_cast<GpioLed*>(arg);
            // 调用闪烁定时器处理函数
            led->OnBlinkTimer();
        },
        // 传递当前对象作为参数
        .arg = this,
        // 定时器调度方法
        .dispatch_method = ESP_TIMER_TASK,
        // 定时器名称
        .name = "Blink Timer",
        // 是否跳过未处理的事件
        .skip_unhandled_events = false,
    };
    // 创建闪烁定时器，并检查是否成功
    ESP_ERROR_CHECK(esp_timer_create(&blink_timer_args, &blink_timer_));

    // 标记 LEDC 已初始化
    ledc_initialized_ = true;
}

// 析构函数，释放资源
GpioLed::~GpioLed() {
    // 停止闪烁定时器
    esp_timer_stop(blink_timer_);
    if (ledc_initialized_) {
        // 停止渐变效果
        ledc_fade_stop(ledc_channel_.speed_mode, ledc_channel_.channel);
        // 卸载渐变服务
        ledc_fade_func_uninstall();
    }
}

// 设置 LED 亮度
void GpioLed::SetBrightness(uint8_t brightness) {
    // 根据传入的亮度百分比计算占空比
    duty_ = brightness * LEDC_DUTY / 100;
}

// 打开 LED
void GpioLed::TurnOn() {
    if (!ledc_initialized_) {
        // 如果 LEDC 未初始化，则直接返回
        return;
    }

    // 使用互斥锁保护临界区，防止多线程冲突
    std::lock_guard<std::mutex> lock(mutex_);
    // 停止闪烁定时器
    esp_timer_stop(blink_timer_);
    // 停止渐变效果
    ledc_fade_stop(ledc_channel_.speed_mode, ledc_channel_.channel);
    // 设置占空比为当前亮度对应的占空比
    ledc_set_duty(ledc_channel_.speed_mode, ledc_channel_.channel, duty_);
    // 更新占空比
    ledc_update_duty(ledc_channel_.speed_mode, ledc_channel_.channel);
}

// 关闭 LED
void GpioLed::TurnOff() {
    if (!ledc_initialized_) {
        // 如果 LEDC 未初始化，则直接返回
        return;
    }

    // 使用互斥锁保护临界区，防止多线程冲突
    std::lock_guard<std::mutex> lock(mutex_);
    // 停止闪烁定时器
    esp_timer_stop(blink_timer_);
    // 停止渐变效果
    ledc_fade_stop(ledc_channel_.speed_mode, ledc_channel_.channel);
    // 设置占空比为 0
    ledc_set_duty(ledc_channel_.speed_mode, ledc_channel_.channel, 0);
    // 更新占空比
    ledc_update_duty(ledc_channel_.speed_mode, ledc_channel_.channel);
}

// 闪烁一次
void GpioLed::BlinkOnce() {
    // 调用 Blink 函数，闪烁 1 次，间隔 100 毫秒
    Blink(1, 100);
}

// 闪烁指定次数
void GpioLed::Blink(int times, int interval_ms) {
    // 启动闪烁任务
    StartBlinkTask(times, interval_ms);
}

// 开始连续闪烁
void GpioLed::StartContinuousBlink(int interval_ms) {
    // 调用 StartBlinkTask 函数，设置闪烁次数为无限次
    StartBlinkTask(BLINK_INFINITE, interval_ms);
}

// 启动闪烁任务
void GpioLed::StartBlinkTask(int times, int interval_ms) {
    if (!ledc_initialized_) {
        // 如果 LEDC 未初始化，则直接返回
        return;
    }

    // 使用互斥锁保护临界区，防止多线程冲突
    std::lock_guard<std::mutex> lock(mutex_);
    // 停止闪烁定时器
    esp_timer_stop(blink_timer_);
    // 停止渐变效果
    ledc_fade_stop(ledc_channel_.speed_mode, ledc_channel_.channel);

    // 计算闪烁计数器的值，闪烁次数乘以 2
    blink_counter_ = times * 2;
    // 设置闪烁间隔时间
    blink_interval_ms_ = interval_ms;
    // 启动周期性定时器
    esp_timer_start_periodic(blink_timer_, interval_ms * 1000);
}

// 闪烁定时器回调函数
void GpioLed::OnBlinkTimer() {
    // 使用互斥锁保护临界区，防止多线程冲突
    std::lock_guard<std::mutex> lock(mutex_);
    // 闪烁计数器减 1
    blink_counter_--;
    if (blink_counter_ & 1) {
        // 如果计数器为奇数，设置占空比为当前亮度对应的占空比
        ledc_set_duty(ledc_channel_.speed_mode, ledc_channel_.channel, duty_);
    } else {
        // 如果计数器为偶数，设置占空比为 0
        ledc_set_duty(ledc_channel_.speed_mode, ledc_channel_.channel, 0);

        if (blink_counter_ == 0) {
            // 如果计数器为 0，停止闪烁定时器
            esp_timer_stop(blink_timer_);
        }
    }
    // 更新占空比
    ledc_update_duty(ledc_channel_.speed_mode, ledc_channel_.channel);
}

// 启动渐变任务
void GpioLed::StartFadeTask() {
    if (!ledc_initialized_) {
        // 如果 LEDC 未初始化，则直接返回
        return;
    }

    // 使用互斥锁保护临界区，防止多线程冲突
    std::lock_guard<std::mutex> lock(mutex_);
    // 停止闪烁定时器
    esp_timer_stop(blink_timer_);
    // 停止渐变效果
    ledc_fade_stop(ledc_channel_.speed_mode, ledc_channel_.channel);
    // 设置渐变方向为向上
    fade_up_ = true;
    // 设置渐变到最大占空比，时间为 LEDC_FADE_TIME
    ledc_set_fade_with_time(ledc_channel_.speed_mode,
                            ledc_channel_.channel, LEDC_DUTY, LEDC_FADE_TIME);
    // 启动渐变效果，不等待
    ledc_fade_start(ledc_channel_.speed_mode,
                    ledc_channel_.channel, LEDC_FADE_NO_WAIT);
}

// 渐变结束回调函数
void GpioLed::OnFadeEnd() {
    // 使用互斥锁保护临界区，防止多线程冲突
    std::lock_guard<std::mutex> lock(mutex_);
    // 反转渐变方向
    fade_up_ = !fade_up_;
    // 根据渐变方向设置渐变到的占空比
    ledc_set_fade_with_time(ledc_channel_.speed_mode,
                            ledc_channel_.channel, fade_up_ ? LEDC_DUTY : 0, LEDC_FADE_TIME);
    // 启动渐变效果，不等待
    ledc_fade_start(ledc_channel_.speed_mode,
                    ledc_channel_.channel, LEDC_FADE_NO_WAIT);
}

// 渐变结束回调函数
bool GpioLed::FadeCallback(const ledc_cb_param_t *param, void *user_arg) {
    if (param->event == LEDC_FADE_END_EVT) {
        // 如果事件为渐变结束，将参数转换为 GpioLed 指针
        auto led = static_cast<GpioLed*>(user_arg);
        // 调用渐变结束处理函数
        led->OnFadeEnd();
    }
    return true;
}

// 设备状态改变时的处理函数
void GpioLed::OnStateChanged() {
    // 获取应用程序实例
    auto& app = Application::GetInstance();
    // 获取设备状态
    auto device_state = app.GetDeviceState();
    switch (device_state) {
        case kDeviceStateStarting:
            // 设置亮度为默认亮度
            SetBrightness(DEFAULT_BRIGHTNESS);
            // 开始连续闪烁，间隔 100 毫秒
            StartContinuousBlink(100);
            break;
        case kDeviceStateWifiConfiguring:
            // 设置亮度为默认亮度
            SetBrightness(DEFAULT_BRIGHTNESS);
            // 开始连续闪烁，间隔 500 毫秒
            StartContinuousBlink(500);
            break;
        case kDeviceStateIdle:
            // 设置亮度为空闲亮度
            SetBrightness(IDLE_BRIGHTNESS);
            // 打开 LED
            TurnOn();
            // TurnOff();
            break;
        case kDeviceStateConnecting:
            // 设置亮度为默认亮度
            SetBrightness(DEFAULT_BRIGHTNESS);
            // 打开 LED
            TurnOn();
            break;
        case kDeviceStateListening:
            if (app.IsVoiceDetected()) {
                // 如果检测到语音，设置亮度为高亮度
                SetBrightness(HIGH_BRIGHTNESS);
            } else {
                // 如果未检测到语音，设置亮度为低亮度
                SetBrightness(LOW_BRIGHTNESS);
            }
            // 启动渐变任务
            StartFadeTask();
            break;
        case kDeviceStateSpeaking:
            // 设置亮度为说话亮度
            SetBrightness(SPEAKING_BRIGHTNESS);
            // 打开 LED
            TurnOn();
            break;
        case kDeviceStateUpgrading:
            // 设置亮度为升级亮度
            SetBrightness(UPGRADING_BRIGHTNESS);
            // 开始连续闪烁，间隔 100 毫秒
            StartContinuousBlink(100);
            break;
        case kDeviceStateActivating:
            // 设置亮度为激活亮度
            SetBrightness(ACTIVATING_BRIGHTNESS);
            // 开始连续闪烁，间隔 500 毫秒
            StartContinuousBlink(500);
            break;
        default:
            // 记录未知设备状态错误日志
            ESP_LOGE(TAG, "Unknown gpio led event: %d", device_state);
            return;
    }
}