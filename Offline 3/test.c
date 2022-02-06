/* This utility runs unit tests*/
#include "types.h"
#include "stat.h"
#include "user.h"
#include "pstat.h"

void spin() {
    unsigned y = 0;
    //int pid = getpid();

    while (1) {
        y = 0;
        while (y < (10000)) {
            y++;
        }
    }
}

void printpinfo(int pid)
{
    struct pstat pinfo = {0};
	getpinfo(&pinfo);
	int i;
    
    for (i = 0; i < NPROC; i++) {
        if(pinfo.pid[i] == pid) {
		    printf(1,
                    "%d\t%d\t%d\t%d\n", 
                    pinfo.pid[i],
                    pinfo.ticks[i],
                    pinfo.tickets[i],
                    pinfo.inuse[i]
                    );
        }
    }
}

int ps(){
	// Get process info first
	struct pstat pinfo = {0};
    
    
    //while(1){
        //sleep(500);
        if(-1 == getpinfo(&pinfo)){
		    printf(1, "\n\t FAILURE\n");return 0;
	    }
       
        printf(1,"PID\tTicks\tTickets\tInUse\n");

        for(int i=0;i<NPROC;i++){
            if(pinfo.pid[i]==0)continue;
            printf(1,
                    "%d\t%d\t%d\t%d\n", 
                    pinfo.pid[i],
                    pinfo.ticks[i],
                    pinfo.tickets[i],
                    pinfo.inuse[i]
                    );
        }
     
   // }
	return 1;
}

int main(int argc, char *argv[])
{
	 int pid1, pid2, pid3;
      
    if ((pid1 = fork()) == 0) {
        //int pp1 = getpid();
	    //printf(0, "Process started with PID %d\n\n", pp1);
        settickets(10);
        spin();
	    //printpinfo(pp1);
        //printf(0, "Process with PID %d finished!\n\n", pp1);
        //exit(); 
    }
    else if ((pid2 = fork()) == 0) {
        //int pp2 = getpid();
	    //printf(0, "Process started with PID %d\n\n", pp2);
        settickets(20);
        spin();
	    //printpinfo(pp2);
        //printf(0, "Process with PID %d finished!\n\n", pp2);
        //exit();
    }
    else if ((pid3 = fork()) == 0) {
        //int pp3 = getpid();
	    //printf(0, "Process started with PID %d\n\n", pp3);
        settickets(50);
        spin();
	    //printpinfo(pp3);
        //printf(0, "Process with PID %d finished!\n\n", pp3);
        //exit();
    }
    while(1){
        printf(1,"PID\tTicks\tTickets\tInUse\n");
        printpinfo(pid1);
        printpinfo(pid2);
        printpinfo(pid3);
        sleep(200);
    }
    wait();
    wait();
    wait();
    exit();
}