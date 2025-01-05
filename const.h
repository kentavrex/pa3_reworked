#ifndef CONST_H
#define CONST_H

#include <stdint.h>
#include "banking.h"



static const short WRITE = 1;
static const short READ = 0;

static const int ERR = 1;
static const int OK = 0;


typedef struct {
    int fd[2];
} Pipe;

typedef struct {
    long num_process;
    Pipe** pipes;
    int8_t pid;
    balance_t cur_balance;
    BalanceHistory history;
} Process;

#endif
