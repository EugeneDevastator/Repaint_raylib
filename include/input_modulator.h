#pragma once
#include "repaint.h"

void InputModulator_Init(void);
void InputModulator_Update(float wx, float wy, double time);
RootModulators InputModulator_GetRootSnapshot(void);
void InputModulator_GetAllSnapshot(ModulatorTable* out);
