#include "websocket_protocol.h"
#include "board.h"
#include "system_info.h"
#include "application.h"

#include <cstring>
#include <cJSON.h>
#include <esp_log.h>
#include <arpa/inet.h>
#include "assets/lang_config.h"

#define TAG "WS" // 定义日志标签

// WebsocketProtocol 构造函数
WebsocketProtocol::WebsocketProtocol()
{
    event_group_handle_ = xEventGroupCreate(); // 创建一个事件组，用于任务间同步
}

// WebsocketProtocol 析构函数
WebsocketProtocol::~WebsocketProtocol()
{
    if (websocket_ != nullptr)
    {
        delete websocket_; // 删除 WebSocket 对象
    }
    vEventGroupDelete(event_group_handle_); // 删除事件组
}

// 启动 WebSocket 协议
void WebsocketProtocol::Start()
{
    // 目前为空，可能用于未来的初始化操作
}

// 发送音频数据
void WebsocketProtocol::SendAudio(const std::vector<uint8_t> &data)
{
    if (websocket_ == nullptr)
    {
        return; // 如果 WebSocket 对象为空，直接返回
    }

    websocket_->Send(data.data(), data.size(), true); // 发送二进制音频数据
}

// 发送文本消息
void WebsocketProtocol::SendText(const std::string &text)
{
    if (websocket_ == nullptr)
    {
        return; // 如果 WebSocket 对象为空，直接返回
    }

    if (!websocket_->Send(text))
    {                                                           // 发送文本消息
        ESP_LOGE(TAG, "Failed to send text: %s", text.c_str()); // 如果发送失败，记录错误日志
        SetError(Lang::Strings::SERVER_ERROR);                  // 设置错误信息
    }
}

// 检查音频通道是否已打开
bool WebsocketProtocol::IsAudioChannelOpened() const
{
    return websocket_ != nullptr && websocket_->IsConnected() && !error_occurred_ && !IsTimeout(); // 如果 WebSocket 已连接且无错误且未超时，返回 true
}

// 关闭音频通道
void WebsocketProtocol::CloseAudioChannel()
{
    if (websocket_ != nullptr)
    {
        delete websocket_; // 删除 WebSocket 对象
        websocket_ = nullptr;
    }
}

// 打开音频通道
// 此方法用于打开音频通道，返回值表示是否成功打开音频通道
bool WebsocketProtocol::OpenAudioChannel()
{
    // 如果当前的 WebSocket 对象已经存在，则先删除它，释放资源
    if (websocket_ != nullptr)
    {
        delete websocket_;
    }

    // 重置错误发生标志，将其设为 false，表示当前没有发生错误
    error_occurred_ = false;
    // 获取配置文件中定义的 WebSocket 服务器的 URL
    std::string url = CONFIG_WEBSOCKET_URL;
    // 构建认证令牌，格式为 "Bearer " 加上配置文件中定义的访问令牌
    std::string token = "Bearer " + std::string(CONFIG_WEBSOCKET_ACCESS_TOKEN);
    // 通过 Board 单例对象创建一个新的 WebSocket 对象
    websocket_ = Board::GetInstance().CreateWebSocket();
    // 设置 WebSocket 请求头中的 Authorization 字段，用于身份认证
    websocket_->SetHeader("Authorization", token.c_str());
    // 设置 WebSocket 请求头中的 Protocol-Version 字段，指定协议版本
    websocket_->SetHeader("Protocol-Version", "1");
    // 设置 WebSocket 请求头中的 Device-Id 字段，使用系统的 MAC 地址作为设备 ID
    websocket_->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    // 设置 WebSocket 请求头中的 Client-Id 字段，使用 Board 的 UUID 作为客户端 ID
    websocket_->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());

    // 设置 WebSocket 数据到达时的回调函数
    websocket_->OnData([this](const char *data, size_t len, bool binary)
                       {
        // 如果接收到的数据是二进制数据
        if (binary) {
            // 如果已经设置了处理二进制音频数据的回调函数 on_incoming_audio_，则调用它来处理数据
            if (on_incoming_audio_ != nullptr) {
                on_incoming_audio_(std::vector<uint8_t>((uint8_t*)data, (uint8_t*)data + len));  
            }
        } else {
            // 如果接收到的数据不是二进制数据，即可能是 JSON 数据，解析 JSON 数据
            auto root = cJSON_Parse(data);  
            // 从解析后的 JSON 数据中获取 "type" 字段，以确定消息类型
            auto type = cJSON_GetObjectItem(root, "type");  
            // 如果成功获取到了消息类型字段
            if (type != NULL) {
                // 如果消息类型为 "hello"，则调用 ParseServerHello 方法处理服务器的 hello 消息
                if (strcmp(type->valuestring, "hello") == 0) {
                    ParseServerHello(root);  
                } else {
                    // 如果消息类型不是 "hello"，且已经设置了处理 JSON 数据的回调函数 on_incoming_json_，则调用它来处理数据
                    if (on_incoming_json_ != nullptr) {
                        on_incoming_json_(root);  
                    }
                }
            } else {
                // 如果没有获取到消息类型字段，记录错误日志，显示消息数据
                ESP_LOGE(TAG, "Missing message type, data: %s", data);  
            }
            // 释放解析后的 JSON 对象占用的内存
            cJSON_Delete(root);  
        }
        // 更新最后接收到消息的时间，记录当前时间
        last_incoming_time_ = std::chrono::steady_clock::now(); });

    // 设置 WebSocket 断开连接时的回调函数
    websocket_->OnDisconnected([this]()
                               {
        // 记录 WebSocket 断开连接的日志信息
        ESP_LOGI(TAG, "Websocket disconnected");  
        // 如果已经设置了音频通道关闭的回调函数 on_audio_channel_closed_，则调用它
        if (on_audio_channel_closed_ != nullptr) {
            on_audio_channel_closed_();  
        } });

    // 尝试连接到指定的 WebSocket 服务器
    if (!websocket_->Connect(url.c_str()))
    {
        // 如果连接失败，记录错误日志，显示连接服务器失败的信息
        ESP_LOGE(TAG, "Failed to connect to websocket server");
        // 设置错误信息为 "服务器未找到"
        SetError(Lang::Strings::SERVER_NOT_FOUND);
        // 返回 false，表示打开音频通道失败
        return false;
    }

    // 发送 hello 消息，描述客户端的信息
    std::string message = "{";
    // 添加消息类型字段
    message += "\"type\":\"hello\",";
    // 添加协议版本字段
    message += "\"version\": 1,";
    // 添加传输方式字段
    message += "\"transport\":\"websocket\",";
    // 添加音频参数字段
    message += "\"audio_params\":{";
    // 添加音频参数的具体内容，包括格式、采样率、通道数和帧持续时间
    message += "\"format\":\"opus\", \"sample_rate\":16000, \"channels\":1, \"frame_duration\":" + std::to_string(OPUS_FRAME_DURATION_MS);
    message += "}}";
    // 发送构建好的 hello 消息到服务器
    websocket_->Send(message);

    // 等待服务器的 hello 响应，设置等待时间为 10 秒（10000 毫秒）
    EventBits_t bits = xEventGroupWaitBits(event_group_handle_, WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT, pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
    // 如果没有接收到服务器的 hello 响应事件
    if (!(bits & WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT))
    {
        // 记录错误日志，显示未收到服务器 hello 消息的信息
        ESP_LOGE(TAG, "Failed to receive server hello");
        // 设置错误信息为 "服务器超时"
        SetError(Lang::Strings::SERVER_TIMEOUT);
        // 返回 false，表示打开音频通道失败
        return false;
    }

    // 如果已经设置了音频通道打开的回调函数 on_audio_channel_opened_，则调用它
    if (on_audio_channel_opened_ != nullptr)
    {
        on_audio_channel_opened_();
    }

    // 如果执行到这里，说明成功打开了音频通道，返回 true
    return true;
}

// 解析服务器 hello 消息
// 此方法用于解析服务器的 Hello 消息，属于 WebsocketProtocol 类
// 传入的参数 root 是一个指向 cJSON 对象的指针，代表服务器发送的 Hello 消息的 JSON 数据
void WebsocketProtocol::ParseServerHello(const cJSON *root)
{
    // 从 JSON 数据根节点中获取名为 "transport" 的子项，用于获取传输方式信息
    auto transport = cJSON_GetObjectItem(root, "transport");
    // 如果获取到的 "transport" 子项为空，或者其值不是 "websocket"
    if (transport == nullptr || strcmp(transport->valuestring, "websocket") != 0)
    {
        // 记录错误日志，显示不支持的传输方式
        ESP_LOGE(TAG, "Unsupported transport: %s", transport->valuestring);
        // 直接返回，不再继续解析后续内容
        return;
    }

    // 从 JSON 数据根节点中获取名为 "audio_params" 的子项，用于获取音频参数信息
    auto audio_params = cJSON_GetObjectItem(root, "audio_params");
    // 如果成功获取到了音频参数子项
    if (audio_params != NULL)
    {
        // 从音频参数子项中获取名为 "sample_rate" 的子项，用于获取采样率信息
        auto sample_rate = cJSON_GetObjectItem(audio_params, "sample_rate");
        // 如果成功获取到了采样率子项
        if (sample_rate != NULL)
        {
            // 将采样率的值（为整数类型）赋给成员变量 server_sample_rate_，设置服务器采样率
            server_sample_rate_ = sample_rate->valueint;
        }
    }

    // 使用 xEventGroupSetBits 函数设置事件组 event_group_handle_ 中的 WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT 事件位
    // 用于通知其他部分服务器的 Hello 消息已成功解析
    xEventGroupSetBits(event_group_handle_, WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT);
}