#ifndef UTIL_H
#define UTIL_H


#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <asm-generic/errno.h>

#include "pa2345.h"
#include "const.h"


Pipe** init_pipes(int process_count, FILE* log_file_ptr);

int send_message(Process* proc, MessageType msg_type, TransferOrder* transfer_order);

int check_all_received(Process* process, MessageType type);

void add_to_history(BalanceHistory* record, timestamp_t current_time, balance_t cur_balance, balance_t delta);

void histories(Process* proc);

void bank_operations(Process *process, FILE* event_file_ptr);

timestamp_t increment_lamport_time(void);

void update_lamport_time(timestamp_t received_time);


#endif
