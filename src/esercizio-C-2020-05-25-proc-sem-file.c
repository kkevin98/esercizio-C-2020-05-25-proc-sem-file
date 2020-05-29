#include <stdlib.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#define CHECK_ERR(a,msg) {if ((a) == -1) { perror((msg)); exit(EXIT_FAILURE); } }

#define CHECK_ERR_MMAP(a,msg) {if ((a) == MAP_FAILED) { perror((msg)); exit(EXIT_FAILURE); } }

#define FILE_SIZE (1024*1024)

#define N 4

sem_t *proc_sem;

sem_t *gettone;

void child_process_A(int i) {

	//int volta=0;
	int bytes = 0;
	int fd;
	int read_res;
	int write_res;
	char read_buffer[1];
	char write_buffer[1];
	off_t file_offset;

	write_buffer[0] = 'A' + i;

	fd = open("output_A.txt", O_RDWR);

	CHECK_ERR(fd, "open")

	while (1) {

		if (sem_wait(proc_sem) == -1) {
			perror("sem_wait");
			exit(EXIT_FAILURE);
		}

		//volta++

		//printf("figlio %d: sono passato dal semaforo per la %d volta\n", i, volta);

		//qui comincio ad accedere al file

		if (sem_wait(gettone) == -1) {
			perror("sem_wait");
			exit(EXIT_FAILURE);
		}

		while ((read_res = read(fd, read_buffer, sizeof(char))) > 0) { //esco dal while per un errore in lettura o per EOF
			if (read_buffer[0] == '\0') { //ho trovato uno zero
				file_offset = lseek(fd, -1, SEEK_CUR); //mi sposto indietro di uno per sovrascrivere lo zero appena trovato
				CHECK_ERR(file_offset, "lseek")
				write_res = write(fd, write_buffer, sizeof(char)); //scrivo
				CHECK_ERR(write_res, "write")
				bytes++;
				break;
			}
		}

		CHECK_ERR(read_res, "read")

		if (read_res == 0) {
			printf(
					"figlio %d, ho finito il file senza trovarci zeri, ho scritto %d volte\n",
					i, bytes);

			if (sem_post(gettone) == -1) {
				perror("sem_wait");
				exit(EXIT_FAILURE);
			}

			exit(EXIT_SUCCESS);
		}

		if (sem_post(gettone) == -1) {
			perror("sem_wait");
			exit(EXIT_FAILURE);
		}
	}
}

void soluzione_A(void) {

	int fd;
	int res;

	//padre crea il file
	fd = open("output_A.txt",
	O_CREAT | O_TRUNC | O_RDWR,
	S_IRUSR | S_IWUSR // l'utente proprietario del file avrà i permessi di lettura e scrittura sul nuovo file
	);

	CHECK_ERR(fd, "open")

	res = ftruncate(fd, FILE_SIZE);

	CHECK_ERR(fd, "ftruncate")

	res = close(fd);

	CHECK_ERR(res, "close")

	//semaforo per far partire i figli
	proc_sem = mmap(NULL, // NULL: è il kernel a scegliere l'indirizzo
			sizeof(sem_t), // dimensione della memory map
			PROT_READ | PROT_WRITE, // memory map leggibile e scrivibile
			MAP_SHARED | MAP_ANONYMOUS, // memory map condivisibile con altri processi e senza file di appoggio
			-1, 0); // offset nel file

	CHECK_ERR_MMAP(proc_sem, "mmap")

	//"mutex" per accedere al file
	gettone = mmap(NULL, // NULL: è il kernel a scegliere l'indirizzo
			sizeof(sem_t), // dimensione della memory map
			PROT_READ | PROT_WRITE, // memory map leggibile e scrivibile
			MAP_SHARED | MAP_ANONYMOUS, // memory map condivisibile con altri processi e senza file di appoggio
			-1, 0); // offset nel file

	//padre inizializza il mutex
	res = sem_init(gettone, 1, // 1 => il semaforo è condiviso tra processi, 0 => il semaforo è condiviso tra threads del processo
			1 // valore iniziale del semaforo
			);

	CHECK_ERR(res, "sem_init")

	//crea i processi figli
	for (int i = 0; i < N; i++) {
		switch (fork()) {
		case -1:
			perror("fork()");
			exit(EXIT_FAILURE);
		case 0: //qui dentro è dove sono presenti i figli
			child_process_A(i);
			break;
		default:
			;
		}
	}

	//incrementa il semaforo
	for (int i = 0; i < N + FILE_SIZE; i++) {
		if (sem_post(proc_sem) == -1) {
			perror("sem_post");
			exit(EXIT_FAILURE);
		}
	}

	do {
		res = wait(NULL);

		if (res > 0) {
			printf("Messaggio dal padre: è appena arrivato un figlio\n");
		}

	} while (res != -1); //non è il massimo, così non riconosco evntauli failure di wait

	res = sem_destroy(proc_sem);

	CHECK_ERR(res, "sem_destroy")

	res = munmap(proc_sem, sizeof(sem_t));

	CHECK_ERR(res, "munmap")

}

void child_process_B(int n_figlio, char *addr) {

	int bytes = 0;

	int posiz=0;

	while (1) {

		//passo al segnale del padre
		if (sem_wait(proc_sem) == -1) {
			perror("sem_wait");
			exit(EXIT_FAILURE);
		}

		//prendo il mutex
		if (sem_wait(gettone) == -1) {
			perror("sem_wait");
			exit(EXIT_FAILURE);
		}

		//printf("Figlio %d: preso il mutex\n", n_figlio);

		for(int i=posiz; i<FILE_SIZE; i++){

			if( i == (FILE_SIZE-1) ) { //sono arrivato in fondo al file senza trovare spazi liberi
				printf("Figlio %d: ho fatto il mio mestiere, ho scritto %d volte\n", n_figlio, bytes);
				if (sem_post(gettone) == -1) {
					perror("sem_post");
					exit(EXIT_FAILURE);
				}
				exit(EXIT_SUCCESS);
			}

			if ( addr[i] == '\0') {
				posiz=i;
				addr[i]='A'+n_figlio;
				//printf("Figlio %d: ho scritto %c nella posizione %d\n", n_figlio, 'A'+n_figlio, posiz);
				bytes++;
				i=(FILE_SIZE);//ho scritto un byte, ora torno al punto di partenza e aspetto il prox segnale dal padre
			}

			//printf("Figlio %d: ho scritto ora torno al punto di partenza\n", n_figlio);
		}

		if (sem_post(gettone) == -1) {
			perror("sem_post");
			exit(EXIT_FAILURE);
		}

	}

}

void soluzione_B(void) {

	int fd;
	int res;
	char *addr;

	//padre crea il file e prepara la memorymap con all'interno il file
	fd = open("output_B.txt",
	O_CREAT | O_TRUNC | O_RDWR,
	S_IRUSR | S_IWUSR // l'utente proprietario del file avrà i permessi di lettura e scrittura sul nuovo file
	);

	CHECK_ERR(fd, "open")

	res = ftruncate(fd, FILE_SIZE);

	CHECK_ERR(fd, "ftruncate")

	addr = mmap(NULL, // NULL: è il kernel a scegliere l'indirizzo
			FILE_SIZE, // dimensione della memory map
			PROT_READ | PROT_WRITE, // memory map leggibile e scrivibile
			MAP_SHARED, // memory map condivisibile con altri processi
			fd, 0); // offset nel file

	CHECK_ERR_MMAP(addr, "mmap")

	res = close(fd);

	CHECK_ERR(res, "close")

	//semaforo per far partire i figli
	proc_sem = mmap(NULL, // NULL: è il kernel a scegliere l'indirizzo
			sizeof(sem_t), // dimensione della memory map
			PROT_READ | PROT_WRITE, // memory map leggibile e scrivibile
			MAP_SHARED | MAP_ANONYMOUS, // memory map condivisibile con altri processi e senza file di appoggio
			-1, 0); // offset nel file

	CHECK_ERR_MMAP(proc_sem, "mmap")

	//"mutex" per accedere al file
	gettone = mmap(NULL, // NULL: è il kernel a scegliere l'indirizzo
			sizeof(sem_t), // dimensione della memory map
			PROT_READ | PROT_WRITE, // memory map leggibile e scrivibile
			MAP_SHARED | MAP_ANONYMOUS, // memory map condivisibile con altri processi e senza file di appoggio
			-1, 0); // offset nel file

	//padre inizializza il mutex
	res = sem_init(gettone, 1, // 1 => il semaforo è condiviso tra processi, 0 => il semaforo è condiviso tra threads del processo
			1 // valore iniziale del semaforo
			);

	CHECK_ERR(res, "sem_init")

	//crea i processi figli
	for (int i = 0; i < N; i++) {
		switch (fork()) {
		case -1:
			perror("fork()");
			exit(EXIT_FAILURE);
		case 0: //qui dentro è dove sono presenti i figli
			child_process_B(i, addr);
			break;
		default:
			;
		}
	}

	//incrementa il semaforo
	for (int i = 0; i < N + FILE_SIZE; i++) {
		if (sem_post(proc_sem) == -1) {
			perror("sem_post");
			exit(EXIT_FAILURE);
		}
	}

	do {
		res = wait(NULL);

		if (res > 0) {
			printf("Messaggio dal padre: è appena arrivato un figlio\n");
		}

	} while (res != -1); //non è il massimo, così non riconosco evntauli failure di wait

	res = sem_destroy(proc_sem);

	CHECK_ERR(res, "sem_destroy")

	res = munmap(proc_sem, sizeof(sem_t));

	CHECK_ERR(res, "munmap")

	res = munmap(addr, FILE_SIZE);
}

int main() {
	printf("ora avvio la soluzione_A()...\n");
	soluzione_A();

	printf("ed ora avvio la soluzione_B()...\n");
	soluzione_B();

	printf("bye!\n");
	return 0;
}

