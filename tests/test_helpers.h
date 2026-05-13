#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include "pe.h"
#include "nt_constants.h"

const char *build_dll_on_disk(const char *dll_path,
                              const char **export_names,
                              int num_exports);

#endif /* TEST_HELPERS_H */
