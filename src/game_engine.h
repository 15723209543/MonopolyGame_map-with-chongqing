#pragma once

#include "game_logger.h"
#include "models.h"

#include <chrono>
#include <random>

namespace monopoly {

enum class pending_payment_kind {
    none,           // 当前没有待处理付款。
    buy_property,   // 购买房产付款。
    build_house,    // 建房付款。
    rent,           // 支付过路费。
    chance_fee,     // 机会卡导致的付款。
    repair_fee      // 房屋维护费。
};

struct pending_payment {
    pending_payment_kind kind = pending_payment_kind::none;  // 待付款类型。
    int amount = 0;                                          // 需要支付的金额。
    int cell_index = -1;                                     // 付款关联的地块编号。
    int receiver_team = -1;                                  // 收款队伍，-1 表示交给银行。
    std::wstring reason;                                     // 右侧说明和日志里使用的付款原因。
};

class game_engine {
public:
    // 构造游戏引擎，初始化日志、地图房产状态和设置阶段按钮。
    game_engine();

    // 记录玩家人数选择，只能在游戏设置阶段使用。
    void select_player_count(int count);
    // 记录初始资金选择，并让房价、建房费和过路费按比例变化。
    void select_initial_cash(int cash);
    // 记录游戏限时分钟数。
    void select_game_minutes(int minutes);
    // 确认开局信息，创建玩家队伍并进入开局掷骰。
    void confirm_setup();
    // 检查游戏限时是否到达，到时自动结算。
    void update_game_time();
    // 判断当前阶段能不能点击骰子。
    bool can_roll_dice() const;
    // 执行一次骰子结果，开局用于排序，正式回合用于移动。
    void roll_dice();
    // 处理按钮点击动作。
    void handle_action(action_id id, int payload);
    // 处理地图点击，主要用于机场选择目的地。
    void handle_map_click(int cell_index);
    // 记录非法点击原因并显示到输出框。
    void register_invalid_click(const std::wstring& reason);

    // 返回当前游戏阶段。
    game_phase phase() const;
    // 返回当前选择的玩家总人数。
    int selected_player_count() const;
    // 返回当前选择的每人初始资金。
    int selected_initial_cash() const;
    // 返回当前选择的游戏限时分钟数。
    int selected_game_minutes() const;
    // 返回距离限时结束还剩多少秒。
    int remaining_game_seconds() const;
    // 返回最近一次骰子点数。
    int last_dice() const;
    // 返回当前操作队伍编号。
    int current_team_index() const;
    // 返回当前操作玩家编号。
    int current_player_id() const;
    // 返回当前操作玩家现金。
    int current_player_cash() const;
    // 返回指定地块的当前价格。
    int cell_price(int cell_index) const;
    // 返回指定地块下一次建房费用。
    int cell_build_cost(int cell_index) const;
    // 返回指定地块基础过路费。
    int cell_base_rent(int cell_index) const;
    // 返回指定地块当前过路费。
    int get_rent(int cell_index) const;
    // 统计指定队伍的房屋总数。
    int total_team_houses(int team_index) const;
    // 返回指定地块的房产归属和房屋状态。
    const property_state& property_at(int cell_index) const;
    // 返回所有玩家状态。
    const std::vector<player>& players() const;
    // 返回所有队伍状态。
    const std::vector<team>& teams() const;
    // 返回当前可点击按钮列表。
    const std::vector<action_option>& options() const;
    // 生成中央输出框显示文字。
    std::wstring build_info_text() const;
    // 返回当前日志文件路径。
    std::wstring log_path() const;
    // 把队伍编号转成显示名称。
    std::wstring team_name(int team_index) const;
    // 把玩家编号转成显示名称。
    std::wstring player_name(int player_id) const;
    // 把地块编号转成地块名称。
    std::wstring cell_name(int cell_index) const;

private:
    // 清空上一局动态数据。
    void reset_game_data();
    // 按人数规则创建玩家和队伍。
    void build_players_and_teams();
    // 按玩家总数计算每队人数。
    int players_per_team_for_count(int player_count) const;
    // 生成 1 到 6 的骰子点数。
    int roll_single_dice();
    // 把基础金额按初始资金比例缩放。
    int scale_money(int base_amount) const;
    // 判断待付款类型是不是必须支付的费用。
    bool is_mandatory_payment(pending_payment_kind kind) const;
    // 根据开局掷骰结果排出行动顺序。
    void resolve_opening_order();
    // 开始新回合，处理监狱暂停。
    void begin_turn();
    // 结束当前回合并推进到下一队。
    void finish_turn();
    // 将行动顺序推进一格。
    void advance_team_turn();
    // 返回当前玩家的可修改状态。
    player& current_player();
    // 返回当前玩家的只读状态。
    const player& current_player() const;
    // 返回当前队伍的可修改状态。
    team& current_team();
    // 返回当前队伍的只读状态。
    const team& current_team() const;
    // 从行动顺序中取当前队伍编号。
    int current_team_from_order() const;
    // 清空旧输出并放入新消息。
    void set_message(const std::wstring& message);
    // 追加一条或多条输出消息。
    void add_message(const std::wstring& message);
    // 按当前阶段重建按钮列表。
    void rebuild_options();
    // 移动当前玩家，并处理经过起点奖励。
    void move_current_player_to(int target_index, bool award_passing_start);
    // 处理玩家到达地块后的规则。
    void process_arrival(int cell_index, bool from_airport);
    // 抽取并执行机会卡。
    void process_chance();
    // 发放起点奖励。
    void award_start_bonus();
    // 发放银行利息。
    void award_bank_interest();
    // 开始付款流程，现金不足时转入抵押。
    void start_payment(pending_payment_kind kind, int amount, int cell_index, int receiver_team, const std::wstring& reason);
    // 执行 pending_payment_ 中保存的付款。
    void execute_pending_payment(bool allow_partial);
    // 未手动选择抵押房产时，自动抵押价值最低的房产。
    void auto_mortgage_lowest_property();
    // 找出当前队伍抵押价值最低的房产。
    int lowest_mortgageable_property_for_current_team() const;
    // 当前玩家无力付款时，先交出已有现金。
    void settle_bankruptcy_payment();
    // 淘汰当前队伍并清理棋盘状态。
    void eliminate_current_team(const std::wstring& reason);
    // 从行动顺序里移除指定队伍。
    void remove_team_from_turn_order(int team_index);
    // 统计仍未淘汰的队伍数量。
    int active_team_count() const;
    // 因只剩一队而结束游戏。
    void finish_game_by_elimination();
    // 因限时时间到而结束游戏。
    void finish_game_by_time();
    // 生成最终排名和财富折算说明。
    std::wstring build_final_result_text(const std::wstring& title) const;
    // 统计某队玩家手中现金总和。
    int team_cash_total(int team_index) const;
    // 统计某队地产总价值，含地价和建房投入。
    int team_property_total_value(int team_index) const;
    // 统计某块地产总投入。
    int property_total_investment(int cell_index) const;
    // 把十分之一元单位的分数格式化成一位小数。
    std::wstring format_score_tenths(int score_tenths) const;
    // 进入抵押房产选择阶段。
    void prompt_mortgage_selection();
    // 抵押指定地块。
    void mortgage_property(int cell_index);
    // 计算指定地块抵押金额。
    int mortgage_value(int cell_index) const;
    // 把收益按队员平分。
    void split_income_to_team(int team_index, int amount, const std::wstring& reason);
    // 生成抵押按钮上的房产说明。
    std::wstring property_line(int cell_index) const;
    // 找出当前队伍能抵押的所有房产。
    std::vector<int> mortgageable_properties_for_current_team() const;

    game_phase phase_ = game_phase::setup;               // 当前游戏阶段，用来保护非法点击。
    int selected_player_count_ = 2;                      // 设置阶段选择的玩家总人数。
    int selected_initial_cash_ = 3000;                   // 设置阶段选择的每人初始资金。
    int selected_game_minutes_ = 10;                     // 设置阶段选择的游戏限时分钟数。
    int last_dice_ = 0;                                  // 最近一次骰子点数。
    int active_order_offset_ = 0;                        // turn_order_ 中当前行动队伍的位置。
    int current_player_id_ = -1;                         // 当前正在操作的玩家编号。
    int opening_roll_cursor_ = 0;                        // 开局掷骰阶段正在处理的队伍下标。
    int mortgage_candidate_cell_ = -1;                   // 抵押确认阶段临时选中的地块编号。
    bool has_last_move_ = false;                         // 是否有上一段移动信息可显示。
    int last_from_ = start_index;                        // 最近一次移动的起点地块。
    int last_to_ = start_index;                          // 最近一次移动的终点地块。
    bool timer_started_ = false;                         // 游戏限时计时器是否已经开始。
    std::chrono::steady_clock::time_point game_start_time_{}; // 游戏限时开始时间。

    std::vector<player> players_;                        // 全部玩家状态。
    std::vector<team> teams_;                            // 全部队伍状态。
    std::vector<int> turn_order_;                        // 队伍行动顺序，存队伍编号。
    std::vector<int> opening_rolls_;                     // 开局每队掷出的点数。
    std::vector<property_state> properties_;             // 每个地块的归属和房屋状态。
    std::vector<std::wstring> messages_;                 // 输出框中的最近操作说明。
    std::vector<action_option> options_;                 // 当前阶段允许点击的操作按钮。
    std::wstring final_result_text_;                     // 游戏结束时中间区域显示的结算文本。
    pending_payment pending_payment_;                    // 资金不足时保留的待支付事项。
    game_logger logger_;                                 // 负责把游戏全过程写入 result 日志。
    std::mt19937 rng_;                                   // 随机数生成器，用于骰子和机会卡。
};

}  // namespace monopoly
