#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "ml307_ssl_transport.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "font_awesome_symbols.h"
#include "iot/thing_manager.h"
#include "assets/lang_config.h"

#include <cstring>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <esp_app_desc.h>

#define TAG "Application"

// 定义设备状态的字符串表示，用于日志输出
static const char* const STATE_STRINGS[] = {
    "unknown",       // 未知状态
    "starting",      // 启动中
    "configuring",   // 配置中
    "idle",          // 空闲状态
    "connecting",    // 连接中
    "listening",     // 监听中
    "speaking",      // 说话中
    "upgrading",     // 升级中
    "activating",    // 激活中
    "fatal_error",   // 致命错误
    "invalid_state"  // 无效状态
};

// 构造函数，初始化应用程序
Application::Application() {
    // 创建事件组，用于任务间通信
    event_group_ = xEventGroupCreate();
    // 创建后台任务，栈大小为4096 * 8字节
    background_task_ = new BackgroundTask(4096 * 8);

    // 创建时钟定时器，每秒触发一次
    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            app->OnClockTimer();  // 定时器回调函数
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer"
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

// 析构函数，释放资源
Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);  // 停止定时器
        esp_timer_delete(clock_timer_handle_);  // 删除定时器
    }
    if (background_task_ != nullptr) {
        delete background_task_;  // 删除后台任务
    }
    vEventGroupDelete(event_group_);  // 删除事件组
}

// 检查新版本
// 此方法用于检查设备是否有新版本的固件可供升级
void Application::CheckNewVersion() {
    // 获取 Board 类的单例对象，用于访问设备相关信息
    auto& board = Board::GetInstance();
    // 从 Board 实例中获取显示设备对象，用于显示信息
    auto display = board.GetDisplay();
    // 设置 OTA（Over-the-Air）升级的 POST 数据，使用设备的 JSON 信息
    ota_.SetPostData(board.GetJson());

    // 定义最大重试次数，当检查版本失败时，最多重试 MAX_RETRY 次
    const int MAX_RETRY = 10;
    // 初始化重试计数器，记录当前重试次数
    int retry_count = 0;

    // 进入无限循环，直到满足退出条件
    while (true) {
        // 调用 ota_ 对象的 CheckVersion 方法检查是否有新版本
        if (!ota_.CheckVersion()) {  // 检查版本失败
            // 重试次数加 1
            retry_count++;
            // 如果重试次数达到最大重试次数
            if (retry_count >= MAX_RETRY) {
                // 记录错误日志，表明重试次数过多，退出版本检查
                ESP_LOGE(TAG, "Too many retries, exit version check");  // 重试次数过多，退出
                // 退出方法
                return;
            }
            // 记录警告日志，显示检查新版本失败，并提示将在 60 秒后重试，同时显示当前重试次数和最大重试次数
            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", 60, retry_count, MAX_RETRY);
            // 任务延迟 60 秒（60000 毫秒）后继续重试
            vTaskDelay(pdMS_TO_TICKS(60000));
            // 跳过本次循环剩余代码，继续下一次循环
            continue;
        }
        // 如果检查版本成功，将重试计数器重置为 0
        retry_count = 0;

        // 检查是否有新版本可用
        if (ota_.HasNewVersion()) {  // 有新版本
            // 调用 Alert 方法显示 OTA 升级提示，包含状态、消息、表情和音效
            Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "happy", Lang::Sounds::P3_UPGRADE);
            // 等待设备状态变为空闲状态
            do {
                // 任务延迟 3 秒（3000 毫秒）
                vTaskDelay(pdMS_TO_TICKS(3000));
            } while (GetDeviceState() != kDeviceStateIdle);

            // 安排一个任务来执行升级操作
            Schedule([this, display]() {
                // 将设备状态设置为升级中状态
                SetDeviceState(kDeviceStateUpgrading);
                
                // 在显示设备上设置下载图标
                display->SetIcon(FONT_AWESOME_DOWNLOAD);
                // 构建显示消息，包含新版本的信息
                std::string message = std::string(Lang::Strings::NEW_VERSION) + ota_.GetFirmwareVersion();
                // 在显示设备上显示新版本信息
                display->SetChatMessage("system", message.c_str());

                // 获取 Board 单例对象
                auto& board = Board::GetInstance();
                // 关闭设备的省电模式，确保升级过程中设备保持正常运行
                board.SetPowerSaveMode(false);
                // 如果配置了使用唤醒词检测，则停止唤醒词检测功能
#if CONFIG_USE_WAKE_WORD_DETECT
                wake_word_detect_.StopDetection();
#endif
                // 获取音频编解码器实例
                auto codec = board.GetAudioCodec();
                // 关闭音频编解码器的输入功能
                codec->EnableInput(false);
                // 关闭音频编解码器的输出功能，避免升级过程中有音频操作
                codec->EnableOutput(false);
                {
                    // 使用互斥锁保护对音频解码队列的访问
                    std::lock_guard<std::mutex> lock(mutex_);
                    // 清空音频解码队列
                    audio_decode_queue_.clear();
                }
                // 等待所有后台任务完成
                background_task_->WaitForCompletion();
                // 删除后台任务对象
                delete background_task_;
                // 将后台任务指针置为 nullptr
                background_task_ = nullptr;
                // 任务延迟 1 秒（1000 毫秒）
                vTaskDelay(pdMS_TO_TICKS(1000));

                // 调用 ota_ 对象的 StartUpgrade 方法开始升级，并传入一个回调函数用于显示升级进度
                ota_.StartUpgrade([display](int progress, size_t speed) {
                    // 定义一个字符数组用于存储升级进度和速度信息
                    char buffer[64];
                    // 格式化升级进度和速度信息到 buffer 数组中
                    snprintf(buffer, sizeof(buffer), "%d%% %zuKB/s", progress, speed / 1024);
                    // 在显示设备上显示升级进度和速度信息
                    display->SetChatMessage("system", buffer);
                });

                // 如果升级成功，设备将重启，不会执行到这里
                // 在显示设备上显示升级失败的状态信息
                display->SetStatus(Lang::Strings::UPGRADE_FAILED);
                // 记录日志，表明固件升级失败
                ESP_LOGI(TAG, "Firmware upgrade failed...");
                // 任务延迟 3 秒（3000 毫秒）
                vTaskDelay(pdMS_TO_TICKS(3000));
                // 调用 Reboot 方法重启设备
                Reboot();
            });

            // 退出方法
            return;
        }

        // 如果没有新版本，标记当前版本为有效
        ota_.MarkCurrentVersionValid();
        // 构建显示消息，包含当前版本的信息
        std::string message = std::string(Lang::Strings::VERSION) + ota_.GetCurrentVersion();
        // 在显示设备上显示当前版本信息
        display->ShowNotification(message.c_str());
    
        // 检查是否有激活码
        if (ota_.HasActivationCode()) {  // 有激活码
            // 将设备状态设置为激活中状态
            SetDeviceState(kDeviceStateActivating);
            // 调用 ShowActivationCode 方法显示激活码
            ShowActivationCode();

            // 60 秒后再次检查或直到设备空闲
            for (int i = 0; i < 60; ++i) {
                // 如果设备状态变为空闲状态，跳出循环
                if (device_state_ == kDeviceStateIdle) {
                    break;
                }
                // 任务延迟 1 秒（1000 毫秒）
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            // 跳过本次循环剩余代码，继续下一次循环
            continue;
        }

        // 将设备状态设置为空闲状态
        SetDeviceState(kDeviceStateIdle);
        // 清空显示设备上的聊天消息
        display->SetChatMessage("system", "");
        // 调用 PlaySound 方法播放成功音效
        PlaySound(Lang::Sounds::P3_SUCCESS);
        // 如果升级或空闲，退出循环
        break;
    }
}

// 显示激活码
// 显示激活码的方法
void Application::ShowActivationCode() {
    // 从 OTA（Over-the-Air）更新模块中获取激活消息
    auto& message = ota_.GetActivationMessage();
    // 从 OTA 更新模块中获取激活码
    auto& code = ota_.GetActivationCode();

    // 定义一个结构体，用于存储数字和对应的音效
    struct digit_sound {
        char digit; // 数字字符
        const std::string_view& sound; // 对应的音效
    };
    // 静态数组，存储 0 - 9 每个数字对应的音效
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::P3_0},
        digit_sound{'1', Lang::Sounds::P3_1}, 
        digit_sound{'2', Lang::Sounds::P3_2},
        digit_sound{'3', Lang::Sounds::P3_3},
        digit_sound{'4', Lang::Sounds::P3_4},
        digit_sound{'5', Lang::Sounds::P3_5},
        digit_sound{'6', Lang::Sounds::P3_6},
        digit_sound{'7', Lang::Sounds::P3_7},
        digit_sound{'8', Lang::Sounds::P3_8},
        digit_sound{'9', Lang::Sounds::P3_9}
    }};

    // 显示激活信息
    // 调用 Alert 方法显示激活提示，包含状态、消息、表情和音效
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "happy", Lang::Sounds::P3_ACTIVATION);
    // 任务延迟 1000 毫秒，给用户一些时间查看激活提示
    vTaskDelay(pdMS_TO_TICKS(1000));
    // 等待所有后台任务完成，确保在播放激活码音效前没有其他任务干扰
    background_task_->WaitForCompletion();  // 等待后台任务完成

    // 播放激活码的每个数字对应的音效
    for (const auto& digit : code) {
        // 在 digit_sounds 数组中查找当前数字对应的音效
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            // 如果找到对应的音效，调用 PlaySound 方法播放该音效
            PlaySound(it->sound);  // 播放数字音效
        }
    }
}

// 显示警告信息的方法
void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    // 记录警告日志，包含状态、消息和表情信息
    ESP_LOGW(TAG, "Alert %s: %s [%s]", status, message, emotion);
    // 从 Board 单例对象中获取显示设备实例
    auto display = Board::GetInstance().GetDisplay();
    // 在显示设备上设置状态信息
    display->SetStatus(status);  // 设置状态
    // 在显示设备上设置表情信息
    display->SetEmotion(emotion);  // 设置表情
    // 在显示设备上显示警告消息
    display->SetChatMessage("system", message);  // 显示消息
    // 如果音效不为空，则调用 PlaySound 方法播放该音效
    if (!sound.empty()) {
        PlaySound(sound);  // 播放音效
    }
}

// 取消警告的方法
void Application::DismissAlert() {
    // 检查设备当前状态是否为空闲状态
    if (device_state_ == kDeviceStateIdle) {
        // 从 Board 单例对象中获取显示设备实例
        auto display = Board::GetInstance().GetDisplay();
        // 在显示设备上设置状态信息为待机状态
        display->SetStatus(Lang::Strings::STANDBY);  // 设置状态为待机
        // 在显示设备上设置表情为中性表情
        display->SetEmotion("neutral");  // 设置表情为中性
        // 清空显示设备上的聊天消息
        display->SetChatMessage("system", "");  // 清空消息
    }
}

// 播放音效
// 此方法用于播放指定的声音
void Application::PlaySound(const std::string_view& sound) {
    // 从 Board 类的单例对象中获取音频编解码器实例
    auto codec = Board::GetInstance().GetAudioCodec();
    // 启用音频编解码器的输出功能，以便能够播放声音
    codec->EnableOutput(true);  // 启用音频输出
    // 设置音频解码的采样率为 16000Hz
    SetDecodeSampleRate(16000);  // 设置解码采样率
    // 获取声音数据的起始指针
    const char* data = sound.data();
    // 获取声音数据的大小
    size_t size = sound.size();
    // 遍历声音数据
    for (const char* p = data; p < data + size; ) {
        // 将当前指针位置的数据解释为 BinaryProtocol3 结构体
        auto p3 = (BinaryProtocol3*)p;
        // 指针向后移动 BinaryProtocol3 结构体的大小
        p += sizeof(BinaryProtocol3);

        // 从 BinaryProtocol3 结构体中获取有效负载的大小，并将其从网络字节序转换为主机字节序
        auto payload_size = ntohs(p3->payload_size);
        // 创建一个存储 Opus 音频数据的向量，并调整其大小以容纳有效负载
        std::vector<uint8_t> opus;
        opus.resize(payload_size);
        // 将有效负载数据复制到 Opus 向量中
        memcpy(opus.data(), p3->payload, payload_size);
        // 指针向后移动有效负载的大小
        p += payload_size;

        // 使用互斥锁保护对音频解码队列的访问，确保线程安全
        std::lock_guard<std::mutex> lock(mutex_);
        // 将 Opus 音频数据移动到音频解码队列中，等待后续解码和播放
        audio_decode_queue_.emplace_back(std::move(opus));  // 将音频数据加入解码队列
    }
}

// 切换聊天状态
void Application::ToggleChatState() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);  // 如果正在激活，设置为空闲状态
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");  // 协议未初始化
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            SetDeviceState(kDeviceStateConnecting);  // 设置为连接状态
            if (!protocol_->OpenAudioChannel()) {  // 打开音频通道
                return;
            }

            keep_listening_ = true;
            protocol_->SendStartListening(kListeningModeAutoStop);  // 开始监听
            SetDeviceState(kDeviceStateListening);  // 设置为监听状态
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);  // 中止说话
        });
    } else if (device_state_ == kDeviceStateListening) {
        Schedule([this]() {
            protocol_->CloseAudioChannel();  // 关闭音频通道
        });
    }
}

// 开始监听
void Application::StartListening() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);  // 如果正在激活，设置为空闲状态
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");  // 协议未初始化
        return;
    }
    
    keep_listening_ = false;
    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);  // 设置为连接状态
                if (!protocol_->OpenAudioChannel()) {  // 打开音频通道
                    return;
                }
            }
            protocol_->SendStartListening(kListeningModeManualStop);  // 开始监听
            SetDeviceState(kDeviceStateListening);  // 设置为监听状态
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);  // 中止说话
            protocol_->SendStartListening(kListeningModeManualStop);  // 开始监听
            SetDeviceState(kDeviceStateListening);  // 设置为监听状态
        });
    }
}

// 停止监听
void Application::StopListening() {
    Schedule([this]() {
        if (device_state_ == kDeviceStateListening) {
            protocol_->SendStopListening();  // 停止监听
            SetDeviceState(kDeviceStateIdle);  // 设置为空闲状态
        }
    });
}

// 启动应用程序
// Application 类的 Start 方法，用于启动应用程序并进行一系列初始化操作
void Application::Start() {
    // 获取 Board 类的单例对象的引用，用于后续对硬件设备的操作
    auto& board = Board::GetInstance();
    // 设置设备状态为启动状态，表明应用程序开始启动流程
    SetDeviceState(kDeviceStateStarting);  // 设置为启动状态

    /* 设置显示 */
    // 从 Board 单例对象中获取显示设备实例，用于后续显示信息
    auto display = board.GetDisplay();

    /* 设置音频编解码器 */
    // 从 Board 单例对象中获取音频编解码器实例，用于处理音频数据的输入和输出
    auto codec = board.GetAudioCodec();
    // 设置 Opus 解码的采样率为音频编解码器的输出采样率
    opus_decode_sample_rate_ = codec->output_sample_rate();  // 设置解码采样率
    // 创建一个 Opus 解码器包装器对象，使用指定的解码采样率和单声道配置
    opus_decoder_ = std::make_unique<OpusDecoderWrapper>(opus_decode_sample_rate_, 1);  // 创建Opus解码器
    // 创建一个 Opus 编码器包装器对象，使用 16000Hz 采样率、单声道和指定的帧持续时间
    opus_encoder_ = std::make_unique<OpusEncoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);  // 创建Opus编码器
    // 根据开发板类型设置 Opus 编码器的复杂度
    // 对于 ML307 开发板，设置编码复杂度为 5 以节省带宽
    if (board.GetBoardType() == "ml307") {
        // 记录日志，表明检测到 ML307 开发板并设置相应的编码器复杂度
        ESP_LOGI(TAG, "ML307 board detected, setting opus encoder complexity to 5");
        // 设置 Opus 编码器的复杂度为 5
        opus_encoder_->SetComplexity(5);
    } else {
        // 记录日志，表明检测到其他类型的开发板（如 WiFi 板）并设置相应的编码器复杂度
        ESP_LOGI(TAG, "WiFi board detected, setting opus encoder complexity to 3");
        // 设置 Opus 编码器的复杂度为 3 以节省 CPU 资源
        opus_encoder_->SetComplexity(3);
    }

    // 如果音频编解码器的输入采样率不是 16000Hz，需要进行重采样处理
    if (codec->input_sample_rate() != 16000) {
        // 配置输入重采样器，将输入采样率转换为 16000Hz
        input_resampler_.Configure(codec->input_sample_rate(), 16000);
        // 配置参考重采样器，将输入采样率转换为 16000Hz
        reference_resampler_.Configure(codec->input_sample_rate(), 16000);
    }
    // 设置音频编解码器的输入就绪回调函数
    codec->OnInputReady([this, codec]() {
        // 用于标记是否有更高优先级的任务被唤醒
        BaseType_t higher_priority_task_woken = pdFALSE;
        // 在中断服务例程中设置事件组中的 AUDIO_INPUT_READY_EVENT 事件位
        xEventGroupSetBitsFromISR(event_group_, AUDIO_INPUT_READY_EVENT, &higher_priority_task_woken);
        // 返回是否有更高优先级的任务被唤醒
        return higher_priority_task_woken == pdTRUE;
    });
    // 设置音频编解码器的输出就绪回调函数
    codec->OnOutputReady([this]() {
        // 用于标记是否有更高优先级的任务被唤醒
        BaseType_t higher_priority_task_woken = pdFALSE;
        // 在中断服务例程中设置事件组中的 AUDIO_OUTPUT_READY_EVENT 事件位
        xEventGroupSetBitsFromISR(event_group_, AUDIO_OUTPUT_READY_EVENT, &higher_priority_task_woken);
        // 返回是否有更高优先级的任务被唤醒
        return higher_priority_task_woken == pdTRUE;
    });
    // 启动音频编解码器，使其开始工作
    codec->Start();  // 启动音频编解码器

    /* 启动主循环 */
    // 创建一个新的 FreeRTOS 任务来运行主循环
    xTaskCreate([](void* arg) {
        // 将传入的参数转换为 Application 类的指针
        Application* app = (Application*)arg;
        // 调用 Application 类的 MainLoop 方法，进入主循环
        app->MainLoop();  // 主循环
        // 删除当前任务
        vTaskDelete(NULL);
    }, "main_loop", 4096 * 2, this, 3, nullptr);

    /* 等待网络准备就绪 */
    // 调用 Board 类的 StartNetwork 方法，启动网络连接
    board.StartNetwork();

    // 初始化协议
    // 在显示设备上设置状态信息，表明正在加载协议
    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);  // 设置状态为加载协议
    // 根据配置选择使用 WebSocket 协议还是 MQTT 协议
#ifdef CONFIG_CONNECTION_TYPE_WEBSOCKET
    // 创建 WebSocket 协议对象
    protocol_ = std::make_unique<WebsocketProtocol>();  // 使用WebSocket协议
#else
    // 创建 MQTT 协议对象
    protocol_ = std::make_unique<MqttProtocol>();  // 使用MQTT协议
#endif
    // 设置协议对象的网络错误回调函数
    protocol_->OnNetworkError([this](const std::string& message) {
        // 当发生网络错误时，将设备状态设置为空闲状态
        SetDeviceState(kDeviceStateIdle);  // 设置为空闲状态
        // 弹出警告框，显示错误信息
        Alert(Lang::Strings::ERROR, message.c_str(), "sad", Lang::Sounds::P3_EXCLAMATION);  // 显示错误信息
    });
    // 设置协议对象的音频数据接收回调函数
    protocol_->OnIncomingAudio([this](std::vector<uint8_t>&& data) {
        // 使用互斥锁保护对音频解码队列的访问
        std::lock_guard<std::mutex> lock(mutex_);
        // 当设备处于说话状态时，将接收到的音频数据加入解码队列
        if (device_state_ == kDeviceStateSpeaking) {
            audio_decode_queue_.emplace_back(std::move(data));  // 将音频数据加入解码队列
        }
    });
    // 设置协议对象的音频通道打开回调函数
    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        // 当音频通道打开时，关闭设备的省电模式
        board.SetPowerSaveMode(false);  // 关闭省电模式
        // 检查服务器的采样率和设备的输出采样率是否一致
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            // 如果不一致，记录警告日志，提示可能会因重采样导致失真
            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                protocol_->server_sample_rate(), codec->output_sample_rate());
        }
        // 设置 Opus 解码的采样率为服务器的采样率
        SetDecodeSampleRate(protocol_->server_sample_rate());  // 设置解码采样率
        // 清空上次的 IoT 设备状态信息
        last_iot_states_.clear();
        // 获取 IoT 设备管理器的单例对象
        auto& thing_manager = iot::ThingManager::GetInstance();
        // 发送 IoT 设备的描述信息到服务器
        protocol_->SendIotDescriptors(thing_manager.GetDescriptorsJson());
    });
    // 设置协议对象的音频通道关闭回调函数
    protocol_->OnAudioChannelClosed([this, &board]() {
        // 当音频通道关闭时，开启设备的省电模式
        board.SetPowerSaveMode(true);  // 开启省电模式
        // 安排一个任务来处理音频通道关闭后的操作
        Schedule([this]() {
            // 获取 Board 单例对象的显示设备实例
            auto display = Board::GetInstance().GetDisplay();
            // 清空显示设备上的聊天消息
            display->SetChatMessage("system", "");
            // 将设备状态设置为空闲状态
            SetDeviceState(kDeviceStateIdle);  // 设置为空闲状态
        });
    });
    // 设置协议对象的 JSON 数据接收回调函数
    protocol_->OnIncomingJson([this, display](const cJSON* root) {
        // 从 JSON 数据中获取 "type" 字段的值
        auto type = cJSON_GetObjectItem(root, "type");
        // 处理文本转语音（TTS）类型的 JSON 数据
        if (strcmp(type->valuestring, "tts") == 0) {  // 文本转语音
            // 从 JSON 数据中获取 "state" 字段的值
            auto state = cJSON_GetObjectItem(root, "state");
            // 处理 TTS 开始状态
            if (strcmp(state->valuestring, "start") == 0) {
                // 安排一个任务来处理 TTS 开始事件
                Schedule([this]() {
                    // 标记未中止说话
                    aborted_ = false;
                    // 如果设备处于空闲状态或监听状态，将设备状态设置为说话状态
                    if (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateListening) {
                        SetDeviceState(kDeviceStateSpeaking);  // 设置为说话状态
                    }
                });
            } 
            // 处理 TTS 停止状态
            else if (strcmp(state->valuestring, "stop") == 0) {
                // 安排一个任务来处理 TTS 停止事件
                Schedule([this]() {
                    // 如果设备处于说话状态
                    if (device_state_ == kDeviceStateSpeaking) {
                        // 等待后台任务完成
                        background_task_->WaitForCompletion();  // 等待后台任务完成
                        // 如果需要继续监听
                        if (keep_listening_) {
                            // 发送开始监听的请求
                            protocol_->SendStartListening(kListeningModeAutoStop);  // 开始监听
                            // 将设备状态设置为监听状态
                            SetDeviceState(kDeviceStateListening);  // 设置为监听状态
                        } else {
                            // 将设备状态设置为空闲状态
                            SetDeviceState(kDeviceStateIdle);  // 设置为空闲状态
                        }
                    }
                });
            } 
            // 处理 TTS 句子开始状态
            else if (strcmp(state->valuestring, "sentence_start") == 0) {
                // 从 JSON 数据中获取 "text" 字段的值
                auto text = cJSON_GetObjectItem(root, "text");
                // 如果 "text" 字段存在
                if (text != NULL) {
                    // 记录日志，显示接收到的文本信息
                    ESP_LOGI(TAG, "<< %s", text->valuestring);
                    // 安排一个任务来显示助手的聊天消息
                    Schedule([this, display, message = std::string(text->valuestring)]() {
                        display->SetChatMessage("assistant", message.c_str());  // 显示助手消息
                    });
                }
            }
        } 
        // 处理语音转文本（STT）类型的 JSON 数据
        else if (strcmp(type->valuestring, "stt") == 0) {  // 语音转文本
            // 从 JSON 数据中获取 "text" 字段的值
            auto text = cJSON_GetObjectItem(root, "text");
            // 如果 "text" 字段存在
            if (text != NULL) {
                // 记录日志，显示接收到的文本信息
                ESP_LOGI(TAG, ">> %s", text->valuestring);
                // 安排一个任务来显示用户的聊天消息
                Schedule([this, display, message = std::string(text->valuestring)]() {
                    display->SetChatMessage("user", message.c_str());  // 显示用户消息
                });
            }
        } 
        // 处理大语言模型（LLM）类型的 JSON 数据
        else if (strcmp(type->valuestring, "llm") == 0) {  // 大语言模型
            // 从 JSON 数据中获取 "emotion" 字段的值
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            // 如果 "emotion" 字段存在
            if (emotion != NULL) {
                // 安排一个任务来设置显示设备的表情状态
                Schedule([this, display, emotion_str = std::string(emotion->valuestring)]() {
                    display->SetEmotion(emotion_str.c_str());  // 设置表情
                });
            }
        } 
        // 处理 IoT 设备类型的 JSON 数据
        else if (strcmp(type->valuestring, "iot") == 0) {  // IoT设备
            // 从 JSON 数据中获取 "commands" 字段的值
            auto commands = cJSON_GetObjectItem(root, "commands");
            // 如果 "commands" 字段存在
            if (commands != NULL) {
                // 获取 IoT 设备管理器的单例对象
                auto& thing_manager = iot::ThingManager::GetInstance();
                // 遍历命令数组
                for (int i = 0; i < cJSON_GetArraySize(commands); ++i) {
                    // 获取数组中的每个命令
                    auto command = cJSON_GetArrayItem(commands, i);
                    // 调用 IoT 设备管理器执行命令
                    thing_manager.Invoke(command);  // 执行IoT命令
                }
            }
        }
    });
    // 启动协议对象，使其开始工作
    protocol_->Start();  // 启动协议

    // 检查新固件版本或获取MQTT代理地址
    // 设置 OTA（Over-the-Air）更新检查的版本 URL
    ota_.SetCheckVersionUrl(CONFIG_OTA_VERSION_URL);
    // 设置 OTA 请求头中的设备 ID 为系统的 MAC 地址
    ota_.SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    // 设置 OTA 请求头中的客户端 ID 为设备的 UUID
    ota_.SetHeader("Client-Id", board.GetUuid());
    // 设置 OTA 请求头中的接受语言为当前配置的语言代码
    ota_.SetHeader("Accept-Language", Lang::CODE);
    // 获取当前应用程序的描述信息
    auto app_desc = esp_app_get_description();
    // 设置 OTA 请求头中的用户代理信息，包含开发板名称和应用程序版本
    ota_.SetHeader("User-Agent", std::string(BOARD_NAME "/") + app_desc->version);

    // 创建任务检查新版本
    // 创建一个新的 FreeRTOS 任务来检查是否有新的固件版本
    xTaskCreate([](void* arg) {
        // 将传入的参数转换为 Application 类的指针
        Application* app = (Application*)arg;
        // 调用 Application 类的 CheckNewVersion 方法来检查新版本
        app->CheckNewVersion();  // 检查新版本
        // 删除当前任务
        vTaskDelete(NULL);
    }, "check_new_version", 4096 * 2, this, 2, nullptr);

#if CONFIG_USE_AUDIO_PROCESSOR
    // 如果配置了使用音频处理器，则进行初始化
    // 初始化音频处理器，传入音频输入通道数和输入参考信息
    audio_processor_.Initialize(codec->input_channels(), codec->input_reference());
    // 设置音频处理器的输出回调函数
    audio_processor_.OnOutput([this](std::vector<int16_t>&& data) {
        // 安排一个后台任务来处理音频处理器的输出数据
        background_task_->Schedule([this, data = std::move(data)]() mutable {
            // 使用 Opus 编码器对音频数据进行编码
            opus_encoder_->Encode(std::move(data), [this](std::vector<uint8_t>&& opus) {
                // 安排一个任务来发送编码后的音频数据
                Schedule([this, opus = std::move(opus)]() {
                    // 通过协议对象发送编码后的音频数据
                    protocol_->SendAudio(opus);  // 发送音频数据
                });
            });
        });
    });
#endif

#if CONFIG_USE_WAKE_WORD_DETECT
    // 如果配置了使用唤醒词检测，则进行初始化
    // 初始化唤醒词检测模块，传入音频输入通道数和输入参考信息
    wake_word_detect_.Initialize(codec->input_channels(), codec->input_reference());
    // 设置唤醒词检测模块的语音活动检测（VAD）状态变化回调函数
    wake_word_detect_.OnVadStateChange([this](bool speaking) {
        // 安排一个任务来处理 VAD 状态变化事件
        Schedule([this, speaking]() {
            // 当设备处于监听状态时
            if (device_state_ == kDeviceStateListening) {
                // 如果检测到正在说话
                if (speaking) {
                    // 标记检测到语音
                    voice_detected_ = true;  // 检测到语音
                } else {
                    // 标记未检测到语音
                    voice_detected_ = false;  // 未检测到语音
                }
                // 获取 Board 单例对象的 LED 设备实例
                auto led = Board::GetInstance().GetLed();
                // 调用 LED 设备的状态变化处理函数
                led->OnStateChanged();  // 更新LED状态
            }
        });
    });

    // 设置唤醒词检测回调
    // 当检测到唤醒词时，执行以下操作
    wake_word_detect_.OnWakeWordDetected([this](const std::string& wake_word) {
        // 安排一个任务来处理唤醒词检测事件
        Schedule([this, &wake_word]() {
            // 如果设备当前处于空闲状态
            if (device_state_ == kDeviceStateIdle) {
                // 将设备状态设置为连接状态
                SetDeviceState(kDeviceStateConnecting);  // 设置为连接状态
                // 对唤醒词数据进行编码
                wake_word_detect_.EncodeWakeWordData();  // 编码唤醒词数据

                // 尝试打开音频通道，如果打开失败
                if (!protocol_->OpenAudioChannel()) {  // 打开音频通道
                    // 重新开始唤醒词检测
                    wake_word_detect_.StartDetection();  // 开始检测
                    return;
                }
                
                // 用于存储编码后的唤醒词音频数据
                std::vector<uint8_t> opus;
                // 循环获取编码后的唤醒词音频数据并发送到服务器
                while (wake_word_detect_.GetWakeWordOpus(opus)) {
                    // 通过协议对象发送编码后的音频数据
                    protocol_->SendAudio(opus);
                }
                // 向服务器发送唤醒词检测到的消息
                protocol_->SendWakeWordDetected(wake_word);
                // 记录日志，显示检测到的唤醒词
                ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
                // 标记需要继续监听
                keep_listening_ = true;
                // 将设备状态设置为监听状态
                SetDeviceState(kDeviceStateListening);  // 设置为监听状态
            } 
            // 如果设备当前处于说话状态
            else if (device_state_ == kDeviceStateSpeaking) {
                // 中止说话操作，并指定中止原因是检测到唤醒词
                AbortSpeaking(kAbortReasonWakeWordDetected);  // 中止说话
            } 
            // 如果设备当前处于激活状态
            else if (device_state_ == kDeviceStateActivating) {
                // 将设备状态设置为空闲状态
                SetDeviceState(kDeviceStateIdle);  // 设置为空闲状态
            }

            // 恢复唤醒词检测
            wake_word_detect_.StartDetection();
        });
    });
    // 开始唤醒词检测
    wake_word_detect_.StartDetection();  // 开始唤醒词检测
#endif

    // 将设备状态设置为空闲状态，表明初始化完成，进入待机状态
    SetDeviceState(kDeviceStateIdle);  // 设置为空闲状态
    // 启动一个周期性的时钟定时器，定时器周期为 1 秒
    esp_timer_start_periodic(clock_timer_handle_, 1000000);  // 启动时钟定时器
}

// 时钟定时器回调函数
// 这是 Application 类的 OnClockTimer 方法，用于处理时钟定时器相关的逻辑
void Application::OnClockTimer() {
    // 增加时钟滴答计数
    clock_ticks_++;

    // 每10秒打印一次调试信息
    // 如果当前时钟滴答计数是10的倍数，则执行以下调试信息打印和相关操作
    if (clock_ticks_ % 10 == 0) {
        // 原本这里注释掉的函数可能是用于打印实时统计信息的，目前未启用
        // SystemInfo::PrintRealTimeStats(pdMS_TO_TICKS(1000));
        // 获取内部 SRAM 的可用大小（以字节为单位）
        int free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        // 获取内部 SRAM 的最小可用大小（以字节为单位），即从启动以来剩余的最小内存量
        int min_free_sram = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        // 使用 ESP_LOGI 宏记录日志，打印内部 SRAM 的可用大小和最小可用大小
        ESP_LOGI(TAG, "Free internal: %u minimal internal: %u", free_sram, min_free_sram);

        // 如果已同步服务器时间，设置状态为时钟 "HH:MM"
        // 检查 ota_ 对象是否已经获取到了服务器时间
        if (ota_.HasServerTime()) {
            // 检查设备状态是否为空闲状态
            if (device_state_ == kDeviceStateIdle) {
                // 安排一个任务来设置设备的显示状态为当前时间
                Schedule([this]() {
                    // 获取当前的时间戳
                    time_t now = time(NULL);
                    // 定义一个字符数组用于存储格式化后的时间字符串
                    char time_str[64];
                    // 使用 strftime 函数将时间戳格式化为 "HH:MM  " 的形式
                    // localtime 函数将时间戳转换为本地时间，然后 strftime 按照指定格式进行格式化
                    strftime(time_str, sizeof(time_str), "%H:%M  ", localtime(&now));
                    // 获取 Board 单例对象的 Display 实例，并设置其状态为格式化后的时间字符串
                    Board::GetInstance().GetDisplay()->SetStatus(time_str);
                });
            }
        }
    }
}

// 调度任务
void Application::Schedule(std::function<void()> callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));  // 将任务加入主任务队列
    }
    xEventGroupSetBits(event_group_, SCHEDULE_EVENT);  // 设置调度事件
}

// 主循环，控制聊天状态和WebSocket连接
// 这是 Application 类的 MainLoop 方法，作为应用程序的主循环，负责处理各类事件
void Application::MainLoop() {
    // 进入一个无限循环，使主循环持续运行
    while (true) {
        // 等待事件组中的指定事件发生
        // xEventGroupWaitBits 是 FreeRTOS 提供的函数，用于等待事件组中的特定事件位被设置
        // event_group_ 是事件组句柄，用于管理事件
        // SCHEDULE_EVENT | AUDIO_INPUT_READY_EVENT | AUDIO_OUTPUT_READY_EVENT 表示等待的事件位组合
        // pdTRUE 表示在事件发生后清除这些事件位
        // pdFALSE 表示只要有一个事件位被设置就返回，而不是所有事件位都被设置才返回
        // portMAX_DELAY 表示无限期等待，直到有事件发生
        auto bits = xEventGroupWaitBits(event_group_,
            SCHEDULE_EVENT | AUDIO_INPUT_READY_EVENT | AUDIO_OUTPUT_READY_EVENT,
            pdTRUE, pdFALSE, portMAX_DELAY);

        // 检查是否有音频输入准备好的事件发生
        if (bits & AUDIO_INPUT_READY_EVENT) {
            // 如果有音频输入准备好的事件，调用 InputAudio 方法处理音频输入
            InputAudio();  // 处理音频输入
        }

        // 检查是否有音频输出准备好的事件发生
        if (bits & AUDIO_OUTPUT_READY_EVENT) {
            // 如果有音频输出准备好的事件，调用 OutputAudio 方法处理音频输出
            OutputAudio();  // 处理音频输出
        }

        // 检查是否有调度事件发生
        if (bits & SCHEDULE_EVENT) {
            // 使用互斥锁保护对主任务列表的访问
            std::unique_lock<std::mutex> lock(mutex_);
            // 将主任务列表中的任务移动到局部变量 tasks 中
            std::list<std::function<void()>> tasks = std::move(main_tasks_);
            // 解锁互斥锁，释放对主任务列表的独占访问
            lock.unlock();

            // 遍历任务列表，依次执行每个任务
            for (auto& task : tasks) {
                // 执行任务
                task();  // 执行任务
            }
        }
    }
}

// 重置解码器
void Application::ResetDecoder() {
    std::lock_guard<std::mutex> lock(mutex_);
    opus_decoder_->ResetState();  // 重置解码器状态
    audio_decode_queue_.clear();  // 清空音频解码队列
    last_output_time_ = std::chrono::steady_clock::now();
}

// 输出音频
// 这是 Application 类的 OutputAudio 方法，用于处理音频输出
void Application::OutputAudio() {
    // 获取当前时间点
    auto now = std::chrono::steady_clock::now();
    // 获取音频编解码器实例
    // 通过 Board 单例对象获取音频编解码器，后续用于音频数据的输出和相关操作
    auto codec = Board::GetInstance().GetAudioCodec();
    // 定义最大静音时间为 10 秒
    const int max_silence_seconds = 10;  // 最大静音时间

    // 使用互斥锁保护对音频解码队列的访问
    std::unique_lock<std::mutex> lock(mutex_);
    // 检查音频解码队列是否为空
    if (audio_decode_queue_.empty()) {
        // 如果设备处于空闲状态且长时间没有音频数据
        if (device_state_ == kDeviceStateIdle) {
            // 计算距离上次输出音频的时间间隔（以秒为单位）
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_output_time_).count();
            // 如果时间间隔大于最大静音时间
            if (duration > max_silence_seconds) {
                // 禁用音频输出
                codec->EnableOutput(false);  // 禁用音频输出
            }
        }
        // 如果音频解码队列为空，直接返回，结束本次音频输出处理
        return;
    }

    // 检查设备状态是否为监听状态
    if (device_state_ == kDeviceStateListening) {
        // 如果设备处于监听状态，清空音频解码队列
        audio_decode_queue_.clear();  // 清空音频解码队列
        // 清空队列后返回，结束本次音频输出处理
        return;
    }

    // 更新上次输出音频的时间为当前时间
    last_output_time_ = now;
    // 从音频解码队列中取出第一个元素（即要处理的音频数据）
    auto opus = std::move(audio_decode_queue_.front());
    // 将取出的元素从队列中移除
    audio_decode_queue_.pop_front();
    // 解锁互斥锁，释放对音频解码队列的独占访问
    lock.unlock();

    // 安排一个后台任务来处理音频数据的解码和输出
    background_task_->Schedule([this, codec, opus = std::move(opus)]() mutable {
        // 检查任务是否已被中止
        if (aborted_) {
            // 如果任务已被中止，直接返回，不进行后续处理
            return;
        }

        // 定义一个用于存储解码后 PCM 音频数据的向量
        std::vector<int16_t> pcm;
        // 使用 Opus 解码器对音频数据进行解码
        // 如果解码失败，直接返回，不进行后续处理
        if (!opus_decoder_->Decode(std::move(opus), pcm)) {  // 解码音频数据
            return;
        }

        // 检查解码后的音频数据采样率与音频编解码器的输出采样率是否不同
        if (opus_decode_sample_rate_ != codec->output_sample_rate()) {
            // 计算重采样后音频数据的目标大小
            int target_size = output_resampler_.GetOutputSamples(pcm.size());
            // 定义一个用于存储重采样后音频数据的向量
            std::vector<int16_t> resampled(target_size);
            // 使用重采样器对音频数据进行重采样处理
            output_resampler_.Process(pcm.data(), pcm.size(), resampled.data());
            // 将重采样后的数据移动到 pcm 向量中
            pcm = std::move(resampled);
        }

        // 将处理后的音频数据发送到音频编解码器进行输出
        codec->OutputData(pcm);  // 输出音频数据
    });
}

// 输入音频
// 这是 Application 类的 InputAudio 方法，用于处理音频输入
void Application::InputAudio() {
    // 获取音频编解码器实例
    // 通过 Board 单例对象获取音频编解码器，后续用于音频数据的输入和处理
    auto codec = Board::GetInstance().GetAudioCodec();
    // 定义一个存储音频数据的向量，数据类型为 int16_t
    std::vector<int16_t> data;
    // 从音频编解码器获取音频输入数据
    // 如果获取数据失败，直接返回，结束本次音频输入处理
    if (!codec->InputData(data)) {  // 获取音频输入数据
        return;
    }

    // 检查音频输入的采样率是否为 16000
    // 如果不是 16000，需要进行重采样处理
    if (codec->input_sample_rate() != 16000) {
        // 检查音频输入的通道数是否为 2（立体声）
        if (codec->input_channels() == 2) {
            // 分离立体声数据为麦克风通道和参考通道
            // 初始化麦克风通道和参考通道的向量，大小为原始数据的一半
            auto mic_channel = std::vector<int16_t>(data.size() / 2);
            auto reference_channel = std::vector<int16_t>(data.size() / 2);
            // 遍历原始数据，将奇数索引的数据存入麦克风通道，偶数索引的数据存入参考通道
            for (size_t i = 0, j = 0; i < mic_channel.size(); ++i, j += 2) {
                mic_channel[i] = data[j];
                reference_channel[i] = data[j + 1];
            }
            // 对麦克风通道和参考通道的数据进行重采样
            // 初始化重采样后的麦克风通道和参考通道的向量，大小根据重采样器计算得出
            auto resampled_mic = std::vector<int16_t>(input_resampler_.GetOutputSamples(mic_channel.size()));
            auto resampled_reference = std::vector<int16_t>(reference_resampler_.GetOutputSamples(reference_channel.size()));
            // 使用重采样器对麦克风通道和参考通道的数据进行重采样处理
            input_resampler_.Process(mic_channel.data(), mic_channel.size(), resampled_mic.data());
            reference_resampler_.Process(reference_channel.data(), reference_channel.size(), resampled_reference.data());
            // 合并重采样后的麦克风通道和参考通道的数据
            // 调整 data 向量的大小，以容纳重采样后的数据
            data.resize(resampled_mic.size() + resampled_reference.size());
            // 将重采样后的麦克风通道和参考通道的数据合并回 data 向量
            for (size_t i = 0, j = 0; i < resampled_mic.size(); ++i, j += 2) {
                data[j] = resampled_mic[i];
                data[j + 1] = resampled_reference[i];
            }
        } else {
            // 单声道数据的重采样
            // 初始化重采样后的向量，大小根据重采样器计算得出
            auto resampled = std::vector<int16_t>(input_resampler_.GetOutputSamples(data.size()));
            // 使用重采样器对单声道数据进行重采样处理
            input_resampler_.Process(data.data(), data.size(), resampled.data());
            // 将重采样后的数据移动到 data 向量中
            data = std::move(resampled);
        }
    }

    // 如果配置了使用唤醒词检测功能
    #if CONFIG_USE_WAKE_WORD_DETECT
    // 检查唤醒词检测是否正在运行
    if (wake_word_detect_.IsDetectionRunning()) {
        // 将音频数据喂入唤醒词检测模块进行检测
        wake_word_detect_.Feed(data);  // 喂入音频数据到唤醒词检测
    }
    #endif

    // 如果配置了使用音频处理器
    #if CONFIG_USE_AUDIO_PROCESSOR
    // 检查音频处理器是否正在运行
    if (audio_processor_.IsRunning()) {
        // 将音频数据输入到音频处理器进行处理
        audio_processor_.Input(data);  // 处理音频数据
    }
    // 如果没有配置使用音频处理器
    else {
        // 检查设备状态是否为监听状态
        if (device_state_ == kDeviceStateListening) {
            // 安排一个后台任务来处理音频数据
            background_task_->Schedule([this, data = std::move(data)]() mutable {
                // 使用 Opus 编码器对音频数据进行编码
                opus_encoder_->Encode(std::move(data), [this](std::vector<uint8_t>&& opus) {
                    // 安排一个任务来发送编码后的音频数据
                    Schedule([this, opus = std::move(opus)]() {
                        // 通过协议对象发送编码后的音频数据
                        protocol_->SendAudio(opus);  // 发送音频数据
                    });
                });
            });
        }
    }
    #endif
}

// 中止说话
// 此方法用于中止设备的说话状态
void Application::AbortSpeaking(AbortReason reason) {
    // 记录日志，表明正在执行中止说话的操作
    ESP_LOGI(TAG, "Abort speaking");
    // 将 aborted_ 标志设置为 true，表示说话操作已被中止
    aborted_ = true;
    // 通过协议对象向服务器发送中止说话的命令，并携带中止原因
    protocol_->SendAbortSpeaking(reason);  // 发送中止说话命令
}

// 设置设备状态
// 此方法用于设置设备的状态
void Application::SetDeviceState(DeviceState state) {
    // 检查当前设备状态是否与要设置的状态相同
    // 如果相同，说明状态无需改变，直接返回，避免不必要的操作
    if (device_state_ == state) {
        return;
    }
    
    // 将时钟滴答计数重置为 0，可能用于与状态相关的计时操作
    clock_ticks_ = 0;
    // 保存当前设备的状态，以便后续使用
    auto previous_state = device_state_;
    // 将设备状态更新为传入的状态
    device_state_ = state;
    // 记录日志，显示设备状态的变化，STATE_STRINGS 是一个存储状态字符串的数组
    ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[device_state_]);  // 记录状态变化
    // 当设备状态发生变化时，等待所有后台任务完成
    // 确保在状态改变前，之前的后台任务都已结束，避免冲突
    background_task_->WaitForCompletion();

    // 获取 Board 类的单例对象，用于访问硬件相关的功能
    auto& board = Board::GetInstance();
    // 从 Board 实例中获取音频编解码器对象
    auto codec = board.GetAudioCodec();
    // 从 Board 实例中获取显示设备对象
    auto display = board.GetDisplay();
    // 从 Board 实例中获取 LED 设备对象
    auto led = board.GetLed();
    // 调用 LED 的状态改变处理函数，更新 LED 的显示状态
    led->OnStateChanged();  // 更新LED状态
    // 根据传入的新状态执行不同的操作
    switch (state) {
        // 未知状态或空闲状态
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            // 在显示设备上设置状态信息为待机状态
            display->SetStatus(Lang::Strings::STANDBY);  // 设置状态为待机
            // 在显示设备上设置表情为中性表情
            display->SetEmotion("neutral");  // 设置表情为中性
            // 如果配置了使用音频处理器，则停止音频处理器的工作
#if CONFIG_USE_AUDIO_PROCESSOR
            audio_processor_.Stop();  // 停止音频处理器
#endif
            break;
        // 连接状态
        case kDeviceStateConnecting:
            // 在显示设备上设置状态信息为连接中
            display->SetStatus(Lang::Strings::CONNECTING);  // 设置状态为连接中
            // 在显示设备上设置表情为中性表情
            display->SetEmotion("neutral");  // 设置表情为中性
            // 清空显示设备上的聊天消息
            display->SetChatMessage("system", "");  // 清空消息
            break;
        // 监听状态
        case kDeviceStateListening:
            // 在显示设备上设置状态信息为监听中
            display->SetStatus(Lang::Strings::LISTENING);  // 设置状态为监听中
            // 在显示设备上设置表情为中性表情
            display->SetEmotion("neutral");  // 设置表情为中性
            // 重置解码器，清除解码器的内部状态
            ResetDecoder();  // 重置解码器
            // 重置编码器的状态，以便重新开始编码操作
            opus_encoder_->ResetState();  // 重置编码器状态
            // 如果配置了使用音频处理器，则启动音频处理器
#if CONFIG_USE_AUDIO_PROCESSOR
            audio_processor_.Start();  // 启动音频处理器
#endif
            // 更新 IoT 设备的状态信息
            UpdateIotStates();  // 更新IoT状态
            // 如果之前的状态是说话状态
            if (previous_state == kDeviceStateSpeaking) {
                // FIXME: 等待扬声器清空缓冲区，这里使用 vTaskDelay 进行短暂延迟
                vTaskDelay(pdMS_TO_TICKS(120));
            }
            break;
        // 说话状态
        case kDeviceStateSpeaking:
            // 在显示设备上设置状态信息为说话中
            display->SetStatus(Lang::Strings::SPEAKING);  // 设置状态为说话中
            // 重置解码器，清除解码器的内部状态
            ResetDecoder();  // 重置解码器
            // 启用音频编解码器的输出功能
            codec->EnableOutput(true);  // 启用音频输出
            // 如果配置了使用音频处理器，则停止音频处理器的工作
#if CONFIG_USE_AUDIO_PROCESSOR
            audio_processor_.Stop();  // 停止音频处理器
#endif
            break;
        // 其他未处理的状态
        default:
            // 对于其他状态，不做任何处理，直接跳出 switch 语句
            // 其他状态不做处理
            break;
    }
}

// 设置解码采样率
// 设置解码采样率的方法
void Application::SetDecodeSampleRate(int sample_rate) {
    // 如果当前的解码采样率已经等于要设置的采样率，则直接返回，不进行后续操作
    if (opus_decode_sample_rate_ == sample_rate) {
        return;
    }

    // 更新当前的解码采样率为传入的采样率
    opus_decode_sample_rate_ = sample_rate;
    // 重置 Opus 解码器智能指针，释放之前的解码器资源
    opus_decoder_.reset();
    // 创建一个新的 Opus 解码器实例，使用新的解码采样率和单声道配置
    opus_decoder_ = std::make_unique<OpusDecoderWrapper>(opus_decode_sample_rate_, 1);  // 创建新的解码器

    // 获取 Board 单例对象的音频编解码器实例
    auto codec = Board::GetInstance().GetAudioCodec();
    // 检查新的解码采样率和音频编解码器的输出采样率是否不一致
    if (opus_decode_sample_rate_ != codec->output_sample_rate()) {
        // 记录日志，显示需要将音频从新的解码采样率重采样到编解码器的输出采样率
        ESP_LOGI(TAG, "Resampling audio from %d to %d", opus_decode_sample_rate_, codec->output_sample_rate());
        // 配置输出重采样器，将新的解码采样率转换为编解码器的输出采样率
        output_resampler_.Configure(opus_decode_sample_rate_, codec->output_sample_rate());  // 配置重采样器
    }
}

// 更新 IoT 状态的方法
void Application::UpdateIotStates() {
    // 获取 IoT 设备管理器的单例对象的引用
    auto& thing_manager = iot::ThingManager::GetInstance();
    // 获取当前 IoT 设备的状态 JSON 数据
    auto states = thing_manager.GetStatesJson();
    // 检查当前的 IoT 设备状态 JSON 数据是否与上次的不同
    if (states != last_iot_states_) {
        // 如果不同，更新上次记录的 IoT 设备状态 JSON 数据
        last_iot_states_ = states;
        // 通过协议对象发送当前的 IoT 设备状态 JSON 数据到服务器
        protocol_->SendIotStates(states);  // 发送IoT状态
    }
}

// 重启设备的方法
void Application::Reboot() {
    // 记录日志，显示设备正在重启
    ESP_LOGI(TAG, "Rebooting...");
    // 调用 esp_restart 函数，重启设备
    esp_restart();  // 重启设备
}

// 唤醒词调用的方法
void Application::WakeWordInvoke(const std::string& wake_word) {
    // 如果设备当前处于空闲状态
    if (device_state_ == kDeviceStateIdle) {
        // 切换聊天状态（具体实现未知，可能涉及 UI 或功能状态的切换）
        ToggleChatState();  // 切换聊天状态
        // 安排一个任务来处理唤醒词检测的发送操作
        Schedule([this, wake_word]() {
            // 如果协议对象存在
            if (protocol_) {
                // 通过协议对象发送检测到唤醒词的消息到服务器，携带唤醒词内容
                protocol_->SendWakeWordDetected(wake_word);  // 发送唤醒词检测
            }
        }); 
    } 
    // 如果设备当前处于说话状态
    else if (device_state_ == kDeviceStateSpeaking) {
        // 安排一个任务来中止当前的说话操作，中止原因设置为无
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);  // 中止说话
        });
    } 
    // 如果设备当前处于监听状态
    else if (device_state_ == kDeviceStateListening) {   
        // 安排一个任务来关闭音频通道
        Schedule([this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();  // 关闭音频通道
            }
        });
    }
}

// 判断是否可以进入睡眠模式
bool Application::CanEnterSleepMode() {
    if (device_state_ != kDeviceStateIdle) {
        return false;  // 如果设备不处于空闲状态，不能进入睡眠模式
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;  // 如果音频通道已打开，不能进入睡眠模式
    }

    // 现在可以安全进入睡眠模式
    return true;
}