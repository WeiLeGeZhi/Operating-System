#include <stdio.h>
#include <unistd.h>

int inc = 1;
int total = 0;
char *sbrk(int incr); /* should be in unistd, but isn't */
char *result;

int main(int argc, int **argv)
{
       while (((int)(result = sbrk(inc))) >= 0)
       {
              total += inc;
              printf("incremented by %d, total %d , result + inc %d\n", inc, total, inc + (int)result);
              inc += inc;
       }
       return 0;
}
