#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
    int c = 3;
    for(int i=0; i<c; i++){

        int p = fork();
        
        if(p == 0){
            printf(1, "in child: %d\n", i+1);
            allocatePage(6);
            sleep(500);
            printf(1, "child %d done!\n", i+1);
            exit();
        }
        sleep(100);
        printf(1, "child: %d, pid: %d\n", i+1, p);
    }

    printf(1, "\n%d child created:\n", c);
    uprocdump();
    printf(1, "\n\n");
    
    for(int i=0; i<c; i++) wait();
    
  exit();
}