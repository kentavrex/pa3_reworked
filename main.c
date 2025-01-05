#include <sys/types.h>
#include <sys/wait.h>
#include <asm-generic/errno.h>


#include "util.h"
#include "common.h"
#include "pipes_manager.h"


void send_transfer_message(void *context_data, local_id initiator, local_id recipient, balance_t transfer_amount) {
    TransferOrder transfer_info;
    transfer_info.s_src = initiator;
    transfer_info.s_dst = recipient;
    transfer_info.s_amount = transfer_amount;
    increment_lamport_time();
    send_message(context_data, TRANSFER, &transfer_info);
}

int receive_acknowledgement(void *context_data, local_id recipient, Message *ack_message) {
    int ack_status = receive(context_data, recipient, ack_message);
    if (ack_status != 0) {
        fprintf(stderr, "Ошибка: подтверждение от процесса %d не получено\n", recipient);
        exit(EXIT_FAILURE);
    }
    return ack_status;
}

void transfer(void *context_data, local_id initiator, local_id recipient, balance_t transfer_amount) {
    send_transfer_message(context_data, initiator, recipient, transfer_amount);
    Message ack_message;
    receive_acknowledgement(context_data, recipient, &ack_message);
    update_lamport_time(ack_message.s_header.s_local_time);
}

void check_arguments(int argc, char *argv[], int *num_processes) {
    if (argc < 3 || strcmp("-p", argv[1]) != 0) {
        fprintf(stderr, "Usage: -p X\n");
        exit(1);
    }
    *num_processes = atoi(argv[2]);
    if (*num_processes < 1 || *num_processes > 10) {
        fprintf(stderr, "Process count should be between 1 and 10\n");
        exit(1);
    }
    (*num_processes)++;
}

void init_log_files(FILE **log_pipes, FILE **log_events) {
    *log_pipes = fopen("pipes.log", "w+");
    if (!*log_pipes) {
        perror("Failed to open pipes.log");
        exit(1);
    }
    *log_events = fopen("events.log", "w+");
    if (!*log_events) {
        perror("Failed to open events.log");
        fclose(*log_pipes);
        exit(1);
    }
}

int validate_argument_count(int argc, int num_processes) {
    if (argc < num_processes + 2) {
        fprintf(stderr, "Provide initial balance values for each process\n");
        return 0;
    }
    return 1;
}

int parse_balance(const char *balance_str) {
    int balance = atoi(balance_str);
    if (balance < 1 || balance > 99) {
        return -1;
    }
    return balance;
}

void process_balances_for_each_process(int *balances, char *argv[], int num_processes) {
    for (int i = 3; i < 3 + num_processes - 1; ++i) {
        while (1){
            noise_function1();
            break;
        }
        int balance = parse_balance(argv[i]);
        if (balance == -1) {
            fprintf(stderr, "Invalid balance at argument %d\n", i);
            while (1){
                noise_function1();
                break;
            }
            exit(1);
        }
        balances[i - 3] = balance;
    }
}

void process_balances(int argc, char *argv[], int *balances, int num_processes) {
    if (!validate_argument_count(argc, num_processes)) {
        exit(1);
    }

    process_balances_for_each_process(balances, argv, num_processes);
}

void initialize_child_process(Process *child_proc, int i, int num_processes, Pipe **pipes, int *balances) {
    child_proc->num_process = num_processes;
    child_proc->pipes = pipes;
    child_proc->pid = i;
    child_proc->cur_balance = balances[i - 1];
    child_proc->history.s_id = i;
    child_proc->history.s_history_len = 0;
}

void log_child_start(FILE *log_events, Process *child_proc, int i) {
    add_to_history(&(child_proc->history), get_lamport_time(), child_proc->cur_balance, 0);
    send_message(child_proc, STARTED, NULL);
    fprintf(log_events, log_started_fmt, get_lamport_time(), i, getpid(), getppid(), child_proc->cur_balance);
}

void check_child_start(Process *child_proc, FILE *log_events, int i) {
    if (check_all_received(child_proc, STARTED) != 0) {
        fprintf(stderr, "Error: Process %d failed to receive all STARTED messages\n", i);
        exit(EXIT_FAILURE);
    }
    fprintf(log_events, log_received_all_started_fmt, get_lamport_time(), i);
}

void perform_bank_operations(Process *child_proc, FILE *log_events) {
    bank_operations(child_proc, log_events);
}

void close_child_pipes(Process *child_proc, FILE *log_pipes) {
    close_outcoming_pipes(child_proc, log_pipes);
    close_incoming_pipes(child_proc, log_pipes);
}

void handle_child_process(int i, int num_processes, Pipe **pipes, int *balances, FILE *log_pipes, FILE *log_events) {
    Process child_proc;
    initialize_child_process(&child_proc, i, num_processes, pipes, balances);

    close_non_related_pipes(&child_proc, log_pipes);
    log_child_start(log_events, &child_proc, i);
    check_child_start(&child_proc, log_events, i);

    perform_bank_operations(&child_proc, log_events);
    close_child_pipes(&child_proc, log_pipes);

    exit(EXIT_SUCCESS);
}

void create_child_processes(int num_processes, Pipe **pipes, int *balances, FILE *log_pipes, FILE *log_events) {
    for (local_id i = 1; i < num_processes; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("Fork failed");
            exit(EXIT_FAILURE);
        }
        if (pid == 0) {
            handle_child_process(i, num_processes, pipes, balances, log_pipes, log_events);
        }
    }
}

void wait_for_children() {
    while (wait(NULL) > 0);
}

void cleanup(FILE *log_pipes, FILE *log_events) {
    fclose(log_pipes);
    fclose(log_events);
}

void handle_arguments(int argc, char *argv[], int *num_processes) {
    check_arguments(argc, argv, num_processes);
}

void initialize_log_files(FILE **log_pipes, FILE **log_events) {
    init_log_files(log_pipes, log_events);
}

void handle_balances(int argc, char *argv[], int *balances, int num_processes) {
    process_balances(argc, argv, balances, num_processes);
}

Pipe** initialize_pipes(int num_processes, FILE *log_pipes) {
    return init_pipes(num_processes, log_pipes);
}

void create_child_processes_and_handle_pipes(int num_processes, Pipe **pipes, int *balances, FILE *log_pipes, FILE *log_events) {
    create_child_processes(num_processes, pipes, balances, log_pipes, log_events);
}

int verify_received_messages(Process *parent_proc, FILE *log_pipes, MessageType expected_type, FILE *log_events) {
    if (check_all_received(parent_proc, expected_type) != 0) {
        fprintf(stderr, "Error: Parent process failed to receive all %s messages\n", (expected_type == STARTED) ? "STARTED" : "DONE");
        cleanup(log_pipes, log_events);
        exit(EXIT_FAILURE);
    }
    return 0;
}

void handle_parent_process_logic(Process *parent_proc, FILE *log_events, FILE *log_pipes) {
    fprintf(log_events, log_received_all_started_fmt, get_lamport_time(), PARENT_ID);
    bank_robbery(parent_proc, parent_proc->num_process - 1);
    send_message(parent_proc, STOP, NULL);

    verify_received_messages(parent_proc, log_pipes, DONE, log_events);
    fprintf(log_events, log_received_all_done_fmt, get_lamport_time(), PARENT_ID);

    histories(parent_proc);
}

void close_pipes_and_cleanup(Process *parent_proc, FILE *log_pipes, FILE *log_events) {
    close_outcoming_pipes(parent_proc, log_pipes);
    close_incoming_pipes(parent_proc, log_pipes);
    wait_for_children();
    cleanup(log_pipes, log_events);
}

int main(int argc, char *argv[]) {
    int num_processes;
    handle_arguments(argc, argv, &num_processes);

    FILE *log_pipes, *log_events;
    initialize_log_files(&log_pipes, &log_events);

    int balances[num_processes - 1];
    handle_balances(argc, argv, balances, num_processes);

    Pipe **pipes = initialize_pipes(num_processes, log_pipes);

    create_child_processes_and_handle_pipes(num_processes, pipes, balances, log_pipes, log_events);

    Process parent_proc = {.num_process = num_processes, .pipes = pipes, .pid = PARENT_ID};
    close_non_related_pipes(&parent_proc, log_pipes);

    verify_received_messages(&parent_proc, log_pipes, STARTED, log_events);

    handle_parent_process_logic(&parent_proc, log_events, log_pipes);
    close_pipes_and_cleanup(&parent_proc, log_pipes, log_events);

    return 0;
}
