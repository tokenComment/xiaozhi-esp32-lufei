#include "iot/thing.h"
#include "board.h"
#include "audio_codec.h"

#include <esp_log.h>

#define TAG "Speaker"

namespace iot
{

    // 这里仅定义 Speaker 的属性和方法，不包含具体的实现
    class Speaker : public Thing
    {
    public:
        // Speaker 类的构造函数，用于初始化 Speaker 对象
        Speaker() : Thing("Speaker", "扬声器")
        {
            // 定义设备的属性
            // 使用 properties_ 对象的 AddNumberProperty 方法添加一个名为 "volume" 的数字属性
            // 该属性表示当前音量值，第二个参数是对该属性的描述
            // 第三个参数是一个 lambda 函数，用于获取当前音量值
            properties_.AddNumberProperty("volume", "当前音量值", [this]() -> int
                                          {
        // 通过 Board 类的单例对象获取音频编解码器实例
        auto codec = Board::GetInstance().GetAudioCodec();
        // 调用音频编解码器的 output_volume 方法获取当前输出音量，并将其作为属性值返回
        return codec->output_volume(); });

            // 定义设备可以被远程执行的指令
            // 使用 methods_ 对象的 AddMethod 方法添加一个名为 "SetVolume" 的方法
            // 该方法用于设置音量，第二个参数是对该方法的描述
            // 第三个参数是一个 ParameterList 对象，用于定义该方法的参数列表
            methods_.AddMethod("SetVolume", "设置音量", ParameterList({// 定义一个名为 "volume" 的参数，第二个参数是对该参数的描述
                                                                       // 第三个参数指定参数类型为数字类型，第四个参数表示该参数是必需的
                                                                       Parameter("volume", "0到100之间的整数", kValueTypeNumber, true)}),
                               [this](const ParameterList &parameters)
                               {
                                   // 通过 Board 类的单例对象获取音频编解码器实例
                                   auto codec = Board::GetInstance().GetAudioCodec();
                                   // 从传入的参数列表中获取名为 "volume" 的参数值，并将其转换为 uint8_t 类型
                                   // 然后调用音频编解码器的 SetOutputVolume 方法设置输出音量
                                   codec->SetOutputVolume(static_cast<uint8_t>(parameters["volume"].number()));
                               });
        }
    };

} // namespace iot

DECLARE_THING(Speaker);
