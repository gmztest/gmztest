#pragma once

#include "board.h"
#include "sgf.h"
#include "cluster.h"
#include "search.h"

#define NAME "= GMZ built on " __DATE__ " " __TIME__ "\n\n"
#define VERSION "= 0.2.1\n\n"

int CallGTP();

extern bool save_log;
extern bool need_time_control;
extern bool use_pondering;
