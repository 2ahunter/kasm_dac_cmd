#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include <syslog.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <errno.h>
#include "filter.h"
#include "UDP_client.h"
#include "protocol.h"

#define	TRUE	(1==1)
#define	FALSE	(!TRUE)
#define BUFFER_SIZE 96
#define NSEC_PER_SEC 1000*1000*1000
#define NUM_CHANNELS 8
#define MAX_CMD 65535 // maximum command value to send to actuater
#define MIN_CMD 0 // minimum command value to send to actuater
#define DAC_CMD_SIZE 31 
#define MIN_PERIOD 500000 // minimum period in nanoseconds for sending commands

/********** Module level variables ***************************/
extern int UDP_fd; // file descriptor for the UDP socket, set by UDP_init()
uint8_t running = TRUE; // set flag to false to terminate the threads and exit the program
char ip_addr[80] = "128.114.22.117";
// char ip_addr[80] = "127.0.0.0";

char port[20] = "5001";
command_t cmd_data; // command data structure to hold the command values to send

int start = DAC_ZERO_CODE; // start command value
int end = DAC_ZERO_CODE; // end command value
int stride = 1; // command increment
int period = MIN_PERIOD; // period in nanoseconds for sending commands
enum {FILTER_0, FILTER_1, FILTER_2, FILTER_3, FILTER_4} filter_select; // filter selection for the DAC
static const double no_filter_coefs[2] = {0,1.0};
const double *coefs = no_filter_coefs;

/********** functions ***************************************/
/**
 * @brief Normalize timer to account for seconds rollover
 * @param timespec_t ts Pointer to timespec structure to normalize
 */
static void normalize_timespec(struct timespec *ts) {
    while (ts->tv_nsec >= NSEC_PER_SEC) {
        ts->tv_sec += 1;
        ts->tv_nsec -= NSEC_PER_SEC;
    }
}

/**/
/* typedef struct command{
    uint8_t version;   // Protocol version
    uint32_t timestamp; // Timestamp in microseconds since app start
    frame_t frame[NUM_CHANNELS];
    uint8_t end_1;
    uint8_t end_2;
} command_t;*/
static void serialize_command(command_t *cmd, uint8_t *buffer) {
    int index = 0;
    buffer[index++] = cmd->version; // Protocol version
    uint32_t timestamp_net = htonl(cmd->timestamp); // Convert timestamp to network
    memcpy(&buffer[index], &timestamp_net, sizeof(timestamp_net)); // Copy timestamp to buffer
    index += sizeof(timestamp_net);
    for (int i = 0; i < NUM_CHANNELS; i++) {
        buffer[index++] = cmd->frame[i].reg; // Register address
        uint16_t data_net = htons(cmd->frame[i].data); // Convert data to network byte order
        memcpy(&buffer[index], &data_net, sizeof(data_net)); // Copy data to buffer
        index += sizeof(data_net);
    }
    buffer[index++] = cmd->end_1; // End byte 1
    buffer[index++] = cmd->end_2; // End byte 2
}




void * recv_UDP(void *data){
    (void)data; // unused parameter
    ssize_t nread;
    socklen_t peer_addrlen;
    struct sockaddr_storage peer_addr;
    char buf_data[BUFFER_SIZE]={0}; // buffer for UDP data
    int timeout_ms = 1; 
    const char *filename = "testing/UDP_client_test_data.csv";

    peer_addrlen = sizeof(peer_addr);
    // set up polling
    struct pollfd fds[1]; //monitor UDP for incoming data
    fds[0].fd = UDP_fd;
    fds[0].events = POLLIN; // look for new data
    int poll_ret = {0};

    /* datalogging of returns values */
    FILE *fp = fopen(filename, "w"); // open file for writing
    if (fp == NULL) {
        perror("Failed to open file");
        exit(EXIT_FAILURE);
    }

    while(running == TRUE){
        poll_ret = poll(fds, 1, timeout_ms);
        if(poll_ret < 0) exit(EXIT_FAILURE); // error
        if(poll_ret == 0) {
            syslog(LOG_WARNING, "UDP poll timeout after %d ms", timeout_ms);
        } else {
            // Receive data from the UDP socket
            nread = recvfrom(UDP_fd, buf_data, BUFFER_SIZE, 0,
                            (struct sockaddr *)&peer_addr, &peer_addrlen);
            if (nread == -1)
            {
                syslog(LOG_ERR, "Error receiving UDP data: %s\n", strerror(errno));
            }

            else {
                syslog(LOG_INFO, "Received %zd bytes", nread);
                buf_data[nread] = '\0'; // null-terminate the response
                fprintf(fp, "%s\r\n", buf_data); // write data to datalog
            }
        }
    }
    fclose(fp); // close the datalog file
    pthread_exit(NULL); // Return NULL to indicate thread completion

}

void * send_UDP(void *data){
    (void)data; // unused parameter
    int num_cmds = 1; // number of commands to send, default is 1
    int num_samples = 100; // samples per step
    struct timespec prd_tmr={0};
    struct timespec curr_tmr={0};
    long int delta_time_nsec = {0};
    int bytes_sent = {0};
    uint8_t buffer[DAC_CMD_SIZE] = {0}; // buffer for sending UDP data

    num_cmds = (end - start)/stride + 1; // calculate number of commands to send
    uint16_t cmd_val = start; 
    uint16_t cmd_filtered = 0; // filtered command value
    for(int i = 0; i < num_cmds; i++){
        for(int j = 0; j < num_samples; j++){
            clock_gettime(CLOCK_MONOTONIC, &prd_tmr); // get the current time for the period timer         
            cmd_data.timestamp = htonl((uint32_t) prd_tmr.tv_nsec/1000); // set the command timestamp to the current time in microseconds
            
            /* Low Pass Filter the command value */
            cmd_filtered = filter(cmd_val, coefs); // apply the selected filter to the command value

            for(int i = 0; i < NUM_CHANNELS; i++){
                cmd_data.frame[i].data = cmd_filtered;
            }

            serialize_command(&cmd_data, buffer); // serialize the command data into a byte array for sending

            /* send command buffer values */
            bytes_sent = UDP_send_protocol(buffer, DAC_CMD_SIZE);
            syslog(LOG_DEBUG, "Sent %zu bytes, iter %d of %d\n", (size_t)bytes_sent,i,num_cmds);

            // Calculate next wake-up time
            prd_tmr.tv_nsec += period; // Increment the period timer by the specified period
            normalize_timespec(&prd_tmr);
            // Get the current time for logging
            clock_gettime(CLOCK_MONOTONIC, &curr_tmr);
            // Compute the difference between current time and next period time
            delta_time_nsec = (prd_tmr.tv_sec - curr_tmr.tv_sec) * NSEC_PER_SEC +
                                (prd_tmr.tv_nsec - curr_tmr.tv_nsec);
            if (delta_time_nsec < 0) {
                syslog(LOG_ERR, "Missed deadline by %ld ns", -delta_time_nsec);
                // If we missed the deadline 
                // set period timer to current time plus one period
                prd_tmr.tv_sec = curr_tmr.tv_sec;
                prd_tmr.tv_nsec = curr_tmr.tv_nsec;
                prd_tmr.tv_nsec += period;
                normalize_timespec(&prd_tmr);
            }
            syslog(LOG_INFO, "Sleep until: %ld.%09ld", prd_tmr.tv_sec, prd_tmr.tv_nsec);
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &prd_tmr, NULL);
        }
        cmd_val += stride; // increment the command value for the next step
    }
    running = FALSE; // set running to false to signal the recv thread to exit
    pthread_exit(NULL); // Return NULL to indicate thread completion
}

int main(int argc, char *argv[])
{
    int opt = {0}; // for getopt()

    /*  parse command line arguments*/
    while ((opt = getopt(argc, argv, "hi:p:b:e:s:f:")) != -1){
        switch(opt){
            case 'i':
                strcpy(ip_addr, optarg); 
                printf("set IP address to %s\n",ip_addr);
                break;
            case 'p':
                strcpy(port, optarg);
                printf("port set to %s\n", port);
                break;
            case 'b':
                start = atoi(optarg);
                if(start < MIN_CMD || start > MAX_CMD){
                    fprintf(stderr, "Starting command value must be between %d and %d\n", MIN_CMD, MAX_CMD);
                    exit(EXIT_FAILURE);
                }
                printf("start command set to %d\n", start);
                break;
            case 'e':
                end = atoi(optarg);
                if(end < MIN_CMD || end > MAX_CMD){
                    fprintf(stderr, "Ending command value must be between %d and %d\n", MIN_CMD, MAX_CMD);
                    exit(EXIT_FAILURE);
                }
                printf("end command set to %d\n", end);
                break;
            case 's':
                stride = atoi(optarg);
                if (stride <= 0) {
                    fprintf(stderr, "Step size must be a positive integer.\n");
                    exit(EXIT_FAILURE);
                }
                printf("step size set to %d\n", stride);
                break;
            // case 't':
            //     period = atoi(optarg);
            //     if (period <= MIN_PERIOD) {
            //         fprintf(stderr, "Period must be a positive integer greater than 500,000.\n");
            //         exit(EXIT_FAILURE);
            //     }
            //     printf("period set to %d nanoseconds\n", period);
            //     break;
            case 'f':
                filter_select = atoi(optarg);
                if(filter_select < FILTER_0 || filter_select > FILTER_4){
                    fprintf(stderr, "Filter selection must be between %d and %d\n", FILTER_0, FILTER_4);
                    exit(EXIT_FAILURE);
                }
                switch (filter_select) {
                    case FILTER_0:
                        printf("filter set to no filtering\n");
                        break;
                    case FILTER_1:
                        printf("filter set to filter_250\n");
                        coefs = filter_250;
                        break;
                    case FILTER_2:
                        printf("filter set to filter_500\n");
                        coefs = filter_500;
                        break;
                    case FILTER_3:
                        printf("filter set to filter_1K\n");
                        coefs = filter_1K;
                        break;
                    case FILTER_4:
                        printf("filter set to filter_2K\n");
                        coefs = filter_2K;
                        break;
                    default:
                        printf("filter set to no filtering\n");
                        break;
                }
                break;
            case 'h':
                printf("Usage: %s -i ip_addr -p port [-b start] [-e end] [-s stride] [-f filter_number] \n", argv[0]);
                exit(EXIT_FAILURE);
                break;
            default:
                printf("Usage: %s -i ip_addr -p port [-b start] [-e end] [-s stride] [-f filter_number] \n", argv[0]);
                exit(EXIT_FAILURE);
                break;
        }
    }

    /*  open syslog for logging */
    openlog(NULL, LOG_PID, LOG_LOCAL6); // Open syslog for logging
    int mask = LOG_MASK(LOG_INFO) | LOG_MASK(LOG_ERR) | LOG_MASK(LOG_NOTICE);
    setlogmask(mask);

    /*  initialize UDP socket */
    UDP_fd = UDP_init(ip_addr, port);

    if(UDP_fd<0){
        syslog(LOG_ERR, "Failed to get socket descriptor");
        exit(EXIT_FAILURE);
    } else {
        syslog(LOG_INFO, "UDP client initialized with socket descriptor %d", UDP_fd);
    }

    /* initialize the command data DAC fields */
    cmd_data.version = PROTOCOL_VERSION;
    for (int i = 0; i < NUM_CHANNELS; i++){
        cmd_data.frame[i].reg= i + DAC80508_REG_DAC0;
    }
    cmd_data.end_1 = PROTOCOL_END_1;
    cmd_data.end_2 = PROTOCOL_END_2;

    /* start UDP listener thread */
    pthread_t udp_recv_thread;
    if(pthread_create(&udp_recv_thread, NULL, recv_UDP, NULL) != 0){
        syslog(LOG_ERR, "Failed to create UDP listener thread: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    /* start UDP send thread */
    pthread_t udp_send_thread;
    if(pthread_create(&udp_send_thread, NULL, send_UDP, NULL) != 0){
        syslog(LOG_ERR, "Failed to create UDP send thread: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    pthread_join(udp_recv_thread, NULL);
    pthread_join(udp_send_thread, NULL);
    syslog(LOG_INFO, "UDP client exiting");
    printf("UDP client exiting\n");

    exit(EXIT_SUCCESS);
}