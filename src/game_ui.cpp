#include "game_ui.h"

#include <algorithm>
#include <sstream>

namespace monopoly {

namespace {

constexpr int window_width = 1360;        // 主窗口宽度。
constexpr int window_height = 900;        // 主窗口高度。
constexpr int board_origin_x = 24;        // 地图左上角 x 坐标。
constexpr int board_origin_y = 24;        // 地图左上角 y 坐标。
constexpr int cell_width = 118;           // 单个地块宽度。
constexpr int cell_height = 100;          // 单个地块高度。
constexpr UINT_PTR dice_timer_id = 1;     // 骰子动画定时器编号。
constexpr UINT_PTR blink_timer_id = 2;    // 当前玩家闪烁定时器编号。
constexpr UINT_PTR game_timer_id = 3;     // 游戏限时检查定时器编号。

// 创建界面用字体。
HFONT create_font(int size, int weight = FW_NORMAL) {
    return CreateFontW(
        size,
        0,
        0,
        0,
        weight,
        FALSE,
        FALSE,
        FALSE,
        GB2312_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        L"Microsoft YaHei UI");
}

// 创建指定颜色的画刷。
HBRUSH make_brush(COLORREF color) {
    return CreateSolidBrush(color);
}

}  // namespace

// 启动 Win32 消息循环，运行游戏窗口。
int game_ui::run() {
    instance_ = GetModuleHandleW(nullptr); // 当前程序实例句柄。
    rng_.seed(static_cast<unsigned int>(GetTickCount()));

    if (!create_window()) {
        MessageBoxW(nullptr, L"窗口创建失败。", L"大富翁游戏", MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    SetTimer(hwnd_, blink_timer_id, 500, nullptr);
    SetTimer(hwnd_, game_timer_id, 1000, nullptr);

    MSG message{};                         // Win32 消息结构。
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

// Win32 静态窗口过程，把消息转发到当前 game_ui 对象。
LRESULT CALLBACK game_ui::window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    game_ui* ui = nullptr;                 // 当前窗口绑定的界面对象。

    if (message == WM_NCCREATE) {
        CREATESTRUCTW* create_struct = reinterpret_cast<CREATESTRUCTW*>(lparam);
        ui = reinterpret_cast<game_ui*>(create_struct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ui));
        ui->hwnd_ = hwnd;
    } else {
        ui = reinterpret_cast<game_ui*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (ui) {
        return ui->handle_message(message, wparam, lparam);
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

// 注册窗口类并创建主窗口。
bool game_ui::create_window() {
    const wchar_t class_name[] = L"monopoly_game_window"; // Win32 窗口类名。

    WNDCLASSW window_class{};              // 窗口类配置。
    window_class.lpfnWndProc = game_ui::window_proc;
    window_class.hInstance = instance_;
    window_class.lpszClassName = class_name;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    RegisterClassW(&window_class);

    hwnd_ = CreateWindowExW(
        0,
        class_name,
        L"重庆大富翁游戏",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        window_width,
        window_height,
        nullptr,
        nullptr,
        instance_,
        this);

    return hwnd_ != nullptr;
}

// 处理窗口消息，包括绘制、点击、定时器和关闭。
LRESULT game_ui::handle_message(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_PAINT:
        paint_window();
        return 0;
    case WM_LBUTTONDOWN:
        on_click(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        return 0;
    case WM_TIMER:
        if (wparam == dice_timer_id) {
            on_timer();
            return 0;
        }
        if (wparam == blink_timer_id) {
            on_blink_timer();
            return 0;
        }
        if (wparam == game_timer_id) {
            on_game_timer();
            return 0;
        }
        break;
    case WM_DESTROY:
        KillTimer(hwnd_, dice_timer_id);
        KillTimer(hwnd_, blink_timer_id);
        KillTimer(hwnd_, game_timer_id);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }

    return DefWindowProcW(hwnd_, message, wparam, lparam);
}

// 重绘整个窗口。
void game_ui::paint_window() {
    PAINTSTRUCT paint{};                   // BeginPaint/EndPaint 使用的绘制信息。
    HDC hdc = BeginPaint(hwnd_, &paint);  // 当前窗口绘图设备。

    RECT client{};                         // 当前窗口客户区范围。
    GetClientRect(hwnd_, &client);
    HBRUSH background = make_brush(RGB(244, 246, 248)); // 窗口背景画刷。
    FillRect(hdc, &client, background);
    DeleteObject(background);

    SetBkMode(hdc, TRANSPARENT);
    draw_board(hdc);
    draw_center(hdc);

    EndPaint(hwnd_, &paint);
}

// 绘制整圈地图地块。
void game_ui::draw_board(HDC hdc) {
    for (int index = 0; index < board_cell_count; ++index) {
        draw_cell(hdc, index);
    }
}

// 绘制单个地图地块的背景、文字和玩家棋子。
void game_ui::draw_cell(HDC hdc, int cell_index) {
    const RECT rect = cell_rect(cell_index);              // 当前地块的屏幕矩形。
    const board_cell_def& cell = board_cells[cell_index]; // 当前地块固定配置。
    const property_state& state = engine_.property_at(cell_index); // 当前地块动态状态。

    COLORREF fill_color = RGB(255, 255, 255);             // 地块填充色，未购买默认白色。
    if (cell.kind == cell_kind::property && state.owner_team >= 0) {
        fill_color = team_color(state.owner_team);
    }

    HBRUSH brush = make_brush(fill_color); // 地块背景画刷。
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);

    HPEN border_pen = CreatePen(PS_SOLID, 1, RGB(80, 80, 80)); // 地块边框画笔。
    HGDIOBJ old_pen = SelectObject(hdc, border_pen);          // 保存原画笔，绘制后恢复。
    HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH)); // 保存原画刷，边框不填充。
    Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
    DeleteObject(border_pen);

    RECT text_rect = rect; // 地块文字绘制区域。
    InflateRect(&text_rect, -6, -5);

    std::wstringstream text;                              // 地块内显示的多行文字。
    if (cell.kind == cell_kind::property) {
        if (state.owner_team >= 0) {
            text << cell.name << L"\n";
            text << L"房屋：" << state.houses << L"\n";
            if (state.houses >= max_houses_per_property) {
                text << L"建房费：已满\n";
            } else {
                text << L"建房费：" << engine_.cell_build_cost(cell_index) << L"\n";
            }
            text << L"过路费：" << engine_.get_rent(cell_index);
        } else {
            text << cell.name << L"\n" << engine_.cell_price(cell_index) << L"元";
        }
    } else {
        text << cell.name;
    }

    const bool owned_property = cell.kind == cell_kind::property && state.owner_team >= 0; // 是否是已购买房产。
    HFONT font = create_font(owned_property ? 13 : 16); // 已购买地块文字更多，所以字号更小。
    HGDIOBJ old_font = SelectObject(hdc, font);         // 保存旧字体，绘制后恢复。
    SetTextColor(hdc, RGB(20, 20, 20));
    draw_text(hdc, text.str(), text_rect, DT_CENTER | DT_WORDBREAK | DT_END_ELLIPSIS);
    SelectObject(hdc, old_font);
    DeleteObject(font);

    if (cell.kind != cell_kind::property) {
        RECT mark_rect{rect.left + 5, rect.top + 5, rect.left + 21, rect.top + 21}; // 特殊地块左上角色块。
        HBRUSH mark_brush = make_brush(kind_color(cell.kind));                     // 特殊地块色块画刷。
        FillRect(hdc, &mark_rect, mark_brush);
        DeleteObject(mark_brush);
    }

    draw_players(hdc, cell_index, rect);
}

// 绘制站在某个地块上的所有玩家圆形棋子。
void game_ui::draw_players(HDC hdc, int cell_index, const RECT& rect) {
    std::vector<const player*> items;       // 当前地块上的玩家指针列表。
    const std::vector<team>& teams = engine_.teams(); // 队伍状态，用于过滤 out 队伍。
    for (const player& item : engine_.players()) {
        if (item.team_index >= 0 &&
            item.team_index < static_cast<int>(teams.size()) &&
            teams[item.team_index].out) {
            continue;
        }
        if (item.position == cell_index) {
            items.push_back(&item);
        }
    }

    if (items.empty()) {
        return;
    }

    HFONT font = create_font(15, FW_BOLD);  // 棋子编号字体。
    HGDIOBJ old_font = SelectObject(hdc, font); // 保存旧字体，绘制后恢复。
    SetTextColor(hdc, RGB(255, 255, 255));

    const int active_player_id = engine_.current_player_id(); // 当前正在操作的玩家编号。
    const int radius = 12;                  // 玩家棋子的半径，放大后更容易看清。
    const int gap = 27;                     // 同一地块多个棋子之间的间距。
    for (size_t index = 0; index < items.size(); ++index) {
        const int x = rect.left + 18 + static_cast<int>(index % 4) * gap;     // 当前棋子圆心 x。
        const int y = rect.bottom - 20 - static_cast<int>(index / 4) * gap;   // 当前棋子圆心 y。
        const bool active = items[index]->id == active_player_id;             // 是否为当前操作玩家。

        HBRUSH brush = make_brush(team_color(items[index]->team_index)); // 棋子填充色。
        HGDIOBJ old_brush = SelectObject(hdc, brush);                    // 保存旧画刷。
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(40, 40, 40));              // 棋子边框画笔。
        HGDIOBJ old_pen = SelectObject(hdc, pen);                        // 保存旧画笔。
        Ellipse(hdc, x - radius, y - radius, x + radius, y + radius);
        SelectObject(hdc, old_pen);
        SelectObject(hdc, old_brush);
        DeleteObject(pen);
        DeleteObject(brush);

        RECT number_rect{x - radius, y - radius - 1, x + radius, y + radius}; // 棋子编号文字范围。
        draw_text(hdc, std::to_wstring(items[index]->id + 1), number_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // 当前操作玩家每秒闪一次外圈，帮助快速定位轮到谁操作。
        if (active && blink_on_) {
            const int active_extra = 6; // 闪烁外圈比棋子大多少。
            HPEN active_pen = CreatePen(PS_SOLID, 4, RGB(255, 215, 0));
            HGDIOBJ old_active_pen = SelectObject(hdc, active_pen);
            HGDIOBJ old_active_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Ellipse(hdc, x - radius - active_extra, y - radius - active_extra, x + radius + active_extra, y + radius + active_extra);
            SelectObject(hdc, old_active_brush);
            SelectObject(hdc, old_active_pen);
            DeleteObject(active_pen);
        }
    }

    SelectObject(hdc, old_font);
    DeleteObject(font);
}

// 绘制地图中心区域：输出框、操作按钮区和骰子区。
void game_ui::draw_center(HDC hdc) {
    RECT rect{                              // 地图中间可用区域。
        board_origin_x + cell_width + 14,
        board_origin_y + cell_height + 14,
        board_origin_x + 10 * cell_width - 14,
        board_origin_y + 7 * cell_height - 14};

    HBRUSH brush = make_brush(RGB(236, 240, 243)); // 中央区域背景画刷。
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(170, 176, 182)); // 中央区域边框画笔。
    HGDIOBJ old_pen = SelectObject(hdc, pen);              // 保存旧画笔。
    HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH)); // 保存旧画刷。
    Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);

    RECT info_rect{rect.left + 18, rect.top + 18, rect.right - 18, rect.bottom - 130}; // 放大的信息输出框。
    RECT control_rect{rect.left + 18, info_rect.bottom + 12, rect.right - 18, rect.bottom - 18}; // 下方控制区。
    dice_rect_ = {control_rect.right - 205, control_rect.top, control_rect.right, control_rect.bottom}; // 骰子点击区域。
    RECT option_rect{control_rect.left, control_rect.top, dice_rect_.left - 12, control_rect.bottom}; // 操作按钮区域。

    HBRUSH info_brush = make_brush(RGB(248, 250, 252)); // 输出框背景画刷。
    FillRect(hdc, &info_rect, info_brush);
    DeleteObject(info_brush);
    Rectangle(hdc, info_rect.left, info_rect.top, info_rect.right, info_rect.bottom);

    HFONT font = create_font(17);              // 输出框正文字体。
    HGDIOBJ old_font = SelectObject(hdc, font); // 保存旧字体。
    SetTextColor(hdc, RGB(20, 28, 38));
    RECT info_text_rect = info_rect; // 输出框文字内边距区域。
    InflateRect(&info_text_rect, -16, -12);
    draw_text(hdc, engine_.build_info_text(), info_text_rect, DT_WORDBREAK);
    SelectObject(hdc, old_font);
    DeleteObject(font);

    draw_buttons(hdc, option_rect);
    draw_dice(hdc, dice_rect_);
}

// 绘制当前阶段允许点击的操作按钮，并记录按钮点击区域。
void game_ui::draw_buttons(HDC hdc, const RECT& area) {
    buttons_.clear();

    HBRUSH area_brush = make_brush(RGB(248, 250, 252)); // 按钮区背景画刷。
    FillRect(hdc, &area, area_brush);
    DeleteObject(area_brush);
    Rectangle(hdc, area.left, area.top, area.right, area.bottom);

    const std::vector<action_option>& options = engine_.options(); // 游戏引擎给出的合法操作。
    if (options.empty()) {
        return;
    }

    const int count = static_cast<int>(options.size());   // 按钮数量。
    const bool setup_phase = engine_.phase() == game_phase::setup; // 是否为开局设置阶段。
    const int columns = setup_phase ? 6 : (count > 18 ? 5 : (count > 8 ? 4 : (count > 4 ? 2 : 1))); // 根据阶段和数量决定列数。
    const int rows = (count + columns - 1) / columns;      // 当前按钮需要占用的行数。
    const int gap = setup_phase ? 8 : (count > 8 ? 6 : 8); // 按钮间距。
    const int preferred_height = setup_phase ? 36 : (count > 18 ? 24 : (count > 8 ? 28 : 36)); // 希望使用的按钮高度。
    const int available_height = area.bottom - area.top - gap * (rows + 1); // 扣掉上下和行间间距后的可用高度。
    const int button_height = std::max(20, std::min(preferred_height, available_height / rows)); // 实际按钮高度，保证不被裁掉。
    const int button_width = (area.right - area.left - gap * (columns + 1)) / columns; // 按钮宽度。

    HFONT font = create_font(count > 18 ? 12 : 14, FW_BOLD); // 按钮文字字体，按钮多时自动变小。
    HGDIOBJ old_font = SelectObject(hdc, font);              // 保存旧字体。

    for (int index = 0; index < count; ++index) {
        const int row = index / columns;       // 按钮所在行。
        const int column = index % columns;    // 按钮所在列。
        RECT rect{
            area.left + gap + column * (button_width + gap),
            area.top + gap + row * (button_height + gap),
            area.left + gap + column * (button_width + gap) + button_width,
            area.top + gap + row * (button_height + gap) + button_height};

        if (rect.bottom > area.bottom - gap) {
            break;
        }

        bool selected = false;                            // 设置阶段用于高亮已选项。
        if (options[index].id == action_id::select_player_count && options[index].payload == engine_.selected_player_count()) {
            selected = true;
        }
        if (options[index].id == action_id::select_initial_cash && options[index].payload == engine_.selected_initial_cash()) {
            selected = true;
        }
        if (options[index].id == action_id::select_game_minutes && options[index].payload == engine_.selected_game_minutes()) {
            selected = true;
        }
        const bool primary = options[index].id == action_id::confirm_setup; // “开始游戏”主按钮。

        const COLORREF button_fill = selected ? RGB(224, 244, 235) : (primary ? RGB(219, 234, 254) : RGB(230, 235, 241)); // 按钮填充色。
        const COLORREF border_color = selected ? RGB(20, 140, 90) : (primary ? RGB(37, 99, 235) : RGB(150, 156, 164)); // 按钮边框颜色。

        HBRUSH brush = make_brush(button_fill); // 按钮背景画刷。
        FillRect(hdc, &rect, brush);
        DeleteObject(brush);

        HPEN pen = CreatePen(PS_SOLID, 1, border_color); // 按钮边框画笔。
        HGDIOBJ old_pen = SelectObject(hdc, pen);          // 保存旧画笔。
        HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH)); // 保存旧画刷。
        RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, 6, 6);
        SelectObject(hdc, old_brush);
        SelectObject(hdc, old_pen);
        DeleteObject(pen);

        SetTextColor(hdc, RGB(22, 32, 44));
        RECT text_rect = rect; // 按钮文字范围。
        InflateRect(&text_rect, -4, 0);
        draw_text(hdc, options[index].label, text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        buttons_.push_back({rect, options[index].id, options[index].payload});
    }

    SelectObject(hdc, old_font);
    DeleteObject(font);
}

// 绘制骰子区域和骰子点数。
void game_ui::draw_dice(HDC hdc, const RECT& rect) {
    HBRUSH brush = make_brush(RGB(248, 250, 252)); // 骰子区背景画刷。
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
    Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);

    RECT dice_area{rect.left + 16, rect.top + 32, rect.right - 16, rect.bottom - 8}; // 骰子必须完全待在这个内部区域。
    const int area_width = dice_area.right - dice_area.left;                         // 骰子内部区域宽度。
    const int area_height = dice_area.bottom - dice_area.top;                        // 骰子内部区域高度。
    const int size = std::max(50, std::min({82, area_width, area_height}));          // 骰子边长，不能超过内部区域。
    RECT dice{                                                                       // 骰子的实际绘制矩形。
        dice_area.left + (area_width - size) / 2,
        dice_area.top + (area_height - size) / 2,
        dice_area.left + (area_width + size) / 2,
        dice_area.top + (area_height + size) / 2};

    HBRUSH dice_brush = make_brush(engine_.can_roll_dice() ? RGB(255, 255, 255) : RGB(226, 230, 235)); // 骰子本体画刷。
    FillRect(hdc, &dice, dice_brush);
    DeleteObject(dice_brush);

    HPEN pen = CreatePen(PS_SOLID, 2, RGB(56, 65, 80)); // 骰子边框画笔。
    HGDIOBJ old_pen = SelectObject(hdc, pen);           // 保存旧画笔。
    HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH)); // 保存旧画刷。
    RoundRect(hdc, dice.left, dice.top, dice.right, dice.bottom, 18, 18);
    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);

    int face = dice_rolling_ ? dice_face_ : std::max(1, engine_.last_dice()); // 当前显示的点数。
    draw_dice_pips(hdc, dice, face);

    HFONT font = create_font(18, FW_BOLD);  // “骰子”标题字体。
    HGDIOBJ old_font = SelectObject(hdc, font); // 保存旧字体。
    SetTextColor(hdc, RGB(41, 50, 65));
    RECT title_rect{rect.left, rect.top + 6, rect.right, rect.top + 30}; // 骰子标题文字范围。
    draw_text(hdc, L"骰子", title_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, old_font);
    DeleteObject(font);
}

// 根据骰子点数绘制圆点。
void game_ui::draw_dice_pips(HDC hdc, const RECT& rect, int face) {
    const int cx = (rect.left + rect.right) / 2;      // 骰子中心 x。
    const int cy = (rect.top + rect.bottom) / 2;      // 骰子中心 y。
    const int offset = (rect.right - rect.left) / 4;  // 圆点到中心的偏移。
    const int radius = 6;                             // 圆点半径。

    auto pip = [&](int x, int y) {                    // 绘制一个骰子圆点。
        HBRUSH brush = make_brush(RGB(31, 41, 55)); // 骰子圆点画刷。
        HGDIOBJ old_brush = SelectObject(hdc, brush); // 保存旧画刷。
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(31, 41, 55)); // 骰子圆点边框画笔。
        HGDIOBJ old_pen = SelectObject(hdc, pen); // 保存旧画笔。
        Ellipse(hdc, x - radius, y - radius, x + radius, y + radius);
        SelectObject(hdc, old_pen);
        SelectObject(hdc, old_brush);
        DeleteObject(pen);
        DeleteObject(brush);
    };

    if (face == 1 || face == 3 || face == 5) {
        pip(cx, cy);
    }
    if (face >= 2) {
        pip(cx - offset, cy - offset);
        pip(cx + offset, cy + offset);
    }
    if (face >= 4) {
        pip(cx + offset, cy - offset);
        pip(cx - offset, cy + offset);
    }
    if (face == 6) {
        pip(cx - offset, cy);
        pip(cx + offset, cy);
    }
}

// 在指定矩形中绘制文字。
void game_ui::draw_text(HDC hdc, const std::wstring& text, const RECT& rect, UINT format) {
    RECT local_rect = rect; // DrawTextW 会改写矩形，所以使用副本。
    DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &local_rect, format);
}

// 处理鼠标点击，只有当前阶段允许的区域会生效。
void game_ui::on_click(int x, int y) {
    if (dice_rolling_) {
        engine_.register_invalid_click(L"骰子正在转动，请等待本次结果。");
        invalidate();
        return;
    }

    for (const button_area& button : buttons_) {
        if (point_in_rect(x, y, button.rect)) {
            engine_.handle_action(button.id, button.payload);
            invalidate();
            return;
        }
    }

    if (point_in_rect(x, y, dice_rect_)) {
        if (engine_.can_roll_dice()) {
            start_dice_animation();
        } else {
            engine_.register_invalid_click(L"当前不能投骰子。");
            invalidate();
        }
        return;
    }

    const int cell_index = hit_cell(x, y);            // 点击命中的地图格子编号。
    if (cell_index >= 0) {
        engine_.handle_map_click(cell_index);
        invalidate();
        return;
    }

    engine_.register_invalid_click(L"没有可执行的操作。");
    invalidate();
}

// 启动骰子转动动画。
void game_ui::start_dice_animation() {
    dice_rolling_ = true;
    dice_ticks_ = 0;
    SetTimer(hwnd_, dice_timer_id, 70, nullptr);
    invalidate();
}

// 定时刷新骰子动画，动画结束后通知游戏引擎真正掷骰。
void game_ui::on_timer() {
    if (!dice_rolling_) {
        KillTimer(hwnd_, dice_timer_id);
        return;
    }

    std::uniform_int_distribution<int> dist(1, 6); // 动画期间随机显示骰子点数。
    dice_face_ = dist(rng_);
    ++dice_ticks_;

    if (dice_ticks_ >= 12) {
        KillTimer(hwnd_, dice_timer_id);
        dice_rolling_ = false;
        engine_.roll_dice();
        invalidate();
        return;
    }

    InvalidateRect(hwnd_, &dice_rect_, FALSE);
}

// 定时切换当前玩家棋子的闪烁状态。
void game_ui::on_blink_timer() {
    blink_on_ = !blink_on_;

    const int active_player_id = engine_.current_player_id(); // 当前操作玩家编号。
    for (const player& item : engine_.players()) {
        if (item.id == active_player_id) {
            RECT rect = cell_rect(item.position); // 只刷新当前玩家所在地块，避免整屏闪烁。
            InflateRect(&rect, 8, 8);
            InvalidateRect(hwnd_, &rect, FALSE);
            return;
        }
    }
}

// 每秒检查限时并刷新中间区域倒计时。
void game_ui::on_game_timer() {
    const game_phase old_phase = engine_.phase(); // 检查前的游戏阶段。
    engine_.update_game_time();

    RECT rect{                              // 中间区域刷新范围。
        board_origin_x + cell_width + 14,
        board_origin_y + cell_height + 14,
        board_origin_x + 10 * cell_width - 14,
        board_origin_y + 7 * cell_height - 14};
    if (old_phase != engine_.phase()) {
        invalidate();
        return;
    }
    if (engine_.phase() != game_phase::setup && engine_.phase() != game_phase::game_over) {
        InvalidateRect(hwnd_, &rect, FALSE);
    }
}

// 请求窗口重绘。
void game_ui::invalidate() {
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// 根据地块编号计算其在屏幕上的矩形。
RECT game_ui::cell_rect(int cell_index) const {
    int column = 0;                                    // 地块所在列。
    int row = 0;                                       // 地块所在行。

    if (cell_index >= 0 && cell_index <= 10) {
        column = cell_index;
        row = 0;
    } else if (cell_index >= 11 && cell_index <= 16) {
        column = 10;
        row = cell_index - 10;
    } else if (cell_index == 17) {
        column = 10;
        row = 7;
    } else if (cell_index >= 18 && cell_index <= 27) {
        column = 10 - (cell_index - 17);
        row = 7;
    } else {
        column = 0;
        row = 7 - (cell_index - 27);
    }

    return RECT{
        board_origin_x + column * cell_width,
        board_origin_y + row * cell_height,
        board_origin_x + (column + 1) * cell_width,
        board_origin_y + (row + 1) * cell_height};
}

// 判断鼠标坐标命中了哪个地图地块。
int game_ui::hit_cell(int x, int y) const {
    for (int index = 0; index < board_cell_count; ++index) {
        if (point_in_rect(x, y, cell_rect(index))) {
            return index;
        }
    }
    return -1;
}

// 判断点是否在矩形内部。
bool game_ui::point_in_rect(int x, int y, const RECT& rect) const {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

// 根据队伍编号返回队伍颜色。
COLORREF game_ui::team_color(int team_index) const {
    static const COLORREF colors[] = { // 每支队伍使用的固定颜色表。
        RGB(231, 76, 60),
        RGB(52, 152, 219),
        RGB(46, 204, 113),
        RGB(241, 196, 15),
        RGB(155, 89, 182),
        RGB(26, 188, 156),
        RGB(230, 126, 34),
        RGB(127, 140, 141),
        RGB(52, 73, 94)
    };
    const int index = std::max(0, team_index) % static_cast<int>(std::size(colors)); // 防止队伍编号越界。
    return colors[index];
}

// 根据特殊地块类型返回角标颜色。
COLORREF game_ui::kind_color(cell_kind kind) const {
    switch (kind) {
    case cell_kind::start:
        return RGB(25, 135, 84);
    case cell_kind::chance:
        return RGB(111, 66, 193);
    case cell_kind::jail:
        return RGB(220, 53, 69);
    case cell_kind::airport:
        return RGB(13, 110, 253);
    case cell_kind::bank:
        return RGB(255, 193, 7);
    default:
        return RGB(255, 255, 255);
    }
}

}  // namespace monopoly
