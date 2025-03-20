import sys
import os
import json

# 切换到项目根目录
os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

def get_board_type():
    """
    从编译命令中提取板子类型 (BOARD_TYPE)。
    返回值: 板子类型字符串，如果未找到则返回 None。
    """
    try:
        # 打开名为 "build/compile_commands.json" 的文件，该文件通常由编译系统生成，包含了项目编译所需的命令信息
        # 使用 with 语句可以确保文件在使用完后自动关闭，避免资源泄漏
        with open("build/compile_commands.json") as f:
            # 使用 json.load 函数将文件内容解析为 Python 数据结构（通常是列表或字典）
            data = json.load(f)
            # 遍历解析后的数据中的每个条目
            for item in data:
                # 检查当前条目中的 "file" 键对应的值是否以 "main.cc" 结尾
                # 目的是筛选出与 main.cc 文件相关的编译命令，因为我们只关心该文件编译时的 BOARD_TYPE 参数
                if not item["file"].endswith("main.cc"):
                    # 如果不满足条件，跳过当前条目，继续处理下一个条目
                    continue
                # 若当前条目是 main.cc 文件的编译命令，提取该条目中 "command" 键对应的值
                # 这个值是完整的编译命令字符串
                command = item["command"]
                # 下面的代码用于从编译命令中提取 -DBOARD_TYPE 参数的值
                # 先使用 split("-DBOARD_TYPE=\\\"") 方法将编译命令按 -DBOARD_TYPE=\\\" 进行分割
                # 分割后会得到一个列表，我们取索引为 1 的元素，即 -DBOARD_TYPE= 后面的部分
                # 接着再使用 split("\\\"") 方法将得到的字符串按 \\\" 进行分割，取索引为 0 的元素
                # 最后使用 strip() 方法去除字符串首尾的空白字符，得到纯净的板子类型名称
                board_type = command.split("-DBOARD_TYPE=\\\"")[1].split("\\\"")[0].strip()
                # 找到所需的板子类型后，直接返回该值
                return board_type
    except (FileNotFoundError, IndexError, KeyError, json.JSONDecodeError):
        # 捕获可能出现的异常
        # FileNotFoundError：如果指定的 "build/compile_commands.json" 文件不存在，会抛出该异常
        # IndexError：如果在使用 split 方法分割字符串时，索引超出了列表范围，会抛出该异常
        # KeyError：如果解析后的 JSON 数据中不包含 "file" 或 "command" 键，会抛出该异常
        # json.JSONDecodeError：如果文件内容不是有效的 JSON 格式，会抛出该异常
        pass
    # 如果遍历完所有条目都没有找到符合条件的编译命令，或者在处理过程中出现异常
    # 则返回 None，表示未找到板子类型
    return None
def get_project_version():
    """
    从 CMakeLists.txt 中提取项目版本号。
    返回值: 项目版本号字符串，如果未找到则返回 None。
    """
    with open("CMakeLists.txt") as f:
        for line in f:
            if line.startswith("set(PROJECT_VER"):
                return line.split("\"")[1].split("\"")[0].strip()
    return None

# 此函数用于合并生成的二进制文件
def merge_bin():
    """
    合并生成的二进制文件。
    如果合并失败，打印错误信息并退出程序。
    """
    # 调用 idf.py 的 merge-bin 命令来合并二进制文件
    # os.system 函数执行系统命令，若命令执行成功返回 0，否则返回非零值
    if os.system("idf.py merge-bin") != 0:
        # 若合并失败，打印错误信息
        print("merge bin failed")
        # 以状态码 1 退出程序，表示异常退出
        sys.exit(1)

# 此函数用于将合并后的二进制文件打包成 ZIP 文件
def zip_bin(board_type, project_version):
    """
    将合并后的二进制文件打包为 ZIP 文件。
    参数:
    - board_type: 板子类型
    - project_version: 项目版本号
    """
    # 检查 releases 目录是否存在，如果不存在则创建该目录
    if not os.path.exists("releases"):
        os.makedirs("releases")
    # 定义 ZIP 文件的输出路径
    output_path = f"releases/v{project_version}_{board_type}.zip"
    # 如果该 ZIP 文件已经存在，则删除它
    if os.path.exists(output_path):
        os.remove(output_path)
    # 调用 zip 命令将 build 目录下的 merged-binary.bin 文件打包到指定的 ZIP 文件中
    if os.system(f"zip -j {output_path} build/merged-binary.bin") != 0:
        # 若打包失败，打印错误信息
        print("zip bin failed")
        # 以状态码 1 退出程序，表示异常退出
        sys.exit(1)
    # 若打包成功，打印打包完成的信息
    print(f"zip bin to {output_path} done")

# 此函数用于发布当前配置的板子类型和项目版本
def release_current():
    """
    发布当前配置的板子类型和项目版本。
    """
    # 调用 merge_bin 函数合并二进制文件
    merge_bin()
    # 调用 get_board_type 函数获取当前板子类型
    board_type = get_board_type()
    # 打印当前板子类型
    print("board type:", board_type)
    # 调用 get_project_version 函数获取当前项目版本号
    project_version = get_project_version()
    # 打印当前项目版本号
    print("project version:", project_version)
    # 调用 zip_bin 函数将合并后的二进制文件打包
    zip_bin(board_type, project_version)

# 此函数用于从 CMakeLists.txt 文件中提取所有板子类型及其配置
def get_all_board_types():
    """
    从 CMakeLists.txt 中提取所有板子类型及其配置。
    返回值: 包含板子配置的字典，键为配置名，值为板子类型。
    """
    # 初始化一个空字典，用于存储板子配置
    board_configs = {}
    # 打开 main 目录下的 CMakeLists.txt 文件
    with open("main/CMakeLists.txt", encoding="utf-8") as f:
        # 读取文件的所有行
        lines = f.readlines()
        # 遍历每一行及其索引
        for i, line in enumerate(lines):
            # 查找包含 if(CONFIG_BOARD_TYPE_*) 的行
            if "if(CONFIG_BOARD_TYPE_" in line:
                # 提取配置名
                config_name = line.strip().split("if(")[1].split(")")[0]
                # 获取下一行并去除首尾空格
                next_line = lines[i + 1].strip()
                # 检查下一行是否以 set(BOARD_TYPE 开头
                if next_line.startswith("set(BOARD_TYPE"):
                    # 提取板子类型
                    board_type = next_line.split('"')[1]
                    # 将配置名和板子类型添加到字典中
                    board_configs[config_name] = board_type
    # 返回包含板子配置的字典
    return board_configs

# 此函数用于发布指定板子类型的固件
def release(board_type, board_config):
    """
    发布指定板子类型的固件。
    参数:
    - board_type: 板子类型
    - board_config: 板子配置名
    """
    # 定义板子配置文件的路径
    config_path = f"main/boards/{board_type}/config.json"
    # 检查配置文件是否存在，如果不存在则跳过该板子类型
    if not os.path.exists(config_path):
        print(f"跳过 {board_type} 因为 config.json 不存在")
        return

    # 调用 get_project_version 函数获取项目版本号
    project_version = get_project_version()
    # 打印项目版本号
    print(f"Project Version: {project_version}")
    # 定义发布文件的路径
    release_path = f"releases/v{project_version}_{board_type}.zip"
    # 检查发布文件是否已经存在，如果存在则跳过该板子类型
    if os.path.exists(release_path):
        print(f"跳过 {board_type} 因为 {release_path} 已存在")
        return

    # 打开配置文件并加载 JSON 数据
    with open(config_path, "r") as f:
        config = json.load(f)
    # 从配置中获取目标设备
    target = config["target"]
    # 从配置中获取构建信息列表
    builds = config["builds"]
    
    # 遍历每个构建信息
    for build in builds:
        # 获取构建名称
        name = build["name"]
        # 检查构建名称是否以板子类型开头，如果不是则抛出异常
        if not name.startswith(board_type):
            raise ValueError(f"name {name} 必须 {board_type} 开头")

        # 初始化 sdkconfig 追加配置列表
        sdkconfig_append = [f"{board_config}=y"]
        # 遍历构建信息中的 sdkconfig_append 列表，并添加到 sdkconfig_append 中
        for append in build.get("sdkconfig_append", []):
            sdkconfig_append.append(append)
        # 打印构建名称
        print(f"name: {name}")
        # 打印目标设备
        print(f"target: {target}")
        # 打印 sdkconfig 追加配置
        for append in sdkconfig_append:
            print(f"sdkconfig_append: {append}")
        # 取消设置 IDF_TARGET 环境变量
        os.environ.pop("IDF_TARGET", None)
        # 调用 idf.py 的 set-target 命令设置目标设备
        if os.system(f"idf.py set-target {target}") != 0:
            # 若设置目标设备失败，打印错误信息
            print("set-target failed")
            # 以状态码 1 退出程序，表示异常退出
            sys.exit(1)
        # 打开 sdkconfig 文件并追加 sdkconfig 配置
        with open("sdkconfig", "a") as f:
            f.write("\n")
            for append in sdkconfig_append:
                f.write(f"{append}\n")
        # 调用 idf.py 的 build 命令进行构建，使用宏 BOARD_NAME
        if os.system(f"idf.py -DBOARD_NAME={name} build") != 0:
            # 若构建失败，打印错误信息
            print("build failed")
            # 以状态码 1 退出程序，表示异常退出
            sys.exit(1)
        # 调用 idf.py 的 merge-bin 命令合并二进制文件
        if os.system("idf.py merge-bin") != 0:
            # 若合并二进制文件失败，打印错误信息
            print("merge-bin failed")
            # 以状态码 1 退出程序，表示异常退出
            sys.exit(1)
        # 调用 zip_bin 函数将合并后的二进制文件打包
        zip_bin(name, project_version)
        # 打印分隔线
        print("-" * 80)

if __name__ == "__main__":
    # 检查命令行参数的数量是否大于 1
    if len(sys.argv) > 1:
        # 调用 get_all_board_types 函数获取所有板子类型及其配置
        board_configs = get_all_board_types()
        # 初始化一个标志变量，用于标记是否找到匹配的板子类型
        found = False
        # 遍历所有板子配置
        for board_config, board_type in board_configs.items():
            # 检查命令行参数是否为 'all' 或者与当前板子类型匹配
            if sys.argv[1] == 'all' or board_type == sys.argv[1]:
                # 若匹配，则调用 release 函数发布该板子类型的固件
                release(board_type, board_config)
                # 将标志变量设置为 True，表示找到匹配的板子类型
                found = True
        # 若未找到匹配的板子类型
        if not found:
            # 打印未找到板子类型的信息
            print(f"未找到板子类型: {sys.argv[1]}")
            # 打印可用的板子类型信息
            print("可用的板子类型:")
            for board_type in board_configs.values():
                print(f"  {board_type}")
    else:
        # 若没有命令行参数，则调用 release_current 函数发布当前配置
        release_current()