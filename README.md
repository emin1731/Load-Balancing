# Load Balancing Simulation (Readers-Writers)

This project is a small C simulation of the readers-writers problem with replicated files.

## What It Does

- Creates NUM_REPLICAS text replicas.
- Spawns reader threads that pick the least loaded replica.
- Runs one writer thread that updates all replicas with the same content version.
- Gives writer priority: when a writer is pending, new readers wait.
- Logs all events to simulation.log file and also prints them to the terminal.

## Main Settings

These values are in main.c:

```c
#define NUM_REPLICAS     3
#define SIM_SECONDS     10
#define MAX_READERS     50
```

- NUM_REPLICAS - how many replica files are used.
- SIM_SECONDS - simulation duration.
- MAX_READERS - maximum number of reader threads to spawn.

## Build And Run

From the project root:

```bash
gcc -Wall -Wextra -g3 main.c -o output/main
cd output
./main
```

After running, check:

- `output/simulation.log`
- `output/replica_1.txt`, `output/replica_2.txt`, `output/replica_3.txt`

## Log Format

Example:

```text
READ_START Reader-3 replica=1 active=1 loads=[1,0,0] writerActive=no content=-
WRITE_DONE Writer active=0 loads=[0,0,0] writerActive=yes content=Version 1 written by Writer.
```

- active: total active readers.
- loads=[a,b,c]: current readers on each replica.
- writerActive: yes when writer is writing, otherwise no.

## Notes

- Replica choice is based on current load.
- Tie cases may still favor lower-index replicas more often.
