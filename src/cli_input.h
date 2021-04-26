#pragma once

#include <stdbool.h>

#include <lauxlib.h>

// whether the cli is currently prompting for input
// - set to 1 when we print the prompt and start waiting for input
// - set to 0 when we get a line, 1 when finished processing it
extern _Atomic(bool) cli_reading_line;
// would like to protect this with a mutex (lock before calling into readline,
// unlock in line handler) but the line handler is called from a different thread
// so locking that way is undefined behavior
// -- any other thing that would work to make it properly safe?

void cli_input_init(void);
void cli_input_deinit(void);

extern const luaL_Reg l_cli_input_fns[];
