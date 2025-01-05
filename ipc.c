#include "util.h"
#include "const.h"
#include <errno.h>
#include <unistd.h>


int send(void *context, local_id destination, const Message *message) {

    Process *proc_ptr = (Process *) context;
    Process current_process = *proc_ptr;
    

    int write_fd = current_process.pipes[current_process.pid][destination].fd[WRITE];
    //printf("Процесс %d записывает в файловый дескриптор для записи: %d, для чтения: %d\n",
           //current_process.pid, write_fd, current_process.pipes[current_process.pid][destination].fd[READ]);
    

    ssize_t bytes_written = write(write_fd, &(message->s_header), sizeof(MessageHeader) + message->s_header.s_payload_len);
    if (bytes_written < 0) {
        fprintf(stderr, "Ошибка при записи из процесса %d в процесс %d\n", current_process.pid, destination);
        return -1;
    }
    
    //printf("Записано сообщение длиной: %d\n", message->s_header.s_payload_len);
    return 0;
}
int send_multicast(void *context, const Message *message) {

    Process *proc_ptr = (Process *) context;
    Process current_proc = *proc_ptr;
    

    for (int idx = 0; idx < current_proc.num_process; idx++) {
        if (idx == current_proc.pid) {
            continue;
        }
        

        if (send(&current_proc, idx, message) < 0) {
            fprintf(stderr, "Ошибка при мультикаст-отправке из процесса %d к процессу %d\n", current_proc.pid, idx);
            return -1;
        }
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


    if (read_status < sizeof(MessageHeader)) {
        fprintf(stderr, "Ошибка: прочитано меньше данных, чем ожидалось (%zd байт)\n", read_status);
        return 1;
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
        //printf("Сообщение получено с длиной %zu (отсутствует полезная нагрузка)\n", payload_length);
        return 0;
    }


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


    if (bytes_read == payload_length) {
        //printf("Успешно считано сообщение длиной %zu байт\n", payload_length);
        return 0;
    } else {
        fprintf(stderr, "Ошибка: несоответствие длины полезной нагрузки. Ожидалось: %zu, прочитано: %zu\n",
                payload_length, bytes_read);
        return -4;
    }
}

int receive(void *process_context, local_id sender_id, Message *msg_buffer) {

    if (process_context == NULL || msg_buffer == NULL) {
        fprintf(stderr, "Ошибка: некорректный процесс или сообщение (NULL указатель)\n");
        return -1;
    }


    Process *proc_info = (Process *)process_context;
    Process active_proc = *proc_info;


    int read_descriptor = active_proc.pipes[sender_id][active_proc.pid].fd[READ];
    //int write_descriptor = active_proc.pipes[sender_id][active_proc.pid].fd[WRITE];
    //printf("Процесс %d осуществляет чтение из канала: запись fd: %d, чтение fd: %d\n", 
           //active_proc.pid, write_descriptor, read_descriptor);


    while (1) {
        int availability_status = check(read_descriptor, msg_buffer);
        

        if (availability_status == 2) {
            //printf("Процесс %d: данные временно недоступны, продолжаем ожидание...\n", active_proc.pid);
            continue;
        }


        if (availability_status == 0) {
            //printf("Процесс %d: заголовок сообщения успешно прочитан\n", active_proc.pid);
            break;
        } 


        fprintf(stderr, "Процесс %d: ошибка при попытке прочитать заголовок от процесса %d\n", active_proc.pid, sender_id);
        return -2;
    }


    int body_read_status = message(read_descriptor, msg_buffer);
    if (body_read_status != 0) {
        fprintf(stderr, "Процесс %d: ошибка при чтении тела сообщения от процесса %d\n", active_proc.pid, sender_id);
        return -3;
    }

    //printf("Процесс %d: сообщение от процесса %d успешно получено и обработано\n", active_proc.pid, sender_id);
    return 0;
}

int receive_any(void *context, Message *msg_buffer) {

    if (context == NULL || msg_buffer == NULL) {
        fprintf(stderr, "Ошибка: некорректный контекст или буфер сообщения (NULL значение)\n");
        return -1;
    }


    Process *proc_info = (Process *)context;
    Process active_proc = *proc_info;

    while (1) {
        for (local_id src_id = 0; src_id < active_proc.num_process; ++src_id) {

            if (src_id == active_proc.pid) {
                continue;
            }


            int channel_fd = active_proc.pipes[src_id][active_proc.pid].fd[READ];
            int availability_check = check(channel_fd, msg_buffer);


            if (availability_check == 2) {
                //printf("Процесс %d: нет данных от процесса %d, продолжаем ожидание...\n", 
                       //active_proc.pid, src_id);
                continue;
            }

            if (availability_check < 0) {
                fprintf(stderr, "Процесс %d: ошибка при чтении заголовка от процесса %d\n", 
                        active_proc.pid, src_id);
                return -2;
            }


            int payload_read_result = message(channel_fd, msg_buffer);
            if (payload_read_result != 0) {
                fprintf(stderr, "Процесс %d: ошибка при чтении тела сообщения от процесса %d\n", 
                        active_proc.pid, src_id);
                return -3;
            }


            printf("Процесс %d: сообщение от процесса %d успешно получено и обработано\n", 
                   active_proc.pid, src_id);
            return 0;
        }
    }


    fprintf(stderr, "Процесс %d: не удалось получить сообщение ни от одного процесса\n", active_proc.pid);
    return -4;
}

