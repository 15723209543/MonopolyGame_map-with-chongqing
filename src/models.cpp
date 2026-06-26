#include "models.h"

namespace monopoly {

// 把常量表中的 wstring_view 转成可拼接的 wstring。
std::wstring to_wide(std::wstring_view text) {
    return std::wstring(text.data(), text.size());
}

// 把整数金额格式化成“xx元”。
std::wstring format_money(int amount) {
    return std::to_wstring(amount) + L"元";
}

}  // namespace monopoly
