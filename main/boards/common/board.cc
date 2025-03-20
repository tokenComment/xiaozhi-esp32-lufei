#include "board.h"
#include "system_info.h"
#include "settings.h"
#include "display/no_display.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_chip_info.h>
#include <esp_random.h>

#define TAG "Board" // 定义日志标签
// Board类的构造函数
Board::Board()
{
    // 初始化Settings对象，使用"board"命名空间，并启用自动保存
    Settings settings("board", true);

    // 从设置中获取UUID
    uuid_ = settings.GetString("uuid");

    // 如果UUID为空，则生成一个新的UUID并保存到设置中
    if (uuid_.empty())
    {
        uuid_ = GenerateUuid();
        settings.SetString("uuid", uuid_);
    }

    // 打印UUID和SKU信息到日志
    ESP_LOGI(TAG, "UUID=%s SKU=%s", uuid_.c_str(), BOARD_NAME);
}

// 生成UUID的函数
std::string Board::GenerateUuid()
{
    // UUID v4 需要 16 字节的随机数据
    uint8_t uuid[16];

    // 使用ESP32的硬件随机数生成器填充UUID数组
    esp_fill_random(uuid, sizeof(uuid));

    // 设置UUID版本 (版本 4) 和变体位
    uuid[6] = (uuid[6] & 0x0F) | 0x40; // 版本 4
    uuid[8] = (uuid[8] & 0x3F) | 0x80; // 变体 1

    // 将字节转换为标准的UUID字符串格式
    char uuid_str[37];
    // 将二进制UUID转换为标准字符串格式
    snprintf(uuid_str, sizeof(uuid_str),                                             // 目标缓冲区及其大小
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", // UUID格式字符串
             uuid[0], uuid[1], uuid[2], uuid[3],                                     // 前4个字节（共8位十六进制）
             uuid[4], uuid[5], uuid[6], uuid[7],                                     // 接下来的2个字节（共4位）
             uuid[8], uuid[9],                                                       // 接下来的2个字节（共4位）
             uuid[10], uuid[11],                                                     // 接下来的2个字节（共4位）
             uuid[12], uuid[13], uuid[14], uuid[15]);                                // 最后4个字节（共12位）
    // 返回生成的UUID字符串
    return std::string(uuid_str);
}

// 获取电池电量的函数（未实现）
bool Board::GetBatteryLevel(int &level, bool &charging)
{
    return false;
}

// 获取显示器的函数
Display *Board::GetDisplay()
{
    static NoDisplay display; // 使用NoDisplay作为默认显示器
    return &display;
}

// 获取LED的函数
Led *Board::GetLed()
{
    static NoLed led; // 使用NoLed作为默认LED
    return &led;
}

// 获取系统信息的JSON格式字符串
// 生成设备信息的JSON字符串
std::string Board::GetJson()
{
    // 初始化JSON字符串，以对象开始
    std::string json = "{";

    // 添加版本号（用于JSON结构的版本控制）
    json += "\"version\":2,";

    // 添加系统语言代码（例如"en"或"zh"）
    json += "\"language\":\"" + std::string(Lang::CODE) + "\",";

    // 添加Flash存储容量（单位：字节）
    json += "\"flash_size\":" + std::to_string(SystemInfo::GetFlashSize()) + ",";

    // 添加运行时最小空闲堆内存（用于系统稳定性评估）
    json += "\"minimum_free_heap_size\":" + std::to_string(SystemInfo::GetMinimumFreeHeapSize()) + ",";

    // 添加设备MAC地址（唯一硬件标识）
    json += "\"mac_address\":\"" + SystemInfo::GetMacAddress() + "\",";

    // 添加设备唯一标识符（UUID）
    json += "\"uuid\":\"" + uuid_ + "\",";

    // 添加芯片型号名称（如"ESP32"）
    json += "\"chip_model_name\":\"" + SystemInfo::GetChipModelName() + "\",";

    // 开始芯片信息的嵌套对象
    json += "\"chip_info\":{";

    // 获取芯片信息结构体
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    // 添加芯片型号（如ESP_CHIP_ESP32）
    json += "\"model\":" + std::to_string(chip_info.model) + ",";

    // 添加CPU核心数量（如1或2）
    json += "\"cores\":" + std::to_string(chip_info.cores) + ",";

    // 添加芯片版本号（如0或1）
    json += "\"revision\":" + std::to_string(chip_info.revision) + ",";

    // 添加芯片功能特性（位掩码，如支持蓝牙、WiFi等）
    json += "\"features\":" + std::to_string(chip_info.features);
    json += "},";

    // 开始应用程序信息的嵌套对象
    json += "\"application\":{";

    // 获取应用程序描述结构体
    auto app_desc = esp_app_get_description();

    // 添加项目名称（在sdkconfig中配置的项目名）
    json += "\"name\":\"" + std::string(app_desc->project_name) + "\",";

    // 添加应用程序版本号（在sdkconfig中配置）
    json += "\"version\":\"" + std::string(app_desc->version) + "\",";

    // 添加编译时间（格式化为ISO 8601标准）
    json += "\"compile_time\":\"" + std::string(app_desc->date) + "T" + std::string(app_desc->time) + "Z\",";

    // 添加ESP-IDF版本号
    json += "\"idf_version\":\"" + std::string(app_desc->idf_ver) + "\",";

    // 计算并添加ELF文件的SHA256哈希值
    char sha256_str[65];
    for (int i = 0; i < 32; i++)
    {
        // 将32字节的哈希值转换为64字符的十六进制字符串
        snprintf(sha256_str + i * 2, sizeof(sha256_str) - i * 2, "%02x", app_desc->app_elf_sha256[i]);
    }
    json += "\"elf_sha256\":\"" + std::string(sha256_str) + "\"";
    json += "},";

    // 开始分区表信息的数组
    json += "\"partition_table\": [";

    // 创建分区迭代器（查找所有分区）
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it)
    {
        // 获取当前分区信息
        const esp_partition_t *partition = esp_partition_get(it);

        // 开始分区对象
        json += "{";
        json += "\"label\":\"" + std::string(partition->label) + "\",";    // 分区标签（如"factory"）
        json += "\"type\":" + std::to_string(partition->type) + ",";       // 分区类型（如数据区、应用区）
        json += "\"subtype\":" + std::to_string(partition->subtype) + ","; // 子类型（如OTA分区）
        json += "\"address\":" + std::to_string(partition->address) + ","; // 起始地址（十六进制格式）
        json += "\"size\":" + std::to_string(partition->size);             // 分区大小（字节）
        json += "},";

        // 移动到下一个分区
        it = esp_partition_next(it);
    }

    // 移除数组末尾多余的逗号
    json.pop_back();
    json += "],";

    // 开始OTA分区信息的对象
    json += "\"ota\":{";
    // 获取当前运行的OTA分区
    auto ota_partition = esp_ota_get_running_partition();
    // 添加分区标签（如"ota_0"）
    json += "\"label\":\"" + std::string(ota_partition->label) + "\"";
    json += "},";

    // 添加板级信息（通过子类实现的具体硬件信息）
    json += "\"board\":" + GetBoardJson();

    // 闭合最外层JSON对象
    json += "}";

    return json;
}