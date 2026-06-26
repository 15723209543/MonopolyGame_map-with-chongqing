#pragma once

#include <array>
#include <string_view>

namespace monopoly {

enum class cell_kind {
    start,     // 起点：通过或到达后给玩家发放起点奖励。
    property,  // 房产：可以买地、建房、收取过路费。
    chance,    // 机会：到达后随机抽取机会卡。
    jail,      // 监狱：到达后当前队伍暂停一次。
    airport,   // 机场：到达后允许点击任意地块飞过去。
    bank       // 银行：到达后获得当前现金十分之一利息。
};

enum class chance_effect {
    gain_money,            // 直接增加现金。
    lose_money,            // 直接扣除现金，不足时进入抵押流程。
    move_to_start,         // 移动到起点并领取起点奖励。
    go_to_jail,            // 移动到监狱并让队伍暂停一次。
    gain_cash_percent,     // 按当前现金比例获得奖励。
    repair_fee_per_house,  // 按本队房屋数量支付维修费。
    move_to_bank           // 移动到银行并领取利息。
};

struct board_cell_def {
    int index;                  // 地块在顺时针路径中的编号。
    cell_kind kind;             // 地块类型，决定到达后的规则。
    std::wstring_view name;     // 界面上显示的地名。
    int price;                  // 5000 元开局时的基础地价。
    int build_cost;             // 5000 元开局时第一栋房子的基础建房费。
    int base_rent;              // 5000 元开局时 0 栋房子的基础过路费。
};

struct chance_card_def {
    std::wstring_view title;        // 机会卡标题。
    std::wstring_view description;  // 抽到机会卡后显示的说明。
    chance_effect effect;           // 机会卡实际触发的效果类型。
    int amount;                     // 金额或百分比参数，含义由 effect 决定。
};

inline constexpr int board_cell_count = 34;          // 地图总格数。
inline constexpr int start_index = 0;                // 起点所在格子编号。
inline constexpr int jail_index = 10;                // 监狱所在格子编号。
inline constexpr int airport_index = 17;             // 机场所在格子编号。
inline constexpr int bank_index = 27;                // 银行所在格子编号。
inline constexpr int max_houses_per_property = 3;    // 每块地最多可建房数量。

inline constexpr std::array<int, 6> allowed_player_counts = {2, 3, 4, 6, 8, 9};        // 开局允许选择的人数。
inline constexpr std::array<int, 4> allowed_initial_cash = {3000, 5000, 8000, 10000};  // 开局允许选择的每人初始资金。
inline constexpr std::array<int, 5> allowed_game_minutes = {5, 10, 15, 20, 30};        // 开局允许选择的游戏限时分钟数。

// 这里存的是 5000 元开局时的基础价格，实际游戏会按所选初始资金缩放。
inline constexpr std::array<board_cell_def, board_cell_count> board_cells = {{
    {0, cell_kind::start, L"起点", 0, 0, 0},
    {1, cell_kind::property, L"渝中区", 1200, 600, 120},
    {2, cell_kind::property, L"江北区", 1300, 650, 130},
    {3, cell_kind::chance, L"机会", 0, 0, 0},
    {4, cell_kind::property, L"沙坪坝", 1400, 700, 140},
    {5, cell_kind::property, L"九龙坡", 1500, 750, 150},
    {6, cell_kind::property, L"南岸区", 1600, 800, 160},
    {7, cell_kind::chance, L"机会", 0, 0, 0},
    {8, cell_kind::property, L"大渡口", 1100, 550, 110},
    {9, cell_kind::property, L"北碚区", 1250, 625, 125},
    {10, cell_kind::jail, L"监狱", 0, 0, 0},
    {11, cell_kind::property, L"渝北区", 1800, 900, 180},
    {12, cell_kind::property, L"巴南区", 1450, 725, 145},
    {13, cell_kind::chance, L"机会", 0, 0, 0},
    {14, cell_kind::property, L"綦江区", 1350, 675, 135},
    {15, cell_kind::property, L"大足区", 1300, 650, 130},
    {16, cell_kind::property, L"璧山区", 1550, 775, 155},
    {17, cell_kind::airport, L"机场", 0, 0, 0},
    {18, cell_kind::property, L"铜梁区", 1450, 725, 145},
    {19, cell_kind::property, L"潼南区", 1250, 625, 125},
    {20, cell_kind::chance, L"机会", 0, 0, 0},
    {21, cell_kind::property, L"荣昌区", 1300, 650, 130},
    {22, cell_kind::property, L"开州区", 1500, 750, 150},
    {23, cell_kind::property, L"梁平区", 1400, 700, 140},
    {24, cell_kind::chance, L"机会", 0, 0, 0},
    {25, cell_kind::property, L"武隆区", 1600, 800, 160},
    {26, cell_kind::property, L"长寿区", 1700, 850, 170},
    {27, cell_kind::bank, L"银行", 0, 0, 0},
    {28, cell_kind::property, L"江津区", 1650, 825, 165},
    {29, cell_kind::property, L"合川区", 1500, 750, 150},
    {30, cell_kind::property, L"永川区", 1550, 775, 155},
    {31, cell_kind::chance, L"机会", 0, 0, 0},
    {32, cell_kind::property, L"涪陵区", 1750, 875, 175},
    {33, cell_kind::property, L"万州区", 1850, 925, 185},
}};

inline constexpr std::array<chance_card_def, 10> chance_cards = {{
    {L"项目分红", L"获得现金600元。", chance_effect::gain_money, 600},
    {L"车辆维修", L"支付维修费500元。", chance_effect::lose_money, 500},
    {L"回到起点", L"前往起点，并获得一次起点奖励。", chance_effect::move_to_start, 0},
    {L"违规停车", L"前往监狱，全队暂停一次。", chance_effect::go_to_jail, 0},
    {L"理财收益", L"获得当前现金10%的收益。", chance_effect::gain_cash_percent, 10},
    {L"房屋维护", L"本队每栋房子支付100元维护费。", chance_effect::repair_fee_per_house, 100},
    {L"银行贵宾", L"前往银行并领取利息。", chance_effect::move_to_bank, 0},
    {L"商圈活动", L"获得现金300元。", chance_effect::gain_money, 300},
    {L"临时罚款", L"支付罚款300元。", chance_effect::lose_money, 300},
    {L"小额返利", L"获得当前现金5%的返利。", chance_effect::gain_cash_percent, 5},
}};

}  // namespace monopoly
