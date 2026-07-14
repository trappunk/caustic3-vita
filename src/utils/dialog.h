/*
 * Common dialog declarations used by the Android native-library loader.
 *
 * Copyright (C) 2021 Andy Nguyen
 * Copyright (C) 2021 fgsfds
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 * Distributed under the MIT license.
 */

#ifndef SOLOADER_DIALOG_H
#define SOLOADER_DIALOG_H

__attribute__((unused)) int init_ime_dialog(const char *title,
                                            const char *initial_text);
__attribute__((unused)) char *get_ime_dialog_result(void);
int init_msg_dialog(const char *msg);
int get_msg_dialog_result(void);
void fatal_error(const char *fmt, ...) __attribute__((noreturn));

#endif
