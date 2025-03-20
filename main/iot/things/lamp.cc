// 引入物联网设备相关的头文件，该头文件可能定义了物联网设备的基础类和相关功能
#include "iot/thing.h"
// 引入开发板相关的头文件，可能包含了开发板的引脚定义、外设配置等信息
#include "board.h"
// 引入音频编解码器相关的头文件，可能在后续会用于音频相关的操作，但此代码中未体现
#include "audio_codec.h"

// 引入GPIO驱动的头文件，用于操作开发板的GPIO引脚
#include <driver/gpio.h>
// 引入ESP_LOG日志库的头文件，用于在开发过程中输出调试信息
#include <esp_log.h>

// 定义日志标签，方便在日志输出中区分不同模块的信息
#define TAG "Lamp"

// 定义命名空间iot，将相关的类和函数封装在该命名空间内，避免命名冲突
namespace iot {

// 定义一个名为Lamp的类，继承自Thing类，用于表示一个灯设备
// 这里仅定义 Lamp 的属性和方法，不包含具体的实现
class Lamp : public Thing {
private:
    // 根据不同的ESP目标芯片型号，选择不同的GPIO引脚
    // 如果是ESP32芯片，使用GPIO_NUM_35引脚
    #ifdef CONFIG_IDF_TARGET_ESP32
        gpio_num_t gpio_num_ = GPIO_NUM_35;
    // 对于其他芯片，使用GPIO_NUM_18引脚
    #else
    gpio_num_t gpio_num_ = GPIO_NUM_18;
    #endif
    // 定义一个布尔类型的变量power_，用于表示灯的开关状态，初始值为关闭状态
    bool power_ = false;

    // 定义一个私有成员函数，用于初始化GPIO引脚
    void InitializeGpio() {
        // 定义一个gpio_config_t类型的结构体变量config，用于配置GPIO引脚的参数
        gpio_config_t config = {
            // 设置要配置的GPIO引脚的位掩码，将对应的引脚位置为1
            .pin_bit_mask = (1ULL << gpio_num_),
            // 设置GPIO引脚的工作模式为输出模式，用于控制灯的开关
            .mode = GPIO_MODE_OUTPUT,
            // 禁用上拉电阻
            .pull_up_en = GPIO_PULLUP_DISABLE,
            // 禁用下拉电阻
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            // 禁用GPIO引脚的中断功能
            .intr_type = GPIO_INTR_DISABLE,
        };
        // 调用gpio_config函数，将配置参数应用到指定的GPIO引脚
        // ESP_ERROR_CHECK用于检查函数执行结果，如果出现错误则会输出错误信息
        ESP_ERROR_CHECK(gpio_config(&config));
        // 将指定的GPIO引脚电平设置为低电平，即初始状态下灯是关闭的
        gpio_set_level(gpio_num_, 0);
    }

public:
    // Lamp类的构造函数，用于初始化灯设备
    // 调用父类Thing的构造函数，传入设备名称和描述信息
    // 同时将power_初始化为false，表示灯初始状态为关闭
    Lamp() : Thing("Lamp", "一个测试用的灯"), power_(false) {
        // 调用私有成员函数InitializeGpio，对GPIO引脚进行初始化配置
        InitializeGpio();

        // 为设备添加一个布尔类型的属性"power"，用于表示灯的开关状态
        // 第一个参数是属性名称，第二个参数是属性描述
        // 第三个参数是一个lambda表达式，用于获取当前灯的开关状态
        properties_.AddBooleanProperty("power", "灯是否打开", [this]() -> bool {
            return power_;
        });

        // 为设备添加一个远程可执行的指令"TurnOn"，用于打开灯
        // 第一个参数是指令名称，第二个参数是指令描述
        // 第三个参数是指令所需的参数列表，这里为空
        // 第四个参数是一个lambda表达式，当接收到该指令时，将灯的开关状态设置为打开，并将对应的GPIO引脚电平设置为高电平
        methods_.AddMethod("TurnOn", "打开灯", ParameterList(), [this](const ParameterList& parameters) {
            power_ = true;
            gpio_set_level(gpio_num_, 1);
        });

        // 为设备添加一个远程可执行的指令"TurnOff"，用于关闭灯
        // 第一个参数是指令名称，第二个参数是指令描述
        // 第三个参数是指令所需的参数列表，这里为空
        // 第四个参数是一个lambda表达式，当接收到该指令时，将灯的开关状态设置为关闭，并将对应的GPIO引脚电平设置为低电平
        methods_.AddMethod("TurnOff", "关闭灯", ParameterList(), [this](const ParameterList& parameters) {
            power_ = false;
            gpio_set_level(gpio_num_, 0);
        });
    }
};

} // 结束iot命名空间的定义

// 声明一个名为Lamp的物联网设备，具体的DECLARE_THING宏的实现可能在其他头文件中
DECLARE_THING(Lamp);