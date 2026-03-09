/*
 * Readers-Writers Problem with Load Balancing
 * ---------------------------------------------------------
 * Compile: gcc -std=c11 -Wall -pthread -o rw readers_writers_simple.c
 * Run    : ./rw
 *
 * Synchronisation (single mutex + one condition variable):
 *   - writer sets writer_pending=1  →  new readers wait
 *   - writer waits until active_readers==0  →  then writes
 *   - after writing, writer clears flag and broadcasts to wake readers
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

// Settings

#define NUM_REPLICAS     3
#define SIM_SECONDS     20
#define MAX_READERS    200

// Shared state

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  lock_condition = PTHREAD_COND_INITIALIZER;

static int active_readers        = 0;
static int replica_readers[NUM_REPLICAS];  // readers per replica
static int writer_pending        = 0;
static int write_version         = 0;
static volatile int is_simulation_running  = 1;

static FILE *log_fp;

// Helpers

// Sleep for ms milliseconds
static void sleep_ms(int ms)
{
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&t, NULL);
}

// Random integer in [lo, hi]
static int rrand(int lo, int hi)
{
    return lo + rand() % (hi - lo + 1);
}

// Timestamp string "HH:MM:SS.mmm"
static void tstamp(char *buf)
{
    struct timespec ts;
    struct tm tm;
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm);
    strftime(buf, 16, "%H:%M:%S", &tm);
    sprintf(buf + 8, ".%03d", (int)(ts.tv_nsec / 1000000));
}

// Log one line to file + stdout (call with lock held for consistency)
static void log_event(const char *tag, const char *who, int replica)
{
    char ts[16];
    tstamp(ts);

    char rep[32] = "";
    if (replica >= 0) sprintf(rep, " replica=%d", replica + 1);

    fprintf(log_fp, "%s [%-14s] %-12s%s active=%d loads=[%d,%d,%d] writer=%s\n",
            ts, tag, who, rep, active_readers,
            replica_readers[0], replica_readers[1], replica_readers[2],
            writer_pending ? "yes" : "no");
    fflush(log_fp);

    printf("%s [%-14s] %-12s%s active=%d loads=[%d,%d,%d] writer=%s\n",
           ts, tag, who, rep, active_readers,
           replica_readers[0], replica_readers[1], replica_readers[2],
           writer_pending ? "yes" : "no");
}

// Return index of least-loaded replica (call with lock held)
static int least_loaded(void)
{
    int best = 0;
    for (int i = 1; i < NUM_REPLICAS; i++)
        if (replica_readers[i] < replica_readers[best])
            best = i;
    return best;
}

// Write content to all replica files
static void write_replicas(const char *content)
{
    char name[32];
    for (int i = 0; i < NUM_REPLICAS; i++) {
        sprintf(name, "replica_%d.txt", i + 1);
        FILE *f = fopen(name, "w");
        if (f) { fputs(content, f); fclose(f); }
    }
}

// Reader thread

static void *reader(void *arg)
{
    int  id = (int)(long)arg;
    char name[20];
    sprintf(name, "Reader-%d", id);

    // reader sjould wait if a writer is pending. 
    pthread_mutex_lock(&lock);
    while (writer_pending)
        pthread_cond_wait(&lock_condition, &lock);

    // this will choose the least loaded replica for reading.  
    int r = least_loaded();
    replica_readers[r]++;
    active_readers++;
    log_event("READ_START", name, r);
    pthread_mutex_unlock(&lock);

    // the read process is stimulated with the sleeping for a random time.
    sleep_ms(rrand(50, 200));

    // exit critical section and update the state
    pthread_mutex_lock(&lock);
    replica_readers[r]--;
    active_readers--;
    log_event("READ_DONE", name, r);
    if (active_readers == 0)
        pthread_cond_broadcast(&lock_condition); // wake writer if it's waiting
    pthread_mutex_unlock(&lock);

    return NULL;
}

// Writer thread

static void *writer(void *arg)
{
    (void)arg;

    while (is_simulation_running) {
        // the writer should wait for a random time before writing
        sleep_ms(rrand(1000, 3000));
        if (!is_simulation_running) break;

        // when it writes it is bloking the readers 
        pthread_mutex_lock(&lock);
        writer_pending = 1;
        log_event("WRITE_PENDING", "Writer", -1);

        // wait until all active readers are done
        while (active_readers > 0)
            pthread_cond_wait(&lock_condition, &lock);

        // this will write all replicas 
        write_version++;
        char content[128];
        sprintf(content, "Version %d written by Writer.", write_version);
        write_replicas(content);
        log_event("WRITE_DONE", "Writer", -1);

        // it finishes and allows readers to proceed
        writer_pending = 0;
        pthread_cond_broadcast(&lock_condition);
        pthread_mutex_unlock(&lock);
    }

    return NULL;
}

// Main
int main(void)
{
    srand((unsigned)time(NULL));
    log_fp = fopen("simulation.log", "w");

    // initialize replicas with initial content
    write_replicas("initial content v0");
    printf("simulation started (%d seconds)\n\n", SIM_SECONDS);

    /* Start writer */
    pthread_t writer_thread;
    pthread_create(&writer_thread, NULL, writer, NULL);

    /* Spawn readers at random intervals for SIM_SECONDS */
    pthread_t reader_thread[MAX_READERS];
    int n = 0;
    time_t start = time(NULL);
    while (time(NULL) - start < SIM_SECONDS && n < MAX_READERS) {
        sleep_ms(rrand(100, 500));
        pthread_create(&reader_thread[n], NULL, reader, (void *)(long)(n + 1));
        n++;
    }

    /* Wait for all readers */
    for (int i = 0; i < n; i++)
        pthread_join(reader_thread[i], NULL);

    is_simulation_running = 0;
    pthread_join(writer_thread, NULL);

    /* Summary */
    printf("\ndone: %d readers; %d writes\n", n, write_version);
    fprintf(log_fp, "\ndone: %d readers; %d writes\n", n, write_version);
    fclose(log_fp);
    return 0;
}