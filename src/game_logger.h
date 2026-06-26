#pragma once

#include <filesystem>
#include <fstream>
#include <string>

namespace monopoly {

class game_logger {
public:
    // 创建 result 目录并打开本局日志文件。
    bool start_new_game();
    // 写入一条带时间戳的日志。
    void log(const std::wstring& line);
    // 返回当前日志文件路径。
    std::wstring path_text() const;

private:
    // 生成当前时分秒文本。
    static std::wstring current_time_text();
    // 把宽字符文本转换成 GBK 字节。
    static std::string to_gbk(const std::wstring& text);

    std::ofstream stream_;       // 日志输出文件流。
    std::filesystem::path path_; // 当前游戏日志文件路径。
};

}  // namespace monopoly
