#ifndef PIPES_MANAGER_H
#define PIPES_MANAGER_H

#include <stdio.h>

#include "const.h"

void close_outcoming_pipes(Process* processes, FILE* pipe_file_ptr);

void close_incoming_pipes(Process* processes, FILE* pipe_file_ptr);

void close_non_related_pipes(Process* process, FILE* pipe_file_ptr);

#endif
