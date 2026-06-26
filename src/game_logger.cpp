#define NOMINMAX
#include "game_logger.h"

#include <Windows.h>

#include <chrono>
#include <ctime>

namespace monopoly {

// 创建 result 目录并打开本局游戏日志文件。
bool game_logger::start_new_game() {
    namespace fs = std::filesystem;

    fs::create_directories(L"result");

    const auto now = std::chrono::system_clock::now();        // 当前系统时间，用于生成文件名。
    const auto time = std::chrono::system_clock::to_time_t(now); // 转成 C 时间格式，便于格式化。
    std::tm local_time{};                                     // 本地时间结构体。
    localtime_s(&local_time, &time);

    wchar_t name_buffer[64]{};                                // 保存日志文件名。
    wcsftime(name_buffer, 64, L"%Y%m%d_%H%M%S.txt", &local_time);
    path_ = fs::path(L"result") / name_buffer;

    stream_.open(path_, std::ios::binary | std::ios::out);
    if (!stream_.is_open()) {
        return false;
    }

    // 日志文件使用 GBK 无 BOM，中文 Windows 直接打开不会乱码。
    log(L"大富翁游戏日志开始");
    return true;
}

// 写入一行日志，并自动在行首加上当前时间。
void game_logger::log(const std::wstring& line) {
    if (!stream_.is_open()) {
        return;
    }

    const std::wstring line_with_time = L"[" + current_time_text() + L"] " + line; // 带时间戳的最终日志内容。
    stream_ << to_gbk(line_with_time) << "\r\n";
    stream_.flush();
}

// 返回当前日志文件路径，供界面显示。
std::wstring game_logger::path_text() const {
    return path_.wstring();
}

// 获取当前时分秒文本，用作日志行前缀。
std::wstring game_logger::current_time_text() {
    const auto now = std::chrono::system_clock::now();        // 当前系统时间。
    const auto time = std::chrono::system_clock::to_time_t(now); // 转成可格式化时间。
    std::tm local_time{};                                     // 本地时间结构体。
    localtime_s(&local_time, &time);

    wchar_t time_buffer[16]{};                                // 保存 HH:MM:SS。
    wcsftime(time_buffer, 16, L"%H:%M:%S", &local_time);
    return time_buffer;
}

// 把宽字符中文转换成 GBK 字节，保证日志用 GBK 编码保存。
std::string game_logger::to_gbk(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(
        936,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);

    std::string result(static_cast<size_t>(size), '\0');      // 转换后的 GBK 字节缓冲区。
    WideCharToMultiByte(
        936,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        size,
        nullptr,
        nullptr);
    return result;
}

}  // namespace monopoly
