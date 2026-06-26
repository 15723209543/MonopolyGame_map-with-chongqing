#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "game_engine.h"

#include <Windows.h>
#include <windowsx.h>

#include <iterator>
#include <random>
#include <vector>

namespace monopoly {

struct button_area {
    RECT rect{};                              // 按钮在窗口中的点击区域。
    action_id id = action_id::confirm_setup;  // 点击按钮后交给游戏引擎的动作。
    int payload = 0;                          // 按钮附带参数，例如人数、金额、地块编号。
};

class game_ui {
public:
    // 创建窗口并进入 Win32 消息循环。
    int run();

private:
    // 静态窗口过程，负责把 Win32 消息转给当前对象。
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    // 注册窗口类并创建主窗口。
    bool create_window();
    // 处理绘制、点击、定时器和关闭消息。
    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);
    // 重绘整个窗口。
    void paint_window();
    // 绘制外圈地图。
    void draw_board(HDC hdc);
    // 绘制单个地块。
    void draw_cell(HDC hdc, int cell_index);
    // 绘制站在地块上的玩家棋子。
    void draw_players(HDC hdc, int cell_index, const RECT& rect);
    // 绘制中间输出框、按钮和骰子。
    void draw_center(HDC hdc);
    // 绘制当前可用按钮。
    void draw_buttons(HDC hdc, const RECT& area);
    // 绘制骰子区域。
    void draw_dice(HDC hdc, const RECT& rect);
    // 按点数绘制骰子圆点。
    void draw_dice_pips(HDC hdc, const RECT& rect, int face);
    // 在指定矩形中绘制文字。
    void draw_text(HDC hdc, const std::wstring& text, const RECT& rect, UINT format);
    // 处理鼠标左键点击。
    void on_click(int x, int y);
    // 启动骰子动画。
    void start_dice_animation();
    // 定时刷新骰子动画。
    void on_timer();
    // 定时切换当前玩家闪烁状态。
    void on_blink_timer();
    // 定时检查游戏限时。
    void on_game_timer();
    // 请求窗口重绘。
    void invalidate();
    // 根据地块编号计算屏幕矩形。
    RECT cell_rect(int cell_index) const;
    // 判断鼠标点命中的地图格子。
    int hit_cell(int x, int y) const;
    // 判断坐标是否在矩形内。
    bool point_in_rect(int x, int y, const RECT& rect) const;
    // 返回队伍颜色。
    COLORREF team_color(int team_index) const;
    // 返回特殊地块角标颜色。
    COLORREF kind_color(cell_kind kind) const;

    game_engine engine_;                  // 游戏规则和状态核心。
    HWND hwnd_ = nullptr;                 // 当前 Win32 主窗口句柄。
    HINSTANCE instance_ = nullptr;        // 当前程序实例句柄。
    std::vector<button_area> buttons_;    // 当前画出来且允许点击的按钮列表。
    RECT dice_rect_{};                    // 骰子区域，用于绘制和点击命中。
    bool dice_rolling_ = false;           // 骰子是否正在播放转动动画。
    bool blink_on_ = true;                 // 当前操作玩家的闪烁亮起状态。
    int dice_ticks_ = 0;                  // 骰子动画已经跳动的次数。
    int dice_face_ = 1;                   // 动画期间当前显示的骰子面。
    std::mt19937 rng_;                    // UI 动画用随机数生成器。
};

}  // namespace monopoly
