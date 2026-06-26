#include "main.h"

#include "game_ui.h"

// 创建并运行游戏窗口，作为 main 调用的公共入口。
int run_game() {
    monopoly::game_ui ui;
    return ui.run();
}

// 程序入口，只负责把控制权交给游戏窗口。
int main() {
    return run_game();
}
