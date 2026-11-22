#ifndef IO_H
#define IO_H

#include <canopennode.h>

void io_init(CO_NMT_t *nmt);
void io_write_output(void);
#if !defined(CONFIG_BOARD_NATIVE_SIM)
void io_read_input(void);
#endif

#endif
