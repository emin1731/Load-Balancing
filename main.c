#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

// settings

#define NUM_REPLICAS     3
#define SIM_SECONDS     10
#define MAX_READERS    50

// shared state

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  stateChanged = PTHREAD_COND_INITIALIZER;

static int activeReaders         = 0;
static int replicaReaders[NUM_REPLICAS];
static int writerPending         = 0;
static int writerActive          = 0;
static int writeVersion          = 0;
static volatile int isSimulationRunning = 1;
static char latestContent[128]   = "initial content v0";

static FILE *logFile;

// helper funcs

// sleep for ms
static void sleepMs(int milliseconds)
{
    struct timespec sleepTime = { milliseconds / 1000, (milliseconds % 1000) * 1000000L };
    nanosleep(&sleepTime, NULL);
}

// random int in range
static int randomInRange(int min, int max)
{
    return min + rand() % (max - min + 1);
}

// print one log line (lock should be held)
static void logEvent(const char *tag, const char *who, int replica, const char *content)
{
    // int replicaNumber = (replica >= 0) ? (replica + 1) : 0;
    FILE *outputs[] = { logFile, stdout };

    for (int outputIndex = 0; outputIndex < 2; outputIndex++) {
        fprintf(outputs[outputIndex], "%s %s replica=%d active=%d loads=[%d,%d,%d] writerActive=%s content=%s\n",
                tag,
                who,
                replica,
                activeReaders,
                replicaReaders[0],
                replicaReaders[1],
                replicaReaders[2],
                writerActive ? "yes" : "no",
                content ? content : "no content");

        fflush(outputs[outputIndex]);
    }
}

// get the least loaded replica
static int leastLoaded(void)
{
    int minLoad = replicaReaders[0];
    int candidates[NUM_REPLICAS];
    int candidateCount = 0;

    for (int i = 0; i < NUM_REPLICAS; i++) {
        if (replicaReaders[i] < minLoad) {
            minLoad = replicaReaders[i];
        }
    }

    for (int i = 0; i < NUM_REPLICAS; i++) {
        if (replicaReaders[i] == minLoad) {
            candidates[candidateCount++] = i;
        }
    }

    return candidates[randomInRange(0, candidateCount - 1)];
}

// write same text to all replicas
static void writeReplicas(const char *content)
{
    char replicaFileName[32];
    for (int i = 0; i < NUM_REPLICAS; i++) {

        sprintf(replicaFileName, "replica_%d.txt", i + 1);

        FILE *replicaFile = fopen(replicaFileName, "w");

        if (replicaFile) {
            fputs(content, replicaFile);
            fclose(replicaFile);
        }
    }
}

// reader thread

static void *reader(void *arg)
{
    int readerId = (int)(long)arg;
    char readerName[20];
    sprintf(readerName, "Reader-%d", readerId);

    // if writer waiting, reader waits too
    pthread_mutex_lock(&lock);
    while (writerPending)
        pthread_cond_wait(&stateChanged, &lock);

    // choose least loaded replica
    int selectedReplicaIndex = leastLoaded();
    replicaReaders[selectedReplicaIndex]++;
    activeReaders++;
    logEvent("READ_START", readerName, selectedReplicaIndex, "-");
    pthread_mutex_unlock(&lock);

    // read one replica file once
    char replicaFileName[32];
    sprintf(replicaFileName, "replica_%d.txt", selectedReplicaIndex + 1);
    FILE *replicaFile = fopen(replicaFileName, "r");

    if (replicaFile) {
        char readBuffer[128];
        fgets(readBuffer, sizeof(readBuffer), replicaFile);
        fclose(replicaFile);
    }

    // simulate reading
    sleepMs(randomInRange(50, 200));

    // done reading, update counters
    pthread_mutex_lock(&lock);
    replicaReaders[selectedReplicaIndex]--;
    activeReaders--;
    logEvent("READ_DONE", readerName, selectedReplicaIndex, "-");

    if (activeReaders == 0)
        pthread_cond_broadcast(&stateChanged);
    pthread_mutex_unlock(&lock);

    return NULL;
}

// writer thread

static void *writer(void *arg)
{
    (void)arg;

    while (1) {
        // wait some time before write
        sleepMs(randomInRange(1000, 3000));

        // block new readers
        pthread_mutex_lock(&lock);

        if (!isSimulationRunning) {
            pthread_mutex_unlock(&lock);
            break;
        }

        writerPending = 1;
        logEvent("WRITE_PENDING", "Writer", -1, latestContent);

        // wait until current readers finish
        while (activeReaders > 0)
            pthread_cond_wait(&stateChanged, &lock);

        // mark writer active, then do file I/O outside lock so lock hold time stays short
        writerActive = 1;
        writeVersion++;
        char content[128];
        sprintf(content, "Version %d written by Writer.", writeVersion);

        pthread_mutex_unlock(&lock);
        writeReplicas(content);

        pthread_mutex_lock(&lock);
        snprintf(latestContent, sizeof(latestContent), "%s", content);
        logEvent("WRITE_DONE", "Writer", -1, latestContent);
        writerActive = 0;

        // let readers continue
        writerPending = 0;
        pthread_cond_broadcast(&stateChanged);
        pthread_mutex_unlock(&lock);
    }

    return NULL;
}

// main
int main(void)
{
    srand((unsigned)time(NULL));
    logFile = fopen("simulation.log", "w");
    if (!logFile) {
        perror("simulation.log");
        return 1;
    }

    // init replicas with starting text
    writeReplicas("initial content v0");
    printf("simulation started for %d sec\n\n", SIM_SECONDS);

    // start writer thread
    pthread_t writerThread;
    pthread_create(&writerThread, NULL, writer, NULL);

    // create readers in random intervals
    pthread_t readerThread[MAX_READERS];
    int totalReadersSpawned = 0;
    time_t simulationStartTime = time(NULL);

    while (time(NULL) - simulationStartTime < SIM_SECONDS && totalReadersSpawned < MAX_READERS) {
        sleepMs(randomInRange(100, 500));
        pthread_create(&readerThread[totalReadersSpawned], NULL, reader, (void *)(long)(totalReadersSpawned + 1));
        totalReadersSpawned++;
    }

    // wait all readers
    for (int readerIndex = 0; readerIndex < totalReadersSpawned; readerIndex++)
        pthread_join(readerThread[readerIndex], NULL);

    pthread_mutex_lock(&lock);
    isSimulationRunning = 0;
    pthread_cond_broadcast(&stateChanged);
    pthread_mutex_unlock(&lock);

    pthread_join(writerThread, NULL);

    fclose(logFile);
    return 0;
}