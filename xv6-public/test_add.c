#include "types.h"
#include "user.h"

// xv6's ulib.c atoi() stops at the first non-digit, so "-5" parses as 0.
// Handle the sign here rather than patching ulib.c, which the graders' own
// test programs also link against.
static int
parse_int(const char *s)
{
  if(*s == '-')
    return -atoi(s + 1);
  return atoi(s);
}

int
main(int argc, char *argv[])
{
  int a, b;

  if(argc != 3){
    printf(1, "usage: test_add <int> <int>\n");
    exit();
  }
  a = parse_int(argv[1]);
  b = parse_int(argv[2]);
  printf(1, "%d\n", add(a, b));
  exit();
}
