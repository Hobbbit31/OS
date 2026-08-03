#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

// Assignment 1, Part 4: distributed sum of an array.
//
//   assign1_8 <type> <input_file_name>
//     type 0 -> unicast, type 1 -> multicast (sum + variance)
//
// The array is loaded once by the parent before forking, so every worker
// inherits its own copy through fork() and no worker needs to touch the file
// system. Workers compute partial sums over disjoint slices and unicast them
// back to the coordinator, which is the original (parent) process.

#define NWORKERS   8
#define MAXELEMS   1000
#define MSGBYTES   8

// xv6 has no floating point: the kernel does not preserve FPU/SSE state across
// context switches and printf() has no %f. The mean of 1000 elements valued
// 0-9 is not an integer (~4.45 here), so it travels as a fixed-point value
// scaled by SCALE. Squared deviations are then scaled by SCALE*SCALE, which
// makes the variance come out pre-multiplied by 10^4 -- see print_variance().
//
// Overflow check with SCALE=100: the largest deviation is 9*100 = 900, so
// d*d <= 810000, and 1000 elements sum to at most 8.1e8, inside a signed int.
#define SCALE      100

static char filebuf[2048];
static int arr[MAXELEMS];

// Parse whitespace-separated decimal integers. xv6 has no stdio, so this
// scans the raw bytes. Returns the count read, or -1 if the file won't open.
static int
load_array(const char *path, int *out, int max)
{
  int fd, n, i, count, cur, indigit;
  char c;

  if((fd = open(path, O_RDONLY)) < 0)
    return -1;

  count = 0;
  cur = 0;
  indigit = 0;
  while((n = read(fd, filebuf, sizeof(filebuf))) > 0){
    for(i = 0; i < n; i++){
      c = filebuf[i];
      if(c >= '0' && c <= '9'){
        cur = cur * 10 + (c - '0');
        indigit = 1;
      } else if(indigit){
        if(count < max)
          out[count++] = cur;
        cur = 0;
        indigit = 0;
      }
    }
  }
  if(indigit && count < max)
    out[count++] = cur;

  close(fd);
  return count;
}

// Slice bounds for worker i. The remainder goes to the last worker so the
// partitioning stays correct if NWORKERS stops dividing the element count.
static void
slice(int i, int n, int *start, int *end)
{
  int chunk = n / NWORKERS;

  *start = i * chunk;
  *end = (i == NWORKERS - 1) ? n : *start + chunk;
}

// ASSUMPTION: output format. The PDF defers this to a sample program that was
// not supplied, so it is isolated here — change this one function to match.
static void
print_sum(int sum)
{
  printf(1, "Sum: %d\n", sum);
}

// var_e4 is the variance multiplied by 10^4. Printed as a fixed-point decimal
// with four places, zero-padded, since xv6's printf has no %f.
//
// ASSUMPTION: as with print_sum(), the expected format is defined by a sample
// program that was not supplied. Isolated here for easy replacement.
static void
print_variance(int var_e4)
{
  int ip = var_e4 / 10000;
  int fp = var_e4 % 10000;

  printf(1, "Variance: %d.", ip);
  if(fp < 1000)
    printf(1, "0");
  if(fp < 100)
    printf(1, "0");
  if(fp < 10)
    printf(1, "0");
  printf(1, "%d\n", fp);
}

// Unicast phase: fork NWORKERS children, each of which sums its slice and
// sends back an 8-byte payload of {worker id, partial sum}. Packing the id
// alongside the value uses the full fixed-size message and lets the
// coordinator tell partial results apart if it ever needs to.
static int
unicast_sum(int n)
{
  int i, pid, coordinator, total, s, j, start, end;
  int payload[2];
  char msg[MSGBYTES];

  coordinator = getpid();

  for(i = 0; i < NWORKERS; i++){
    pid = fork();
    if(pid < 0){
      printf(1, "assign1_8: fork failed\n");
      exit();
    }
    if(pid == 0){
      slice(i, n, &start, &end);
      s = 0;
      for(j = start; j < end; j++)
        s += arr[j];

      payload[0] = i;
      payload[1] = s;
      memmove(msg, payload, MSGBYTES);
      if(send(getpid(), coordinator, msg) < 0)
        printf(1, "assign1_8: worker %d send failed\n", i);
      exit();
    }
  }

  // Coordinator. recv() blocks, so the order children finish in does not
  // matter; the mailbox buffers whatever arrives before we get here.
  total = 0;
  for(i = 0; i < NWORKERS; i++){
    if(recv(msg) < 0){
      printf(1, "assign1_8: coordinator recv failed\n");
      exit();
    }
    memmove(payload, msg, MSGBYTES);
    total += payload[1];
  }

  for(i = 0; i < NWORKERS; i++)
    wait();

  return total;
}

// Multicast phase: the two-phase algorithm from the spec.
//
//   phase 1  workers unicast partial sums -> coordinator computes the mean
//   phase 2  coordinator MULTICASTS the mean -> workers unblock, compute the
//            sum of squared deviations, and unicast those back
//
// Workers stay alive between the phases, blocked in recv(). They do not need
// to already be blocked when the multicast goes out: the mailbox buffers the
// message, so there is no rendezvous requirement and no lost-wakeup race.
static void
multicast_run(int n)
{
  int i, pid, coordinator, total, s, j, d, start, end, mean_s, sumsq;
  int payload[2];
  int pids[NWORKERS + 1];
  char msg[MSGBYTES];

  coordinator = getpid();

  for(i = 0; i < NWORKERS; i++){
    pid = fork();
    if(pid < 0){
      printf(1, "assign1_8: fork failed\n");
      exit();
    }
    if(pid == 0){
      slice(i, n, &start, &end);

      s = 0;
      for(j = start; j < end; j++)
        s += arr[j];
      payload[0] = i;
      payload[1] = s;
      memmove(msg, payload, MSGBYTES);
      if(send(getpid(), coordinator, msg) < 0)
        printf(1, "assign1_8: worker %d phase-1 send failed\n", i);

      if(recv(msg) < 0){
        printf(1, "assign1_8: worker %d recv failed\n", i);
        exit();
      }
      memmove(payload, msg, MSGBYTES);
      mean_s = payload[0];

      sumsq = 0;
      for(j = start; j < end; j++){
        d = arr[j] * SCALE - mean_s;
        sumsq += d * d;
      }
      payload[0] = i;
      payload[1] = sumsq;
      memmove(msg, payload, MSGBYTES);
      if(send(getpid(), coordinator, msg) < 0)
        printf(1, "assign1_8: worker %d phase-2 send failed\n", i);
      exit();
    }
    pids[i] = pid;
  }
  pids[NWORKERS] = 0;   // sentinel: sys_send_multi walks until non-positive

  total = 0;
  for(i = 0; i < NWORKERS; i++){
    if(recv(msg) < 0){
      printf(1, "assign1_8: coordinator phase-1 recv failed\n");
      exit();
    }
    memmove(payload, msg, MSGBYTES);
    total += payload[1];
  }

  mean_s = (total * SCALE) / n;

  payload[0] = mean_s;
  payload[1] = 0;
  memmove(msg, payload, MSGBYTES);
  if(send_multi(coordinator, pids, msg) < 0)
    printf(1, "assign1_8: multicast failed\n");

  sumsq = 0;
  for(i = 0; i < NWORKERS; i++){
    if(recv(msg) < 0){
      printf(1, "assign1_8: coordinator phase-2 recv failed\n");
      exit();
    }
    memmove(payload, msg, MSGBYTES);
    sumsq += payload[1];
  }

  for(i = 0; i < NWORKERS; i++)
    wait();

  // sumsq is sum((x*SCALE - mean_s)^2), i.e. the variance times SCALE^2 times
  // n. Dividing by n leaves the variance times 10^4.
  print_sum(total);
  print_variance(sumsq / n);
}

int
main(int argc, char *argv[])
{
  int type, n;

  if(argc != 3){
    printf(1, "usage: assign1_8 <type> <input_file_name>\n");
    exit();
  }

  type = atoi(argv[1]);

  n = load_array(argv[2], arr, MAXELEMS);
  if(n < 0){
    printf(1, "assign1_8: cannot open %s\n", argv[2]);
    exit();
  }
  if(n == 0){
    printf(1, "assign1_8: %s contains no elements\n", argv[2]);
    exit();
  }

  if(type == 0){
    print_sum(unicast_sum(n));
  } else if(type == 1){
    multicast_run(n);
  } else {
    printf(1, "assign1_8: type must be 0 (unicast) or 1 (multicast)\n");
  }

  exit();
}
