/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* The app's configuration (app.h) with fm1-base's defaults; shared by the device (device/unity.c) and the host
 * simulator (host/plat_host.c). Included after app.h and gfx.c (RGB). */
#pragma once
#ifndef APP_NAME
#define APP_NAME "TEMPLATE"              /* on the splash, crash and safe-mode screens and for the screen reader */
#endif
#ifndef APP_SPLASH_RGB
#define APP_SPLASH_RGB RGB(242, 120, 72) /* the name on the splash screen */
#endif
#ifndef APP_PALETTE
#define APP_PALETTE 1u                   /* gfx.c PALETTES: the screen's colours (1 amber) */
#endif
#ifndef APP_VERSION
#define APP_VERSION "DEV"                /* a release build passes its own (tools/build.py --release) */
#endif
#ifndef APP_BLOCK
#define APP_BLOCK 256u                   /* frames per audio half and render: 5.8 ms at 44.1 kHz */
#endif
