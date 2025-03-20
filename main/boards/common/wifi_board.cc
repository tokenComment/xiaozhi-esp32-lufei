#include "wifi_board.h"

#include "display.h"
#include "application.h"
#include "system_info.h"
#include "font_awesome_symbols.h"
#include "settings.h"
#include "assets/lang_config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_http.h>
#include <esp_mqtt.h>
#include <esp_udp.h>
#include <tcp_transport.h>
#include <tls_transport.h>
#include <web_socket.h>
#include <esp_log.h>

#include <wifi_station.h>
#include <wifi_configuration_ap.h>
#include <ssid_manager.h>

static const char *TAG = "WifiBoard";  // 定义日志标签

// WifiBoard类的构造函数
WifiBoard::WifiBoard() {
    // 从设置中读取是否强制进入WiFi配置模式
    Settings settings("wifi", true);
    wifi_config_mode_ = settings.GetInt("force_ap") == 1;
    if (wifi_config_mode_) {
        ESP_LOGI(TAG, "force_ap is set to 1, reset to 0");  // 记录日志
        settings.SetInt("force_ap", 0);  // 重置标志位
    }
}

// 获取板子类型的函数
std::string WifiBoard::GetBoardType() {
    return "wifi";  // 返回板子类型为"wifi"
}

// 进入WiFi配置模式的函数
// 进入WiFi配置模式的函数
void WifiBoard::EnterWifiConfigMode() {
    // 获取应用程序单例对象
    auto& application = Application::GetInstance();
    // 设置设备状态为WiFi配置中（用于状态管理和UI显示）
    application.SetDeviceState(kDeviceStateWifiConfiguring);  

    // 获取WiFi配置热点单例对象
    auto& wifi_ap = WifiConfigurationAp::GetInstance();
    // 设置热点的语言环境（根据系统语言动态切换）
    wifi_ap.SetLanguage(Lang::CODE);  
    // 设置WiFi热点的SSID前缀（便于用户识别）
    wifi_ap.SetSsidPrefix("Xiaozhi");  
    // 启动WiFi配置热点（创建AP并初始化Web服务器）
    wifi_ap.Start();  

    // 构建用户提示信息
    std::string hint = Lang::Strings::CONNECT_TO_HOTSPOT;
    // 添加生成的热点SSID到提示信息
    hint += wifi_ap.GetSsid();
    // 添加访问方式提示（通过浏览器配置）
    hint += Lang::Strings::ACCESS_VIA_BROWSER;
    // 添加Web服务器URL到提示信息
    hint += wifi_ap.GetWebServerUrl();
    // 添加换行分隔符
    hint += "\n\n";
    
    // 触发系统提示（语音播报+屏幕显示）
    application.Alert(
        Lang::Strings::WIFI_CONFIG_MODE,  // 提示标题
        hint.c_str(),                    // 详细内容
        "",                              // 图标名称（留空不显示）
        Lang::Sounds::P3_WIFICONFIG      // 提示音文件
    );
    
    // 进入等待配置完成的循环（设备将在此循环中保持运行）
    while (true) {
        // 获取内部内存的当前空闲大小
        int free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);  
        // 获取内部内存的最小历史空闲大小
        int min_free_sram = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);  
        // 打印内存使用情况（用于调试和内存监控）
        ESP_LOGI(TAG, "Free internal: %u minimal internal: %u", free_sram, min_free_sram);  
        // 延迟10秒后再次检查内存（避免高频打印影响性能）
        vTaskDelay(pdMS_TO_TICKS(10000));  
    }
}

// 启动网络的函数
void WifiBoard::StartNetwork() {
    // 如果强制进入WiFi配置模式，则直接进入配置模式
    if (wifi_config_mode_) {
        // 调用进入WiFi配置模式的函数
        EnterWifiConfigMode();
        // 函数执行完毕，直接返回
        return;
    }

    // 如果没有配置WiFi SSID，则进入WiFi配置模式
    // 获取SsidManager的单例对象，用于管理WiFi的SSID列表
    auto& ssid_manager = SsidManager::GetInstance();
    // 从SsidManager中获取存储的WiFi SSID列表
    auto ssid_list = ssid_manager.GetSsidList();
    // 检查SSID列表是否为空
    if (ssid_list.empty()) {
        // 如果为空，将强制进入WiFi配置模式的标志设置为true
        wifi_config_mode_ = true;
        // 调用进入WiFi配置模式的函数
        EnterWifiConfigMode();
        // 函数执行完毕，直接返回
        return;
    }

    // 初始化WiFi Station模式
    // 获取WifiStation的单例对象，用于控制WiFi的Station模式
    auto& wifi_station = WifiStation::GetInstance();
    // 注册WiFi扫描开始的回调函数
    wifi_station.OnScanBegin([this]() {
        // 获取显示设备的实例
        auto display = Board::GetInstance().GetDisplay();
        // 显示扫描WiFi的通知，持续30000毫秒
        display->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000);  
    });
    // 注册WiFi连接开始的回调函数
    wifi_station.OnConnect([this](const std::string& ssid) {
        // 获取显示设备的实例
        auto display = Board::GetInstance().GetDisplay();
        // 构建连接WiFi的通知消息
        std::string notification = Lang::Strings::CONNECT_TO;
        // 将目标WiFi的SSID添加到通知消息中
        notification += ssid;
        // 完善通知消息的格式
        notification += "...";
        // 显示连接WiFi的通知，持续30000毫秒
        display->ShowNotification(notification.c_str(), 30000);  
    });
    // 注册WiFi连接成功的回调函数
    wifi_station.OnConnected([this](const std::string& ssid) {
        // 获取显示设备的实例
        auto display = Board::GetInstance().GetDisplay();
        // 构建已连接WiFi的通知消息
        std::string notification = Lang::Strings::CONNECTED_TO;
        // 将已连接的WiFi的SSID添加到通知消息中
        notification += ssid;
        // 显示已连接WiFi的通知，持续30000毫秒
        display->ShowNotification(notification.c_str(), 30000);  
    });
    // 启动WiFi Station模式，开始进行WiFi扫描和连接操作
    wifi_station.Start();  

    // 尝试连接WiFi，如果失败则启动WiFi配置热点
    // 等待WiFi连接，最多等待60 * 1000毫秒（即60秒）
    if (!wifi_station.WaitForConnected(60 * 1000)) {
        // 如果超时仍未连接成功，停止WiFi Station模式
        wifi_station.Stop();
        // 将强制进入WiFi配置模式的标志设置为true
        wifi_config_mode_ = true;
        // 调用进入WiFi配置模式的函数
        EnterWifiConfigMode();
        // 函数执行完毕，直接返回
        return;
    }
}

// 创建HTTP对象的函数
Http* WifiBoard::CreateHttp() {
    return new EspHttp();  // 返回基于ESP的HTTP对象
}

// 创建WebSocket对象的函数
// 创建WebSocket实例的方法
WebSocket* WifiBoard::CreateWebSocket() {
#ifdef CONFIG_CONNECTION_TYPE_WEBSOCKET  // 条件编译：仅当配置了WebSocket连接类型时编译此代码
    // 从配置系统获取WebSocket服务器URL
    std::string url = CONFIG_WEBSOCKET_URL;
    
    // 检查URL是否使用安全WebSocket协议（wss://）
    if (url.find("wss://") == 0) {
        // 创建使用TLS加密传输层的WebSocket实例
        return new WebSocket(new TlsTransport());  // TLS传输层提供加密通信
    } else {
        // 创建使用普通TCP传输层的WebSocket实例
        return new WebSocket(new TcpTransport());  // TCP传输层提供明文通信
    }
#endif  // 结束条件编译块
    
    // 如果未启用WebSocket连接类型，返回空指针
    return nullptr;
}

// 创建MQTT对象的函数
Mqtt* WifiBoard::CreateMqtt() {
    return new EspMqtt();  // 返回基于ESP的MQTT对象
}

// 创建UDP对象的函数
Udp* WifiBoard::CreateUdp() {
    return new EspUdp();  // 返回基于ESP的UDP对象
}

// 获取网络状态图标的函数
const char* WifiBoard::GetNetworkStateIcon() {
    if (wifi_config_mode_) {
        return FONT_AWESOME_WIFI;  // WiFi配置模式，返回WiFi图标
    }
    auto& wifi_station = WifiStation::GetInstance();
    if (!wifi_station.IsConnected()) {
        return FONT_AWESOME_WIFI_OFF;  // 未连接WiFi，返回WiFi关闭图标
    }
    int8_t rssi = wifi_station.GetRssi();  // 获取WiFi信号强度
    if (rssi >= -60) {
        return FONT_AWESOME_WIFI;  // 信号强，返回WiFi图标
    } else if (rssi >= -70) {
        return FONT_AWESOME_WIFI_FAIR;  // 信号中等，返回中等信号图标
    } else {
        return FONT_AWESOME_WIFI_WEAK;  // 信号弱，返回弱信号图标
    }
}

// 获取板子信息的JSON格式字符串
std::string WifiBoard::GetBoardJson() {
    // 设置OTA的板子类型
    auto& wifi_station = WifiStation::GetInstance();
    std::string board_json = std::string("{\"type\":\"" BOARD_TYPE "\",");
    board_json += "\"name\":\"" BOARD_NAME "\",";
    if (!wifi_config_mode_) {
        board_json += "\"ssid\":\"" + wifi_station.GetSsid() + "\",";  // WiFi SSID
        board_json += "\"rssi\":" + std::to_string(wifi_station.GetRssi()) + ",";  // WiFi信号强度
        board_json += "\"channel\":" + std::to_string(wifi_station.GetChannel()) + ",";  // WiFi信道
        board_json += "\"ip\":\"" + wifi_station.GetIpAddress() + "\",";  // IP地址
    }
    board_json += "\"mac\":\"" + SystemInfo::GetMacAddress() + "\"}";  // MAC地址
    return board_json;
}

// 设置省电模式的函数
void WifiBoard::SetPowerSaveMode(bool enabled) {
    auto& wifi_station = WifiStation::GetInstance();
    wifi_station.SetPowerSaveMode(enabled);  // 设置WiFi省电模式
}

// 重置WiFi配置的函数
void WifiBoard::ResetWifiConfiguration() {
    // 设置标志位并重启设备以进入WiFi配置模式
    {
        Settings settings("wifi", true);
        settings.SetInt("force_ap", 1);  // 设置强制进入WiFi配置模式的标志
    }
    GetDisplay()->ShowNotification(Lang::Strings::ENTERING_WIFI_CONFIG_MODE);  // 显示进入WiFi配置模式的通知
    vTaskDelay(pdMS_TO_TICKS(1000));  // 延迟1秒
    esp_restart();  // 重启设备
}