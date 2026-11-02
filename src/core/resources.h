#ifndef DATARUN_RESOURCES_H
#define DATARUN_RESOURCES_H
#include <stdbool.h>
#include <stddef.h>
// Prefer adjacent editable files; Windows EXEs include a complete fallback.
bool resources_prepare(char *root, size_t capacity);
#endif
