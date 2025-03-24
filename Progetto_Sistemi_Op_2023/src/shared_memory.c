/************************************
*VR445930 - Andrea Doci
*VR443680 - Michele Todeschini
*01/07/23
*************************************/


#include <sys/shm.h>
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include "../inc/shared_memory.h"


 int alloc_shared_memory(key_t shmKey, size_t size) {
    // get, or create, a shared memory segment
     int shmid = shmget(shmKey, size , IPC_CREAT | S_IRUSR | S_IWUSR);
    if (shmid == -1) {
        perror("shmget failed");
        exit(1);
    }
    return shmid;
}


void *get_shared_memory(int shmid, int shmflg) {
    // attach the shared memory
    void *ptr_sh = shmat(shmid, NULL, shmflg);
    if (ptr_sh == (void *)-1) {
        perror("shmat failed or the server is not running");
        exit(1);
    }

    return ptr_sh;
}

void free_shared_memory(void *ptr_sh) {
    // detach the shared memory segments
    if (shmdt(ptr_sh) == -1) {
        perror("shmdt failed");
        exit(1);
    }
}

void remove_shared_memory(int shmid) {
    // delete the shared memory segment
    if (shmctl(shmid, IPC_RMID, NULL) == -1){
        perror("shmctl failed");
        exit(1);
    }
}
