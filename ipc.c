#include "util.h"
#include "const.h"
#include <errno.h>
#include <unistd.h>


int get_write_fd(Process *proc_ptr, local_id destination) {
    return proc_ptr->pipes[proc_ptr->pid][destination].fd[WRITE];
}

ssize_t write_message(int write_fd, const Message *message) {
    return write(write_fd, &(message->s_header), sizeof(MessageHeader) + message->s_header.s_payload_len);
}

void handle_write_error(Process *proc_ptr, local_id destination) {
    fprintf(stderr, "Ошибка при записи из процесса %d в процесс %d\n", proc_ptr->pid, destination);
}

int send(void *context, local_id destination, const Message *message) {
    Process *proc_ptr = (Process *) context;
    int write_fd = get_write_fd(proc_ptr, destination);

    ssize_t bytes_written = write_message(write_fd, message);
    if (bytes_written < 0) {
        handle_write_error(proc_ptr, destination);
        return -1;
    }
    return 0;
}


int should_skip_process(Process *proc_ptr, int idx) {
    return idx == proc_ptr->pid;
}

int send_message_to_process(Process *proc_ptr, const Message *message, int idx) {
    if (send(proc_ptr, idx, message) < 0) {
        return -1;
    }
    return 0;
}

void log_multicast_error(Process *proc_ptr, int idx) {
    fprintf(stderr, "Ошибка при мультикаст-отправке из процесса %d к процессу %d\n", proc_ptr->pid, idx);
}

int should_skip_process_if_needed(Process *proc_ptr, int idx) {
    return should_skip_process(proc_ptr, idx);
}

int send_message_to_target_process(Process *proc_ptr, const Message *message, int idx) {
    return send_message_to_process(proc_ptr, message, idx);
}

void log_multicast_error_for_process(Process *proc_ptr, int idx) {
    log_multicast_error(proc_ptr, idx);
}

int send_multicast(void *context, const Message *message) {
    Process *proc_ptr = (Process *) context;
    Process current_proc = *proc_ptr;

    for (int idx = 0; idx < current_proc.num_process; idx++) {
        if (should_skip_process_if_needed(&current_proc, idx)) {
            continue;
        }

        if (send_message_to_target_process(&current_proc, message, idx) < 0) {
            log_multicast_error_for_process(&current_proc, idx);
            return -1;
        }
    }
    return 0;
}

ssize_t read_message_header(int fd_to_read, Message *message) {
    return read(fd_to_read, &(message->s_header), sizeof(MessageHeader));
}

int validate_message_pointer(Message *message) {
    if (message == NULL) {
        fprintf(stderr, "Error: pointer to message is NULL\n");
        return -1;
    }
    return 0;
}

int validate_fd(int fd_to_read) {
    if (fd_to_read < 0) {
        fprintf(stderr, "Error: invalid file descriptor (%d)\n", fd_to_read);
        return -1;
    }
    return 0;
}

int validate_args(int fd_to_read, Message *message) {
    if (validate_message_pointer(message) < 0) {
        return -1;
    }
    if (validate_fd(fd_to_read) < 0) {
        return -1;
    }
    return 0;
}

int handle_read_error(ssize_t read_status) {
    if (read_status == -1) {
        if (errno == EAGAIN) {
            return 2;
        } else {
            perror("Error reading data");
            return 1;
        }
    }
    if (read_status == 0) {
        fprintf(stderr, "Attention: end of file or no data\n");
        return 2;
    }
    return 0;
}

int check(int fd_to_read, Message *message) {
    if (validate_args(fd_to_read, message) < 0) {
        return -1;
    }
    ssize_t read_status = read_message_header(fd_to_read, message);
    return handle_read_error(read_status);
}

int validate_message_args(int fd, Message *msg_ptr) {
    if (msg_ptr == NULL) {
        fprintf(stderr, "Ошибка: сообщение не инициализировано (NULL указатель)\n");
        return -1;
    }
    if (fd < 0) {
        fprintf(stderr, "Ошибка: неверный файловый дескриптор (%d)\n", fd);
        return -1;
    }
    return 0;
}

ssize_t read_payload(int fd, char *payload_buffer, size_t bytes_to_read) {
    return read(fd, payload_buffer, bytes_to_read);
}

int handle_read_error2(ssize_t result) {
    if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 1;
        } else {
            perror("Ошибка при чтении содержимого сообщения");
            return -2;
        }
    }
    if (result == 0) {
        fprintf(stderr, "Предупреждение: данные не доступны, неожиданное завершение\n");
        return -3;
    }
    return 0;
}

int check_payload_length(size_t expected_length, size_t bytes_read) {
    if (bytes_read == expected_length) {
        return 0;  // Все данные прочитаны корректно
    } else {
        fprintf(stderr, "Ошибка: несоответствие длины полезной нагрузки. Ожидалось: %zu, прочитано: %zu\n",
                expected_length, bytes_read);
        return -4;
    }
}

int validate_args1(int fd, Message *msg_ptr) {
    if (validate_message_args(fd, msg_ptr) < 0) {
        return -1;
    }
    return 0;
}

int handle_empty_payload(size_t payload_length) {
    if (payload_length == 0) {
        return 0;
    }
    return -1;
}

int read_data(int fd, char *payload_buffer, size_t payload_length, size_t *bytes_read) {
    while (*bytes_read < payload_length) {
        ssize_t result = read_payload(fd, payload_buffer + *bytes_read, payload_length - *bytes_read);
        int error_code = handle_read_error2(result);
        if (error_code != 0) {
            return error_code;
        }
        *bytes_read += result;
    }
    return 0;
}

int check_and_return_length(size_t payload_length, size_t bytes_read) {
    return check_payload_length(payload_length, bytes_read);
}

int message(int fd, Message *msg_ptr) {
    int validation_result = validate_args1(fd, msg_ptr);
    if (validation_result < 0) {
        return -1;
    }

    size_t payload_length = msg_ptr->s_header.s_payload_len;
    int empty_payload_result = handle_empty_payload(payload_length);
    if (empty_payload_result == 0) {
        return 0;
    }

    size_t bytes_read = 0;
    char *payload_buffer = (char *) &(msg_ptr->s_payload);

    int read_result = read_data(fd, payload_buffer, payload_length, &bytes_read);
    if (read_result != 0) {
        return read_result;
    }

    return check_and_return_length(payload_length, bytes_read);
}


int validate_receive_args(void *process_context, Message *msg_buffer) {
    if (process_context == NULL || msg_buffer == NULL) {
        fprintf(stderr, "Ошибка: некорректный процесс или сообщение (NULL указатель)\n");
        return -1;
    }
    return 0;
}

int get_read_descriptor(Process *proc_info, local_id sender_id) {
    return proc_info->pipes[sender_id][proc_info->pid].fd[READ];
}

int check_availability(int read_descriptor, Message *msg_buffer) {
    int availability_status = check(read_descriptor, msg_buffer);
    if (availability_status == 2) {
        return 1;
    }
    if (availability_status != 0) {
        fprintf(stderr, "Ошибка при попытке прочитать заголовок\n");
        return 0;
    }
    return 0;
}

int read_message_body(int read_descriptor, Message *msg_buffer) {
    int body_read_status = message(read_descriptor, msg_buffer);
    if (body_read_status != 0) {
        fprintf(stderr, "Ошибка при чтении тела сообщения\n");
        return -3;
    }
    return 0;
}

int validate_receive_arguments(void *process_context, Message *msg_buffer) {
    return validate_receive_args(process_context, msg_buffer);
}

int get_read_descriptor_for_process(Process *proc_info, local_id sender_id) {
    return get_read_descriptor(proc_info, sender_id);
}

int wait_for_message_availability(int read_descriptor, Message *msg_buffer) {
    while (1) {
        int availability_status = check_availability(read_descriptor, msg_buffer);
        if (availability_status == 1) {
            continue;
        }
        if (availability_status == 0) {
            return 0;
        }
    }
    return -1;
}

int receive_message2(int read_descriptor, Message *msg_buffer) {
    return read_message_body(read_descriptor, msg_buffer);
}

int receive(void *process_context, local_id sender_id, Message *msg_buffer) {
    if (validate_receive_arguments(process_context, msg_buffer) < 0) {
        return -1;
    }

    Process *proc_info = (Process *)process_context;
    int read_descriptor = get_read_descriptor_for_process(proc_info, sender_id);

    if (wait_for_message_availability(read_descriptor, msg_buffer) < 0) {
        return -1;
    }

    return receive_message2(read_descriptor, msg_buffer);
}


int validate_input(void *context, Message *msg_buffer) {
    if (context == NULL || msg_buffer == NULL) {
        fprintf(stderr, "Ошибка: некорректный контекст или буфер сообщения (NULL значение)\n");
        return -1;
    }
    return 0;
}

int check_availability1(int channel_fd, Message *msg_buffer) {
    return check(channel_fd, msg_buffer);
}

int read_payload1(int channel_fd, Message *msg_buffer) {
    return message(channel_fd, msg_buffer);
}

int handle_check_result(int availability_check) {
    if (availability_check == 2) {
        return 1;
    }
    if (availability_check < 0) {
        return -1;
    }
    return 0;
}

int read_message_from_channel(int channel_fd, Message *msg_buffer) {
    int availability_check = check_availability1(channel_fd, msg_buffer);
    int availability_result = handle_check_result(availability_check);

    if (availability_result != 0) {
        return availability_result;
    }

    int payload_read_result = read_payload1(channel_fd, msg_buffer);
    if (payload_read_result != 0) {
        return -2;
    }

    return 0;
}


int validate_input_and_return(void *context, Message *msg_buffer) {
    int validation_result = validate_input(context, msg_buffer);
    if (validation_result != 0) {
        return validation_result;
    }
    return 0;
}

int read_message_from_channel_and_handle(int channel_fd, Message *msg_buffer) {
    int result = read_message_from_channel(channel_fd, msg_buffer);
    if (result == 1) {
        return 1;
    }
    if (result < 0) {
        return result;
    }
    return 0;
}

int process_message(int src_id, Process active_proc, Message *msg_buffer) {
    int channel_fd = active_proc.pipes[src_id][active_proc.pid].fd[READ];
    int result = read_message_from_channel_and_handle(channel_fd, msg_buffer);
    if (result == 1) {
        return 1;
    }
    if (result < 0) {
        fprintf(stderr, "Процесс %d: ошибка при чтении от процесса %d\n", active_proc.pid, src_id);
        return result;
    }
    printf("Процесс %d: сообщение от процесса %d успешно получено и обработано\n", active_proc.pid, src_id);
    return 0;
}

int receive_any(void *context, Message *msg_buffer) {
    int validation_result = validate_input_and_return(context, msg_buffer);
    if (validation_result != 0) {
        return validation_result;
    }

    Process *proc_info = (Process *)context;
    Process active_proc = *proc_info;

    while (1) {
        for (local_id src_id = 0; src_id < active_proc.num_process; ++src_id) {
            if (src_id == active_proc.pid) {
                continue;
            }
            int result = process_message(src_id, active_proc, msg_buffer);
            if (result == 0) {
                return 0;
            }
            if (result < 0) {
                return result;
            }
        }
    }

    fprintf(stderr, "Процесс %d: не удалось получить сообщение ни от одного процесса\n", active_proc.pid);
    return -4;
}
