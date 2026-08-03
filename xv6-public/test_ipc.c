#include "types.h"
#include "user.h"

// Round-trip test for Part 3 unicast: the parent sends a string to the child,
// the child replies with an integer. The parent deliberately sends before the
// child is guaranteed to have called recv(), which exercises the mailbox
// buffering rather than requiring a rendezvous.
int
main(int argc, char *argv[])
{
  int pid, parent, reply;
  char out[8], in[8];

  parent = getpid();

  pid = fork();
  if(pid < 0){
    printf(1, "fork failed\n");
    exit();
  }

  if(pid == 0){
    if(recv(in) < 0){
      printf(1, "child: recv failed\n");
      exit();
    }
    printf(1, "child received: %s\n", in);

    memset(out, 0, sizeof(out));
    reply = 42;
    memmove(out, &reply, sizeof(reply));
    if(send(getpid(), parent, out) < 0)
      printf(1, "child: send failed\n");
    exit();
  }

  memset(out, 0, sizeof(out));
  strcpy(out, "hello");
  if(send(parent, pid, out) < 0)
    printf(1, "parent: send failed\n");

  if(recv(in) < 0){
    printf(1, "parent: recv failed\n");
    exit();
  }
  memmove(&reply, in, sizeof(reply));
  printf(1, "parent received: %d\n", reply);

  wait();
  exit();
}
