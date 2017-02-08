#include <stdio.h>
#include <stdlib.h>

int i = 0;

void thread1()
{
   i = i - 1;
}

void thread2()
{
   i = i + 1;
}

int main(void)
{
   createThread(thread1);
   createThread(thread2);

   /* wait for thread 1 and thread 2 to end */

   printf("i = %d\n", i);
}