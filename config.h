// Copyright 2024 vy
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#define TRI_LAYER_LOWER_LAYER 1
#define TRI_LAYER_UPPER_LAYER 2
#define TRI_LAYER_ADJUST_LAYER 3

// Register a custom split transaction for timer sync (master -> slave)
#define SPLIT_TRANSACTION_IDS_USER RPC_ID_USER_TIMER_SYNC

// RGB Matrix
#define RGB_MATRIX_KEYPRESSES
#define ENABLE_RGB_MATRIX_SOLID_SPLASH
#define SPLIT_LAYER_STATE_ENABLE

// Default: purple splash + underglow forced in indicators callback
#define RGB_MATRIX_DEFAULT_HUE 200
#define RGB_MATRIX_DEFAULT_SAT 255
#define RGB_MATRIX_DEFAULT_VAL 120
#define RGB_MATRIX_DEFAULT_MODE RGB_MATRIX_SOLID_SPLASH
