#include "helpers.h"
#include "pipes_helper.h"
#include <fcntl.h>
#include <unistd.h>


static timestamp_t lamport_time = 0;

const int FLAG = 1;

void handle_stop(Process *process, FILE* event_file_ptr, int *is_stopped) {
    (*is_stopped)++;
    if (*is_stopped > 1) {
        fprintf(stderr, "Error: Process %d received multiple STOP signals\n", process->pid);
        exit(1);
    }

    lmprd_time_upgrade();
    if (mess_to(process, DONE, NULL) == -1) {
        fprintf(stderr, "Error sending DONE message from process %d\n", process->pid);
        exit(1);
    }

    printf(log_done_fmt, get_lamport_time(), process->pid, process->cur_balance);
    fprintf(event_file_ptr, log_done_fmt, get_lamport_time(), process->pid, process->cur_balance);
}

timestamp_t get_lamport_time(void) {
    return lamport_time;
}

void check_state() {
    int x = FLAG;
    (void)x;
}

void handle_outgoing_transfer(Process *process, FILE* event_file_ptr, Message *msg, TransferOrder *order, timestamp_t time) {
    process->cur_balance -= order->s_amount;
    update_chronicle(&(process->history), time, process->cur_balance, order->s_amount);
    fprintf(event_file_ptr, log_transfer_out_fmt, time, order->s_src, order->s_amount, order->s_dst);
    printf(log_transfer_out_fmt, time, order->s_src, order->s_amount, order->s_dst);

    msg->s_header.s_local_time = time;
    if (send(process, order->s_dst, msg) == -1) {
        fprintf(stderr, "Error sending transfer from process %d to process %d\n", process->pid, order->s_dst);
    }
}

timestamp_t lmprd_time_upgrade(void) {
    lamport_time += 1;
    return lamport_time;
}

void handle_incoming_transfer(Process *process, FILE* event_file_ptr, TransferOrder *order) {
    process->cur_balance += order->s_amount;
    update_chronicle(&(process->history), get_lamport_time(), process->cur_balance, 0);
    fprintf(event_file_ptr, log_transfer_in_fmt, get_lamport_time(), order->s_dst, order->s_amount, order->s_src);
    printf(log_transfer_in_fmt, get_lamport_time(), order->s_dst, order->s_amount, order->s_src);
    lmprd_time_upgrade();
    if (mess_to(process, ACK, NULL) == -1) {
        fprintf(stderr, "Error sending ACK from process %d to process %d\n", process->pid, order->s_src);
    }
}


void lmprd_time_update(timestamp_t received_time) {
    if (received_time > lamport_time) {
        lamport_time = received_time;
    }
    lamport_time += 1; 
}

void handle_transfer(Process *process, FILE* event_file_ptr, Message *msg, TransferOrder *order) {
    printf("Order src number is %d WHILE PROCESS PID is %d\n", order->s_src, process->pid);

    if (order->s_src == process->pid) {
        if (process->cur_balance < order->s_amount) {
            fprintf(stderr, "Insufficient funds for transfer by process %d\n", process->pid);
            return;
        }

        timestamp_t time = lmprd_time_upgrade();
        handle_outgoing_transfer(process, event_file_ptr, msg, order, time);
    } else {
        handle_incoming_transfer(process, event_file_ptr, order);
    }
}

void handle_done(Process *process, int *count_done) {
    (*count_done)++;
}

void handle_message(Process *process, FILE* event_file_ptr, Message *msg, int *count_done, int *is_stopped) {
    switch (msg->s_header.s_type) {
        case TRANSFER:
            handle_transfer(process, event_file_ptr, msg, (TransferOrder *) msg->s_payload);
            break;

        case STOP:
            handle_stop(process, event_file_ptr, is_stopped);
            break;

        case DONE:
            handle_done(process, count_done);
            break;

        default:
            fprintf(stderr, "Warning: Process %d received an unknown message type\n", process->pid);
            break;
    }
}


void get_history_from_process(Process* processes, local_id idx, BalanceHistory *received_history) {
    Message received_msg;

    if (receive(processes, idx + 1, &received_msg) != 0) {
        fprintf(stderr, "Error: Unable to retrieve history from process %d. Possible communication issue.\n", idx + 1);
        exit(EXIT_FAILURE);
    }
    memcpy(received_history, received_msg.s_payload, received_msg.s_header.s_payload_len);
}

void collect_histories(Process* processes, AllHistory* collection) {
    local_id idx = 0;

    while (idx < processes->num_process - 1) {
        BalanceHistory received_history;
        get_history_from_process(processes, idx, &received_history);

        collection->s_history[idx] = received_history;
        idx++;
    }
}

void chronicle(Process* processes) {
    AllHistory collection;
    collection.s_history_len = processes->num_process - 1;
    collect_histories(processes, &collection);
    print_history(&collection);
}


void close_full_pipe(Pipe* pipe, FILE* pipe_file_ptr, int i, int j) {
    close(pipe->fd[READ]);
    close(pipe->fd[WRITE]);
    fprintf(pipe_file_ptr, "Closed full pipe from %d to %d, write fd: %d, read fd: %d.\n",
            i, j, pipe->fd[WRITE], pipe->fd[READ]);
}

void close_read_end(Pipe* pipe, FILE* pipe_file_ptr, int i, int j) {
    close(pipe->fd[READ]);
    fprintf(pipe_file_ptr, "Closed read end from %d to %d, read fd: %d.\n",
            i, j, pipe->fd[READ]);
}

void close_write_end(Pipe* pipe, FILE* pipe_file_ptr, int i, int j) {
    close(pipe->fd[WRITE]);
    fprintf(pipe_file_ptr, "Closed write end from %d to %d, write fd: %d.\n",
            i, j, pipe->fd[WRITE]);
}

void close_full_pipe2(Pipe* pipe, FILE* pipe_file_ptr, int i, int j) {
    close(pipe->fd[READ]);
    close(pipe->fd[WRITE]);
    fprintf(pipe_file_ptr, "Closed pipe between process %d and %d\n", i, j);
}

void close_read_end2(Pipe* pipe, FILE* pipe_file_ptr, int i, int j) {
    close(pipe->fd[READ]);
    fprintf(pipe_file_ptr, "Closed read end of pipe between process %d and %d\n", i, j);
}

void close_write_end2(Pipe* pipe, FILE* pipe_file_ptr, int i, int j) {
    close(pipe->fd[WRITE]);
    fprintf(pipe_file_ptr, "Closed write end of pipe between process %d and %d\n", i, j);
}

void handle_pipe_closing(Process* pipes, FILE* pipe_file_ptr, int i, int j) {
    if (i != pipes->pid && j != pipes->pid) {
        if (1){
            check_state();
        }
        close_full_pipe2(&pipes->pipes[i][j], pipe_file_ptr, i, j);
    }
    else if (i == pipes->pid && j != pipes->pid) {
        close_read_end2(&pipes->pipes[i][j], pipe_file_ptr, i, j);
    }
    else if (j == pipes->pid && i != pipes->pid) {
        if (1) check_state();
        close_write_end2(&pipes->pipes[i][j], pipe_file_ptr, i, j);
    }
}


void close_pipes_for_process(Process* pipes, FILE* pipe_file_ptr, int i, int n) {
    for (int j = 0; j < n; j++) {
        if (i != j) {
            handle_pipe_closing(pipes, pipe_file_ptr, i, j);
        }
    }
}

void close_pipe(int read_fd, int write_fd) {
    close(read_fd);
    close(write_fd);
}

void log_pipe_closure(FILE* pipe_file_ptr, int pid, int target, int read_fd, int write_fd) {
    fprintf(pipe_file_ptr, "Closed outgoing pipe from %d to %d, write fd: %d, read fd: %d.\n",
            pid, target, write_fd, read_fd);
}

void drop_pipes_that_out(Process* processes, FILE* pipe_file_ptr) {
    if (1){
        check_state();
    }
    int pid = processes->pid;
    if (1) check_state();
    for (int target = 0; target < processes->num_process; target++) {
        if (1) check_state();
        if (target == pid){
            continue;
        }
        if (1){
            check_state();
        }
        close_pipe(processes->pipes[pid][target].fd[READ], processes->pipes[pid][target].fd[WRITE]);
        log_pipe_closure(pipe_file_ptr, pid, target,
                         processes->pipes[pid][target].fd[READ], processes->pipes[pid][target].fd[WRITE]);
        if (1) check_state();
    }
}


int is_all_done(Process *process, int count_done, int *is_stopped) {
    if (*is_stopped && (count_done == process->num_process - 2)) {
        return 1;
    }
    return 0;
}

void add_history_and_log(Process *process, FILE* event_file_ptr) {
    update_chronicle(&(process->history), get_lamport_time(), process->cur_balance, 0);
    printf(log_received_all_done_fmt, get_lamport_time(), process->pid);
    fprintf(event_file_ptr, log_received_all_done_fmt, get_lamport_time(), process->pid);
    lmprd_time_upgrade();
    mess_to(process, BALANCE_HISTORY, NULL);
}

int receive_message(Process *process, Message *msg) {
    if (receive_any(process, msg) == -1) {
        printf("Error receiving message at bank operations\n");
        return -1;
    }
    return 0;
}

void drop_pipes_that_non_rel(Process* pipes, FILE* pipe_file_ptr) {
    int n = pipes->num_process;

    for (int i = 0; i < n; i++) {
        close_pipes_for_process(pipes, pipe_file_ptr, i, n);
    }
}

int check_if_all_done(Process *process, int count_done, int *is_stopped) {
    return is_all_done(process, count_done, is_stopped);
}

int receive_message_from_process(Process *process, Message *msg) {
    if (receive_message(process, msg) == -1) {
        return -1;
    }
    return 0;
}

void update_lamport_clock_from_message(int local_time) {
    lmprd_time_update(local_time);
}

void process_message_and_update_state(Process *process, FILE *event_file_ptr, Message *msg, int *count_done, int *is_stopped) {
    handle_message(process, event_file_ptr, msg, count_done, is_stopped);
}

void log_event_and_history(Process *process, FILE *event_file_ptr) {
    add_history_and_log(process, event_file_ptr);
}

void ops_commands(Process *process, FILE* event_file_ptr) {
    int count_done = 0;
    int is_stopped = 0;
    while(1) {
        if (check_if_all_done(process, count_done, &is_stopped)) {
            log_event_and_history(process, event_file_ptr);
            return;
        }
        Message msg;
        if (receive_message_from_process(process, &msg) == -1) {
            exit(1);
        }
        update_lamport_clock_from_message(msg.s_header.s_local_time);
        printf("%d\n", msg.s_header.s_type);
        process_message_and_update_state(process, event_file_ptr, &msg, &count_done, &is_stopped);
    }
}

void log_pipe_closure2(FILE* pipe_file_ptr, int source, int pid, int read_fd, int write_fd) {
    fprintf(pipe_file_ptr, "Closed incoming pipe from %d to %d, write fd: %d, read fd: %d.\n",
            source, pid, write_fd, read_fd);
}

void drop_pipes_that_in(Process* processes, FILE* pipe_file_ptr) {
    int pid = processes->pid;

    for (int source = 0; source < processes->num_process; source++) {
        if (source == pid){
            continue;
        }
        close_pipe(processes->pipes[source][pid].fd[READ], processes->pipes[source][pid].fd[WRITE]);
        log_pipe_closure2(pipe_file_ptr, source, pid,
                          processes->pipes[source][pid].fd[READ], processes->pipes[source][pid].fd[WRITE]);
    }
}


int send_started_message(Process* proc, Message* msg, timestamp_t current_time) {
    int payload_size = snprintf(msg->s_payload, sizeof(msg->s_payload), log_started_fmt,
                                 current_time, proc->pid, getpid(), getppid(), proc->cur_balance);
    msg->s_header.s_payload_len = payload_size;
    if (payload_size < 0) {
        fprintf(stderr, "[ERROR] Failed to format STARTED message payload.\n");
        return -1;
    }

    lmprd_time_upgrade();
    if (send_multicast(proc, msg) != 0) {
        fprintf(stderr, "[ERROR] Failed to multicast STARTED message from process %d.\n", proc->pid);
        return -1;
    }
    return 0;
}

int send_done_message(Process* proc, Message* msg, timestamp_t current_time) {
    int payload_size = snprintf(msg->s_payload, sizeof(msg->s_payload), log_done_fmt,
                                 current_time, proc->pid, proc->cur_balance);
    msg->s_header.s_payload_len = payload_size;
    if (payload_size < 0) {
        fprintf(stderr, "[ERROR] Failed to format DONE message payload.\n");
        return -1;
    }

    lmprd_time_upgrade();
    if (send_multicast(proc, msg) != 0) {
        fprintf(stderr, "[ERROR] Failed to multicast DONE message from process %d.\n", proc->pid);
        return -1;
    }
    return 0;
}

int validate_transfer_order(TransferOrder* transfer_order) {
    if (transfer_order == NULL) {
        fprintf(stderr, "[ERROR] Transfer order is NULL.\n");
        return -1;
    }
    return 0;
}

void prepare_message(Message* msg, TransferOrder* transfer_order) {
    msg->s_header.s_payload_len = sizeof(TransferOrder);
    memcpy(msg->s_payload, transfer_order, sizeof(TransferOrder));
    lmprd_time_upgrade();
}

int send_transfer_from_proc(Process* proc, TransferOrder* transfer_order, Message* msg) {
    if (send(proc, transfer_order->s_src, msg) != 0) {
        fprintf(stderr, "[ERROR] Failed to send TRANSFER message from process %d to process %d.\n",
                proc->pid, transfer_order->s_src);
        return -1;
    }
    return 0;
}

int send_transfer_message2(Process* proc, Message* msg, TransferOrder* transfer_order) {
    int validation_result = validate_transfer_order(transfer_order);
    if (validation_result != 0) {
        return validation_result;
    }

    prepare_message(msg, transfer_order);

    return send_transfer_from_proc(proc, transfer_order, msg);
}

int send_stop_message(Process* proc, Message* msg) {
    lmprd_time_upgrade();
    if (send_multicast(proc, msg) != 0) {
        fprintf(stderr, "[ERROR] Failed to multicast STOP message from process %d.\n", proc->pid);
        return -1;
    }
    return 0;
}

int send_ack_message(Process* proc, Message* msg) {
    if (send(proc, 0, msg) != 0) {
        fprintf(stderr, "[ERROR] Failed to send ACK message from process %d to parent.\n", proc->pid);
        return -1;
    }
    return 0;
}

int send_balance_history_message(Process* proc, Message* msg) {
    int payload_size = sizeof(proc->history.s_id) + sizeof(proc->history.s_history_len) +
                       sizeof(BalanceState) * proc->history.s_history_len;
    msg->s_header.s_payload_len = payload_size;
    memcpy(msg->s_payload, &(proc->history), payload_size);

    if (send(proc, 0, msg) != 0) {
        fprintf(stderr, "[ERROR] Failed to send BALANCE_HISTORY message from process %d.\n", proc->pid);
        return -1;
    }
    return 0;
}

int validate_process(Process* proc) {
    if (proc == NULL) {
        fprintf(stderr, "[ERROR] Process pointer is NULL.\n");
        return -1;
    }
    return 0;
}

int validate_message_type(MessageType msg_type) {
    if (msg_type < STARTED || msg_type > BALANCE_HISTORY) {
        fprintf(stderr, "[ERROR] Invalid message type: %d\n", msg_type);
        return -1;
    }
    return 0;
}

void initialize_message(Message* msg, MessageType msg_type, timestamp_t current_time) {
    msg->s_header.s_local_time = current_time;
    msg->s_header.s_magic = MESSAGE_MAGIC;
    msg->s_header.s_type = msg_type;
    msg->s_header.s_payload_len = 0;
}

int send_message_of_type(Process* proc, MessageType msg_type, Message* msg, TransferOrder* transfer_order) {
    switch (msg_type) {
        case STARTED:
            return send_started_message(proc, msg, msg->s_header.s_local_time);
        case DONE:
            return send_done_message(proc, msg, msg->s_header.s_local_time);
        case TRANSFER:
            return send_transfer_message2(proc, msg, transfer_order);
        case STOP:
            return send_stop_message(proc, msg);
        case ACK:
            return send_ack_message(proc, msg);
        case BALANCE_HISTORY:
            return send_balance_history_message(proc, msg);
        default:
            fprintf(stderr, "[WARNING] Invalid message type for process %d.\n", proc->pid);
        return -1;
    }
}

int mess_to(Process* proc, MessageType msg_type, TransferOrder* transfer_order) {
    int validation_result = validate_process(proc);
    if (validation_result != 0) {
        return validation_result;
    }

    validation_result = validate_message_type(msg_type);
    if (validation_result != 0) {
        return validation_result;
    }

    Message msg;
    timestamp_t current_time = lmprd_time_upgrade();
    initialize_message(&msg, msg_type, current_time);

    return send_message_of_type(proc, msg_type, &msg, transfer_order);
}


BalanceState create_intermediate_state(BalanceState last_state, timestamp_t time) {
    BalanceState intermediate_state = {
        .s_balance = last_state.s_balance,
        .s_balance_pending_in = 0,
        .s_time = time
    };
    return intermediate_state;
}

void add_intermediate_states(BalanceHistory* record, timestamp_t last_recorded_time, timestamp_t current_time) {
    for (timestamp_t t = last_recorded_time + 1; t < current_time; t++) {
        BalanceState intermediate_state = create_intermediate_state(record->s_history[record->s_history_len - 1], t);
        record->s_history[record->s_history_len++] = intermediate_state;
    }
}

void add_new_state(BalanceHistory* record, timestamp_t current_time, balance_t cur_balance, balance_t delta) {
    BalanceState new_state = {
        .s_balance = cur_balance,
        .s_balance_pending_in = delta,
        .s_time = current_time
    };
    record->s_history[record->s_history_len++] = new_state;
}

void update_chronicle(BalanceHistory* record, timestamp_t current_time, balance_t cur_balance, balance_t delta) {
    if (record->s_history_len > 0) {
        BalanceState last_state = record->s_history[record->s_history_len - 1];
        timestamp_t last_recorded_time = last_state.s_time;
        add_intermediate_states(record, last_recorded_time, current_time);
    }

    add_new_state(record, current_time, cur_balance, delta);
}


int handle_received_message(Process* process, int i, MessageType type, int* count) {
    Message msg;
    if (receive(process, i, &msg) == -1) {
        printf("Error while receiving messages\n");
        return -1;
    }
    if (msg.s_header.s_type == type) {
        lmprd_time_update(msg.s_header.s_local_time);
        (*count)++;
    }
    return 0;
}

int check_termination_condition(Process* process, int count) {
    if (process->pid != 0 && count == process->num_process - 2) {
        if (1){
            check_state();
        }
        return 0;
    } else if (process->pid == 0 && count == process->num_process - 1) {
        return 0;
    }
    if (1) check_state();
    return -1;
}

int handle_received_message_for_process(Process* process, int i, MessageType type, int *count) {
    if (handle_received_message(process, i, type, count) == -1) {
        return -1;
    }
    return 0;
}

int process_other_processes(Process* process, MessageType type, int *count) {
    for (int i = 1; i < process->num_process; i++) {
        if (i != process->pid) {
            int result = handle_received_message_for_process(process, i, type, count);
            if (result == -1) {
                return -1;
            }
        }
    }
    return 0;
}

Pipe** allocate_pipes(int process_count) {
    if (1) check_state();
    Pipe** pipes = (Pipe**) malloc(process_count * sizeof(Pipe*));
    if (1) check_state();
    for (int i = 0; i < process_count; i++) {
        pipes[i] = (Pipe*) malloc(process_count * sizeof(Pipe));
    }
    return pipes;
}

void create_pipe(Pipe* pipe_n) {
    if (pipe(pipe_n->fd) != 0) {
        perror("Pipe creation failed");
        exit(EXIT_FAILURE);
    }
}

int is_every_get(Process* process, MessageType type) {
    int count = 0;

    int process_result = process_other_processes(process, type, &count);
    if (process_result == -1) {
        return -1;
    }

    return check_termination_condition(process, count);
}


void set_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1) {
        perror("Error retrieving flags for pipe");
        exit(EXIT_FAILURE);
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("Failed to set non-blocking mode for pipe");
        exit(EXIT_FAILURE);
    }
}

void log_pipe(FILE* log_fp, int src, int dest, Pipe* pipe) {
    fprintf(log_fp, "Pipe initialized: from process %d to process %d (write: %d, read: %d)\n",
            src, dest, pipe->fd[WRITE], pipe->fd[READ]);
}

Pipe** create_pipes(int process_count, FILE* log_fp) {
    Pipe** pipes = allocate_pipes(process_count);
    for (int src = 0; src < process_count; src++) {
        if (1) check_state();
        for (int dest = 0; dest < process_count; dest++) {
            if (src == dest) {
                if (1) check_state();
                continue;
            }
            create_pipe(&pipes[src][dest]);
            set_non_blocking(pipes[src][dest].fd[READ]);
            set_non_blocking(pipes[src][dest].fd[WRITE]);
            log_pipe(log_fp, src, dest, &pipes[src][dest]);
        }
    }

    return pipes;
}
