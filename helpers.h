#ifndef UTIL_H
#define UTIL_H


#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <asm-generic/errno.h>

#include "pa2345.h"
#include "base_vars.h"


Pipe** create_pipes(int process_count, FILE* log_file_ptr);

int mess_to(Process* proc, MessageType msg_type, TransferOrder* transfer_order);

int is_every_get(Process* process, MessageType type);

void update_chronicle(BalanceHistory* record, timestamp_t current_time, balance_t cur_balance, balance_t delta);

void chronicle(Process* proc);

void ops_commands(Process *process, FILE* event_file_ptr);

timestamp_t lmprd_time_upgrade(void);

void lmprd_time_update(timestamp_t received_time);


#endif
