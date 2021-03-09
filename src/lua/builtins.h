#pragma once

// adds all of our global C functions and variables to the lua state
// also includes ones from other "modules" like click() from click.c
void lua_define_builtins(void *L);
