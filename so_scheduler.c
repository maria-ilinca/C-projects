#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <semaphore.h>

// structura care retine date despre thread-uri : prioritatea, starea, timpul de executie ramas al procesului
typedef struct {
	pthread_t thread_id; 
	so_handler *handler; 
	int priority; 
	state_t state;
	unsigned int remtime; // timpul de executie al unui thread ramas

	// semafor asignat proceselor care sunt planificate sau in curs de rulare
	sem_t plan, run;
} thread_t;

// reprezentam starile unui thread printr-o structura enum
typedef enum {
	New, 
    Ready, 
    Running, 
    Waiting,
    Terminated
} state_t;

// inf despre threadul planificat
typedef struct {
	unsigned int timpalocat; // cat poate rula un thread
	unsigned int devicescounter; // numar disp IO
	queue_t *ready,  *finished, **deviceswaiting; 
	int threadcounter; // numarul de thread-uri la care dam fork
	thread_t *running; // thread-ul care ruleaza
	sem_t stop; // marcheaza cand un proces este oprit
} scheduler_t;

int check_terminated_thread_priority(void *addr)
{
	return 0;
}

// coada de prioritati pentru thread-uri
int check_thread_priority(void *addr)
{
	return ((thread_t *) addr)->priority;
}

scheduler_t *scheduler;

void *thread_routine(void *arg);

// planificare thread
void thread_planning(thread_t *thr)
{
	if (!scheduler->running) {
		// daca thread-ul nu ruleaza inca
		thr->state = Running;
		scheduler->running = thr;
		sem_post(&scheduler->running->run);
		return;
	}

	if (thr->priority > scheduler->running->priority) {
		// preemption
		thread_t *thread_schedule = scheduler->running;
		scheduler->running = t;
		t->state = Running;
		thread_schedule->state = Ready;
		push_back(scheduler->ready, thread_schedule);
		return;
	}

	thr->state = Ready;
	push_back(scheduler->ready, thr);
}

void thread_finished();

// initializam componentele planificatorului
int so_init(unsigned int timpalocat, unsigned int no_device)
{
	if (scheduler != NULL || no_device > SO_MAX_NUM_EVENTS || timpalocat <= 0)
		return -1;

	scheduler = (scheduler_t *) calloc(sizeof(scheduler_t));
	if (!scheduler){
		perror("Memoria pentru scheduler nu a fost alocata corespunzator!");
	    return errno;
    }
	scheduler->timpalocat = timpalocat;
	scheduler->devicescounter = no_device;
	scheduler->running = NULL;
	scheduler->ready = new_queue(check_thread_priority);
	
	if (!scheduler->ready) {
		free(scheduler);
		perror("Nu s-a eliberat resursa corespunzator!");
		return errno;
	}
	scheduler->finished = new_queue(check_terminated_thread_priority);
	if (!scheduler->finished) {
		free_queue(&scheduler->ready, free);
		free(scheduler);
		perror("Nu s-a eliberat resursa corespunzator!");
		return errno;
	}
	scheduler->deviceswaiting = (queue_t **) calloc(no_device, sizeof(queue_t *));
	if (!scheduler->deviceswaiting) {
		free_queue(&scheduler->ready, free);
		free_queue(&scheduler->finished, free);
		free(scheduler);
		perror("Nu s-a eliberat resursa corespunzator!");
		return errno;
	}
	for (int i = 0; i < no_device; i++) {
		scheduler->deviceswaiting[i] =new_queue(check_terminated_thread_priority);
		if (!scheduler->deviceswaiting[i])
			return;
	}

	scheduler->threadcounter = 0;
	
	int semaphore_value = 0;
    if(sem_init(&semaphore, 0, semaphore_value)){
        perror("Error at sem_init\n");
        return errno;
    }

	return 0;
}

// pornim un thread si planificam executia lui
thread_id_t so_fork(so_handler *func, unsigned int thr_prior)
{
	int rc;
	thread_t *t;

	if (!func || thr_prior > SO_MAX_PRIO)
		perror("Invalid thread id");

	if (!(thread_t *) malloc(sizeof(thread_t)))
		perror("Invalid thread id");

	t->priority = thr_prior;
	t->state = New;
	t->remtime = scheduler->timpalocat;
	t->handler = func;
	sem_init(&t->plan, 0, 0);
	sem_init(&t->run, 0, 0);
	(scheduler->threadcounter)++;
	
	if (pthread_create(&t->thread_id, NULL, thread_routine, t)) {
		free(t);
		perror("Invalid thread id");
		return errno;
	}

	// asteptam pana se consuma thread-ul
	sem_wait(&t->plan);

	if (scheduler->running != t)
		so_exec();

	return t->thread_id;
}

// blocam un thread pentru executie
int so_wait(unsigned int io)
{

	if (io >= scheduler->devicescounter)
		return -1;

	thread_t *t = scheduler->running;
	t->state = Waiting;
	push_back(scheduler->deviceswaiting[io], t);

	// executam un nou thread care este gata
	scheduler->running = pop_front(scheduler->ready);
	if (scheduler->running != NULL)
		sem_post(&scheduler->running->run);

	sem_wait(&t->run);

	return 0;
}

// deblocam toate thread-urile
int so_signal(unsigned int io)
{
	if (io >= scheduler->devicescounter)
		return -1;

	thread_t *t = pop_front(scheduler->deviceswaiting[io]);
	int count = 0;

	while (t) {
		t->state = Ready;
		push_back(scheduler->ready, t);

		count++;
		t = pop_front(scheduler->deviceswaiting[io]);
	}

	so_exec();
	return count;
}

// simulam executia unei instructiuni
void so_exec(void)
{
	if (scheduler->running == NULL)
		return;

	thread_t *t = scheduler->running;

	t->remtime--;
	if (t->remtime == 0) {
		t->state = Ready;
		t->remtime = scheduler->timpalocat;
		push_back(scheduler->ready, t);

		// luam urm thread pregatit care nu a fost terminat
		thread_t *next = pop_front(scheduler->ready);

		while (next && next->state == Terminated) {
			push_back(scheduler->finished, next);
			next = pop_front(scheduler->ready);
		}

		scheduler->running = next;
		if (scheduler->running) {
			scheduler->running->state = Running;
			sem_post(&scheduler->running->run);
		}
	} else if (peek_front(scheduler->ready) &&
		t->priority < ((thread_t *)peek_front(scheduler->ready))->priority) {
		// inlocuim un thread deja terminat cu unul cu prioritatea cea mai mare
		// care asteapta sa fie executat
		t->state = Ready;
		
		push_back(scheduler->ready, t);
		scheduler->running = pop_front(scheduler->ready);
		scheduler->running->state = Running;
		sem_post(&scheduler->running->run);
	}

	if (t != scheduler->running)
		sem_wait(&t->run);	
}


// asteptam ca toate thread-urile sa se termine si dezalocam resursele
void so_end(void)
{
	if (scheduler) {
		
		if (scheduler->threadcounter)
			sem_wait(&scheduler->stop);

		free_queue(&scheduler->finished, thread_destroy);
		free_queue(&scheduler->ready, thread_destroy);

		if (scheduler->running)
			thread_destroy(&scheduler->running);

		for (int i = 0; i < scheduler->devicescounter; i++)
			free_queue(&scheduler->deviceswaiting[i], free);

		free(scheduler->deviceswaiting);
		free(scheduler);
		sem_destroy(&scheduler->stop);
	}

	scheduler = NULL;
}

// unim thread-urile si dezalocam memoria
void thread_destroy(void *addr)
{
	thread_t *t = (thread_t *) addr;
	
	if (pthread_mutex_destroy(t))
	{perror("Error at pthread_mutex_destroy\n");
        return errno;
	}

	pthread_join(t->thread_id, NULL);
	
	sem_destroy(&t->plan);
	sem_destroy(&t->run);
	
	free(t);
}


void *thread_routine(void *arg)
{
	thread_t *t = (thread_t *) arg;

	// planificare + semnalare

	thread_planning(t);
	sem_post(&t->plan);

	// asteptam pana termina de rulat
	sem_wait(&t->run);

	t->handler(t->priority);
	
	t->state = Terminated;
	
	// thread-ul e terminat acum
	thread_t *t = scheduler->running;
	
		push_back(scheduler->finished, t);
		scheduler->running = pop_front(scheduler->ready);

		if (scheduler->running != NULL)
			sem_post(&scheduler->running->run);

		if (scheduler->running == NULL && peek_front(scheduler->ready) == NULL)
			sem_post(&scheduler->stop);

	return NULL;
}