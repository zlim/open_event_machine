/*
 *   Copyright (c) 2013, Nokia Siemens Networks
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions are met:
 *       * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *       * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *       * Neither the name of Nokia Siemens Networks nor the
 *         names of its contributors may be used to endorse or promote products
 *         derived from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 *   ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 *   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY
 *   DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *   LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *   ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/**
 * @file
 *
 * Event Machine test common initialization functions
 *
 */

#define _GNU_SOURCE /* See feature_test_macros(7) */
#include <pthread.h>
#include <sched.h>

#include "event_machine.h"
#include "environment.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "em_intel_packet.h" // for init_eth_pmd_drivers()

#include "test_common.h"



#define CORE_MASK_STR_LEN (32)


#define LOWEST_BIT(n)       ((n) & (~((n)-1)))
#define CLEAR_LOWEST_BIT(n) ((n) &   ((n)-1) )



/*
 * Local data types
 */


/**
 * Application process command line parameters (modified from argc, argv[])
 */
typedef struct
{
  int    argc; /**< process argument count */
  
  char **argv; /**< process arguments, allocated */
  
} proc_args_t;



/*
 * Local function prototypes
 */
static sync_t *
init_sync(int core_count);

static char *
init_proc_core_mask(int proc_idx, em_core_mask_t phys_mask);

static proc_args_t
init_proc_argv(int argc, char *argv[], char *mask_str, int argv_phys_mask_idx, int my_argv_idx);

static void
install_sigchld_handler(void);

static void
fork_em_procs(em_conf_t *em_conf, sem_t *block_child_sem); 
 
static void
sigchld_handler(int sig);

static int
get_phys_core_idx(int n, em_core_mask_t phys_mask);

static int
core_set_affinity(int phys_core, int core_count);


/*
 * Functions
 */


/**
 * Parse and store relevant command line arguments. Set config options for both
 * application and EM.
 *
 * EM options are stored into em_conf and application specific options into appl_conf.
 * Note that both application and EM parsing is done here since EM should not, by design,
 * be concerned with the parsing of options, instead em_conf_t specifies the options
 * needed by the EM-implementation (HW, device and env specific).
 *
 * @param argc       Command line argument count
 * @param argv[]     Command line arguments
 * @param em_conf    EM config options parsed from argv[]
 * @param appl_conf  Application config options parsed from argv[]
 */
void
parse_args(int argc, char *argv[],
           em_conf_t      *em_conf   /* out param */,
           appl_conf_t    *appl_conf /* out param */)
{
  int    i;
  int    my_argv_idx = 0;
  int    my_argc     = 0;
  char **my_argv     = NULL;
  char  *argv_save   = NULL;


  /*
   * Parse the mandatory DPDK args (-c, -n) and save the core mask.
   * Note:   Use '+' at the beginning of optstring - don't permute the contents of argv[].
   * Note 2: Stops at "--"
   */
  while(1)
  {
    int opt;
    int long_index;
    static struct option longopts[] = {
      {"help", no_argument, NULL, 'h'}, // return 'h'
      {NULL, 0, NULL, 0}
    };

    opt = getopt_long(argc, argv, "+c:n:h", longopts, &long_index);

    if(opt == -1) {
      break; // No more options
    }

    switch (opt)
    {
      case 'c':
        {
          unsigned long long mask;
          char              *mask_str = optarg;
          char              *endptr = NULL;

          /* parse hexadecimal string */
          mask = strtoull(mask_str, &endptr, 16/*base-16*/);
          if((mask_str[0] == '\0') || (endptr == NULL) ||
             (*endptr != '\0')     || (mask == 0))
          {
            APPL_EXIT_FAILURE("Invalid coremask (%s) given\n", mask_str);
          }

          /* Store the core mask for EM - usage depends on the process-per-core or
           * thread-per-core mode selected. */
          em_conf->phys_mask.u64[0]  = (uint64_t) mask;
          em_conf->core_count = em_core_mask_count(&em_conf->phys_mask);

          appl_conf->phys_mask_idx = optind-1;


          printf("Coremask:   0x%"PRIx64"\n"
                 "Core Count: %i\n",
                 (uint64_t) mask, em_conf->core_count);

        }
        break;

      case 'n':
        /* Let DPDK worry about the number of memory channels*/
        break;

      case 'h':
        usage(argv[0]);
        exit(EXIT_SUCCESS);
        break;

      default:
        break;
    }
  }

  optind = 1; /* reset 'extern optind' from the getopt lib */



  /*
   * Skip the DPDK cmd line args and find the user/appl args starting with "--".
   */
  for(i = 0; i < argc; i++)
  {
    if(!strcmp(argv[i], "--"))
    {
      my_argv_idx = i;
      break;
    }
  }

  if(my_argv_idx)
  {
    appl_conf->my_argv_idx = my_argv_idx;

    argv_save = argv[my_argv_idx];
    argv[my_argv_idx] = argv[0]; // prog name

    my_argv = &argv[my_argv_idx];
    my_argc = argc - my_argv_idx;
  }



  /*
   * Parse the application & EM arguments
   * Note:   Use '+' at the beginning of optstring - don't permute the contents of argv[].
   * Note 2: parse my_argc, my_argv, i.e. args starting after "--"
   */
  while(1)
  {
    int opt;
    int long_index;
    static struct option longopts[] = {
      {"process-per-core", no_argument, NULL, 'p'}, // return 'p'
      {"thread-per-core",  no_argument, NULL, 't'}, // return 't'
      {"help",             no_argument, NULL, 'h'}, // return 'h'
      {NULL, 0, NULL, 0}
    };

    opt = getopt_long(my_argc, my_argv, "+pth", longopts, &long_index);

    if(opt == -1) {
      break; // No more options
    }

    switch (opt)
    {
      case 'p':
        em_conf->process_per_core = 1;
        break;

      case 't':
        em_conf->thread_per_core = 1;
        break;

      case 'h':
        usage(argv[0]);
        exit(EXIT_SUCCESS);
        break;

      default:
        break;
    }
  }

  optind = 1; /* reset 'extern optind' from the getopt lib */


  em_conf->em_instance_id = 0; // Currently only support one (1) EM-instance

  // Sanity check:
  if(!(em_conf->process_per_core ^ em_conf->thread_per_core))
  {
    usage(argv[0]);
    APPL_EXIT_FAILURE("Select EITHER process-per-core (-p) OR thread-per-core (-t)!\n");
  }

  if(em_conf->process_per_core) {
    printf("Process-per-core mode selected!\n");
  }
  if(em_conf->thread_per_core) {
    printf("Thread-per-core mode selected!\n");
  }



  /*
   * Set application specific config
   */
  if(my_argc > 0)
  {
    (void) strncpy(appl_conf->name, NO_PATH(my_argv[0]), APPL_NAME_LEN);
    appl_conf->name[APPL_NAME_LEN-1] = '\0'; // always '\0'-terminate string
  }

  if(em_conf->thread_per_core)
  {
    appl_conf->num_procs   = 1;
    appl_conf->num_threads = em_conf->core_count;
  }
  else
  {
    appl_conf->num_procs   = em_conf->core_count;
    appl_conf->num_threads = appl_conf->num_procs;
  }


  /*
   * Restore original argv[] (replace 'progname' with  "--" if needed),
   * not affected by argv[] permutations done by getopt since
   * save was done on the first of my_argv[].
   */
  if(my_argv_idx) {
    argv[my_argv_idx] = argv_save;
  }
}



/**
 * Initialize startup synchronization
 */
static sync_t *
init_sync(int core_count)
{
  sync_t *sync;

  sync = mmap(NULL, sizeof(sync_t), PROT_READ | PROT_WRITE,
              MAP_SHARED | MAP_ANONYMOUS, -1, 0);

  if(sync == MAP_FAILED)
  {
    perror("mmap(MAP_SHARED|MAP_ANONYMOUS)");
    return NULL;
  }
  
  /* Init semaphore as 'taken' to block child processes (if any)*/
  if(sem_init(&sync->sem, 1, 0) == -1)
  {
    perror("sem_init()");
    return NULL;
  }

  if(pthread_barrierattr_init(&sync->attr)) {
    perror("pthread_barrierattr_init()");
    return NULL;
  }

  if(pthread_barrierattr_setpshared(&sync->attr, PTHREAD_PROCESS_SHARED) != 0) {
    perror("pthread_barrierattr_setpshared()");
    return NULL;
  }

  if(pthread_barrier_init(&sync->barrier, &sync->attr, core_count) != 0) {
    perror("pthread_barrier_init()");
    return NULL;
  }

  return sync;
}



/**
 * Initialize the run-time environment for the parent process before forking children.
 */
int
init_rt_env(int argc, char *argv[], em_conf_t *em_conf, appl_conf_t *appl_conf)
{
  char       *mask_str;
  proc_args_t proc_args;
  int         ret;
  sync_t     *startup_sync = NULL;
  


  /*
   * Initialize the application startup sync vars
   */
  appl_conf->startup_sync = init_sync(em_conf->core_count);

  if(appl_conf->startup_sync == NULL) {
    APPL_EXIT_FAILURE("init_sync() fails!");
  }

  
  if(em_conf->thread_per_core)
  {
    /* 
     * THREAD-PER-CORE MODE
     */

    /*
     * Initialize the DPDK - called once, creates threads etc.
     */
    ret = rte_eal_init(argc, argv);
    if(ret < 0) {
      APPL_EXIT_FAILURE("Cannot init the Intel DPDK EAL!");
      return -1;
    }
  
    if(em_conf->pkt_io)
    {
      /* Init the eth poll mode drivers (done here to be similar to proc-per-core setup below) */
      int nb_ports = init_eth_pmd_drivers();
      
      if(nb_ports < 0)
      {
        APPL_EXIT_FAILURE("Cannot init the Eth Poll Mode Drivers (nb_ports=%i)!", nb_ports);
        return -2;
      }      
    }
    
    /* Only one process in EM-thread-per-core mode */
    em_conf->proc_idx = 0;
    
  }
  else if(em_conf->process_per_core)
  {
    /* 
     * PROCESS-PER-CORE MODE
     */


    /*
     * Initialize the startup sync vars
     */
    startup_sync = init_sync(em_conf->core_count);
    
    if(startup_sync == NULL) {
      APPL_EXIT_FAILURE("init_sync() fails!");
    }

    
    /*
     * Create a signal handler for the SIGCHLD signal that is sent to the parent process
     * when a forked child process dies.
     */
    install_sigchld_handler();
    
    
    /* 
     * Allocate and init the core mask string for this process.
     * em_conf.phys_mask is split into several masks containing only one set bit each.
     */
    mask_str = init_proc_core_mask(em_conf->proc_idx, em_conf->phys_mask);
    if(mask_str == NULL) {
      APPL_EXIT_FAILURE("init_proc_core_mask() fails!");
      return -3;
    }
    
    
    /*
     * Allocate and init the argc,argv for this process
     */
    proc_args = init_proc_argv(argc, argv, mask_str, appl_conf->phys_mask_idx, appl_conf->my_argv_idx);
    if(proc_args.argv == NULL) {
      APPL_EXIT_FAILURE("init_proc_argv() fails!");
      return -4;
    }
    
    
    /* Print the used CL args for the process */
    //for(int i = 0; i < proc_args.argc; i++) {
    //  printf("Idx%02i proc_args.argv[%i]: %s\n", em_conf->proc_idx, i, proc_args.argv[i]);
    //}
    
    
    /*
     * Initialize the DPDK - called once per process
     */
    ret = rte_eal_init(proc_args.argc, proc_args.argv);
    if(ret < 0) {
      APPL_EXIT_FAILURE("Cannot init the Intel DPDK EAL!");
      return -5;
    }
  
    if(em_conf->pkt_io)
    {
      /* 
       * Init the eth poll mode drivers before the fork to ensure same setup for each process.
       * Init after fork led in some cases to errors when not all devices were found by all processes...
       */
      int nb_ports = init_eth_pmd_drivers();

      if(nb_ports < 0)
      {
        APPL_EXIT_FAILURE("Cannot init the Eth Poll Mode Drivers (nb_ports=%i)!", nb_ports);
        return -6;
      }
    }
    
    
    /* Free allocated resources needed only in startup */
    free(mask_str);
    free(proc_args.argv);
    
    
    /*
     * Fork child processes for EM.
     * Blocks all child processes (using startup_sync->sem, parent signals sem later).
     * Sets em_conf.proc_idx (0 ... em_conf.core_count-1)
     */
    fork_em_procs(em_conf, &appl_conf->startup_sync->sem);
    
    /**********************************
     * All processes run from here.
     **********************************/
    
    
    /* 
     * Allow next process (if any) to run.
     * The fork_em_procs() func blocked all children (in proc-per-core mode)
     */
    if(sem_post(&appl_conf->startup_sync->sem) == -1) {
      APPL_EXIT_FAILURE("sem_post() fails (errno(%i)=%s)", errno, strerror(errno));
    }
  }
  
  return 0;
}


/**
 * Create a process specific core mask (with only one bit set) from the global 
 * physical core mask (which has a bit set for each core to use)
 *
 * @param proc_idx  Process index in the range [0 ... proc_count-1]
 * @param phys_mask Physical core mask containing a bit set for each core to be used.
 *
 * @return An allocated & filled core mask for the calling process.
 */
static char *
init_proc_core_mask(int proc_idx, em_core_mask_t phys_mask)
{
  char    *core_mask_str;
  uint64_t mask;
  uint64_t tmp_mask;
  int      i;


  core_mask_str = malloc(CORE_MASK_STR_LEN * sizeof(char));
  
  if(core_mask_str == NULL)
  {
    perror("malloc()");
    return NULL;
  }

  mask = phys_mask.u64[0];

  for(i = 0; i <= proc_idx; i++)
  {
    tmp_mask = LOWEST_BIT(mask);
    mask     = CLEAR_LOWEST_BIT(mask);
  }
  
  (void) snprintf(core_mask_str, CORE_MASK_STR_LEN, "0x%"PRIx64"", tmp_mask);
  core_mask_str[CORE_MASK_STR_LEN-1] = '\0';
  
  return core_mask_str;
}


/**
 * Return the position of the Nth (='n') set bit in 'phys_mask', range [0 ... MaxCores-1].
 */
static int
get_phys_core_idx(int n, em_core_mask_t phys_mask)
{
  uint64_t mask;
  int      i;
  int      bit_idx;


  mask = phys_mask.u64[0];

  for(i = 0; i < n; i++)
  {
    mask = CLEAR_LOWEST_BIT(mask);
  }
  
  mask = LOWEST_BIT(mask);
  
  for(i = -1; mask > 0; i++) {
    mask = mask >> 1;
  }
  
  
  bit_idx = i;
  return bit_idx;
}



/**
 * Initialize command line arguments for a process.
 * 
 * Create process specific CL args based on the original CL args.
 * Used in EM proc-per-core-mode only.
 * 
 * @param argc                Original argc 
 * @param argv                Original argv
 * @param mask_str            Physical core mask for the calling process (in string format) - only one bit set
 * @param argv_phys_mask_idx  Location of the phys mask cmd line option containing the phys core mask in the aroginal argv[]
 * @param my_argv_idx         Start index in argv[] of user args (i.e. non-DPDK args)
 *
 * @return Returns a filled proc_args_t with argc,argv[] filled for the calling process
 */
static proc_args_t
init_proc_argv(int argc, char *argv[], char *mask_str, int argv_phys_mask_idx, int my_argv_idx)
{
  proc_args_t proc_args;
  int         i; 
  
  
  proc_args.argc = (my_argv_idx > 0) ? (my_argv_idx + 1) : (argc + 1);
  proc_args.argv = malloc((proc_args.argc+1) * sizeof(char *));

  if(proc_args.argv == NULL) {
    perror("malloc()");
    return proc_args;
  }

  for(i = 0; i < (proc_args.argc - 1); i++) {
    proc_args.argv[i] = argv[i];
  }
  
  
  /*
   * Update, for each process(=EM-core), the argument in proc_argv[] 
   * that contains the DPDK physical core mask.
   */
  proc_args.argv[argv_phys_mask_idx] = mask_str;  
  
  proc_args.argv[i]   = "--proc-type=auto";
  proc_args.argv[i+1] = NULL; // malloc has reserved space for this extra NULL termination.

  return proc_args;
}



/**
 * Create a signal handler for the SIGCHLD signal that is sent to the parent process
 * when a forked child process dies.
 */
static void
install_sigchld_handler(void)
{
  struct sigaction sa;
  
  
  sigemptyset(&sa.sa_mask);
  
  sa.sa_flags   = 0;
  sa.sa_handler = sigchld_handler;
  
  if(sigaction(SIGCHLD, &sa, NULL) == -1) {
    APPL_EXIT_FAILURE("sigaction() fails (errno(%i)=%s)", errno, strerror(errno));
  }
}



/**
 * Signal handler for SIGCHLD (parent receives when child process dies)
 */
static void
sigchld_handler(int sig)
{
  int   status;
  pid_t child;
  // int tmp_errno = errno; // no need to save&restore errno when we end with _exit()


  (void) sig; // unused

  /* Nonblocking waits until no more dead children are found */
  do {
    child = waitpid(-1, &status, WNOHANG);
  } while(child > 0);

  if((child == -1) && (errno != ECHILD)) {
    _exit(EXIT_FAILURE);
  }

  /* 
   * Exit the parent process - triggers SIGTERM in the remaining children
   * (set by prctl(PR_SET_PDEATHSIG, SIGTERM)).
   */
  _exit(EXIT_SUCCESS);
  
  //errno = tmp_errno;
}



/**
 * Set affinity for a thread - pin EM process to a specific core = EM-core
 * Only used in EM-proc-per-core mode.
 *
 * Code similar to the DPDK function "static int eal_thread_set_affinity(void)".
 */
static int
core_set_affinity(int phys_core, int core_count)
{
  int s;
  pthread_t thread_id;


#if defined(CPU_ALLOC) /* glibc 2.7 or newer */
  size_t     size;
  cpu_set_t *cpusetp;

  cpusetp = CPU_ALLOC(core_count);
  if(cpusetp == NULL) {
    return -1;
  }

  size = CPU_ALLOC_SIZE(core_count);
  CPU_ZERO_S(size, cpusetp);
  CPU_SET_S(phys_core, size, cpusetp);

  thread_id = pthread_self();
  s = pthread_setaffinity_np(thread_id, size, cpusetp);
  if(s != 0) {
    CPU_FREE(cpusetp);
    return -2;
  }

  CPU_FREE(cpusetp);
  
#else /* glibc 2.3.3 to glibc 2.6 */
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(phys_core, &cpuset);
  
  (void) core_count; /* unused */

  thread_id = pthread_self();
  s = pthread_setaffinity_np(thread_id, sizeof(cpuset), &cpuset);
  if(s != 0) {
    return -1;
  } 
#endif

  return 0;
}




/**
 * Fork child processes for EM (in-process-per-core mode)
 * 
 * Each forked child is blocked by a semaphore that the parent will signal
 * when it is done (outside of this function).
 *
 * Sets the em_conf.proc_idx
 *
 * @param em_conf          EM configuration
 * @param block_child_sem  Semaphore to be used to properly synhronize child processes during setup
 */
static void
fork_em_procs(em_conf_t *em_conf, sem_t *block_child_sem)
{
  int proc_idx;
  int phys_core;
  int i, ret;
  
  struct rte_config *const rte_config = rte_eal_get_configuration();
  
  
  proc_idx = 0; /* Parent index = 0*/
  
  for(i = 1; i < em_conf->core_count; i++)
  {
    pid_t child_pid = fork();

    if(child_pid == 0) /* Child */
    {
      /* Request SIGTERM if parent dies */
      prctl(PR_SET_PDEATHSIG, SIGTERM);
      /* Parent died already? */
      if(getppid() == 1) { 
        kill(getpid(), SIGTERM);
      }
      
      /* Block all children, parent signals sem when done initializing */
      if(sem_wait(block_child_sem) == -1) {
        APPL_EXIT_FAILURE("sem_wait() fails (errno(%i)=%s)", errno, strerror(errno));
      }

      proc_idx = i; /* Child index = i */
      break;
    }
    else if(child_pid == -1) {
      APPL_EXIT_FAILURE("fork() fails (errno(%i)=%s)", errno, strerror(errno));
    }
  }
  
  
  /* Update the EM-conf for each process */
  em_conf->proc_idx = proc_idx;
  
  
  phys_core = get_phys_core_idx(proc_idx, em_conf->phys_mask);
  if(phys_core < 0)
  {
    APPL_EXIT_FAILURE("get_phys_core_idx() fails (ret=%i, proc_idx=%i, phys_mask=0x%"PRIx64")",
                      phys_core, proc_idx, em_conf->phys_mask.u64[0]);
  }
  
  ret = core_set_affinity(phys_core, em_conf->core_count);
  if(ret < 0)
  {
    APPL_EXIT_FAILURE("core_set_affinity(phys_core=%i, core_count=%i) fails (ret=%i)",
                      phys_core, em_conf->core_count, ret);
  }
  
  
  
  /*
   * Change DPDK internal info related to core-id and which core is used. After a fork
   * each child will have exactly the same info as the parent - need to change this.
   */
    
   /* Set per-thread variable containing the physical core-id. */
   RTE_PER_LCORE(_lcore_id) = phys_core;
   /* Master core for this process is the phys core */
   rte_config->master_lcore = phys_core;
   // rte_config->lcore_count = OK as set by parent
   
   if(em_conf->proc_idx != 0)
   {
     // Change child proc type, parent uses 'RTE_PROC_PRIMARY'
     rte_config->process_type = RTE_PROC_SECONDARY;
   }
   
   /* Init the per-process array that tracks which cores are in use. */
   for(i = 0; i < RTE_MAX_LCORE; i++)
   {
     if(i == phys_core) {
       rte_config->lcore_role[phys_core] = ROLE_RTE;
     }
     else {
       rte_config->lcore_role[i] = ROLE_OFF;
     }
   }

  
  // printf("\n%s(): EM-proc%02i, PhysCore:%02i(0x%04"PRIx64"), RTECore:%02i, Mask:0x%04"PRIx64"\n\n",
  //        __func__, em_conf->proc_idx, phys_core, (uint64_t)(1 << phys_core), RTE_PER_LCORE(_lcore_id), em_conf->phys_mask.u64[0]); fflush(NULL);
}



/**
 * Launch the application by calling the start function 'appl_start_fptr' with argument 'arg'
 * as given by the user.
 *
 * 
 */
void
launch_application(int (*appl_start_fptr)(appl_conf_t *), appl_conf_t *appl_conf)
{
  int lcore_id;
  
  
  if(appl_start_fptr != NULL)
  {
    RTE_LCORE_FOREACH_SLAVE(lcore_id)
    {
      /* only in EM-thread-per-core mode - no slaves in EM-proc-per-core mode*/
      rte_eal_remote_launch((int (*)(void *))appl_start_fptr, appl_conf, lcore_id);
    }
    
    /* 
     * Call on master lcore thread in EM-thread-per-core mode,
     *  OR 
     * Called by each process in EM-process-per-core mode.
     */
    (void) appl_start_fptr(appl_conf);    
  }
}



/**
 * Print usage information
 */
void
usage(char *progname)
{
  printf("\n"
         "Usage: %s DPDK-OPTIONS -- APPL&EM-OPTIONS\n"
         "  E.g. %s -c 0xfe -n 4 -- -p\n"
         "\n"
         "Open Event Machine example application.\n"
         "\n"
         "Mandatory DPDK-OPTIONS options:\n"
         "  -c                      Coremask (hex) - select the cores that the application&EM will run on.\n"
         "  -n                      DPDK-option: Number of memory channels in the system\n"
         "\n"
         "Optional [DPDK-OPTIONS]\n"
         " (see DPDK manuals for an extensive list)\n"
         "\n"
         "Mandatory APPL&EM-OPTIONS:\n"
         "  -p, --process-per-core  Running OpenEM with one process per core.\n"
         "  -t, --thread-per-core   Running OpenEM with one thread per core.\n"
         "\n"
         "  Select EITHER -p OR -t, but not both!\n"
         "\n"
         "Optional [APPL&EM-OPTIONS]\n"
         "  -h, --help              Display help and exit.\n"
         "\n"
         ,
         NO_PATH(progname), NO_PATH(progname)
        );
}

