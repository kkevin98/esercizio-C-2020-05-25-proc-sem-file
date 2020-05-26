/*
 * il processo principale crea un file "output.txt" di dimensione FILE_SIZE (all'inizio ogni byte del file deve avere valore 0)

 #define FILE_SIZE (1024*1024)

 #define N 4
 è dato un semaforo senza nome: proc_sem

 il processo principale crea N processi figli

 i processi figli aspettano al semaforo proc_sem.

 ogni volta che il processo i-mo riceve semaforo "verde", cerca il primo byte del file che abbia valore 0 e ci scrive il valore ('A' + i). La scrittura su file è concorrente e quindi va gestita opportunamente (ad es. con un mutex).

 se il processo i-mo non trova una posizione in cui poter scrivere il valore, allora termina.

 il processo padre:

 per (FILE_SIZE+N) volte, incrementa il semaforo proc_sem

 aspetta i processi figli e poi termina.

 risolvere il problema in due modi:

 soluzione A:

 usare le system call open(), lseek(), write()

 soluzione B:

 usare le system call open(), mmap()
 */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>


#define FILE_SIZE (1024*1024)
#define N 4

sem_t * proc_sem;
sem_t *mutex;

char * file_name_A = "output_A.txt";
char * file_name_B = "output_B.txt";

void create_file_set_size(char *file_name, unsigned int file_size);
void solution_A();
void solution_B();

void child_process_solution_A(int i);

#define CHECK_ERR(a,msg) {if ((a) == -1) { perror((msg)); exit(EXIT_FAILURE); } }
#define CHECK_ERR_MMAP(a,msg) {if ((a) == MAP_FAILED) { perror((msg)); exit(EXIT_FAILURE); } }

//#define DEBUG_MSG

int main(int argc, char *argv[]) {

	int res;

	proc_sem = mmap(NULL, // NULL: è il kernel a scegliere l'indirizzo
			sizeof(sem_t) * 2, // dimensione della memory map
			PROT_READ | PROT_WRITE, // memory map leggibile e scrivibile
			MAP_SHARED | MAP_ANONYMOUS, // memory map condivisibile con altri processi e senza file di appoggio
			-1, 0); // offset nel file
	CHECK_ERR_MMAP(proc_sem, "mmap")

	mutex = proc_sem + 1;
	res = sem_init(proc_sem,
			1, // 1 => il semaforo è condiviso tra processi, 0 => il semaforo è condiviso tra threads del processo
			0 // valore iniziale del semaforo
			);
	CHECK_ERR(res, "sem_init")

	res = sem_init(mutex,
			1, // 1 => il semaforo è condiviso tra processi, 0 => il semaforo è condiviso tra threads del processo
			1 // valore iniziale del semaforo (se mettiamo 0 che succede?)
			);
	CHECK_ERR(res, "sem_init")

	printf("ora avvio la soluzione_A()...\n");
	solution_A();

	printf("\n\n");

	printf("ed ora avvio la soluzione_B()...\n");
	solution_B();

	res = sem_destroy(proc_sem);
	CHECK_ERR(res, "sem_destroy")

	res = sem_destroy(mutex);
	CHECK_ERR(res, "sem_destroy")

	printf("bye!\n");
	return 0;
}

void solution_A() {
//	usare le system call open(), lseek(), write()

	create_file_set_size(file_name_A, FILE_SIZE);

	// creiamo N processi
	for (int i = 0; i < N; i++) {
		switch (fork()) {
			case 0:

				child_process_solution_A(i);

				break;
			case -1:
				perror("fork()");
				exit(EXIT_FAILURE);
			default:
			;
		}
	}

	for (int i = 0; i < FILE_SIZE + N; i++) {
		if (sem_post(proc_sem) == -1) {
			perror("sem_post");
			exit(EXIT_FAILURE);
		}
	}

	while (wait(NULL) != -1) ;

	printf("[parent] solution A - bye\n");

}


void solution_B() {

	int fd;
	char ch2write;
	int exit_while = 0;

	char * data;
	char * ptr;

	// quanti byte ha scritto il processo figlio
	int write_counter = 0;

	create_file_set_size(file_name_B, FILE_SIZE);

	fd = open(file_name_B, O_RDWR);

	// unica memory map condivisa tra i processi
	data = mmap(NULL,
			FILE_SIZE,
			PROT_READ | PROT_WRITE, // memory map leggibile e scrivibile
			MAP_SHARED,
			fd,
			0
			);

	if (data == MAP_FAILED) {
		perror("mmap()");
		exit(EXIT_FAILURE);
	}

	close(fd);

	ptr = data;

	// creiamo N processi
	for (int i = 0; i < N; i++) {
		switch (fork()) {
			case 0:

				ch2write = 'A' + i;

				while (!exit_while) {

					if (sem_wait(proc_sem) == -1) {
						perror("sem_wait");
						exit(EXIT_FAILURE);
					}
					// se siamo qui, possiamo scrivere un byte nel file (se c'è spazio)

					if (sem_wait(mutex) == -1) {
						perror("sem_wait");
						exit(EXIT_FAILURE);
					}

					// cerchiamo la prossima posizione libera (ch == 0)
					while (*ptr != 0 && ptr - data < FILE_SIZE)
						ptr++;

					if (ptr - data == FILE_SIZE) {
						// non c'è una posizione libera in cui poter scrivere nel file
						// usciamo dal while e terminiamo
						printf("[child %d] EOF\n", i);

						exit_while = 1;
					} else {
						// ok, abbiamo trovato una posizione dove scrivere il carattere
#ifdef DEBUG_MSG
						printf("[child %d] scrivo nel file all'offset %ld\n", i, ptr - data);
#endif

						*ptr = ch2write;
						ptr++;

						write_counter++;
					}

					if (sem_post(mutex) == -1) {
						perror("sem_post");
						exit(EXIT_FAILURE);
					}

				}

				printf("[child %d] bye  write_counter=%d\n", i, write_counter);

				exit(EXIT_SUCCESS);

				break;
			case -1:
				perror("fork()");
				exit(EXIT_FAILURE);
			default:
			;
		}
	}

	for (int i = 0; i < FILE_SIZE + N; i++) {
		if (sem_post(proc_sem) == -1) {
			perror("sem_post");
			exit(EXIT_FAILURE);
		}
	}

	while (wait(NULL) != -1) ;

	printf("[parent] solution B - bye\n");

}

/*
 * crea il file e lo apre in lettura e scrittura, imposta la dimensione del file
 *
 */
void create_file_set_size(char *file_name, unsigned int file_size) {
	int res;
	int fd = open(file_name,
	O_CREAT | O_TRUNC | O_RDWR, // apriamo il file in lettura e scrittura
	S_IRUSR | S_IWUSR // l'utente proprietario del file avrà i permessi di lettura e scrittura sul nuovo file
	);

	CHECK_ERR(fd, "open file")

	res = ftruncate(fd, file_size);
	CHECK_ERR(res, "ftruncate()")

	close(fd);
}


void child_process_solution_A(int i) {
	int fd;
	int res;
	char ch, ch2write;
	off_t file_offset;
	int exit_while = 0;

	// quanti byte ha scritto il processo figlio
	int write_counter = 0;

	ch2write = 'A' + i;

	fd = open(file_name_A, O_RDWR);

	while (!exit_while) {

		if (sem_wait(proc_sem) == -1) {
			perror("sem_wait");
			exit(EXIT_FAILURE);
		}
		// se siamo qui, possiamo scrivere un byte nel file (se c'è spazio)

		if (sem_wait(mutex) == -1) {
			perror("sem_wait");
			exit(EXIT_FAILURE);
		}

		// cerchiamo la prossima posizione libera (ch == 0)
		// read restituisce 0 in caso di EOF
		while ((res = read(fd, &ch, 1)) > 0 && ch != 0) ;

		// ok, abbiamo trovato una posizione dove scrivere il carattere
		if (res == 1) {
			// spostiamo il file offset indietro di -1
			// perchè ogni read avanza di 1 byte
			file_offset = lseek(fd, -1, SEEK_CUR);

#ifdef DEBUG_MSG
			printf("[child %d] scrivo nel file all'offset %ld\n", i, file_offset);
#endif
			res = write(fd, &ch2write, sizeof(ch2write));
			CHECK_ERR(res, "write()")

			write_counter++;

		} else {
			// non c'è una posizione libera in cui poter scrivere nel file
			// usciamo dal while e terminiamo
			printf("[child %d] EOF\n", i);

			exit_while = 1;
		}

		if (sem_post(mutex) == -1) {
			perror("sem_post");
			exit(EXIT_FAILURE);
		}

	}

	close(fd);

	printf("[child %d] bye  write_counter=%d\n", i, write_counter);

	exit(EXIT_SUCCESS);
}


