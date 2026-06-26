#include "game_engine.h"

#include <algorithm>
#include <chrono>
#include <numeric>
#include <sstream>

namespace monopoly {

// 初始化游戏引擎，创建日志并进入设置阶段。
game_engine::game_engine()
    : properties_(board_cell_count),
      rng_(static_cast<unsigned int>(
          std::chrono::steady_clock::now().time_since_epoch().count())) {
    logger_.start_new_game();
    rebuild_options();
    set_message(L"请选择玩家数量和初始资金。点“确认游戏信息”后才会进入开局掷骰。");
}

// 设置玩家人数，只有 setup 阶段允许调用。
void game_engine::select_player_count(int count) {
    if (phase_ != game_phase::setup) {
        register_invalid_click(L"游戏已经开始，不能重新选择玩家数量。");
        return;
    }

    selected_player_count_ = count;
    set_message(L"已选择玩家数量：" + std::to_wstring(count) + L"人。还需要选择初始资金，然后确认游戏信息。");
    rebuild_options();
}

// 设置每人初始资金，同时影响房价、建房费和过路费缩放。
void game_engine::select_initial_cash(int cash) {
    if (phase_ != game_phase::setup) {
        register_invalid_click(L"游戏已经开始，不能重新选择初始资金。");
        return;
    }

    selected_initial_cash_ = cash;
    set_message(L"已选择初始资金：每人" + format_money(cash) + L"。房价已按初始资金同步调整。");
    rebuild_options();
}

// 设置本局游戏限时分钟数。
void game_engine::select_game_minutes(int minutes) {
    if (phase_ != game_phase::setup) {
        register_invalid_click(L"游戏已经开始，不能重新选择游戏时间。");
        return;
    }

    selected_game_minutes_ = minutes;
    set_message(L"已选择游戏时间：" + std::to_wstring(minutes) + L"分钟。");
    rebuild_options();
}

// 确认开局信息，创建玩家和队伍，并进入开局掷骰阶段。
void game_engine::confirm_setup() {
    if (phase_ != game_phase::setup) {
        register_invalid_click(L"当前不能再次确认游戏信息。");
        return;
    }

    reset_game_data();
    build_players_and_teams();
    phase_ = game_phase::opening_roll;
    opening_roll_cursor_ = 0;
    opening_rolls_.assign(teams_.size(), 0);
    last_dice_ = 0;
    has_last_move_ = false;
    timer_started_ = true;
    game_start_time_ = std::chrono::steady_clock::now();

    std::wstringstream text; // 开局确认后显示和写入日志的说明文字。
    text << L"游戏信息已确认：共" << selected_player_count_ << L"名玩家，";
    text << teams_.size() << L"支队伍，每人初始资金" << selected_initial_cash_ << L"元，";
    text << L"限时" << selected_game_minutes_ << L"分钟。\n";
    text << L"现在每队派一名玩家掷骰子，点数大的队伍先行动。";
    set_message(text.str());
    logger_.log(text.str());
    rebuild_options();
}

// 检查限时是否结束，到点后直接进入最终排名。
void game_engine::update_game_time() {
    if (!timer_started_ || phase_ == game_phase::setup || phase_ == game_phase::game_over) {
        return;
    }
    if (remaining_game_seconds() <= 0) {
        finish_game_by_time();
        rebuild_options();
    }
}

// 判断当前阶段是否允许点击骰子。
bool game_engine::can_roll_dice() const {
    return phase_ == game_phase::opening_roll || phase_ == game_phase::turn_ready;
}

// 掷骰：开局阶段用于排顺序，游戏阶段用于移动玩家。
void game_engine::roll_dice() {
    if (!can_roll_dice()) {
        register_invalid_click(L"当前阶段不能投骰子。");
        return;
    }

    last_dice_ = roll_single_dice(); // 保存本次骰子结果，供界面和移动规则使用。

    if (phase_ == game_phase::opening_roll) {
        const int team_index = opening_roll_cursor_; // 当前正在开局掷骰的队伍编号。
        opening_rolls_[team_index] = last_dice_;

        std::wstring line = L"开局掷骰：" + team_name(team_index) + L"的" + // 本次开局掷骰日志。
            player_name(teams_[team_index].member_player_ids.front()) +
            L"投出了" + std::to_wstring(last_dice_) + L"点。";
        set_message(line);
        logger_.log(line);

        ++opening_roll_cursor_;
        if (opening_roll_cursor_ >= static_cast<int>(teams_.size())) {
            resolve_opening_order();
            begin_turn();
        }
        rebuild_options();
        return;
    }

    player& mover = current_player(); // 本回合实际移动的玩家。
    const int from = mover.position;  // 移动前所在格子。
    move_current_player_to((mover.position + last_dice_) % board_cell_count, true);
    last_from_ = from;
    last_to_ = mover.position;
    has_last_move_ = true;

    std::wstring move_line = player_name(mover.id) + L"投出" + std::to_wstring(last_dice_) + // 移动结果说明。
        L"点，从" + cell_name(from) + L"走到了" + cell_name(mover.position) + L"。";
    set_message(move_line);
    logger_.log(move_line);
    process_arrival(mover.position, false);
    rebuild_options();
}

// 处理按钮动作，所有按钮都先经过这里再改游戏状态。
void game_engine::handle_action(action_id id, int payload) {
    switch (id) {
    case action_id::select_player_count:
        select_player_count(payload);
        return;
    case action_id::select_initial_cash:
        select_initial_cash(payload);
        return;
    case action_id::select_game_minutes:
        select_game_minutes(payload);
        return;
    case action_id::confirm_setup:
        confirm_setup();
        return;
    default:
        break;
    }

    if (phase_ == game_phase::awaiting_property_decision) {
        if (id == action_id::buy_property) {
            const int cell_index = current_player().position; // 当前玩家停留的无人房产。
            start_payment(pending_payment_kind::buy_property, cell_price(cell_index), cell_index, -1, L"购买" + cell_name(cell_index));
        } else if (id == action_id::skip_property) {
            add_message(L"已放弃购买" + cell_name(current_player().position) + L"。");
            logger_.log(L"放弃购买：" + cell_name(current_player().position));
            finish_turn();
        } else {
            register_invalid_click(L"当前请选择是否购买房产。");
        }
        rebuild_options();
        return;
    }

    if (phase_ == game_phase::awaiting_build_decision) {
        if (id == action_id::build_house) {
            const int cell_index = current_player().position; // 当前玩家停留的本队房产。
            start_payment(pending_payment_kind::build_house, cell_build_cost(cell_index), cell_index, -1, L"在" + cell_name(cell_index) + L"建造房屋");
        } else if (id == action_id::skip_build) {
            add_message(L"已放弃本次建房。");
            logger_.log(L"放弃建房：" + cell_name(current_player().position));
            finish_turn();
        } else {
            register_invalid_click(L"当前请选择是否建房。");
        }
        rebuild_options();
        return;
    }

    if (phase_ == game_phase::awaiting_mortgage_consent) {
        if (id == action_id::mortgage_yes) {
            prompt_mortgage_selection();
        } else if (id == action_id::mortgage_no) {
            auto_mortgage_lowest_property();
        } else {
            register_invalid_click(L"当前请选择是否抵押房产。");
        }
        rebuild_options();
        return;
    }

    if (phase_ == game_phase::awaiting_mortgage_selection) {
        if (id == action_id::mortgage_cell) {
            mortgage_candidate_cell_ = payload;
            phase_ = game_phase::awaiting_mortgage_confirm;
            set_message(L"确认抵押" + cell_name(payload) + L"吗？抵押后该地块和房屋归还银行。");
        } else if (id == action_id::mortgage_no) {
            auto_mortgage_lowest_property();
        } else {
            register_invalid_click(L"请从列表中选择要抵押的房产。");
        }
        rebuild_options();
        return;
    }

    if (phase_ == game_phase::awaiting_mortgage_confirm) {
        if (id == action_id::confirm_mortgage) {
            mortgage_property(mortgage_candidate_cell_);
            mortgage_candidate_cell_ = -1;
            if (pending_payment_.kind != pending_payment_kind::none && current_player().cash >= pending_payment_.amount) {
                execute_pending_payment(false);
            } else if (pending_payment_.kind != pending_payment_kind::none) {
                if (is_mandatory_payment(pending_payment_.kind) && mortgageable_properties_for_current_team().empty()) {
                    settle_bankruptcy_payment();
                    eliminate_current_team(pending_payment_.reason + L"仍无法付清，且本队没有房产可以继续抵押。");
                    rebuild_options();
                    return;
                } else {
                    phase_ = game_phase::awaiting_mortgage_consent;
                    add_message(L"资金仍不足，请继续选择是否抵押房产。");
                }
            } else {
                finish_turn();
            }
        } else if (id == action_id::back_to_mortgage_list) {
            prompt_mortgage_selection();
        } else if (id == action_id::mortgage_no) {
            auto_mortgage_lowest_property();
        } else {
            register_invalid_click(L"请确认或返回抵押列表。");
        }
        rebuild_options();
        return;
    }

    if (phase_ == game_phase::awaiting_airport_destination) {
        if (id == action_id::cancel_airport) {
            add_message(L"已取消机场移动，本回合结束。");
            logger_.log(L"机场移动取消。");
            finish_turn();
        } else {
            register_invalid_click(L"请在地图上点击机场目的地。");
        }
        rebuild_options();
        return;
    }

    register_invalid_click(L"当前点击无效。");
}

// 处理地图点击；只有机场选择目的地时地图点击才有效。
void game_engine::handle_map_click(int cell_index) {
    if (cell_index < 0 || cell_index >= board_cell_count) {
        register_invalid_click(L"没有点击到有效地块。");
        return;
    }

    if (phase_ != game_phase::awaiting_airport_destination) {
        register_invalid_click(L"当前不能通过点击地图移动。");
        return;
    }

    player& flyer = current_player(); // 机场移动的玩家。
    const int from = flyer.position;  // 飞行前所在格子。
    flyer.position = cell_index;
    last_from_ = from;
    last_to_ = cell_index;
    has_last_move_ = true;

    std::wstring line = player_name(flyer.id) + L"从机场飞往" + cell_name(cell_index) + L"。"; // 机场移动说明。
    set_message(line);
    logger_.log(line);

    if (cell_index == airport_index) {
        add_message(L"目的地仍为机场，本回合结束。");
        finish_turn();
    } else {
        process_arrival(cell_index, true);
    }
    rebuild_options();
}

// 记录非法点击原因，让玩家知道为什么本次点击无效。
void game_engine::register_invalid_click(const std::wstring& reason) {
    if (phase_ == game_phase::game_over) {
        return;
    }
    add_message(L"无效点击：" + reason);
    logger_.log(L"无效点击：" + reason);
}

// 返回当前游戏阶段。
game_phase game_engine::phase() const {
    return phase_;
}

// 返回设置阶段选择的玩家人数。
int game_engine::selected_player_count() const {
    return selected_player_count_;
}

// 返回设置阶段选择的初始资金。
int game_engine::selected_initial_cash() const {
    return selected_initial_cash_;
}

// 返回设置阶段选择的游戏限时。
int game_engine::selected_game_minutes() const {
    return selected_game_minutes_;
}

// 返回游戏剩余秒数，设置阶段返回完整限时。
int game_engine::remaining_game_seconds() const {
    const int total_seconds = selected_game_minutes_ * 60; // 本局总秒数。
    if (!timer_started_) {
        return total_seconds;
    }

    const auto now = std::chrono::steady_clock::now(); // 当前时间点。
    const int elapsed = static_cast<int>(
        std::chrono::duration_cast<std::chrono::seconds>(now - game_start_time_).count());
    return std::max(0, total_seconds - elapsed);
}

// 返回最近一次骰子点数。
int game_engine::last_dice() const {
    return last_dice_;
}

// 返回当前行动队伍编号。
int game_engine::current_team_index() const {
    if (phase_ == game_phase::setup || phase_ == game_phase::game_over || teams_.empty()) {
        return -1;
    }
    if (phase_ == game_phase::opening_roll) {
        return std::min(opening_roll_cursor_, static_cast<int>(teams_.size()) - 1);
    }
    return current_team_from_order();
}

// 返回当前行动玩家编号。
int game_engine::current_player_id() const {
    if (phase_ == game_phase::game_over) {
        return -1;
    }
    if (phase_ == game_phase::opening_roll && !teams_.empty()) {
        const int team_index = std::min(opening_roll_cursor_, static_cast<int>(teams_.size()) - 1); // 开局阶段当前掷骰队伍。
        return teams_[team_index].member_player_ids.front();
    }
    return current_player_id_;
}

// 返回当前行动玩家现金。
int game_engine::current_player_cash() const {
    if (current_player_id_ < 0 || current_player_id_ >= static_cast<int>(players_.size())) {
        return 0;
    }
    return players_[current_player_id_].cash;
}

// 返回某地块按初始资金缩放后的价格。
int game_engine::cell_price(int cell_index) const {
    if (cell_index < 0 || cell_index >= board_cell_count) {
        return 0;
    }
    return scale_money(board_cells[cell_index].price);
}

// 返回下一次建房需要的费用，房屋越多费用越高。
int game_engine::cell_build_cost(int cell_index) const {
    if (cell_index < 0 || cell_index >= board_cell_count) {
        return 0;
    }
    const property_state& state = properties_[cell_index]; // 当前地块已有房屋状态。
    if (state.houses >= max_houses_per_property) {
        return 0;
    }
    return scale_money(board_cells[cell_index].build_cost) * (state.houses + 1);
}

// 返回某地块按初始资金缩放后的基础过路费。
int game_engine::cell_base_rent(int cell_index) const {
    if (cell_index < 0 || cell_index >= board_cell_count) {
        return 0;
    }
    return scale_money(board_cells[cell_index].base_rent);
}

// 根据基础过路费和房屋数量计算当前过路费。
int game_engine::get_rent(int cell_index) const {
    if (cell_index < 0 || cell_index >= board_cell_count || board_cells[cell_index].kind != cell_kind::property) {
        return 0;
    }

    const property_state& state = properties_[cell_index]; // 当前房屋数量影响过路费倍数。
    return cell_base_rent(cell_index) * (state.houses + 1);
}

// 统计某队当前拥有的房屋总数。
int game_engine::total_team_houses(int team_index) const {
    int total = 0; // 统计出的房屋总数。
    for (int index = 0; index < board_cell_count; ++index) {
        if (properties_[index].owner_team == team_index) {
            total += properties_[index].houses;
        }
    }
    return total;
}

// 返回指定地块的动态房产状态。
const property_state& game_engine::property_at(int cell_index) const {
    return properties_[cell_index];
}

// 返回全部玩家状态，供界面绘制棋子和资金。
const std::vector<player>& game_engine::players() const {
    return players_;
}

// 返回全部队伍状态，供界面显示队伍信息。
const std::vector<team>& game_engine::teams() const {
    return teams_;
}

// 返回当前阶段允许点击的操作按钮。
const std::vector<action_option>& game_engine::options() const {
    return options_;
}

// 组装中间输出框显示的完整文字。
std::wstring game_engine::build_info_text() const {
    std::wstringstream text; // 最终输出框文字。

    if (phase_ == game_phase::game_over) {
        return final_result_text_;
    }

    if (phase_ == game_phase::setup) {
        text << L"当前操作是：游戏设置\n";
        text << L"玩家数量：" << selected_player_count_ << L"人\n";
        text << L"初始资金：每人" << selected_initial_cash_ << L"元\n";
        text << L"游戏时间：" << selected_game_minutes_ << L"分钟\n";
        text << L"当前房价按" << selected_initial_cash_ << L"元开局缩放。\n";
    } else {
        const int team_index = current_team_index(); // 当前应显示的队伍编号。
        const int player_id = current_player_id();   // 当前应显示的玩家编号。
        if (team_index >= 0 && player_id >= 0) {
            text << L"当前操作是：" << team_name(team_index) << L"的" << player_name(player_id) << L"\n";
        } else {
            text << L"当前操作是：等待操作\n";
        }
        const int remaining = remaining_game_seconds(); // 当前剩余游戏秒数。
        text << L"剩余时间：" << remaining / 60 << L"分" << remaining % 60 << L"秒\n";
    }

    if (last_dice_ > 0) {
        text << L"当前玩家投掷出的点数是：" << last_dice_ << L"\n";
    }
    if (has_last_move_) {
        text << L"该玩家从" << cell_name(last_from_) << L"走到了" << cell_name(last_to_) << L"\n";
    }

    text << L"\n";
    const int first_message_index = std::max(0, static_cast<int>(messages_.size()) - 4); // 最近操作最多显示 4 行。
    for (int index = first_message_index; index < static_cast<int>(messages_.size()); ++index) {
        text << messages_[index] << L"\n";
    }

    if (!players_.empty()) {
        text << L"\n资金状态：\n";
        for (const team& item : teams_) {
            text << team_name(item.id) << L"：";
            if (item.out) {
                text << L"out\n";
                continue;
            }
            bool first_player = true; // 控制同队玩家之间的分隔符。
            for (int player_id : item.member_player_ids) {
                if (!first_player) {
                    text << L"；";
                }
                const player& player_item = players_[player_id];
                text << player_name(player_item.id) << L" " << player_item.cash
                     << L"元 " << cell_name(player_item.position);
                first_player = false;
            }
            if (first_player) {
                text << L"暂无玩家";
            }
            text << L"\n";
        }
    }

    if (!teams_.empty()) {
        text << L"\n队伍资产：\n";
        for (const team& item : teams_) {
            if (item.out) {
                text << team_name(item.id) << L"：【out】\n";
                continue;
            }
            int asset_count = 0;       // 本队拥有的房产数量。
            int house_count = 0;       // 本队所有房产上的房屋总数。
            std::wstringstream detail; // 本队具体房产明细。
            bool has_detail = false;   // 是否已经写入至少一块房产。
            for (int index = 0; index < board_cell_count; ++index) {
                if (properties_[index].owner_team == item.id) {
                    ++asset_count;
                    house_count += properties_[index].houses;
                    if (has_detail) {
                        detail << L"，";
                    }
                    detail << cell_name(index) << L"(" << properties_[index].houses << L"栋)";
                    has_detail = true;
                }
            }
            text << team_name(item.id) << L"：房产" << asset_count << L"处，房屋" << house_count << L"栋";
            if (item.skip_turns > 0) {
                text << L"，监狱暂停" << item.skip_turns << L"次";
            }
            if (asset_count > 0) {
                text << L"，" << detail.str();
            } else {
                text << L"，暂无房产";
            }
            text << L"\n";
        }
    }

    return text.str();
}

// 返回日志文件路径文本。
std::wstring game_engine::log_path() const {
    return logger_.path_text();
}

// 把队伍编号转换成“x队”。
std::wstring game_engine::team_name(int team_index) const {
    if (team_index < 0) {
        return L"无队伍";
    }
    return std::to_wstring(team_index + 1) + L"队";
}

// 把玩家编号转换成“x号玩家”。
std::wstring game_engine::player_name(int player_id) const {
    return std::to_wstring(player_id + 1) + L"号玩家";
}

// 根据地块编号返回地块名称。
std::wstring game_engine::cell_name(int cell_index) const {
    if (cell_index < 0 || cell_index >= board_cell_count) {
        return L"未知地块";
    }
    return to_wide(board_cells[cell_index].name);
}

// 清空上一局数据，准备按当前设置重新开局。
void game_engine::reset_game_data() {
    players_.clear();
    teams_.clear();
    turn_order_.clear();
    opening_rolls_.clear();
    properties_.assign(board_cell_count, {});
    pending_payment_ = {};
    active_order_offset_ = 0;
    current_player_id_ = -1;
    mortgage_candidate_cell_ = -1;
    timer_started_ = false;
    final_result_text_.clear();
}

// 根据人数规则创建玩家和队伍。
void game_engine::build_players_and_teams() {
    const int players_per_team = players_per_team_for_count(selected_player_count_); // 每队人数。
    const int team_count = selected_player_count_ / players_per_team;                // 队伍数量。

    teams_.resize(team_count);
    for (int team_index = 0; team_index < team_count; ++team_index) {
        teams_[team_index].id = team_index;
    }

    players_.reserve(selected_player_count_);
    for (int player_index = 0; player_index < selected_player_count_; ++player_index) {
        player item; // 即将加入 players_ 的玩家状态。
        item.id = player_index;
        item.team_index = player_index / players_per_team;
        item.cash = selected_initial_cash_;
        item.position = start_index;
        players_.push_back(item);
        teams_[item.team_index].member_player_ids.push_back(item.id);
    }
}

// 根据玩家总人数返回每队人数。
int game_engine::players_per_team_for_count(int player_count) const {
    if (player_count == 6 || player_count == 8) {
        return 2;
    }
    if (player_count == 9) {
        return 3;
    }
    return 1;
}

// 返回 1 到 6 的随机骰子点数。
int game_engine::roll_single_dice() {
    std::uniform_int_distribution<int> dist(1, 6); // 骰子只能产生 1 到 6。
    return dist(rng_);
}

// 按初始资金缩放金额，保持不同资金开局的购买能力接近。
int game_engine::scale_money(int base_amount) const {
    if (base_amount <= 0) {
        return 0;
    }

    // 5000 元开局是基准；3000/8000/10000 会等比例缩放，平均每人约能买三块地。
    int amount = base_amount * selected_initial_cash_ / 5000; // 按初始资金比例缩放后的金额。
    const int unit = 10;                                      // 金额取整到 10 元，避免显示零散数。
    amount = ((amount + unit / 2) / unit) * unit;
    return std::max(unit, amount);
}

// 判断某类付款是不是必须付款。
bool game_engine::is_mandatory_payment(pending_payment_kind kind) const {
    return kind == pending_payment_kind::rent ||
        kind == pending_payment_kind::chance_fee ||
        kind == pending_payment_kind::repair_fee;
}

// 根据开局掷骰点数生成队伍行动顺序。
void game_engine::resolve_opening_order() {
    turn_order_.resize(teams_.size());
    std::iota(turn_order_.begin(), turn_order_.end(), 0);

    // 点数相同时自动隐藏加掷，保证开局不会卡住。
    std::vector<int> tie_break(teams_.size(), 0); // 同点时的隐藏加掷点数。
    for (int& value : tie_break) {
        value = roll_single_dice();
    }

    std::sort(turn_order_.begin(), turn_order_.end(), [&](int left, int right) {
        if (opening_rolls_[left] != opening_rolls_[right]) {
            return opening_rolls_[left] > opening_rolls_[right];
        }
        if (tie_break[left] != tie_break[right]) {
            return tie_break[left] > tie_break[right];
        }
        return left < right;
    });

    std::wstringstream line; // 行动顺序说明。
    line << L"开局顺序确定：";
    for (size_t index = 0; index < turn_order_.size(); ++index) {
        if (index > 0) {
            line << L" -> ";
        }
        const int team_index = turn_order_[index]; // 当前顺序位置上的队伍编号。
        line << team_name(team_index) << L"(" << opening_rolls_[team_index] << L"点)";
    }
    add_message(line.str());
    logger_.log(line.str());
    active_order_offset_ = 0;
}

// 开始一个队伍的新回合，跳过需要暂停的队伍。
void game_engine::begin_turn() {
    if (active_team_count() <= 1) {
        finish_game_by_elimination();
        return;
    }
    if (turn_order_.empty()) {
        return;
    }

    int guard = 0; // 防止所有队伍都被暂停时死循环。
    while (guard < static_cast<int>(turn_order_.size()) + static_cast<int>(teams_.size())) {
        team& item = current_team(); // 当前尝试行动的队伍。
        if (item.out) {
            advance_team_turn();
            ++guard;
            continue;
        }
        if (item.skip_turns <= 0) {
            const int member_index = item.next_player_offset % static_cast<int>(item.member_player_ids.size()); // 本队轮到的队员序号。
            current_player_id_ = item.member_player_ids[member_index];
            phase_ = game_phase::turn_ready;
            has_last_move_ = false;
            last_dice_ = 0;
            add_message(L"轮到" + team_name(item.id) + L"的" + player_name(current_player_id_) + L"掷骰子。");
            return;
        }

        --item.skip_turns;
        std::wstring line = team_name(item.id) + L"因监狱效果暂停一次。"; // 暂停回合日志。
        set_message(line);
        logger_.log(line);
        advance_team_turn();
        ++guard;
    }

    phase_ = game_phase::turn_ready;
    current_player_id_ = teams_[current_team_from_order()].member_player_ids.front();
}

// 结束当前玩家回合，并切换到下一队。
void game_engine::finish_turn() {
    if (phase_ == game_phase::game_over) {
        return;
    }
    if (current_player_id_ >= 0) {
        team& item = teams_[players_[current_player_id_].team_index]; // 当前玩家所属队伍。
        ++item.next_player_offset;
    }

    current_player_id_ = -1;
    pending_payment_ = {};
    mortgage_candidate_cell_ = -1;
    if (active_team_count() <= 1) {
        finish_game_by_elimination();
        return;
    }
    advance_team_turn();
    begin_turn();
}

// 将行动顺序推进到下一支队伍。
void game_engine::advance_team_turn() {
    if (!turn_order_.empty()) {
        active_order_offset_ = (active_order_offset_ + 1) % static_cast<int>(turn_order_.size());
    }
}

// 返回当前行动玩家的可修改引用。
player& game_engine::current_player() {
    return players_[current_player_id_];
}

// 返回当前行动玩家的只读引用。
const player& game_engine::current_player() const {
    return players_[current_player_id_];
}

// 返回当前行动队伍的可修改引用。
team& game_engine::current_team() {
    return teams_[current_team_from_order()];
}

// 返回当前行动队伍的只读引用。
const team& game_engine::current_team() const {
    return teams_[current_team_from_order()];
}

// 根据行动顺序返回当前队伍编号。
int game_engine::current_team_from_order() const {
    if (turn_order_.empty()) {
        return -1;
    }
    return turn_order_[active_order_offset_];
}

// 清空旧消息并设置一段新消息。
void game_engine::set_message(const std::wstring& message) {
    messages_.clear();
    add_message(message);
}

// 追加输出框消息，并限制保留数量。
void game_engine::add_message(const std::wstring& message) {
    std::wstringstream input(message); // 把可能含多行的消息拆开保存。
    std::wstring line;                 // 单行消息缓存。
    while (std::getline(input, line)) {
        messages_.push_back(line);
    }
    while (messages_.size() > 18) {
        messages_.erase(messages_.begin());
    }
}

// 根据当前阶段刷新可点击按钮列表。
void game_engine::rebuild_options() {
    options_.clear();

    if (phase_ == game_phase::setup) {
        for (int count : allowed_player_counts) {
            options_.push_back({action_id::select_player_count, count, std::to_wstring(count) + L"人"});
        }
        for (int cash : allowed_initial_cash) {
            options_.push_back({action_id::select_initial_cash, cash, std::to_wstring(cash) + L"元"});
        }
        for (int minutes : allowed_game_minutes) {
            options_.push_back({action_id::select_game_minutes, minutes, std::to_wstring(minutes) + L"分钟"});
        }
        options_.push_back({action_id::confirm_setup, 0, L"开始游戏"});
        return;
    }

    if (phase_ == game_phase::awaiting_property_decision) {
        const int cell_index = current_player().position;
        options_.push_back({action_id::buy_property, cell_index, L"购买 " + cell_name(cell_index)});
        options_.push_back({action_id::skip_property, cell_index, L"暂不购买"});
        return;
    }

    if (phase_ == game_phase::awaiting_build_decision) {
        const int cell_index = current_player().position;
        options_.push_back({action_id::build_house, cell_index, L"建造一栋房屋"});
        options_.push_back({action_id::skip_build, cell_index, L"暂不建房"});
        return;
    }

    if (phase_ == game_phase::awaiting_mortgage_consent) {
        options_.push_back({action_id::mortgage_yes, 0, L"抵押房产"});
        options_.push_back({action_id::mortgage_no, 0, L"不抵押"});
        return;
    }

    if (phase_ == game_phase::awaiting_mortgage_selection) {
        for (int cell_index : mortgageable_properties_for_current_team()) {
            options_.push_back({action_id::mortgage_cell, cell_index, property_line(cell_index)});
        }
        options_.push_back({action_id::mortgage_no, 0, L"取消抵押"});
        return;
    }

    if (phase_ == game_phase::awaiting_mortgage_confirm) {
        options_.push_back({action_id::confirm_mortgage, mortgage_candidate_cell_, L"确认抵押"});
        options_.push_back({action_id::back_to_mortgage_list, 0, L"返回房产列表"});
        options_.push_back({action_id::mortgage_no, 0, L"取消抵押"});
        return;
    }

    if (phase_ == game_phase::awaiting_airport_destination) {
        options_.push_back({action_id::cancel_airport, 0, L"取消机场移动"});
    }
}

// 移动当前玩家，并在经过起点时发放奖励。
void game_engine::move_current_player_to(int target_index, bool award_passing_start) {
    player& mover = current_player(); // 被移动的玩家。
    const int from = mover.position;  // 移动前位置，用于判断是否经过起点。
    if (award_passing_start && from + last_dice_ >= board_cell_count) {
        award_start_bonus();
    }
    mover.position = target_index;
}

// 处理玩家到达某个地块后的规则。
void game_engine::process_arrival(int cell_index, bool from_airport) {
    const board_cell_def& cell = board_cells[cell_index]; // 到达地块的固定规则配置。

    if (cell.kind == cell_kind::start) {
        if (from_airport) {
            award_start_bonus();
        }
        add_message(L"到达起点。");
        logger_.log(player_name(current_player().id) + L"到达起点。");
        finish_turn();
        return;
    }

    if (cell.kind == cell_kind::chance) {
        process_chance();
        return;
    }

    if (cell.kind == cell_kind::jail) {
        current_team().skip_turns = std::max(current_team().skip_turns, 1);
        add_message(L"到达监狱，" + team_name(current_team().id) + L"全队下一次行动暂停。");
        logger_.log(team_name(current_team().id) + L"到达监狱，全队暂停一次。");
        finish_turn();
        return;
    }

    if (cell.kind == cell_kind::bank) {
        award_bank_interest();
        finish_turn();
        return;
    }

    if (cell.kind == cell_kind::airport) {
        phase_ = game_phase::awaiting_airport_destination;
        add_message(L"到达机场，请点击地图上的任意地点作为目的地。");
        logger_.log(player_name(current_player().id) + L"到达机场，等待选择目的地。");
        return;
    }

    if (cell.kind != cell_kind::property) {
        finish_turn();
        return;
    }

    property_state& state = properties_[cell_index]; // 到达房产的购买和房屋状态。
    if (state.owner_team < 0) {
        phase_ = game_phase::awaiting_property_decision;
        add_message(L"到达未购买房产：" + cell_name(cell_index) + L"，价格" + format_money(cell_price(cell_index)) + L"。");
        return;
    }

    if (state.owner_team == current_team().id) {
        if (state.houses >= max_houses_per_property) {
            add_message(L"这是本队房产，房屋已满三栋，本回合结束。");
            finish_turn();
            return;
        }
        phase_ = game_phase::awaiting_build_decision;
        add_message(L"这是本队房产，可建造一栋房屋，费用" + format_money(cell_build_cost(cell_index)) + L"。");
        return;
    }

    start_payment(pending_payment_kind::rent, get_rent(cell_index), cell_index, state.owner_team, L"支付" + cell_name(cell_index) + L"过路费");
}

// 随机抽取并执行机会卡。
void game_engine::process_chance() {
    std::uniform_int_distribution<int> dist(0, static_cast<int>(chance_cards.size()) - 1); // 在所有机会卡中随机取一张。
    const chance_card_def& card = chance_cards[dist(rng_)];                                // 本次抽到的机会卡。

    const std::wstring chance_prefix = L"机会卡：" + to_wide(card.title) + L"，" + to_wide(card.description); // 合并显示机会卡效果。
    logger_.log(L"机会卡：" + to_wide(card.title) + L" - " + to_wide(card.description));

    switch (card.effect) {
    case chance_effect::gain_money:
        current_player().cash += card.amount;
        add_message(chance_prefix + L"结果：" + player_name(current_player().id) + L"获得" + format_money(card.amount) + L"。");
        logger_.log(player_name(current_player().id) + L"获得机会卡现金" + format_money(card.amount));
        finish_turn();
        return;
    case chance_effect::lose_money:
        add_message(chance_prefix);
        start_payment(pending_payment_kind::chance_fee, card.amount, current_player().position, -1, L"机会卡支出");
        return;
    case chance_effect::move_to_start:
        current_player().position = start_index;
        last_to_ = start_index;
        add_message(chance_prefix + L"结果：" + player_name(current_player().id) + L"回到起点。");
        award_start_bonus();
        finish_turn();
        return;
    case chance_effect::go_to_jail:
        current_player().position = jail_index;
        last_to_ = jail_index;
        current_team().skip_turns = std::max(current_team().skip_turns, 1);
        add_message(chance_prefix + L"结果：前往监狱，" + team_name(current_team().id) + L"下一次行动暂停。");
        logger_.log(player_name(current_player().id) + L"因机会卡前往监狱。");
        finish_turn();
        return;
    case chance_effect::gain_cash_percent: {
        const int gain = current_player().cash * card.amount / 100; // 按玩家当前现金计算的收益。
        current_player().cash += gain;
        add_message(chance_prefix + L"结果：获得现金收益" + format_money(gain) + L"。");
        logger_.log(player_name(current_player().id) + L"获得机会卡收益" + format_money(gain));
        finish_turn();
        return;
    }
    case chance_effect::repair_fee_per_house: {
        const int fee = total_team_houses(current_team().id) * card.amount; // 按本队房屋数量计算的维护费。
        if (fee <= 0) {
            add_message(chance_prefix + L"结果：本队暂无房屋，不需要支付维护费。");
            finish_turn();
            return;
        }
        add_message(chance_prefix);
        start_payment(pending_payment_kind::repair_fee, fee, current_player().position, -1, L"机会卡房屋维护费");
        return;
    }
    case chance_effect::move_to_bank:
        current_player().position = bank_index;
        last_to_ = bank_index;
        add_message(chance_prefix + L"结果：" + player_name(current_player().id) + L"前往银行。");
        award_bank_interest();
        finish_turn();
        return;
    default:
        finish_turn();
        return;
    }
}

// 发放通过或到达起点的奖励。
void game_engine::award_start_bonus() {
    const int bonus = selected_initial_cash_ / 10; // 起点奖励为每人初始资金的十分之一。
    current_player().cash += bonus;
    add_message(L"通过或到达起点，获得" + format_money(bonus) + L"。");
    logger_.log(player_name(current_player().id) + L"获得起点奖励" + format_money(bonus));
}

// 发放到达银行时的利息。
void game_engine::award_bank_interest() {
    const int interest = current_player().cash / 10; // 银行利息为当前手中现金的十分之一。
    current_player().cash += interest;
    add_message(L"到达银行，获得当前资金十分之一的利息：" + format_money(interest) + L"。");
    logger_.log(player_name(current_player().id) + L"获得银行利息" + format_money(interest));
}

// 开始一笔付款；现金不足则进入抵押流程。
void game_engine::start_payment(
    pending_payment_kind kind,
    int amount,
    int cell_index,
    int receiver_team,
    const std::wstring& reason) {
    pending_payment_ = {kind, amount, cell_index, receiver_team, reason};
    if (current_player().cash >= amount) {
        execute_pending_payment(false);
        return;
    }

    if (mortgageable_properties_for_current_team().empty()) {
        if (is_mandatory_payment(kind)) {
            settle_bankruptcy_payment();
            eliminate_current_team(reason + L"无法付清，且本队没有房产可以抵押。");
            return;
        }

        add_message(reason + L"需要" + format_money(amount) + L"，当前现金不足，且本队没有可抵押房产，本次操作失败。");
        logger_.log(player_name(current_player().id) + L"现金不足，未完成：" + reason);
        pending_payment_ = {};
        finish_turn();
        return;
    }

    phase_ = game_phase::awaiting_mortgage_consent;
    add_message(reason + L"需要" + format_money(amount) + L"，当前现金不足。请选择是否抵押房产。");
    logger_.log(player_name(current_player().id) + L"现金不足，需要处理：" + reason);
}

// 执行待付款事项，allow_partial 为 true 时允许现金不足后付清现有现金。
void game_engine::execute_pending_payment(bool allow_partial) {
    if (pending_payment_.kind == pending_payment_kind::none) {
        finish_turn();
        return;
    }

    player& payer = current_player();                 // 需要付款的当前玩家。
    int amount_to_pay = pending_payment_.amount;      // 本次实际尝试支付的金额。
    bool partial = false;                             // 是否因为现金不足只支付了部分金额。

    if (payer.cash < amount_to_pay) {
        if (!allow_partial) {
            phase_ = game_phase::awaiting_mortgage_consent;
            add_message(L"资金仍不足，请继续处理抵押。");
            return;
        }
        amount_to_pay = payer.cash;
        partial = true;
    }

    switch (pending_payment_.kind) {
    case pending_payment_kind::buy_property:
        if (partial) {
            add_message(L"资金不足，未能购买房产。");
            logger_.log(player_name(payer.id) + L"因资金不足未购买" + cell_name(pending_payment_.cell_index));
            break;
        }
        payer.cash -= amount_to_pay;
        properties_[pending_payment_.cell_index].owner_team = current_team().id;
        properties_[pending_payment_.cell_index].houses = 0;
        add_message(player_name(payer.id) + L"花费" + format_money(amount_to_pay) + L"购买了" + cell_name(pending_payment_.cell_index) + L"。");
        logger_.log(player_name(payer.id) + L"购买房产：" + cell_name(pending_payment_.cell_index));
        break;
    case pending_payment_kind::build_house:
        if (partial) {
            add_message(L"资金不足，未能建房。");
            logger_.log(player_name(payer.id) + L"因资金不足未能在" + cell_name(pending_payment_.cell_index) + L"建房");
            break;
        }
        payer.cash -= amount_to_pay;
        ++properties_[pending_payment_.cell_index].houses;
        add_message(player_name(payer.id) + L"花费" + format_money(amount_to_pay) + L"在" + cell_name(pending_payment_.cell_index) + L"建造了一栋房屋。");
        logger_.log(player_name(payer.id) + L"建房：" + cell_name(pending_payment_.cell_index));
        break;
    case pending_payment_kind::rent:
        payer.cash -= amount_to_pay;
        split_income_to_team(pending_payment_.receiver_team, amount_to_pay, L"过路费收入");
        add_message(player_name(payer.id) + L"支付过路费" + format_money(amount_to_pay) + L"给" + team_name(pending_payment_.receiver_team) + L"。");
        if (partial) {
            add_message(L"现金不足，只支付了当前持有的全部现金。");
        }
        logger_.log(player_name(payer.id) + L"支付过路费" + format_money(amount_to_pay));
        break;
    case pending_payment_kind::chance_fee:
    case pending_payment_kind::repair_fee:
        payer.cash -= amount_to_pay;
        add_message(player_name(payer.id) + L"因" + pending_payment_.reason + L"支出" + format_money(amount_to_pay) + L"。");
        if (partial) {
            add_message(L"现金不足，只支付了当前持有的全部现金。");
        }
        logger_.log(player_name(payer.id) + L"支出" + format_money(amount_to_pay) + L"：" + pending_payment_.reason);
        break;
    default:
        break;
    }

    pending_payment_ = {};
    finish_turn();
}

// 玩家不手动选房产时，自动抵押本队抵押价值最低的房产。
void game_engine::auto_mortgage_lowest_property() {
    if (pending_payment_.kind == pending_payment_kind::none) {
        finish_turn();
        return;
    }

    if (!is_mandatory_payment(pending_payment_.kind)) {
        execute_pending_payment(true);
        return;
    }

    const int cell_index = lowest_mortgageable_property_for_current_team(); // 自动选中的最低价值房产。
    if (cell_index < 0) {
        settle_bankruptcy_payment();
        eliminate_current_team(pending_payment_.reason + L"无法付清，且本队没有房产可以抵押。");
        return;
    }

    add_message(L"未手动选择抵押房产，自动抵押价值最低的" + cell_name(cell_index) + L"。");
    logger_.log(L"自动抵押价值最低房产：" + cell_name(cell_index));
    mortgage_property(cell_index);

    if (pending_payment_.kind == pending_payment_kind::none) {
        finish_turn();
        return;
    }
    if (current_player().cash >= pending_payment_.amount) {
        execute_pending_payment(false);
        return;
    }
    if (mortgageable_properties_for_current_team().empty()) {
        settle_bankruptcy_payment();
        eliminate_current_team(pending_payment_.reason + L"仍无法付清，且本队没有房产可以继续抵押。");
        return;
    }

    phase_ = game_phase::awaiting_mortgage_consent;
    add_message(L"自动抵押后资金仍不足，请继续选择是否抵押房产。");
}

// 找出当前队伍可抵押房产中抵押价值最低的一块。
int game_engine::lowest_mortgageable_property_for_current_team() const {
    const std::vector<int> cells = mortgageable_properties_for_current_team(); // 当前队伍可抵押房产列表。
    if (cells.empty()) {
        return -1;
    }

    int selected_cell = cells.front(); // 当前找到的最低价值房产。
    int selected_value = mortgage_value(selected_cell); // 当前最低抵押价值。
    for (int cell_index : cells) {
        const int value = mortgage_value(cell_index);
        if (value < selected_value || (value == selected_value && cell_index < selected_cell)) {
            selected_cell = cell_index;
            selected_value = value;
        }
    }
    return selected_cell;
}

// 破产前先处理当前玩家手中已有现金，队友现金不会参与补给。
void game_engine::settle_bankruptcy_payment() {
    if (pending_payment_.kind == pending_payment_kind::none || current_player_id_ < 0) {
        return;
    }

    player& payer = current_player(); // 破产付款人。
    const int available = std::max(0, payer.cash); // 该玩家手里还能拿出的现金。
    if (available <= 0) {
        return;
    }

    if (pending_payment_.kind == pending_payment_kind::rent) {
        split_income_to_team(pending_payment_.receiver_team, available, L"破产前过路费收入");
        add_message(player_name(payer.id) + L"破产前支付剩余现金" + format_money(available) + L"作为过路费。");
    } else if (is_mandatory_payment(pending_payment_.kind)) {
        add_message(player_name(payer.id) + L"破产前向银行支付剩余现金" + format_money(available) + L"。");
    }

    payer.cash = 0;
}

// 淘汰当前队伍，清除棋盘棋子，并进入下一队或直接结算。
void game_engine::eliminate_current_team(const std::wstring& reason) {
    if (current_player_id_ < 0) {
        return;
    }

    const int team_index = players_[current_player_id_].team_index; // 本次被淘汰的队伍。
    team& item = teams_[team_index];
    item.out = true;
    item.skip_turns = 0;

    for (int player_id : item.member_player_ids) {
        players_[player_id].cash = 0;
        players_[player_id].position = -1;
    }
    for (property_state& state : properties_) {
        if (state.owner_team == team_index) {
            state.owner_team = -1;
            state.houses = 0;
        }
    }

    std::wstring line = team_name(team_index) + L"【out】：" + reason;
    set_message(line);
    logger_.log(line);

    pending_payment_ = {};
    mortgage_candidate_cell_ = -1;
    current_player_id_ = -1;
    remove_team_from_turn_order(team_index);

    if (active_team_count() <= 1) {
        finish_game_by_elimination();
        return;
    }

    begin_turn();
}

// 把淘汰队伍从行动顺序中删除。
void game_engine::remove_team_from_turn_order(int team_index) {
    const auto position = std::find(turn_order_.begin(), turn_order_.end(), team_index);
    if (position == turn_order_.end()) {
        return;
    }

    const int removed_index = static_cast<int>(std::distance(turn_order_.begin(), position)); // 被删队伍原来的顺序位置。
    turn_order_.erase(position);
    if (turn_order_.empty()) {
        active_order_offset_ = 0;
        return;
    }
    if (removed_index < active_order_offset_) {
        --active_order_offset_;
    }
    if (active_order_offset_ >= static_cast<int>(turn_order_.size())) {
        active_order_offset_ = 0;
    }
}

// 统计尚未 out 的队伍数。
int game_engine::active_team_count() const {
    int count = 0; // 仍在游戏中的队伍数量。
    for (const team& item : teams_) {
        if (!item.out) {
            ++count;
        }
    }
    return count;
}

// 只剩一支队伍时结束游戏。
void game_engine::finish_game_by_elimination() {
    if (phase_ == game_phase::game_over || teams_.empty()) {
        return;
    }

    final_result_text_ = build_final_result_text(L"游戏结束：其他队伍已经out。");
    messages_.clear();
    options_.clear();
    pending_payment_ = {};
    current_player_id_ = -1;
    phase_ = game_phase::game_over;
    logger_.log(final_result_text_);
}

// 限时时间到时结束游戏。
void game_engine::finish_game_by_time() {
    if (phase_ == game_phase::game_over) {
        return;
    }

    final_result_text_ = build_final_result_text(L"游戏时间到，开始按财富折算排名。");
    messages_.clear();
    options_.clear();
    pending_payment_ = {};
    current_player_id_ = -1;
    phase_ = game_phase::game_over;
    logger_.log(final_result_text_);
}

// 生成最终排名和每队财富折算过程。
std::wstring game_engine::build_final_result_text(const std::wstring& title) const {
    struct team_score {
        int team_index = 0;        // 队伍编号。
        int cash = 0;              // 队员手中现金总和。
        int property_value = 0;    // 地产总价值。
        int score_tenths = 0;      // 折算总额，单位是十分之一元。
        bool out = false;          // 是否已经淘汰。
    };

    std::vector<team_score> scores; // 所有队伍的结算数据。
    for (const team& item : teams_) {
        team_score score;
        score.team_index = item.id;
        score.cash = item.out ? 0 : team_cash_total(item.id);
        score.property_value = item.out ? 0 : team_property_total_value(item.id);
        score.score_tenths = score.cash * 10 + score.property_value * 12;
        score.out = item.out;
        scores.push_back(score);
    }

    std::sort(scores.begin(), scores.end(), [](const team_score& left, const team_score& right) {
        if (left.out != right.out) {
            return !left.out;
        }
        if (left.score_tenths != right.score_tenths) {
            return left.score_tenths > right.score_tenths;
        }
        return left.team_index < right.team_index;
    });

    std::wstringstream text; // 最终显示的结算文字。
    text << title << L"\n\n";
    if (!scores.empty() && !scores.front().out) {
        text << L"胜利队伍：" << team_name(scores.front().team_index) << L"\n\n";
    }
    text << L"折算公式：队伍玩家手上剩余钱财之和×1 + 所买地产总价值×1.2\n";
    text << L"地产总价值按地价加已建房投入计算。\n\n";
    text << L"排名：\n";

    int rank = 1; // 当前名次。
    for (const team_score& score : scores) {
        if (score.out) {
            text << team_name(score.team_index) << L"：【out】\n";
            continue;
        }
        text << rank << L". " << team_name(score.team_index)
             << L"：现金" << score.cash << L"×1 + 地产" << score.property_value
             << L"×1.2 = " << format_score_tenths(score.score_tenths) << L"\n";
        ++rank;
    }
    return text.str();
}

// 统计某队所有未淘汰成员现金。
int game_engine::team_cash_total(int team_index) const {
    int total = 0; // 队伍现金总和。
    for (const player& item : players_) {
        if (item.team_index == team_index) {
            total += item.cash;
        }
    }
    return total;
}

// 统计某队拥有地产的总价值。
int game_engine::team_property_total_value(int team_index) const {
    int total = 0; // 地价加建房投入。
    for (int index = 0; index < board_cell_count; ++index) {
        if (properties_[index].owner_team == team_index) {
            total += property_total_investment(index);
        }
    }
    return total;
}

// 计算某块地产的地价和全部房屋投入。
int game_engine::property_total_investment(int cell_index) const {
    if (cell_index < 0 || cell_index >= board_cell_count) {
        return 0;
    }

    const property_state& state = properties_[cell_index]; // 地块当前房屋数量。
    int total = cell_price(cell_index);
    const int base_build_cost = scale_money(board_cells[cell_index].build_cost);
    for (int index = 0; index < state.houses; ++index) {
        total += base_build_cost * (index + 1);
    }
    return total;
}

// 把十分之一元转成保留一位小数的文本。
std::wstring game_engine::format_score_tenths(int score_tenths) const {
    std::wstringstream text;
    text << score_tenths / 10 << L"." << score_tenths % 10 << L"元";
    return text.str();
}

// 进入抵押房产选择阶段。
void game_engine::prompt_mortgage_selection() {
    const std::vector<int> cells = mortgageable_properties_for_current_team(); // 当前队伍可抵押房产列表。
    if (cells.empty()) {
        if (is_mandatory_payment(pending_payment_.kind)) {
            settle_bankruptcy_payment();
            eliminate_current_team(pending_payment_.reason + L"无法付清，且本队没有房产可以抵押。");
            return;
        }
        phase_ = game_phase::awaiting_mortgage_consent;
        add_message(L"本队没有可抵押房产。");
        return;
    }

    phase_ = game_phase::awaiting_mortgage_selection;
    set_message(L"请选择要抵押给银行的房产。抵押金额为该地块总投入的30%，收益由队员平分。");
}

// 抵押指定房产，房产和房屋归还银行，抵押收入队员平分。
void game_engine::mortgage_property(int cell_index) {
    if (cell_index < 0 || cell_index >= board_cell_count || properties_[cell_index].owner_team != current_team().id) {
        add_message(L"抵押失败：该房产不属于当前队伍。");
        return;
    }

    const int value = mortgage_value(cell_index);         // 银行返还的抵押金额。
    const int old_houses = properties_[cell_index].houses; // 抵押前房屋数量，用于说明。
    properties_[cell_index].owner_team = -1;
    properties_[cell_index].houses = 0;
    split_income_to_team(current_team().id, value, L"抵押收入");

    std::wstringstream line; // 抵押结果说明。
    line << team_name(current_team().id) << L"抵押" << cell_name(cell_index)
         << L"，原有房屋" << old_houses << L"栋，获得" << value << L"元并由队员平分。";
    add_message(line.str());
    logger_.log(line.str());
}

// 计算抵押金额，按地价和递增建房投入的 30% 返还。
int game_engine::mortgage_value(int cell_index) const {
    if (cell_index < 0 || cell_index >= board_cell_count) {
        return 0;
    }
    const property_state& state = properties_[cell_index]; // 被抵押房产的当前状态。
    int total_build_cost = 0;                                      // 已建房屋的总投入。
    const int base_build_cost = scale_money(board_cells[cell_index].build_cost); // 第一栋房的基础费用。
    for (int index = 0; index < state.houses; ++index) {
        total_build_cost += base_build_cost * (index + 1);
    }
    return (cell_price(cell_index) + total_build_cost) * 30 / 100;
}

// 把一笔收益按队员人数平分。
void game_engine::split_income_to_team(int team_index, int amount, const std::wstring& reason) {
    if (team_index < 0 || team_index >= static_cast<int>(teams_.size()) || amount <= 0) {
        return;
    }

    const team& item = teams_[team_index];                              // 收益所属队伍。
    const int member_count = static_cast<int>(item.member_player_ids.size()); // 队员人数。
    const int base = amount / member_count;                             // 每名队员至少分到的金额。
    int remainder = amount % member_count;                              // 无法整除时剩余的钱，从前面的队员开始补。
    for (int player_id : item.member_player_ids) {
        int gain = base; // 当前队员本次实际获得的钱。
        if (remainder > 0) {
            ++gain;
            --remainder;
        }
        players_[player_id].cash += gain;
        logger_.log(player_name(player_id) + L"获得" + reason + format_money(gain));
    }
}

// 生成抵押列表中的单行房产文字。
std::wstring game_engine::property_line(int cell_index) const {
    std::wstringstream text; // 按钮上显示的抵押房产文字。
    text << cell_name(cell_index) << L" 抵押" << mortgage_value(cell_index) << L"元";
    return text.str();
}

// 找出当前队伍所有可抵押房产。
std::vector<int> game_engine::mortgageable_properties_for_current_team() const {
    std::vector<int> result;              // 最终返回的可抵押地块编号列表。
    const int team_index = current_team().id; // 当前操作队伍编号。
    for (int index = 0; index < board_cell_count; ++index) {
        if (properties_[index].owner_team == team_index) {
            result.push_back(index);
        }
    }
    return result;
}

}  // namespace monopoly
