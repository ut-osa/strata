#ifndef _SHIM_INIT_H

void shim_init() __attribute((constructor));
void shim_fini() __attribute((destructor));

extern int initialized;

#endif
