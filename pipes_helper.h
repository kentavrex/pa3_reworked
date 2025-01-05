#ifndef PIPES_MANAGER_H
#define PIPES_MANAGER_H

#include <stdio.h>

#include "base_vars.h"

void drop_pipes_that_out(Process* processes, FILE* pipe_file_ptr);

void drop_pipes_that_in(Process* processes, FILE* pipe_file_ptr);

void drop_pipes_that_non_rel(Process* process, FILE* pipe_file_ptr);

#endif
