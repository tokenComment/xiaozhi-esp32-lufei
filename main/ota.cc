#include "ota.h"
#include "system_info.h"
#include "board.h"
#include "settings.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <esp_app_format.h>

#include <cstring>
#include <vector>
#include <sstream>
#include <algorithm>

#define TAG "Ota"

// Ota类的构造函数
Ota::Ota() {
}

// Ota类的析构函数
Ota::~Ota() {
}

// 设置检查版本的URL
void Ota::SetCheckVersionUrl(std::string check_version_url) {
    check_version_url_ = check_version_url;
}

// 设置HTTP请求头
void Ota::SetHeader(const std::string& key, const std::string& value) {
    headers_[key] = value;
}

// 设置POST请求的数据
void Ota::SetPostData(const std::string& post_data) {
    post_data_ = post_data;
}
// 检查是否有新版本
bool Ota::CheckVersion() {
    // 获取当前运行固件的版本信息
    current_version_ = esp_app_get_description()->version;
    // 打印当前固件版本号到日志
    ESP_LOGI(TAG, "Current version: %s", current_version_.c_str());

    // 验证版本检查URL的有效性（最小长度限制）
    if (check_version_url_.length() < 10) {
        // 记录错误日志并返回失败
        ESP_LOGE(TAG, "Check version URL is not properly set");
        return false;
    }

    // 创建HTTP客户端实例
    auto http = Board::GetInstance().CreateHttp();
    // 遍历自定义请求头并设置到HTTP客户端
    for (const auto& header : headers_) {
        http->SetHeader(header.first, header.second);
    }

    // 设置请求内容类型为application/json格式
    http->SetHeader("Content-Type", "application/json");
    // 根据POST数据是否存在选择请求方法（POST或GET）
    std::string method = post_data_.length() > 0 ? "POST" : "GET";
    // 打开HTTP连接并发送请求
    if (!http->Open(method, check_version_url_, post_data_)) {
        // 记录连接失败日志并清理资源
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        delete http;
        return false;
    }

    // 获取HTTP响应体内容
    auto response = http->GetBody();
    // 关闭HTTP连接并释放客户端资源
    http->Close();
    delete http;

    // 使用cJSON库解析JSON响应数据
    cJSON *root = cJSON_Parse(response.c_str());
    if (root == NULL) {
        // 解析失败时记录错误日志并返回
        ESP_LOGE(TAG, "Failed to parse JSON response");
        return false;
    }

    // 初始化激活码状态
    has_activation_code_ = false;
    // 从JSON根对象中获取activation字段
    cJSON *activation = cJSON_GetObjectItem(root, "activation");
    if (activation != NULL) {
        // 获取activation中的message字段
        cJSON* message = cJSON_GetObjectItem(activation, "message");
        if (message != NULL) {
            // 保存激活消息内容
            activation_message_ = message->valuestring;
        }
        // 获取activation中的code字段
        cJSON* code = cJSON_GetObjectItem(activation, "code");
        if (code != NULL) {
            // 保存激活码内容
            activation_code_ = code->valuestring;
        }
        // 标记存在激活码信息
        has_activation_code_ = true;
    }

    // 初始化MQTT配置状态
    has_mqtt_config_ = false;
    // 从JSON根对象中获取mqtt字段
    cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt");
    if (mqtt != NULL) {
        // 创建MQTT配置存储对象（使用settings类）
        Settings settings("mqtt", true);
        // 遍历JSON数组中的每个配置项
        cJSON_ArrayForEach(item, mqtt) {
            // 检查是否为字符串类型
            if (item->type == cJSON_String) {
                // 比较并更新配置项值
                if (settings.GetString(item->string) != item->valuestring) {
                    settings.SetString(item->string, item->valuestring);
                }
            }
        }
        // 标记存在MQTT配置信息
        has_mqtt_config_ = true;
    }

    // 初始化服务器时间同步状态
    has_server_time_ = false;
    // 从JSON根对象中获取server_time字段
    cJSON *server_time = cJSON_GetObjectItem(root, "server_time");
    if (server_time != NULL) {
        // 获取时间戳字段
        cJSON *timestamp = cJSON_GetObjectItem(server_time, "timestamp");
        // 获取时区偏移字段
        cJSON *timezone_offset = cJSON_GetObjectItem(server_time, "timezone_offset");
        
        if (timestamp != NULL) {
            // 创建时间结构体
            struct timeval tv;
            // 获取时间戳数值（毫秒级）
            double ts = timestamp->valuedouble;
            
            // 应用时区偏移（转换为毫秒）
            if (timezone_offset != NULL) {
                ts += (timezone_offset->valueint * 60 * 1000); 
            }
            
            // 转换为Unix时间戳（秒和微秒）
            tv.tv_sec = (time_t)(ts / 1000);
            tv.tv_usec = (suseconds_t)((long long)ts % 1000) * 1000;
            // 设置系统时间
            settimeofday(&tv, NULL);
            // 标记时间同步成功
            has_server_time_ = true;
        }
    }

    // 从JSON根对象中获取firmware字段
    cJSON *firmware = cJSON_GetObjectItem(root, "firmware");
    if (firmware == NULL) {
        // 记录固件信息缺失错误并清理资源
        ESP_LOGE(TAG, "Failed to get firmware object");
        cJSON_Delete(root);
        return false;
    }
    // 获取firmware中的version字段
    cJSON *version = cJSON_GetObjectItem(firmware, "version");
    if (version == NULL) {
        // 记录版本信息缺失错误并清理资源
        ESP_LOGE(TAG, "Failed to get version object");
        cJSON_Delete(root);
        return false;
    }
    // 获取firmware中的url字段
    cJSON *url = cJSON_GetObjectItem(firmware, "url");
    if (url == NULL) {
        // 记录URL缺失错误并清理资源
        ESP_LOGE(TAG, "Failed to get url object");
        cJSON_Delete(root);
        return false;
    }

    // 保存新版本号和下载URL
    firmware_version_ = version->valuestring;
    firmware_url_ = url->valuestring;
    // 释放JSON解析占用的内存
    cJSON_Delete(root);

    // 比较当前版本与新版本号
    has_new_version_ = IsNewVersionAvailable(current_version_, firmware_version_);
    if (has_new_version_) {
        // 打印新版本可用日志
        ESP_LOGI(TAG, "New version available: %s", firmware_version_.c_str());
    } else {
        // 打印已是最新版本日志
        ESP_LOGI(TAG, "Current is the latest version");
    }
    return true;
}

// 标记当前版本为有效
void Ota::MarkCurrentVersionValid() {
    // 获取当前运行的分区信息
    auto partition = esp_ota_get_running_partition();
    // 跳过factory分区处理
    if (strcmp(partition->label, "factory") == 0) {
        ESP_LOGI(TAG, "Running from factory partition, skipping");
        return;
    }

    // 打印当前运行分区信息
    ESP_LOGI(TAG, "Running partition: %s", partition->label);
    // 获取分区状态
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(partition, &state) != ESP_OK) {
        // 记录状态获取失败错误
        ESP_LOGE(TAG, "Failed to get state of partition");
        return;
    }

    // 处理待验证状态的分区
    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        // 打印标记有效日志
        ESP_LOGI(TAG, "Marking firmware as valid");
        // 标记当前应用为有效并取消回滚
        esp_ota_mark_app_valid_cancel_rollback();
    }
}

// 升级固件
void Ota::Upgrade(const std::string& firmware_url) {
    // 打印升级开始日志
    ESP_LOGI(TAG, "Upgrading firmware from %s", firmware_url.c_str());
    esp_ota_handle_t update_handle = 0;
    // 获取下一个可用的OTA升级分区
    auto update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        // 记录分区获取失败错误
        ESP_LOGE(TAG, "Failed to get update partition");
        return;
    }

    // 打印写入分区信息
    ESP_LOGI(TAG, "Writing to partition %s at offset 0x%lx", update_partition->label, update_partition->address);
    bool image_header_checked = false;
    std::string image_header;

    // 创建HTTP客户端实例
    auto http = Board::GetInstance().CreateHttp();
    // 打开HTTP连接并发送GET请求
    if (!http->Open("GET", firmware_url)) {
        // 记录连接失败错误并清理资源
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        delete http;
        return;
    }

    // 获取固件包总大小
    size_t content_length = http->GetBodyLength();
    if (content_length == 0) {
        // 记录内容长度获取失败错误
        ESP_LOGE(TAG, "Failed to get content length");
        delete http;
        return;
    }

    char buffer[512];
    size_t total_read = 0, recent_read = 0;
    auto last_calc_time = esp_timer_get_time();
    while (true) {
        // 从HTTP流中读取数据
        int ret = http->Read(buffer, sizeof(buffer));
        if (ret < 0) {
            // 记录读取失败错误并清理资源
            ESP_LOGE(TAG, "Failed to read HTTP data: %s", esp_err_to_name(ret));
            delete http;
            return;
        }

        // 累计读取量并计算进度和速度
        recent_read += ret;
        total_read += ret;
        // 每秒更新一次进度显示
        if (esp_timer_get_time() - last_calc_time >= 1000000 || ret == 0) {
            size_t progress = total_read * 100 / content_length;
            // 打印进度和速度信息
            ESP_LOGI(TAG, "Progress: %zu%% (%zu/%zu), Speed: %zuB/s", progress, total_read, content_length, recent_read);
            // 触发用户注册的升级回调
            if (upgrade_callback_) {
                upgrade_callback_(progress, recent_read);
            }
            last_calc_time = esp_timer_get_time();
            recent_read = 0;
        }

        if (ret == 0) {
            break; // 数据读取完成
        }

        // 检查固件头信息
        if (!image_header_checked) {
            image_header.append(buffer, ret);
            // 检查是否已读取足够的头信息
            if (image_header.size() >= sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
                // 解析新固件的应用描述信息
                esp_app_desc_t new_app_info;
                memcpy(&new_app_info, image_header.data() + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), sizeof(esp_app_desc_t));
                // 打印新版本号
                ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);

                // 获取当前运行的版本信息
                auto current_version = esp_app_get_description()->version;
                // 比较版本号是否相同
                if (memcmp(new_app_info.version, current_version, sizeof(new_app_info.version)) == 0) {
                    // 版本相同则跳过升级
                    ESP_LOGE(TAG, "Firmware version is the same, skipping upgrade");
                    delete http;
                    return;
                }

                // 初始化OTA升级过程
                if (esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle)) {
                    // 升级初始化失败时清理资源
                    esp_ota_abort(update_handle);
                    delete http;
                    ESP_LOGE(TAG, "Failed to begin OTA");
                    return;
                }

                image_header_checked = true;
                // 清空临时存储的头信息
                std::string().swap(image_header);
            }
        }
        // 将数据写入OTA分区
        auto err = esp_ota_write(update_handle, buffer, ret);
        if (err != ESP_OK) {
            // 记录写入失败错误并清理资源
            ESP_LOGE(TAG, "Failed to write OTA data: %s", esp_err_to_name(err));
            esp_ota_abort(update_handle);
            delete http;
            return;
        }
    }
    delete http;

    // 完成OTA写入并验证
    esp_err_t err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        // 处理验证失败和其他错误
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        } else {
            ESP_LOGE(TAG, "Failed to end OTA: %s", esp_err_to_name(err));
        }
        return;
    }

    // 设置下一次启动分区
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        // 记录分区设置失败错误
        ESP_LOGE(TAG, "Failed to set boot partition: %s", esp_err_to_name(err));
        return;
    }

    // 打印升级成功日志并准备重启
    ESP_LOGI(TAG, "Firmware upgrade successful, rebooting in 3 seconds...");
    // 等待3秒
    vTaskDelay(pdMS_TO_TICKS(3000));
    // 执行系统重启
    esp_restart();
}

// 开始升级固件（带回调函数）
void Ota::StartUpgrade(std::function<void(int progress, size_t speed)> callback) {
    // 保存用户提供的升级回调函数
    upgrade_callback_ = callback;
    // 执行实际的升级流程
    Upgrade(firmware_url_);
}

// 解析版本字符串为整数数组
std::vector<int> Ota::ParseVersion(const std::string& version) {
    std::vector<int> versionNumbers;
    std::stringstream ss(version);
    std::string segment;
    
    // 按点号分割版本字符串
    while (std::getline(ss, segment, '.')) {
        // 转换每个部分为整数并存储
        versionNumbers.push_back(std::stoi(segment));
    }
    
    return versionNumbers;
}

// 检查是否有新版本可用
bool Ota::IsNewVersionAvailable(const std::string& currentVersion, const std::string& newVersion) {
    // 解析当前版本和新版本为整数数组
    std::vector<int> current = ParseVersion(currentVersion);
    std::vector<int> newer = ParseVersion(newVersion);
    
    // 逐位比较版本号
    for (size_t i = 0; i < std::min(current.size(), newer.size()); ++i) {
        if (newer[i] > current[i]) {
            return true; // 新版本存在
        } else if (newer[i] < current[i]) {
            return false; // 旧版本
        }
    }
    
    // 处理版本号位数不同的情况（如1.0 vs 1.0.1）
    return newer.size() > current.size();
}