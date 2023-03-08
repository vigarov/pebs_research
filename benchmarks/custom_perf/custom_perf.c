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

#define DEBUG 0

#define likely(x) __builtin_expect((x),1)
#define unlikely(x) __builtin_expect((x),0)

#define err(error_string) do{ unsigned int error_line = __LINE__;                                     \
    char line_nr[10] = {0};                                                                          \
    snprintf(line_nr,9,"%d",error_line);                                                              \
    const char* user_error_string = error_string;                                                      \
    const size_t f_len = strlen(__func__), l_len = strlen(line_nr), us_len = strlen(user_error_string), \
    nb_calloc_chars = f_len + l_len + us_len + 3;                                                        \
    char *final_str = calloc(nb_calloc_chars,sizeof(char));                                               \
    if(final_str == NULL){printf("Error allocating memory for... error string. Exiting."); exit(-1);}      \
    strncat(final_str,__func__,nb_calloc_chars); strncat(final_str,":",nb_calloc_chars-f_len);              \
    strncat(final_str,line_nr,nb_calloc_chars-f_len-1); strncat(final_str," ",nb_calloc_chars-f_len-1-l_len);\
    strncat(final_str,user_error_string,nb_calloc_chars-f_len-1-l_len-1);                                     \
    perror(final_str); free(final_str); }while(0)

const char *argp_program_version = "custom_perf 1.0";

// Default values for options
const char *default_output_dir = "comparison/";
int default_counter = 1;
int default_mmap_size = 8*1024;

// Structure to store command line arguments
struct arguments {
    const char *executable_path;
    char **executable_args;
    union {
        uint8_t executable_path_position;
        uint8_t num_extra_executable_args;  // Once we parse the main argument, we'll be abe to infer num_executable args, and we won't need executable_path_position anymore
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
                argp_error(state, "Counter must be a positive integer\n");
            }
            break;
        case 'm': {
            char *end = NULL;
            arguments->mmap_size = strtoul(arg, &end, 10);
            if(end != NULL){
                if(*end == 'K' || *end == 'k')arguments->mmap_size*=1024;
                else if(*end == 'M' || *end == 'm') arguments->mmap_size*=1024*1024;
                else if(*end == 'G' || *end == 'g') arguments->mmap_size*=1024*1024*1024;
                else argp_error(state,"Incorrect letter for mmap size");
            }
            if (arguments->mmap_size <= 0) {
                argp_error(state, "Mmap size must be a positive integer\n");
            } else if (__builtin_popcount(arguments->mmap_size) != 1) {
                // Not a power of 2 --> Approximate to the closest power of 2
                arguments->mmap_size = (8 * sizeof(arguments->mmap_size)) - __builtin_clzl(arguments->mmap_size - 1);
            }
            break;
        }
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
.config=(config_struct), .sample_period=(args_p_var_name)->counter,.sample_type=PERF_SAMPLE_IP|PERF_SAMPLE_TIME|PERF_SAMPLE_ADDR|PERF_SAMPLE_PHYS_ADDR, \
.read_format=PERF_FORMAT_TOTAL_TIME_RUNNING, .disabled=1, .exclude_kernel=1, .exclude_hv = 1 ,.freq=0, .enable_on_exec=1, .precise_ip=2}

struct perf_sample{ //TODO: update as ATTR above changes
    uint64_t   ip;
    uint64_t   time;
    uint64_t   addr;
    uint64_t   phys_addr;
};

enum event_type{NONE_EVENT=-1,LOAD,STORE,NM_EVENTS};


static inline void switch_events(int loads_event_fd, int stores_event_fd, unsigned long int signal) {
    int error = ioctl(loads_event_fd, signal);
    if(error){
        err("Failed to stop load event\n");
    }
    error = ioctl(stores_event_fd,signal);
    if(error){
        err("Failed to stop load event");
    }
}

#define ENABLE_PEBS_EVENTS(load_fd,store_fd) switch_events((load_fd),(store_fd),PERF_EVENT_IOC_ENABLE)
#define DISABLE_PEBS_EVENTS(load_fd,store_fd) switch_events((load_fd),(store_fd),PERF_EVENT_IOC_DISABLE)

#define GET_START_OFFSET() (addr_to_use + ((mmap_header->data_offset+at)%mmap_header->data_size))


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

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        err("Couldn't create pipe");
        goto free_args;
    }

    #if DEBUG
    printf("Forking\n");
    #endif
    pid_t child_pid = fork();

    if(child_pid==-1){
        err("Failed to fork\n");
        goto close_pipe;//unmap_store;
    }
    else if(child_pid == 0){
        //Child
        if(close(pipefd[1])==-1)err("Child couldn't close W side of pipe");
        char buf;
        if(unlikely(read(pipefd[0],&buf,1)!=0)){
            printf("Child didn't receive EOF, but rather char w/ `int` value of %d. Still continuing.",(unsigned)buf);
        }
        #if DEBUG
        printf("Child Process received parent signal: continuing\n");
        #endif
        close(pipefd[0]);
        error = execvp(args->executable_path,args->executable_args);
        if(error){
            err("Child Process: Failed to exec");
            error = kill(getppid(),SIGTERM);
            if(error){
                err("Child Process: Could not terminate parent");
            }
            exit(error);
        }
    }
    else{
        if(close(pipefd[0])==-1)err("Parent couldn't close R side of pipe");
        // Set up the PEBS events
        //TODO: Usage of wakeup_watermark/events when sample_period is specified??
        GET_PERF_ATTR(loads_event_arguments,0x81d0,args);
        GET_PERF_ATTR(stores_event_arguments,0x82d0,args);

        int loads_event_fd = perf_event_open(&loads_event_arguments,child_pid,-1,-1,0);
        if(loads_event_fd == -1) goto terminate_child;
        int stores_event_fd = perf_event_open(&stores_event_arguments,child_pid,-1,-1,0);
        if(stores_event_fd == -1) goto close_load_fd;

        //umask = config:8-15 ; event = config:0-7
        // mem_inst_retired.all_loads -> cpu/(null)=0x1e8483,umask=0x81,event=0xd0/
        // mem_inst_retired.all_stores -> cpu/(null)=0x1e8483,umask=0x82,event=0xd0/
        const size_t mmap_size = sizeof(struct perf_event_mmap_page) + (args->mmap_size);
        // Must 1 + 2^n pages big
        unsigned char* load_mmap_addr_start = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, loads_event_fd, 0);
        if(load_mmap_addr_start == MAP_FAILED){
            err("Failed to create load map");
            goto close_store_fd;
        }
        unsigned char* store_mmap_addr_start = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, stores_event_fd, 0);
        if(store_mmap_addr_start == MAP_FAILED){
            err("Failed to create store map");
            goto unmap_load;
        }

        //Parent
        struct pollfd to_poll[NM_EVENTS] = {{.fd=loads_event_fd,.events=POLLIN | POLLERR | POLLHUP},{.fd=stores_event_fd,.events=POLLIN | POLLERR | POLLHUP}};
            if(unlikely(close(pipefd[1])==-1)) err("Parent finished but couldn't close W side of pipefd "); //Starts child
        uint64_t l_count = 0, s_count = 0, woken = 0;
        while(1){
            int read = poll(to_poll,NM_EVENTS,-1);
            if(likely(read > 0)){
                // One of the fds is ready to be read
                //Stop the process
                error = kill(child_pid,SIGSTOP);
                if(unlikely(error)){
                    err("Couldn't pause child process");
                }
                //Just in case, disable PEBS sampling
                //DISABLE_PEBS_EVENTS(loads_event_fd, stores_event_fd);
                woken+=1;
                #if DEBUG
                printf("Woke up from read\n");
                #endif
                if(read!=1){
                    printf("More than one event ready at the same time... Only parsing first, losing others\n");
                }
                uint8_t quit = 0;
                for(int i = LOAD;i<NM_EVENTS;i++){
                    if(to_poll[i].revents!=0){
                        // Analyze the data
#if DEBUG
                        printf("Got revent from %d: %d !\n",(uint8_t)i,to_poll[i].revents);
#endif
                        if(to_poll[i].revents != POLL_IN) {
                            quit = 1;
                            continue;
                        }
                        unsigned char* addr_to_use = ((i == LOAD )? load_mmap_addr_start : store_mmap_addr_start);
                        struct perf_event_mmap_page* mmap_header = (struct perf_event_mmap_page* )addr_to_use;
                        size_t at = mmap_header->data_tail;
                        while(at<mmap_header->data_head){
                            struct perf_event_header* event_header = (struct perf_event_header*)(addr_to_use + ((mmap_header->data_offset+at)%mmap_header->data_size));
                            at += sizeof(struct perf_event_header);
                            if(event_header->type == PERF_RECORD_SAMPLE){
                                    printf("Different sizes...");
                                }
#if DEBUG
#endif
                            if(event_header->size != sizeof(struct perf_sample)){
                                struct perf_sample* sample = (struct perf_sample*)GET_START_OFFSET();
                                if(i == LOAD) l_count++;
                                else if(i==STORE) s_count++;
                                //printf("dsadsad");
                            }
#if DEBUG
                            else{
                                printf("Got non sample... weird");
                            }
#endif
                            at+=sizeof(struct perf_sample);
                        }
                        mmap_header->data_tail = at;
                    }
                }
                if(quit) break;
            }
            else if(unlikely(read == 0)){
                printf("Poll timed out yet no timeout set...\n");
                break;
            }
            else{
                // Error
                err("Error polling on the file descriptors");
                break;
            }
            //Reset revents
            for(int i=0;i<NM_EVENTS;i++){
                to_poll[i].revents=0;
            }
            //Reenable PEBS
            //ENABLE_PEBS_EVENTS(loads_event_fd,stores_event_fd);
            //Resume the process
            error = kill(child_pid,SIGCONT);
            if(unlikely(error)){
                err("Couldn't resume child process");
            }
        }

        for(enum event_type i = LOAD;i<NM_EVENTS;i++) {
            unsigned char *addr_to_use = ((i == LOAD) ? load_mmap_addr_start : store_mmap_addr_start);
            struct perf_event_mmap_page *mmap_header = (struct perf_event_mmap_page *) addr_to_use;
            size_t at = mmap_header->data_tail;
            while (at < mmap_header->data_head) {
                if(i == LOAD) l_count++;
                else if(i==STORE) s_count++;
                struct perf_event_header *event_header = (struct perf_event_header *) (addr_to_use +
                                                                                       ((mmap_header->data_offset +
                                                                                         at) % mmap_header->data_size));
                at += sizeof(struct perf_event_header);
                if (event_header->type == PERF_RECORD_SAMPLE) {
                    struct perf_sample *sample = (struct perf_sample *) GET_START_OFFSET();
                    //printf("dsadsad");
                }
                at += sizeof(struct perf_sample);
            }
            if(at > mmap_header->data_head) at = mmap_header->data_head;
            mmap_header->data_tail = at;
        }

        printf("Got load %llu,store %llu, woken %llu",l_count,s_count,woken);


        unmap_store:
        error = munmap(store_mmap_addr_start,mmap_size);
        if(error == -1){
            printf("Failed to unmap store map\n");
        }
        store_mmap_addr_start = NULL;
        unmap_load:
        error = munmap(load_mmap_addr_start,mmap_size);
        if(error == -1){
            printf("Failed to unmap load map\n");
        }
        load_mmap_addr_start = NULL;
        close_store_fd:
        error = close(stores_event_fd);
        if(error==-1){
            printf("Failed to close store perf event fd\n");
        }
        close_load_fd:
        error = close(loads_event_fd);
        if(error==-1) {
            printf("Failed to close loads perf event fd\n");
        }
        terminate_child:
        if(kill(child_pid,0) == 0){ //child is still alive
            kill(child_pid,SIGTERM);
        }
        goto free_args; //Skip close_pipe, as we've already closed some of the parts
    }


    close_pipe:
    if(close(pipefd[0])==-1)err("couldn't close pipefd0");
    if(close(pipefd[1]) == -1)err("couldn't close pipefd1");
    free_args:
    // free argument memory
    free(args->executable_args); //never null if file specified, as first argument is always path name
}

int get_remainder_arguments(struct arguments *arguments, int argc, char **argv) {
    uint8_t exec_pos = arguments->executable_path_position;
    if(arguments == NULL || exec_pos >= argc || argv == NULL)
        return EINVAL;

    //We have argc - exec_pos - 1 for-the-executable arguments
    arguments->num_extra_executable_args = argc - exec_pos - 1;
    arguments->executable_args = calloc(arguments->num_extra_executable_args + 2,sizeof(char*));//+1 for program name, +1 for null ptr at the end
    if(arguments->executable_args == NULL){
       return ENOMEM;
    }
    for(uint8_t i=0; i<arguments->num_extra_executable_args+1; i++){ //+1 iteration since the arguments start at the exec name, situated at exec_pos
        arguments->executable_args[i] = argv[i+exec_pos];
    }
    return 0;
}

int main(int argc, char* argv[]){

    struct arguments arguments = {
            .executable_path = NULL,
            .executable_args = NULL,
            .num_extra_executable_args = 0,
            .output_dir = default_output_dir,
            .counter = default_counter,
            .mmap_size = default_mmap_size
    };

    argp_parse(&argp, argc, argv, ARGP_IN_ORDER, 0, &arguments);

    int error = get_remainder_arguments(&arguments,argc,argv);
    if(error){
        err("Error parsing program_arguments");
        return error;
    }

    gather_stats(&arguments);

   return 0;

}
