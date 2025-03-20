# 配置ESP32-S3的Flash大小为16MB。增大Flash容量可存储更多代码/数据。
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
# 设置Flash工作模式为QIO（Quad I/O）。支持四线同时读写，提升访问速度。
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y

# 设置ESP32-S3默认CPU频率为240MHz。最高频率可提升性能，但会增加功耗。
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y

# 启用外部SPIRAM扩展内存。通过PSRAM增加可用内存空间。
CONFIG_SPIRAM=y
# 设置SPIRAM工作模式为OCT（八线模式）。支持八线同时传输，带宽翻倍。
CONFIG_SPIRAM_MODE_OCT=y
# 设置SPIRAM运行速率为80MHz。高速模式需硬件支持。
CONFIG_SPIRAM_SPEED_80M=y
# 保留4KB内部内存用于紧急分配。防止SPIRAM分配失败时系统崩溃。
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096
# 优先为Wi-Fi/LWIP分配SPIRAM内存。确保网络功能稳定运行。
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y
# 预保留48KB内部内存。用于系统关键内存分配。
CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=49152
# 禁用SPIRAM内存测试。测试会延长启动时间，生产环境建议关闭。
CONFIG_SPIRAM_MEMTEST=n
# 启用mbedTLS外部分配内存。使用SPIRAM进行加密相关内存分配。
CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y

# 启用32KB指令缓存。提升代码执行效率。
CONFIG_ESP32S3_INSTRUCTION_CACHE_32KB=y
# 启用64KB数据缓存。增加数据吞吐量，降低主存访问延迟。
CONFIG_ESP32S3_DATA_CACHE_64KB=y
# 设置数据缓存行大小为64字节。优化缓存命中率，减少内存访问次数。
CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y

# 启用WakeNet语音唤醒功能。支持语音唤醒词检测。
CONFIG_USE_WAKENET=y
# 选择"你好小智"语音合成模型。需搭配对应TTS库使用。
CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS=y
# 禁用多网络支持。单网络模式减少资源占用。
CONFIG_USE_MULTINET=n