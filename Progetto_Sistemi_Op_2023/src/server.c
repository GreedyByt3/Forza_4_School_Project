/************************************
*VR445930 - Andrea Doci
*VR443680 - Michele Todeschini
*01/07/23
*************************************/


#include "../inc/shared_memory.h"
#include "../inc/semaphore.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <sys/shm.h>
#include <sys/sem.h>
#include <signal.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <stdbool.h>

void startMatch();
void controlloMossa(pid_t);
void giocatoreSingolo();
void chiusura();
bool horizontalCheck(char Symbol);
bool verticalCheck(char Symbol);
bool positiveDiagonalCheck(char Symbol);
bool negativeDiagonalCheck(char Symbol);
bool checkMove(int column);
void chiusuraChild();

/**Variabili globali gestite in seguito*/
bool noAlarm = true;
int countSigInt = 0;
char symbol1;
char symbol2;
struct ShMemMsg *ShmGioco;
int row, column;
pid_t pidPlayer1,pidPlayer2;
int sem_Id, shmId, matId;
char * matriceDiGioco;
struct shmid_ds info;
bool player1;
pid_t pidChild;


/**Funzione generica per la creazione di semafori*/
int create_sem_set(int numSemaphore) {
    // Create a semaphore set with 3 semaphores
    key_t semkey = ftok("src/Server.c", 's');
    printf("key semaphore: %d\n", semkey);
    int semid = semget(semkey, numSemaphore, IPC_CREAT | S_IRUSR | S_IWUSR);
    if (semid == -1) {
        perror("semget failed");
        exit(1);
    }

    // Initialize the semaphore set
    union semun arg;
    unsigned short values[] = {0, 0, 0};
    arg.array = values;

    if (semctl(semid, 0, SETALL, arg) == -1){
        perror("semctl SETALL failed");
        exit(1);
    }
    return semid;
}
/**Handler per la gestione di segnali per la terminazione dall'esterno, terminazione dovuta ai client, connessione dei client ecc...*/
void sigusrHandler(int sig){
    /**Eseguito nel momento in cui si è trovato il primo giocatore*/
    if(sig == SIGUSR1){
        pidPlayer1 = ShmGioco->pids[1];
        printf("\n-----------------------------\n");
        printf("Player 1 connected\n");
        if(ShmGioco->singlePlayer != 1) {
            //printf("Test");
            fflush(stdout);
            pause();
        }
        if(ShmGioco->singlePlayer == 1){
            raise(SIGCONT);
        }
    }
    /**Eseguito nel momento in cui si è trovato il secondo giocatore*/
    if(sig == SIGUSR2){
        pidPlayer2 = ShmGioco->pids[2];
        printf("-----------------------------\n");
        printf("Player 2 connected\n");
        printf("Starting match...");
        fflush(stdout);
    }
    /**Gestione TIMER per giocatore*/
    if(sig == SIGALRM){
        if(player1){
            if (kill(pidPlayer1, SIGALRM) == -1)  {
                perror("SIGALRM failed");
                exit(1);
            }
            noAlarm = false;
            alarm(TIMER);
        }
        else{
            if (kill(pidPlayer2, SIGALRM) == -1)  {
                perror("SIGALRM failed");
                exit(1);
            }
            noAlarm = false;
            alarm(TIMER);
        }
    }
    /**CTRL-C e chiusura controllata figlio*/
    if(sig == SIGINT){
        if(pidChild == 0){
            chiusuraChild();
            exit(0);
        }
        if(!ShmGioco->singlePlayer) {
            if (countSigInt == 0) {
                printf("\nYou sure you want to close?\n");
                fflush(stdout);
            }
            countSigInt++;
            if (countSigInt == 2) {
                ShmGioco->flag[2] = -2;     //Indica che la partita sta per finire dall'esterno
                if ((kill(pidPlayer1, SIGINT) == -1) || (kill(pidPlayer2, SIGINT) == -1)) {
                    perror("SIGINT failed");
                    exit(1);
                }
                chiusura();
                exit(0);
            }
        }else{
            if(kill(pidChild, SIGINT) == -1){
                perror("Kill to pidchild failed!");
                exit(1);
            }
            chiusura();
            exit(0);
        }
    }
    /**Nel caso di chiusura esterna di uno dei due client--> Vittoria per forfait dell'altro giocatore*/
    if(sig == SIGCHLD){
        if(ShmGioco->flag[0] == -2){
            if (kill(pidPlayer2, SIGUSR1) == -1)  {
                perror("SIGUSR2 failed");
                exit(1);
            }
            chiusura();
            exit(0);
        }
        if(ShmGioco->flag[1] == -2){
            if (kill(pidPlayer1, SIGUSR1) == -1)  {
                perror("SIGUSR2 failed");
                exit(1);
            }
            chiusura();
            exit(0);
        }
    }
    /**Eseguito per la reinizializzazione di una nuova partita,
     * di conseguenza avviene la pulizia della matrice,
     * il risettaggio dei semafori ecc...*/
    if(sig == SIGWINCH){
        if(ShmGioco->newGameCount == 2){
            //Restart game
            for(int i = 0; i < row; i++){
                for(int j = 0; j < column; j++){
                    matriceDiGioco[i * column + j] = ' ';
                }
            }
            ShmGioco->flag[0] = 0;
            ShmGioco->flag[1] = 0;
            ShmGioco->flag[2] = 0;
            union semun arg;
            unsigned short values[] = {0, 0, 0};
            arg.array = values;
            if (semctl(sem_Id, 0, SETALL, arg) == -1){
                perror("semctl SETALL failed");
                exit(1);
            }
            startMatch();
        }
        semOp(sem_Id,0,1,0);
        semOp(sem_Id,0,0,0);
    }

    if(sig == SIGTERM){
        //uno dei giocatori non vuole più continuare quindi
        //dobbiamo chiudere l'altro giocatore e il server
        if(shmctl(shmId,IPC_STAT,&info) == -1){
            perror("shmctl failed");
            exit(1);
        }
        ShmGioco->flag[2] = -2;
        pid_t pid = info.shm_lpid;
        if(pid == pidPlayer1){
            if (kill(pidPlayer2, SIGINT) == -1)  {
                perror("SIGUSR2 failed");
                exit(1);
            }
        }
        if(pid == pidPlayer2){
            if (kill(pidPlayer1, SIGINT) == -1)  {
                perror("SIGUSR2 failed");
                exit(1);
            }
        }

        chiusura();
        exit(0);

    }

}

/**Funzione per l'inizio del gioco; si occuperà di gestire i vari semafori, il timer ecc...*/
void startMatch(){
    semOp(sem_Id,1,1,0);
    semOp(sem_Id,2,1,0);

    if ((kill(pidPlayer1, SIGCONT) == -1) || (kill(pidPlayer2, SIGCONT) == -1))  {
        perror("SIGCONT failed");
        exit(1);
    }

    //setta il timer entro il quale il giocatore deve fare la sua mossa
    //altrimenti il turno passa
    alarm(TIMER);

    while(1) {
        //Semaforo del server a 1 e aspetto che il giocatore 1 faccia la sua mossa
        player1 = true;
        noAlarm = true;
        semOp(sem_Id,0,1,0);
        semOp(sem_Id,1,-1,IPC_NOWAIT);
        semOp(sem_Id,0,0,0);

        //Clock per il limite di tempo di una mossa
        //Allo scadere del clock il giocatore 1 riceve un segnale SIGALRM
        //Quando succede, il server mette il semaforo del client1 a 1 e
        //fa in modo che ricominci il codice della mossa e metta il semaforo del server a 0

        //EINTR --> errore che si genera quando avviene una chiamata da parte di un segnale durante operazioni di lettura/scrittura,
        //di conseguenza è stato inserito per ovviare a blocchi del programma in fasi di lettura/scrittura
        if(errno == EINTR){
            semOp(sem_Id,0,0,0);
        }

        //Dopo che il giocatore 1 ha finito la sua mossa e messo il semaforo del server a 0,
        //Controllo la mossa fatta dal client per vedere se ha vinto

        if(noAlarm){
            alarm(TIMER);
            controlloMossa(pidPlayer1);
        }

        player1 = false;
        noAlarm = true;

        semOp(sem_Id,0,1,0);
        semOp(sem_Id,2,-1,IPC_NOWAIT);
        semOp(sem_Id,0,0,0);

        //Clock per il limite di tempo di una mossa
        //Allo scadere del clock il giocatore 2 riceve un segnale SIGALRM
        //Quando succede, il server mette il semaforo del client2 a 1 e
        //fa in modo che ricominci il codice della mossa e metta il semaforo del server a 0

        //EINTR --> errore che si genera quando avviene una chiamata da parte di un segnale durante operazioni di lettura/scrittura,
        //di conseguenza è stato inserito per ovviare a blocchi del programma in fasi di lettura/scrittura
        if(errno == EINTR){
            semOp(sem_Id,0,0,0);
        }

        if(noAlarm){
            alarm(TIMER);
            controlloMossa(pidPlayer2);

        }

    }
}
/**Funzione per chiudere in maniera controllata il server ed eventuali shdMemory aperte*/
void chiusura(){
    printf("\nExiting application\n");
    fflush(stdout);
    free_shared_memory(ShmGioco);
    free_shared_memory(matriceDiGioco);
    remove_shared_memory(shmId);
    remove_shared_memory(matId);
}
/**Funzione per chiudere in maniera controllata il child del server nel caso del giocatore singolo*/
void chiusuraChild(){
    printf("\nChild closing\n");
    fflush(stdout);
    free_shared_memory(ShmGioco);
    free_shared_memory(matriceDiGioco);
}


int main(int argc, char *argv[]) {

    //Controllo Numero di parametri inseriti
    if(argc != 5){
        perror("Error into the parameters --> <./F4Server N M P1Symbol p2Symbol> \n(N = Rows; M =Columns)");
        exit(1);
    }
    /**Memorizzaione valori in input che saranno le righe, le colonne ecc..*/
    row = atoi(argv[1]);
    column= atoi(argv[2]);
    symbol1 = argv[3][0];
    symbol2 = argv[4][0];


    //semaphore identifier
    sem_Id = create_sem_set(3);

    /**shMemMsg creation --> per il controllo della partita*/
    key_t shmKey = ftok("src/Server.c", 'h');
    //Prova per verificare la corretta creazione della chiave: --> printf("Chiave shared memory: %d\n", shmKey);
    shmId = alloc_shared_memory(shmKey, sizeof(struct ShMemMsg));
    //Verifica della corretta creazione della sharedMemoryId: --> printf("Shared memory id: %d\n", shmId);

    //attaching the shared memory to the server process
    //creazione struttura condivisa nella shm e allocazione matrice dinamica
    ShmGioco = (struct ShMemMsg *) get_shared_memory(shmId, 0);
    for (int i = 0; i < 3; ++i) {
        ShmGioco->pids[i] = 0;
    }
    /** Inizializzazione flag a 0*/
    for(int i = 0; i < 1; i++){
        ShmGioco->flag[i] = 0;
    }
    /**Contatore per verificare i giocatori che vogliono giocare di nuovo*/
    ShmGioco->newGameCount = 0;

    /**Indica che il gioco non è ancora iniziato*/
    ShmGioco->flag[2] = -1;

    /**Creazione chiave ed Inizializzazione, a valori vuoti, Memoria condivisa del Campo da gioco;
     *di modo da renderla accessibile ai vari processi esterni*/
    key_t matKey = ftok("src/Server.c", 'm');
    matId = alloc_shared_memory(matKey, row * column * sizeof(char));
    ShmGioco->ID_matriceDiGioco = matId;
    matriceDiGioco = (char *)get_shared_memory(matId,0);

    for(int i = 0; i < row; i++){
        for(int j = 0; j < column; j++){
            matriceDiGioco[i * column + j] = ' ';
        }
    }

    /**Memorizzazione dati del Campo da gioco sulla sh*/
    ShmGioco->symbols[0] = symbol1;
    ShmGioco->symbols[1] = symbol2;
    ShmGioco->row = row;
    ShmGioco->column = column;

    /**pid_saving of server process*/
    ShmGioco->pids[0] = getpid();
    //Verifica corretto salvataggio del pid del Server: --> printf("Server pid: %d\n", ShmGioco->pids[0]);

    //Assegnamento Handler per i vari segnali ricevuti;
    if( (signal(SIGUSR1,sigusrHandler) == SIG_ERR) ||
        (signal(SIGUSR2,sigusrHandler) == SIG_ERR) ||
        (signal(SIGCHLD,sigusrHandler) == SIG_ERR) ||
        (signal(SIGINT,sigusrHandler) == SIG_ERR) ||
        (signal(SIGCONT,sigusrHandler) == SIG_ERR) ||
        (signal(SIGTERM,sigusrHandler) == SIG_ERR) ||
        (signal(SIGALRM,sigusrHandler) == SIG_ERR) ||
        (signal(SIGWINCH,sigusrHandler) == SIG_ERR)){
            perror("signal failed");
            exit(1);
    }

    //mettere in attesa il server fino a che non riceve SIGUSR1
    printf("Waiting for players to connect...");
    fflush(stdout);
    pause();
    /**flag per capire che il gioco è partito*/
    ShmGioco->flag[2] = 0;
        if(ShmGioco->singlePlayer == 1) {
            pidChild = fork();
            if (pidChild == 0) {
                ShmGioco->pids[2] = getpid();
                while (ShmGioco->pids[2] != getpid());
                sleep(2);
                giocatoreSingolo();
            }
        }
    startMatch();
}

/**Codice eseguito solo nel caso in qui si esegua il comando: "./client Nome '*' ".
 **/
void giocatoreSingolo(){
    while(1) {
        semOp(sem_Id,2,0,0);

        //Dopo aver chiesto la mossa, posiziono il gettone nella matrice, prestando attenzione che la colonna scelta sia valida e non piena
        int mossa;

        //mossa casuale
        do {
            mossa = rand()%(column+1);
        } while (!checkMove(mossa));

        bool ok = false;
        int i;
        /**Inserisci la mossa nella matrice di gioco*/
        for (i = ShmGioco->row - 1; i >= 0 && !ok; i--) {
            if (matriceDiGioco[i * ShmGioco->column + (mossa)] == ' ') {
                ok = true;
                matriceDiGioco[i * ShmGioco->column + (mossa)] = symbol2;
                ShmGioco->mossa[0] = i;
                ShmGioco->mossa[1] = (mossa);
            }
        }


        /**Setto il semaforo del child a +1,
         * e quello del server a 0*/
        semOp(sem_Id,2,1,0);
        semOp(sem_Id,0,-1,IPC_NOWAIT);


    }
}
/**Controlla la mossa creata dal figlio del server,
 * per fare in modo che rimanga nei limiti delle dimensioni della matrice*/
bool checkMove(int column){
    if(column >= 0 && column < ShmGioco->column){
        if(matriceDiGioco[column] == ' '){
            return true;
        }
    }
    return false;
}

/**Controllo della mossa che effettua il server per vedere se la matrice è piene o se uno dei due giocatori ha vinto*/
void controlloMossa(pid_t pid){
    //controllo mossa giocatore con pid 'pid'
    bool piena = true;
    for(int i = 0; i < column; i++){
        if(matriceDiGioco[i] == ' '){
            piena = false;
        }
    }
    if(piena){
        if(!ShmGioco->singlePlayer) {
            alarm(0);
            ShmGioco->flag[2] = 1;  //Indica la fine del gioco
            ShmGioco->flag[0] = 2;
            ShmGioco->flag[1] = 2;
            if ((kill(pidPlayer1, SIGUSR2) == -1) || (kill(pidPlayer2, SIGUSR2) == -1)) {
                perror("SIGUSR2 failed");
                exit(1);
            }
        }
        else{
            alarm(0);
            ShmGioco->flag[2] = 1;
            ShmGioco->flag[0] = 2;
            if(kill(pidPlayer1,SIGUSR2) == -1){
                perror("SIGUSR2 failed");
                exit(1);
            }
        }
        pause();
    }
     if(pid == pidPlayer1){
         if(horizontalCheck(ShmGioco->symbols[0]) || verticalCheck(ShmGioco->symbols[0]) || positiveDiagonalCheck(ShmGioco->symbols[0]) || negativeDiagonalCheck(ShmGioco->symbols[0])){
             if(!ShmGioco->singlePlayer) {
                 alarm(0);
                 ShmGioco->flag[2] = 1;  //Indica la fine del gioco
                 ShmGioco->flag[0] = 1;
                 ShmGioco->flag[1] = -1;
                 if ((kill(pidPlayer1, SIGUSR2) == -1) || (kill(pidPlayer2, SIGUSR2) == -1)) {
                     perror("SIGUSR2 failed");
                     exit(1);
                 }
             }else{
                 alarm(0);
                 ShmGioco->flag[2] = 1;
                 ShmGioco->flag[0] = 1;
                 if(kill(pidPlayer1, SIGUSR2) == -1){
                     perror("SIGUSR2 failed");
                     exit(1);
                 }
             }
         }
     }else{
         if(horizontalCheck(ShmGioco->symbols[1])  || verticalCheck(ShmGioco->symbols[1]) || positiveDiagonalCheck(ShmGioco->symbols[1]) || negativeDiagonalCheck(ShmGioco->symbols[1]) ){
             if(!ShmGioco->singlePlayer) {
                 alarm(0);
                 ShmGioco->flag[2] = 1;  //Indica la fine del gioco
                 ShmGioco->flag[0] = -1;
                 ShmGioco->flag[1] = 1;
                 if ((kill(pidPlayer1, SIGUSR2) == -1) || (kill(pidPlayer2, SIGUSR2) == -1)) {
                     perror("SIGUSR2 failed");
                     exit(1);
                 }
             }else{
                 alarm(0);
                 ShmGioco->flag[2] = 1;
                 ShmGioco->flag[0] = -1;
                 if(kill(pidPlayer1, SIGUSR2) == -1){
                     perror("SIGUSR2 failed");
                     exit(1);
                 }
             }


         }
     }
}

//Funzione per controllare la colonna in orizzontale
bool horizontalCheck(char Symbol){
    int count = 1;
    int countOutOfBound = 0;
    int r = ShmGioco->mossa[0];
    int c = ShmGioco->mossa[1];
    int path = 1;

    while(countOutOfBound < 2){
            if( (c + path < column) &&
                (c + path >= 0) &&
                (matriceDiGioco[r*column + c + path] == Symbol))
            {
                count++;
                c += path;
            } else {
                countOutOfBound++;
                path = -1;
                c = ShmGioco->mossa[1];
            }
    }
    if(count >= 4){
        return true;
    }
    return false;
}

//Funzione per controllare la colonna in verticale
bool verticalCheck(char Symbol){
    int count = 1;
    int countOutOfBound = 0;
    int r = ShmGioco->mossa[0];
    int c = ShmGioco->mossa[1];
    int path = 1;

    while(countOutOfBound < 2){
        if( (r + path < row) &&
            (r + path >= 0) &&
            (matriceDiGioco[(r + path)*column + c] == Symbol))
        {
            count++;
            r += path;
        } else {
            countOutOfBound++;
            path = -1;
            r = ShmGioco->mossa[0];
        }
    }
    if(count >= 4){
        return true;
    }
    return false;

}
//controllo diagonale positiva
bool positiveDiagonalCheck(char Symbol){
    int count = 1;
    int countOutOfBound = 0;
    int r = ShmGioco->mossa[0];
    int c = ShmGioco->mossa[1];
    int path_r = 1;
    int path_c = -1;

    while(countOutOfBound < 2){
        if( (r + path_r < row  && c + path_c < column) &&
            (r + path_r >= 0 && c + path_c >= 0) &&
            (matriceDiGioco[(r + path_r)*column + (c + path_c)] == Symbol))
        {
            count++;
            r += path_r;
            c += path_c;
        } else {
            countOutOfBound++;
            path_r = -1;
            path_c = 1;
            r = ShmGioco->mossa[0];
            c = ShmGioco->mossa[1];
        }
    }
    if(count >= 4){
        return true;
    }
    return false;
}

//controllo diagonale negativa
bool negativeDiagonalCheck(char Symbol){
    int count = 1;
    int countOutOfBound = 0;
    int r = ShmGioco->mossa[0];
    int c = ShmGioco->mossa[1];
    int path_r = 1;
    int path_c = 1;

    while(countOutOfBound < 2){
        if( (r + path_r < row  && c + path_c < column) &&
            (r + path_r >= 0 && c + path_c >= 0) &&
            (matriceDiGioco[(r + path_r)*column + (c + path_c)] == Symbol))
        {
            count++;
            r += path_r;
            c += path_c;
        } else {
            countOutOfBound++;
            path_r = -1;
            path_c = -1;
            r = ShmGioco->mossa[0];
            c = ShmGioco->mossa[1];
        }
    }
    if(count >= 4){
        return true;
    }
    return false;
}
