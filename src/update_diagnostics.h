#ifndef IO_UPDATE_DIAGNOSTICS_H
#define IO_UPDATE_DIAGNOSTICS_H

#include <canopennode.h>

#if defined(CONFIG_IO_UPDATE_DIAGNOSTICS)
void io_update_diagnostics_attach(CO_SDO_t *sdo);
#endif

#endif
