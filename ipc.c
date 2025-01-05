#include "util.h"
#include "const.h"
#include <errno.h>
#include <unistd.h>


int send(void *context, local_id destination, const Message *message) {
    Process *proc_ptr = (Process *) context;
    Process current_process = *proc_ptr;
    int write_fd = current_process.pipes[current_process.pid][destination].fd[WRITE];
    ssize_t bytes_written = write(write_fd, &(message->s_header), sizeof(MessageHeader) + message->s_header.s_payload_len);
    if (bytes_written < 0) {
        fprintf(stderr, "Ошибка при записи из процесса %d в процесс %d\n", current_process.pid, destination);
        return -1;
    }
    return 0;
}

int send_to_process(Process *process, local_id destination, const Message *message) {
    return send(process, destination, message);
}

int send_multicast(void *context, const Message *message) {
    Process *proc_ptr = (Process *) context;
    for (int idx = 0; idx < proc_ptr->num_process; idx++) {
        if (idx == proc_ptr->pid) continue;
        if (send_to_process(proc_ptr, idx, message) < 0) {
            fprintf(stderr, "Ошибка при мультикаст-отправке из процесса %d к процессу %d\n", proc_ptr->pid, idx);
            return -1;
        }
    }
    return 0;
}

int handle_read_error(ssize_t read_status) {
    if (read_status == -1) {
        if (errno == EAGAIN) {
            return 2;
        } else {
            perror("Ошибка при чтении данных");
            return 1;
        }
    }
    if (read_status == 0) {
        fprintf(stderr, "Внимание: конец файла или данных нет\n");
        return 2;
    }
    return 0;
}

int check(int fd_to_read, Message *message) {
    if (message == NULL) {
        fprintf(stderr, "Ошибка: указатель на сообщение является NULL\n");
        return -1;
    }
    if (fd_to_read < 0) {
        fprintf(stderr, "Ошибка: некорректный файловый дескриптор (%d)\n", fd_to_read);
        return -1;
    }
    ssize_t read_status = read(fd_to_read, &(message->s_header), sizeof(MessageHeader));
    return handle_read_error(read_status);
}


int read_message_payload(int fd, Message *msg_ptr, size_t payload_length) {
    size_t bytes_read = 0;
    char *payload_buffer = (char *) &(msg_ptr->s_payload);
    while (bytes_read < payload_length) {
        ssize_t result = read(fd, payload_buffer + bytes_read, payload_length - bytes_read);
        if (result < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            } else {
                perror("Ошибка при чтении содержимого сообщения");
                return -2;
            }
        }
        if (result == 0) {
            fprintf(stderr, "Предупреждение: данные не доступны, неожиданное завершение\n");
            return -3;
        }
        bytes_read += result;
    }
    if (bytes_read != payload_length) {
        fprintf(stderr, "Ошибка: несоответствие длины полезной нагрузки. Ожидалось: %zu, прочитано: %zu\n",
                payload_length, bytes_read);
        return -4;
    }
    return 0;
}

int message(int fd, Message *msg_ptr) {
    if (msg_ptr == NULL) {
        fprintf(stderr, "Ошибка: сообщение не инициализировано (NULL указатель)\n");
        return -1;
    }
    if (fd < 0) {
        fprintf(stderr, "Ошибка: неверный файловый дескриптор (%d)\n", fd);
        return -1;
    }
    size_t payload_length = msg_ptr->s_header.s_payload_len;
    if (payload_length == 0) {
        return 0;
    }
    return read_message_payload(fd, msg_ptr, payload_length);
}


int process_message(int fd, Message *msg_buffer) {
    int availability_status = check(fd, msg_buffer);
    if (availability_status == 2) {
        return 2;
    }
    if (availability_status == 0) {
        return message(fd, msg_buffer);
    }
    return -1;
}

int receive(void *process_context, local_id sender_id, Message *msg_buffer) {
    if (process_context == NULL || msg_buffer == NULL) {
        fprintf(stderr, "Ошибка: некорректный процесс или сообщение (NULL указатель)\n");
        return -1;
    }
    Process *proc_info = (Process *)process_context;
    Process active_proc = *proc_info;
    int read_descriptor = active_proc.pipes[sender_id][active_proc.pid].fd[READ];
    while (1) {
        int result = process_message(read_descriptor, msg_buffer);
        if (result == 0) {
            break;
        }
        if (result == -1) {
            fprintf(stderr, "Процесс %d: ошибка при попытке прочитать заголовок от процесса %d\n", active_proc.pid, sender_id);
            return -2;
        }
    }
    return 0;
}

int receive_from_process(Process *proc_info, local_id src_id, Message *msg_buffer) {
    int channel_fd = proc_info->pipes[src_id][proc_info->pid].fd[READ];
    int availability_check = check(channel_fd, msg_buffer);
    if (availability_check == 2) {
        return 2;
    }
    if (availability_check < 0) {
        fprintf(stderr, "Процесс %d: ошибка при чтении заголовка от процесса %d\n",
                proc_info->pid, src_id);
        return -2;
    }
    return message(channel_fd, msg_buffer);
}

int receive_any(void *context, Message *msg_buffer) {
    if (context == NULL || msg_buffer == NULL) {
        fprintf(stderr, "Ошибка: некорректный контекст или буфер сообщения (NULL значение)\n");
        return -1;
    }
    Process *proc_info = (Process *)context;
    Process active_proc = *proc_info;
    for (local_id src_id = 0; src_id < active_proc.num_process; ++src_id) {
        if (src_id == active_proc.pid) {
            continue;
        }
        int result = receive_from_process(proc_info, src_id, msg_buffer);
        if (result == 0) {
            printf("Процесс %d: сообщение от процесса %d успешно получено и обработано\n",
                   active_proc.pid, src_id);
            return 0;
        }
    }
    fprintf(stderr, "Процесс %d: не удалось получить сообщение ни от одного процесса\n", active_proc.pid);
    return -4;
}
