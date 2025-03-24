/************************************
*VR445930 - Andrea Doci
*VR443680 - Michele Todeschini
*01/07/23
*************************************/


#include <sys/sem.h>

#include "../inc/semaphore.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

void semOp (int semid, unsigned short sem_num, short sem_op, short sem_flg) {
    struct sembuf sop = {.sem_num = sem_num, .sem_op = sem_op, .sem_flg = sem_flg};

    if (semop(semid, &sop, 1) == -1 && errno != EINTR){
        perror("semop failed");
        exit(1);
    }
}
