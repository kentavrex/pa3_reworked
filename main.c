#include <sys/types.h>
#include <sys/wait.h>
#include <asm-generic/errno.h>


#include "util.h"
#include "common.h"
#include "pipes_manager.h"



void transfer(void *context_data, local_id initiator, local_id recipient, balance_t transfer_amount) {

    TransferOrder transfer_info;
    transfer_info.s_src = initiator;
    transfer_info.s_dst = recipient;
    transfer_info.s_amount = transfer_amount;

    increment_lamport_time();
    send_message(context_data, TRANSFER, &transfer_info);
        



    Message ack_message;


    int ack_status = receive(context_data, recipient, &ack_message);
    if (ack_status != 0) {
        fprintf(stderr, "Ошибка: подтверждение от процесса %d не получено\n", recipient);
        exit(EXIT_FAILURE);
    }
    update_lamport_time(ack_message.s_header.s_local_time);
}


int main(int argc, char *argv[]) {
    if (argc < 3 || strcmp("-p", argv[1]) != 0) {
        fprintf(stderr, "Usage: -p X\n");
        exit(1);
    }

    int num_processes = atoi(argv[2]);
    if (num_processes < 1 || num_processes > 10) {
        fprintf(stderr, "Process count should be between 1 and 10\n");
        exit(1);
    }
    num_processes++;

    FILE* log_pipes = fopen("pipes.log", "w+");
    if (!log_pipes) {
        perror("Failed to open pipes.log");
        exit(1);
    }

    FILE* log_events = fopen("events.log", "w+");
    if (!log_events) {
        perror("Failed to open events.log");
        fclose(log_pipes);
        exit(1);
    }

    if (argc < num_processes + 2) {
        fprintf(stderr, "Provide initial balance values for each process\n");
        fclose(log_pipes);
        fclose(log_events);
        exit(1);
    }

    int balances[num_processes - 1];
    for (int i = 3; i < 3 + num_processes - 1; ++i) {
        int balance = atoi(argv[i]);
        if (balance < 1 || balance > 99) {
            fprintf(stderr, "Invalid balance at argument %d\n", i);
            fclose(log_pipes);
            fclose(log_events);
            exit(1);
        }
        balances[i - 3] = balance;
    }

    Pipe** pipes = init_pipes(num_processes, log_pipes);

    for (local_id i = 1; i < num_processes; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("Fork failed");
            exit(EXIT_FAILURE);
        }
        if (pid == 0) {
            Process child_proc = {
                .num_process = num_processes, .pipes = pipes, .pid = i, .cur_balance = balances[i - 1],
                .history = {.s_id = i, .s_history_len = 0}};

            close_non_related_pipes(&child_proc, log_pipes);
            add_to_history(&(child_proc.history), get_lamport_time(), child_proc.cur_balance, 0);
            send_message(&child_proc, STARTED, NULL);
            fprintf(log_events, log_started_fmt, get_lamport_time(), i, getpid(), getppid(), child_proc.cur_balance);
            if (check_all_received(&child_proc, STARTED) != 0) {
                fprintf(stderr, "Error: Process %d failed to receive all STARTED messages\n", i);
                exit(EXIT_FAILURE);
            }
            fprintf(log_events, log_received_all_started_fmt, get_lamport_time(), i);

            bank_operations(&child_proc, log_events);

            close_outcoming_pipes(&child_proc, log_pipes);
            close_incoming_pipes(&child_proc, log_pipes);
            exit(EXIT_SUCCESS);
        }
    }

    Process parent_proc = {.num_process = num_processes, .pipes = pipes, .pid = PARENT_ID};
    close_non_related_pipes(&parent_proc, log_pipes);

    if (check_all_received(&parent_proc, STARTED) != 0) {
        fprintf(stderr, "Error: Parent process failed to receive all STARTED messages\n");
        fclose(log_pipes);
        fclose(log_events);
        exit(EXIT_FAILURE);
    }
    fprintf(log_events, log_received_all_started_fmt, get_lamport_time(), PARENT_ID);

    bank_robbery(&parent_proc, num_processes - 1);
    send_message(&parent_proc, STOP, NULL);

    if (check_all_received(&parent_proc, DONE) != 0) {
        fprintf(stderr, "Error: Parent process failed to receive all DONE messages\n");
        fclose(log_pipes);
        fclose(log_events);
        exit(EXIT_FAILURE);
    }
    fprintf(log_events, log_received_all_done_fmt, get_lamport_time(), PARENT_ID);

    histories(&parent_proc);
    close_outcoming_pipes(&parent_proc, log_pipes);
    close_incoming_pipes(&parent_proc, log_pipes);
    while (wait(NULL) > 0);

    fclose(log_pipes);
    fclose(log_events);
    return 0;
}
