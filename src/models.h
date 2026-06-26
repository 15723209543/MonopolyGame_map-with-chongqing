#pragma once

#include "constants.h"

#include <string>
#include <string_view>
#include <vector>

namespace monopoly {

enum class game_phase {
    setup,                        // 游戏设置阶段，选择人数和初始资金。
    opening_roll,                 // 开局每队掷骰决定行动顺序。
    turn_ready,                   // 当前玩家可点击骰子行动。
    awaiting_property_decision,   // 当前玩家到达无人房产，等待买或不买。
    awaiting_build_decision,      // 当前玩家到达本队房产，等待建房或跳过。
    awaiting_mortgage_consent,    // 资金不足，等待是否抵押。
    awaiting_mortgage_selection,  // 等待从列表里选抵押房产。
    awaiting_mortgage_confirm,    // 等待确认某个抵押选择。
    awaiting_airport_destination, // 到达机场，等待点击目的地。
    game_over                     // 游戏结束，显示胜利结算。
};

enum class action_id {
    select_player_count,       // 选择玩家人数按钮。
    select_initial_cash,       // 选择初始资金按钮。
    select_game_minutes,       // 选择游戏限时分钟数按钮。
    confirm_setup,             // 确认开局设置按钮。
    buy_property,              // 购买当前房产按钮。
    skip_property,             // 放弃购买当前房产按钮。
    build_house,               // 在当前房产建房按钮。
    skip_build,                // 放弃建房按钮。
    mortgage_yes,              // 进入抵押流程按钮。
    mortgage_no,               // 不抵押或取消抵押按钮。
    mortgage_cell,             // 选择某个房产作为抵押对象。
    confirm_mortgage,          // 确认抵押当前选择。
    back_to_mortgage_list,     // 返回抵押房产列表。
    cancel_airport             // 取消机场移动。
};

struct action_option {
    action_id id;          // 按钮触发的动作类型。
    int payload;           // 动作携带的参数，例如人数、金额或地块编号。
    std::wstring label;    // 按钮上显示的文字。
};

struct player {
    int id = 0;                  // 玩家编号，从 0 开始存储，显示时加 1。
    int team_index = 0;          // 玩家所属队伍编号。
    int cash = 0;                // 玩家当前手中现金。
    int position = start_index;  // 玩家当前所在地图格子编号。
};

struct team {
    int id = 0;                              // 队伍编号，从 0 开始存储，显示时加 1。
    std::vector<int> member_player_ids;      // 队伍里所有玩家的编号。
    int skip_turns = 0;                      // 因监狱等效果还需要暂停的回合数。
    int next_player_offset = 0;              // 本队下一次行动轮到哪个队员。
    bool out = false;                        // 队伍是否已经破产淘汰。
};

struct property_state {
    int owner_team = -1;  // 房产所属队伍，-1 表示无人购买。
    int houses = 0;      // 当前房产已有房屋数量。
};

// 把常量表里的 wstring_view 转成可拼接的 wstring。
std::wstring to_wide(std::wstring_view text);
// 把整数金额格式化成“xx元”。
std::wstring format_money(int amount);

}  // namespace monopoly
