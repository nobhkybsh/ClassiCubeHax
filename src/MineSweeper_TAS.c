#include "MineSweeper_TAS.h"
#include "World.h"
#include "Game.h"
#include "Entity.h"
#include "Input.h"
#include "Chat.h"
#include "Picking.h"
#include "Logger.h"
#include <stdlib.h>
#include <string.h>

struct MineSweeper_TAS MineSweeper = { 0 };

static int MineSweeper_BlockToValue(BlockID block) {
	switch (block) {
		case MINESWEEPER_EMPTY: return 0;
		case MINESWEEPER_1:     return 1;
		case MINESWEEPER_2:     return 2;
		case MINESWEEPER_3:     return 3;
		case MINESWEEPER_4:     return 4;
		case MINESWEEPER_5:     return 5;
		case MINESWEEPER_6:     return 6;
		case MINESWEEPER_7:     return 7;
		case MINESWEEPER_8:     return 8;
		default:                return -1;
	}
}

static cc_bool MineSweeper_IsValidCell(int x, int z) {
	return x >= 0 && x < MINESWEEPER_WIDTH && z >= 0 && z < MINESWEEPER_LENGTH;
}

static int MineSweeper_GetCellIndex(int x, int z) {
	return z * MINESWEEPER_WIDTH + x;
}

static struct MineSweeper_Cell* MineSweeper_GetCell(int x, int z) {
	if (!MineSweeper_IsValidCell(x, z)) return NULL;
	return &MineSweeper.board[MineSweeper_GetCellIndex(x, z)];
}

/* Получение значения соседних клеток */
static int MineSweeper_CountRevealed(int x, int z) {
	int count = 0;
	int dx, dz;
	struct MineSweeper_Cell* cell;
	
	for (dx = -1; dx <= 1; dx++) {
		for (dz = -1; dz <= 1; dz++) {
			if (dx == 0 && dz == 0) continue;
			cell = MineSweeper_GetCell(x + dx, z + dz);
			if (cell && cell->is_revealed && !cell->is_mine) count++;
		}
	}
	return count;
}

static int MineSweeper_CountFlagged(int x, int z) {
	int count = 0;
	int dx, dz;
	struct MineSweeper_Cell* cell;
	
	for (dx = -1; dx <= 1; dx++) {
		for (dz = -1; dz <= 1; dz++) {
			if (dx == 0 && dz == 0) continue;
			cell = MineSweeper_GetCell(x + dx, z + dz);
			if (cell && cell->is_flagged) count++;
		}
	}
	return count;
}

static int MineSweeper_CountUnrevealed(int x, int z) {
	int count = 0;
	int dx, dz;
	struct MineSweeper_Cell* cell;
	
	for (dx = -1; dx <= 1; dx++) {
		for (dz = -1; dz <= 1; dz++) {
			if (dx == 0 && dz == 0) continue;
			cell = MineSweeper_GetCell(x + dx, z + dz);
			if (cell && !cell->is_revealed && !cell->is_flagged) count++;
		}
	}
	return count;
}

void MineSweeper_Init(void) {
	Mem_Set(&MineSweeper, 0, sizeof(MineSweeper));
	MineSweeper.state = MS_STATE_INACTIVE;
	MineSweeper.update_timer = 0.5f; /* Задержка перед первым кликом */
	
	/* Инициализация клеток */
	int i;
	for (i = 0; i < MINESWEEPER_WIDTH * MINESWEEPER_LENGTH; i++) {
		MineSweeper.board[i].x = i % MINESWEEPER_WIDTH;
		MineSweeper.board[i].z = i / MINESWEEPER_WIDTH;
		MineSweeper.board[i].value = -1;
		MineSweeper.board[i].is_mine = false;
		MineSweeper.board[i].is_revealed = false;
		MineSweeper.board[i].is_flagged = false;
	}
	
	Chat_AddRaw("&aMineSweeper TAS initialized. Press Numpad 5 to activate.");
}

void MineSweeper_Activate(void) {
	if (MineSweeper.active) return;
	
	MineSweeper.active = true;
	MineSweeper.state = MS_STATE_SCANNING;
	MineSweeper.update_timer = 0.2f;
	MineSweeper.has_next_click = false;
	
	/* Получить координаты центра из позиции игрока */
	struct Entity* player = &Entities.CurPlayer->Base;
	MineSweeper.center_x = (int)player->Position.x;
	MineSweeper.center_z = (int)player->Position.z;
	MineSweeper.center_y = (int)player->Position.y;
	
	Chat_AddRaw("&eMineSweeper TAS &aACTIVATED");
	MineSweeper_ScanBoard();
}

void MineSweeper_Deactivate(void) {
	MineSweeper.active = false;
	MineSweeper.state = MS_STATE_INACTIVE;
	Chat_AddRaw("&eMineSweeper TAS &cDEACTIVATED");
}

void MineSweeper_ScanBoard(void) {
	int x, z;
	BlockID block;
	struct MineSweeper_Cell* cell;
	int world_x, world_y, world_z;
	int value;
	
	/* Платформа 9x1x9 находится в центре */
	int start_x = MineSweeper.center_x - 4;
	int start_z = MineSweeper.center_z - 4;
	int board_y = MineSweeper.center_y;
	
	for (z = 0; z < MINESWEEPER_LENGTH; z++) {
		for (x = 0; x < MINESWEEPER_WIDTH; x++) {
			world_x = start_x + x;
			world_z = start_z + z;
			
			if (!World_Contains(world_x, board_y, world_z)) continue;
			
			block = World_GetBlock(world_x, board_y, world_z);
			cell = MineSweeper_GetCell(x, z);
			if (!cell) continue;
			
			cell->block = block;
			cell->is_revealed = (block != MINESWEEPER_UNREVEALED);
			cell->is_flagged = false;
			
			value = MineSweeper_BlockToValue(block);
			cell->value = value;
		}
	}
	
	MineSweeper.state = MS_STATE_SOLVING;
}

void MineSweeper_Solve(void) {
	int x, z;
	struct MineSweeper_Cell* cell;
	struct MineSweeper_Cell* neighbor;
	int dx, dz;
	int unrevealed_count, flagged_count;
	
	MineSweeper.has_next_click = false;
	
	/* Стратегия 1: Если количество флагов равно значению, раскрыть остальные */
	for (z = 0; z < MINESWEEPER_LENGTH; z++) {
		for (x = 0; x < MINESWEEPER_WIDTH; x++) {
			cell = MineSweeper_GetCell(x, z);
			if (!cell || !cell->is_revealed || cell->value <= 0 || cell->value == -1) continue;
			
			flagged_count = MineSweeper_CountFlagged(x, z);
			
			if (flagged_count == cell->value) {
				/* Все мины отмечены, раскрыть остальные */
				for (dx = -1; dx <= 1; dx++) {
					for (dz = -1; dz <= 1; dz++) {
						if (dx == 0 && dz == 0) continue;
						neighbor = MineSweeper_GetCell(x + dx, z + dz);
						if (neighbor && !neighbor->is_revealed && !neighbor->is_flagged) {
							MineSweeper.next_click_x = x + dx;
							MineSweeper.next_click_z = z + dz;
							MineSweeper.has_next_click = true;
							return;
						}
					}
				}
			}
		}
	}
	
	/* Стратегия 2: Если нераскрытых клеток столько же, сколько оставалось мин, отметить как мины */
	for (z = 0; z < MINESWEEPER_LENGTH; z++) {
		for (x = 0; x < MINESWEEPER_WIDTH; x++) {
			cell = MineSweeper_GetCell(x, z);
			if (!cell || !cell->is_revealed || cell->value <= 0 || cell->value == -1) continue;
			
			flagged_count = MineSweeper_CountFlagged(x, z);
			unrevealed_count = MineSweeper_CountUnrevealed(x, z);
			
			if (unrevealed_count > 0 && (flagged_count + unrevealed_count) == cell->value) {
				/* Остальные нераскрытые - мины */
				for (dx = -1; dx <= 1; dx++) {
					for (dz = -1; dz <= 1; dz++) {
						if (dx == 0 && dz == 0) continue;
						neighbor = MineSweeper_GetCell(x + dx, z + dz);
						if (neighbor && !neighbor->is_revealed && !neighbor->is_flagged) {
							neighbor->is_flagged = true;
						}
					}
				}
			}
		}
	}
	
	/* Стратегия 3: Найти безопасную клетку для раскрытия */
	for (z = 0; z < MINESWEEPER_LENGTH; z++) {
		for (x = 0; x < MINESWEEPER_WIDTH; x++) {
			cell = MineSweeper_GetCell(x, z);
			if (!cell || cell->is_revealed || cell->is_flagged) continue;
			
			/* Проверить, безопасна ли эта клетка */
			cc_bool safe = false;
			if (cell->value == 0) safe = true;
			
			for (dx = -1; dx <= 1; dx++) {
				for (dz = -1; dz <= 1; dz++) {
					if (dx == 0 && dz == 0) continue;
					neighbor = MineSweeper_GetCell(x + dx, z + dz);
					if (neighbor && neighbor->is_revealed) {
						flagged_count = MineSweeper_CountFlagged(neighbor->x, neighbor->z);
						if (flagged_count == neighbor->value) {
							safe = true;
							break;
						}
					}
				}
				if (safe) break;
			}
			
			if (safe) {
				MineSweeper.next_click_x = x;
				MineSweeper.next_click_z = z;
				MineSweeper.has_next_click = true;
				return;
			}
		}
	}
	
	/* Если нечего делать, задача решена */
	MineSweeper.state = MS_STATE_COMPLETED;
}

void MineSweeper_ClickCell(int board_x, int board_z) {
	int start_x = MineSweeper.center_x - 4;
	int start_z = MineSweeper.center_z - 4;
	int world_x = start_x + board_x;
	int world_y = MineSweeper.center_y;
	int world_z = start_z + board_z;
	
	if (!World_Contains(world_x, world_y, world_z)) return;
	
	/* Переместить камеру/взгляд на клетку */
	struct Entity* player = &Entities.CurPlayer->Base;
	Vec3 pos = player->Position;
	
	/* Определить позицию клика (примерно центр блока) */
	Vec3 target = { 
		(float)world_x + 0.5f,
		(float)world_y + 0.5f,
		(float)world_z + 0.5f
	};
	
	/* Выполнить клик */
	MouseStatePress(MOUSE_LEFT);
	InputHandler_DeleteBlock();
	MouseStateRelease(MOUSE_LEFT);
}

void MineSweeper_Update(float delta) {
	if (!MineSweeper.active) return;
	
	MineSweeper.update_timer -= delta;
	
	if (MineSweeper.update_timer <= 0) {
		if (MineSweeper.state == MS_STATE_SCANNING) {
			MineSweeper_ScanBoard();
			MineSweeper.update_timer = 0.1f;
		} else if (MineSweeper.state == MS_STATE_SOLVING) {
			MineSweeper_Solve();
			
			if (MineSweeper.has_next_click) {
				MineSweeper_ClickCell(MineSweeper.next_click_x, MineSweeper.next_click_z);
				MineSweeper.update_timer = 0.15f;
				MineSweeper_ScanBoard();
			} else {
				MineSweeper.state = MS_STATE_COMPLETED;
				Chat_AddRaw("&aMineSweeper puzzle solved!");
				MineSweeper.update_timer = 1.0f;
			}
		} else if (MineSweeper.state == MS_STATE_COMPLETED) {
			MineSweeper.update_timer = 5.0f;
		}
	}
}
