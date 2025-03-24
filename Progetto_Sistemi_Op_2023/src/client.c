/************************************
*VR445930 - Andrea Doci
*VR443680 - Michele Todeschini
*01/07/23
*************************************/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>


#include <sys/shm.h>
#include <sys/sem.h>
#include <signal.h>

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <stdbool.h>

#include "../inc/shared_memory.h"
#include "../inc/semaphore.h"

#define N 20

void codiceGiocatore1();
void codiceGiocatore2();
bool checkMove(int column);
void stampaMatriceGioco();
void chiusura();

//Global Variables
bool player1 = true;
bool singlePlayer = false;
int semId, shmId, matId;
char * matriceDiGioco;
char username[N];
char symbol;
pid_t ServerPid;
struct ShMemMsg *ClientMsg;



void sigHandler(int sig){
    //Handler per i segnali di fine partita e di exit esterno (CTRL-C)
    if(sig == SIGINT){
        if(ClientMsg->flag[2] == 0) {
            printf("\nYou lost by forfeit\n");
            fflush(stdout);
            if(player1){
                ClientMsg->flag[0] = -2;
            }
            else{
                ClientMsg->flag[1] = -2;
            }
            if(kill(ClientMsg->pids[0], SIGCHLD) == -1){
                perror("Kill failed");
                exit(1);
            }
            chiusura();
            exit(0);
        }
        else if(ClientMsg->flag[2] != 0){ //Chiusura da parte del server
            if(ClientMsg->flag[2] == -2){
                printf("\nApplication terminated because the other player doesn't want to continue\n");
            }
            chiusura();
            exit(0);
        }
    }
    if(sig == SIGUSR1){
        printf("%s, You won by forfeit",username);
        fflush(stdout);
        chiusura();

        //TODO Si può fare la ricerca di un nuovo giocatore,
        // nel caso del giocatore 2 devo trattarlo come se fosse il giocatore 1

        exit(0);
    }

    if(sig == SIGALRM){
        if(player1) {
            printf("\nTimer has expired. Giving the turn to the other player...\n");
            semOp(semId, 1, 1, 0);
            semOp(semId, 0, -1, IPC_NOWAIT);

        }
        else {
            printf("\nTimer has expired. Giving the turn to the other player...\n");
            semOp(semId, 2, 1, 0);
            semOp(semId, 0, -1, IPC_NOWAIT);
        }
    }


    if(sig == SIGUSR2){
        if(player1){
            if(ClientMsg->flag[0] == 2) {
                printf("\nDraw\n");
            }else if(ClientMsg->flag[0] == 1){
                printf("\n%s, you won\n",username);
            }
            else if(ClientMsg->flag[0] == -1){
                printf("\n%s, you lost\n", username);
            }
            char choice;
            scanf("%c",&choice);
            //blocco per scelta di giocare nuovamente;
            if(!ClientMsg->singlePlayer) {
                do {
                    printf("\nWanna play again? (y/n)\n");
                    scanf("%c", &choice);
                } while (choice != 'y' && choice != 'n');

                if (choice == 'y') {
                    ClientMsg->newGameCount++;
                    while (ClientMsg->newGameCount != 2);
                    if (kill(ServerPid, SIGWINCH) == -1) {
                        perror("Kill failed");
                        exit(1);
                    }
                    printf("Waiting for player 2 ...");
                    fflush(stdout);
                    pause();
                    codiceGiocatore1();

                } else if (choice == 'n') {
                    chiusura();
                    if (kill(ServerPid, SIGTERM) == -1) {
                        perror("Kill failed");
                        exit(1);
                    }
                    exit(0);
                }
            }
            //
            if(kill(ServerPid, SIGINT) == -1){
                perror("SIGINT (for singlePlayer) to server failed");
                exit(1);
            }
            chiusura();
            exit(0);

        }
        else{
            if(ClientMsg->flag[1] == 2) {
                printf("\nDraw\n");
            }else if(ClientMsg->flag[1] == 1){
                printf("\n%s, you won\n",username);
            }
            else if(ClientMsg->flag[1] == -1){
                printf("\n%s, you lost\n", username);
            }
            char choice;
            scanf("%c",&choice);
            do {
                printf("\nWanna play again? (y/n)\n");
                scanf("%c", &choice);
            }while(choice != 'y' && choice != 'n');

            if(choice == 'y'){
                ClientMsg->newGameCount++;
                while(ClientMsg->newGameCount != 2);
                if(kill(ServerPid, SIGWINCH) == -1){
                    perror("Kill failed");
                    exit(1);
                }
                printf("Waiting for player 1 ...");
                fflush(stdout);
                pause();
                codiceGiocatore2();
            }else if(choice == 'n'){
                chiusura();
                if(kill(ServerPid, SIGTERM) == -1){
                    perror("Kill failed");
                    exit(1);
                }
                exit(0);
            }
        }
    }
}

int main(int argc, char *argv[]){
    if(argc < 2){
        perror("Error! You should use the command like this: <./F4Client Username>");
        exit(1);
    }
    if(strlen(argv[1]) > N){
        perror("Error, Username too long, try again!");
        exit(1);
    }
    strncpy(username, argv[1], sizeof(username));
    printf("Username Player: %s\n", username);

    //recupero chiave set semafori nel server
    key_t semKey = ftok("src/Server.c", 's');
    semId = semget(semKey, 3, S_IRUSR | S_IWUSR);
    if(semId == -1){
        perror("Error during the retrieval of the semaphore set identifier");
        exit(1);
    }

    //shmget per prendere la shared memory e poi fare l'attaching
    key_t shmKey = ftok("src/Server.c", 'h');
    shmId = alloc_shared_memory(shmKey, sizeof(struct ShMemMsg));

    //Struttura vuota del Client
    ClientMsg = (struct ShMemMsg *)get_shared_memory(shmId, 0);

    matId = ClientMsg->ID_matriceDiGioco;

    matriceDiGioco = (char *) get_shared_memory(matId,0);

    //Prelevare il pid del server e salvarlo in una costante per poi inviare
    ServerPid = ClientMsg->pids[0];

    //salva il proprio pid nella struttura dati con controllo giocatore 1 o 2;
    if(argc == 3) {
        if (strcmp(argv[2], "*") == 0) {
            ClientMsg->singlePlayer = 1;
            while (ClientMsg->singlePlayer != 1);
        }
    }else{
        ClientMsg->singlePlayer = 0;
    }


    if(ClientMsg->pids[1] != 0) {
        ClientMsg->pids[2] = getpid();
        while (ClientMsg->pids[2] != getpid());
        printf("Sono il giocatore 2.");
        fflush(stdout); //IMPORTANTE
        //Per indicare al processo che è il giocatore 2
        strcpy(ClientMsg->username[1],username);
        player1 = false;
        if (kill(ServerPid, SIGUSR2) == -1) {
            perror("Kill failed");
            exit(1);
        }
    }

    if(ClientMsg->pids[1] == 0){
        ClientMsg->pids[1] = getpid();
        while(ClientMsg->pids[1] != getpid());
        printf("Sono il giocatore 1\nIn attesa del secondo giocatore...\n");
        fflush(stdout);       //IMPORTANTE
        //send SIGUSR1 to Server
        strcpy(ClientMsg->username[0],username);
        if(kill(ServerPid, SIGUSR1) == -1){
            perror("Kill failed");
            exit(1);
        }
    }

    if((signal(SIGCONT,sigHandler) == SIG_ERR) ||
       (signal(SIGINT,sigHandler) == SIG_ERR) ||
       (signal(SIGUSR2,sigHandler) == SIG_ERR) ||
       (signal(SIGUSR1,sigHandler) == SIG_ERR) ||
       (signal(SIGTERM, sigHandler) == SIG_ERR) ){
        perror("signal handler failed");
        exit(1);
    }
    //Per fare in modo che non si creino problemi per bloccare lo scanf come operazione
    struct sigaction sa;
    sa.sa_handler = &sigHandler;
    sa.sa_flags = 0;
    sigaction(SIGALRM,&sa, NULL);




    //il segnale: "SONO PRONTO" al server (SIGUSR1 per client 1 solo se nell'array di pid
    //non è gia presente un secondo pid in posizione 1, altrimenti SIGUSR2 per indicare il client 2)

    pause();

    if(player1) {
        symbol = ClientMsg->symbols[0];
        codiceGiocatore1();
    }
    else{
        printf("\nIn attesa del giocatore 1...\n");
        symbol = ClientMsg->symbols[1];
        codiceGiocatore2();
    }
}

void codiceGiocatore1(){
    //Dopo essere stato sbloccato dal server, inizio chiedendo al giocatore la sua mossa

    while (1) {
        semOp(semId,1,0,0);
        //Dopo aver chiesto la mossa, posiziono il gettone nella matrice, prestando attenzione che la colonna scelta sia valida e non piena
        int mossa;
        stampaMatriceGioco();

        do {
            printf("%s, inserisci la mossa ", username);
            int charRead = scanf("%i", &mossa);
            //nel caso di timer scaduto e per evitare l'errore EOF chiama codicegiocatore 1
            if(charRead == EOF){
                codiceGiocatore1();
            }
        } while (!checkMove((mossa-1)) ); //controlla se la mossa può esser effettuata.


        bool ok = false;
        int i;
        //inserimento gettone con controllo casella vuota.
        for (i = ClientMsg->row - 1; i >= 0 && !ok; i--) {
            if (matriceDiGioco[i * ClientMsg->column + (mossa-1)] == ' ') {
                ok = true;
                matriceDiGioco[i * ClientMsg->column + (mossa-1)] = symbol;
                ClientMsg->mossa[0] = i;
                ClientMsg->mossa[1] = (mossa-1);
            }
        }
        stampaMatriceGioco();

        //Setto il mio semaforo a -1 e quello del server a 0

        semOp(semId,1,1,0);
        semOp(semId,0,-1,IPC_NOWAIT);

        printf("\nIn attesa del giocatore 2...\n");
    }
}


void codiceGiocatore2(){
    //Dopo essere stato sbloccato dal server, inizio chiedendo al giocatore la sua mossa


    while(1) {
        semOp(semId,2,0,0);

        //Dopo aver chiesto la mossa, posiziono il gettone nella matrice, prestando attenzione che la colonna scelta sia valida e non piena
        int mossa;

        stampaMatriceGioco();

        do {
            printf("%s, inserisci la mossa ", username);
            int charRead = scanf("%i", &mossa);   //TODO SISTEMARE CON SIGACTION
            if(charRead == EOF){
                codiceGiocatore2();
            }
        } while (!checkMove(mossa-1));

        bool ok = false;
        int i;

        for (i = ClientMsg->row - 1; i >= 0 && !ok; i--) {
            if (matriceDiGioco[i * ClientMsg->column + (mossa-1)] == ' ') {
                ok = true;
                matriceDiGioco[i * ClientMsg->column + (mossa-1)] = symbol;
                ClientMsg->mossa[0] = i;
                ClientMsg->mossa[1] = (mossa-1);
            }
        }

        stampaMatriceGioco();

        //Setto il mio semaforo a -1 e quello del server a 0

        semOp(semId,2,1,0);
        semOp(semId,0,-1,IPC_NOWAIT);

        printf("\nIn attesa del giocatore 1...\n");

        //Dopo aver chiesto la mossa, posiziono il gettone nella matrice, prestando attenzione che la colonna scelta sia valida e non piena

        //Setto il mio semaforo a -1 e quello del server a 0

    }
}

void stampaMatriceGioco(){
    int row = ClientMsg->row;
    int column = ClientMsg->column;

    printf("\n_________________________________________________\n\t\tMATRICE DI GIOCO\n");
    for(int i = 0; i < row;i++){
        for(int j = 0; j < column;j++){
            printf("|%c|  ",matriceDiGioco[i * column + j]);
        }
        printf("\n");
    }
    printf("\n-------------------------------------------------\n");
}

bool checkMove(int column){
    if(column >= 0 && column < ClientMsg->column){
        if(matriceDiGioco[column] == ' '){
            return true;
        }
    }
    printf("\nColumn is full, please select a different one\n");
    return false;
}


void chiusura(){
    printf("\nExiting application\n");
    fflush(stdout);
    free_shared_memory(ClientMsg);
    free_shared_memory(matriceDiGioco);
}



