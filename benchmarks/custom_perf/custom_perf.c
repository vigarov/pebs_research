// perf_event_open
#include <linux/perf_event.h>
#include <asm/unistd.h>
// fork + exec + getpid
#include <sys/types.h>
#include <unistd.h>
// mmap
#include <sys/mman.h>
// uintN_t
#include <stdint.h>
// argp
#include <argp.h>
// strlen et. al
#include <string.h>
#include <stdlib.h>
//signals
#include <signal.h>
//poll
#include <poll.h>
//ioctl
#include <sys/ioctl.h>
#include <error.h>

#define DEBUG 1

#define likely(x) __builtin_expect((x),1)
#define unlikely(x) __builtin_expect((x),0)

const char *argp_program_version = "custom_perf 1.0";

// Default values for options
const char *default_output_dir = "comparison/";
int default_counter = 1;
int default_mmap_size = 8*1024;

// Structure to store command line arguments
struct arguments {
    char *executable_path;
    char **executable_args;
    union {
        uint8_t executable_path_position;
        uint8_t num_executable_args;  // Once we parse the main argument, we'll be abe to infer num_executable args, and we won't need executable_path_position anymore
    };
    const char *output_dir;
    uint64_t counter;
    size_t mmap_size;
};

// Option parser function
error_t parse_opt(int key, char *arg, struct argp_state *state) {
    struct arguments *arguments = state->input;

    switch (key) {
        case 'd':
            arguments->output_dir = (const char*) arg;
            break;
        case 'c':
            arguments->counter = strtoul(arg,NULL,10);
            if (arguments->counter <= 0) {
                argp_error(state, "Counter must be a positive integer");
            }
            break;
        case 'm':
            arguments->mmap_size = strtoul(arg,NULL,10);
            if (arguments->mmap_size <= 0) {
                argp_error(state, "Mmap size must be a positive integer");
            }
            else if(__builtin_popcount(arguments->mmap_size) != 1){
                // Not a power of 2 --> Approximate to the closest power of 2
                arguments->mmap_size = (8*sizeof(arguments->mmap_size)) - __builtin_clzl(arguments->mmap_size-1);
            }
            break;
        case ARGP_KEY_INIT:
            break; // Do nothing
        case ARGP_KEY_ARG: {
            arguments->executable_path = arg;
            arguments->executable_path_position = state->next - 1;
            return -1; //Preemptively finish parsing arguments; we'll gather `executable_arguments` manually afterwards, as argp doesn't support python's `argparse` ARG_REMAINDER equivalent
        }
        case ARGP_KEY_END:
            if (state->arg_num < 1) {
                argp_usage(state);
            }
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

// Documentation for command line arguments
static char args_doc[] = "<executable_path> [<executable_arguments>]";
static char doc[] = "Custom Perf -- A program to execute a given executable and collect performance data.\n"
                    "OPTIONS:\n"
                    "  -d DIR  Specify the output directory for the collected data. (default: comparison/)\n"
                    "  -c N    Set the period of the sampling to the positive integer N. (default: 1)\n"
                    "  -m M    Set the size of the mmap to positive integer M. (default:\n"
                    "\n"
                    "ARGUMENTS:\n"
                    "  executable         The name of the executable to execute.\n"
                    "  executable_arguments (optional) A space separated list of arguments to pass to the executable.\n";

// argp parser options
static struct argp_option options[] = {
        {"dir", 'd', "DIR", 0, "Specify output directory (default: comparison/)."},
        {"count", 'c', "N", 0, "Specify the period of the sampling (default: 1 == sample every event)."},
        {"mmap", 'm', "M", 0, "Specify mmap size (default: 8K). Will be rounded to closest power of 2. Can use 'K','M', and 'G' for convenience (e.g. `-m 8M`)"},
        {0}
};


// Create an argp object with the specified options
static struct argp argp = {options, parse_opt, args_doc, doc};

static int
perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                int cpu, int group_fd, unsigned long flags)
{
    int ret;

    ret = (int)syscall(__NR_perf_event_open, hw_event, pid, cpu,
                  group_fd, flags);
    return ret;
}

#define GET_PERF_ATTR(var_name,config_struct,args_p_var_name) struct perf_event_attr var_name = { .type=PERF_TYPE_RAW, .size=sizeof(struct perf_event_attr), \
.config=(config_struct), .sample_period=(args_p_var_name)->counter, .sample_type=PERF_SAMPLE_TIME|PERF_SAMPLE_ADDR|PERF_SAMPLE_PHYS_ADDR|PERF_SAMPLE_TID|PERF_SAMPLE_IP, \
.read_format=PERF_FORMAT_TOTAL_TIME_RUNNING, .disabled=1, .inherit=0, .exclude_kernel=1, .exclude_hv = 1 ,.freq=0, .enable_on_exec=1, .precise_ip=2,}

enum event_type{NONE_EVENT=-1,LOAD,STORE,NM_EVENTS};


static inline void switch_events(int loads_event_fd, int stores_event_fd, unsigned long int signal) {
    int error = ioctl(loads_event_fd, signal);
    if(error){
        printf("Failed to stop load event: %s", strerror(error));
    }
    error = ioctl(stores_event_fd,signal);
    if(error){
        printf("Failed to stop load event: %s", strerror(error));
    }
}

#define ENABLE_PEBS_EVENTS(load_fd,store_fd) switch_events((load_fd),(store_fd),PERF_EVENT_IOC_ENABLE)
#define DISABLE_PEBS_EVENTS(load_fd,store_fd) switch_events((load_fd),(store_fd),PERF_EVENT_IOC_DISABLE)




void gather_stats(struct arguments *args) {
    /*
       1. mmap requested amount of memory
       2. open perf fd on this pid, cpu = -1, on_exec = True
       3. fork
           .1 Child: (set at_exit?) wait for parent to set up then exec into benchmark (will activate the sampling)
           .2 Parent: setup to select/poll on fd in a while loop (until child process hasn't exited)
               ..1: when wake up, send pause (kill -TSTP) to child process, pause PEBS (just in case)
               ..2 gather data:
                       time 1 and 2? PEBS evaluate them lol?
                       ...1: empty mmap buffer, store to file?
                       ...2: page walk: do something with that information?
               ..3 : Resume PEBS, resume parent (kill -CONT)
   */
    int error = 0;

    //TODO: Usage of wakeup_watermark/events when sample_period is specified??
    GET_PERF_ATTR(loads_event_arguments,0x81d0,args);
    GET_PERF_ATTR(stores_event_arguments,0x82d0,args);

    // This process (future parent) will be the one `exec`ing the benchmark --> we want to "pin" the PERF events to its pid, but (potentially) any cpu
    pid_t curr_pid = getpid();

    int loads_event_fd = perf_event_open(&loads_event_arguments,curr_pid,-1,-1,0);
    if(loads_event_fd == -1) goto free_args;
    int stores_event_fd = perf_event_open(&stores_event_arguments,curr_pid,-1,-1,0);
    if(stores_event_fd == -1) goto close_load_fd;

    //umask = config:8-15 ; event = config:0-7
    // mem_inst_retired.all_loads -> cpu/(null)=0x1e8483,umask=0x81,event=0xd0/
    // mem_inst_retired.all_stores -> cpu/(null)=0x1e8483,umask=0x82,event=0xd0/
    const size_t mmap_size = sizeof(struct perf_event_mmap_page) + (args->mmap_size);
    // Must 1 + 2^n pages big
    void* load_mmap_addr_start = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, loads_event_fd, 0);
    if(load_mmap_addr_start == MAP_FAILED){
        printf("Failed to create load map");
        goto close_store_fd;
    }
    void* store_mmap_addr_start = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, stores_event_fd, 0);
    if(store_mmap_addr_start == MAP_FAILED){
        printf("Failed to create store map");
        goto unmap_load;
    }
    #if DEBUG
    printf("Forking");
    #endif
    pid_t child_pid = fork();

    if(child_pid==-1){
        printf("Failed to fork");
        goto unmap_store;
    }
    else if(child_pid == 0){
        //Child
        pause(); // Wait for CONT signal;
        //exec

    }
    else{
        //Parent
        sleep(1); //Give time for child to pause() and wait for us to set up
        struct pollfd to_poll[NM_EVENTS] = {{.fd=loads_event_fd,.events=POLLIN | POLLERR | POLLHUP},{.fd=stores_event_fd,.events=POLLIN | POLLERR | POLLHUP}};
        while(1){
            //Reset revents
            for(int i=0;i<NM_EVENTS;i++){
                to_poll[i].revents=0;
            }
            int read = poll(to_poll,NM_EVENTS,-1);
            if(likely(read > 0)){
                // One of the fds is ready to be read
                //Stop the process
                error = kill(child_pid,SIGSTOP);
                if(unlikely(error)){
                    printf("Couldn't pause child process: %s", strerror(errno));
                }
                //Just in case, disable PEBS sampling
                DISABLE_PEBS_EVENTS(loads_event_fd, stores_event_fd);
                if(read!=1){
                    printf("More than one event ready at the same time... Only parsing first, losing others");
                }
                for(int i = LOAD;i<NM_EVENTS;i++){
                    if(to_poll[i].revents!=0){
                        // Analyze the data
                        printf("Got data from %d !",(uint8_t)i);
                        goto terminate_child;
                    }
                }
            }
            else if(unlikely(read == 0)){
                printf("Poll timed out yet no timeout set...");
                break;
            }
            else{
                // Error
                printf("Error polling on the file descriptors: %s", strerror(errno));
                break;
            }
            //Reenable PEBS
            ENABLE_PEBS_EVENTS(loads_event_fd,stores_event_fd);
            //Resume the process
            error = kill(child_pid,SIGCONT);
            if(unlikely(error)){
                printf("Couldn't resume child process: %s", strerror(errno));
            }
        }

        /*while(kill(child_pid,0) == 0) { //while child is alive
            #if DEBUG
            printf("Child is alive, waking it up");
            #endif
            kill(child_pid, SIGCONT); //Resume child process execution

        }*/

    }


    terminate_child:
    if(kill(child_pid,0) == 0){ //child is still alive
        kill(child_pid,SIGTERM);
    }
    unmap_store:
    error = munmap(store_mmap_addr_start,mmap_size);
    if(error == -1){
        printf("Failed to unmap store map");
    }
    store_mmap_addr_start = NULL;
    unmap_load:
    error = munmap(load_mmap_addr_start,mmap_size);
    if(error == -1){
        printf("Failed to unmap load map");
    }
    load_mmap_addr_start = NULL;
    close_store_fd:
    error = close(stores_event_fd);
    if(error==-1){
        printf("Failed to close store perf event fd");
    }
    close_load_fd:
    error = close(loads_event_fd);
    if(error==-1) {
        printf("Failed to close loads perf event fd");
    }
    free_args:
    // free argument memory
    if (args->executable_args != NULL) {
        free(args->executable_args);
    }
}

int get_remainder_arguments(struct arguments *arguments, int argc, char **argv) {
    uint8_t exec_pos = arguments->executable_path_position;
    if(arguments == NULL || exec_pos >= argc || argv == NULL)
        return EINVAL;

    //We have argc - exec_pos - 1 for-the-executable arguments
    arguments->num_executable_args = argc - exec_pos - 1;
    if(arguments->num_executable_args == 0){
        //No additional executable arguments; our job here is done
        return 0;
    }
    arguments->executable_args = malloc(arguments->num_executable_args * sizeof(char*));
    if(arguments->executable_args == NULL){
       return ENOMEM;
    }
    for(uint8_t i=0; i<arguments->num_executable_args;i++){
        arguments->executable_args[i] = argv[i+exec_pos+1]; //+1 since the arguments start after the exec name, situated at exec_pos
    }
    return 0;
}

int main(int argc, char* argv[]){

    struct arguments arguments = {
            .executable_path = NULL,
            .executable_args = NULL,
            .num_executable_args = 0,
            .output_dir = default_output_dir,
            .counter = default_counter,
            .mmap_size = default_mmap_size
    };

    argp_parse(&argp, argc, argv, ARGP_IN_ORDER, 0, &arguments);

    int error = get_remainder_arguments(&arguments,argc,argv);
    if(error){
        printf("Error parsing program_arguments : %s", strerror(error));
        return error;
    }

    gather_stats(&arguments);

   return 0;

}
