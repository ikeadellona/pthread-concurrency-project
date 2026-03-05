#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/types.h>

struct customer_info{ /// use this struct to record the customer information read from customers.txt
    int user_id;
	int class_type;
	int service_time;
	int arrival_time;
};

typedef struct Clerk{
    int clerk_id;
    pthread_cond_t clerk_cv;
} Clerk;

/* global variables */
const int NQUEUE = 2;
struct timeval init_time; // use this variable to record the simulation start time; No need to use mutex_lock when reading this variable since the value would not be changed by thread once the initial time was set.
//int queue_length[NQUEUE];// variable stores the real-time queue length information; mutex_lock needed
double overall_waiting_time; //A global variable to add up the overall waiting time for all customers, every customer add their own waiting time to this variable, mutex_lock is necessary.
// int queue0_length[NQUEUE];// variable stores the real-time queue length information; mutex_lock needed
// int queue1_length[NQUEUE];


Clerk* queue_status[NQUEUE]; // variable to record the status of a queue, the value could be idle (not using by any clerk) or the clerk id (1 ~ 4), indicating that the corresponding clerk is now signaling this queue.
int winner_selected[NQUEUE];// variable to record if the first customer in a queue has been successfully selected and left the queue.

/* Other global variable may include: 
 1. condition_variables (and the corresponding mutex_lock) to represent each queue; 
 2. condition_variables to represent clerks
 3. others.. depend on your design
 */

 // Construct queues structure
typedef struct Customer{
    struct customer_info *info;
    struct Customer *next;
} Customer;

typedef struct Queue{
    Customer *head;
    Customer *tail;
    int size;
    pthread_mutex_t lock;
    pthread_cond_t cv;
} Queue;

Queue economyQueue;
Queue businessQueue;

//Inititalizer
void initializeQueue(Queue* q){
    Queue* q = (Queue*)malloc(sizeof(Queue));
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
    if (pthread_mutex_init(&q->lock, NULL) != 0){ //mutex initialization
        printf("\n mutex init failed\n");
        return 1;
    }
    if (pthread_cond_init(&q->cv, NULL) != 0){ //mutex initialization
        printf("\n queue convar init failed\n");
        return 1;
    }
    return q
}

Customer* createCustomer(struct customer_info *cust){
    Customer* customer = (Customer*)malloc(sizeof(Customer));
    customer->info = cust;
    customer->next = NULL;
    return customer;
}

Clerk* createClerk(int clerk_info) {
    Clerk* clerk = (Clerk*)malloc(sizeof(Clerk));
    clerk->clerk_id = clerk_info;
     if (pthread_cond_init(&clerk->clerk_cv, NULL) != 0){ //mutex initialization
        printf("\n clerk convar init failed\n");
        return 1;
    }
    return clerk;

}
int isEmpty(Queue* q){
    return q->head==NULL;
}

void enQueue(Queue* q, struct customer_info *cust){
    Customer* customer = createCustomer(cust);
    if(isEmpty(q)){
        q->head =customer;
        q->tail =customer;
    } else{
        q->tail->next = customer;
        q->tail = customer;
    }
}

// remove the front customer
void deQueue(Queue* q){
    Customer* front = q->head;
    // struct customer_info  removed_customer = front->info;
    q->head = q->head->next;
    if (q->head == NULL){
        q->tail=NULL;
    }
    free(front);
}

//-----------------------
// time structure
double getCurSystemTime(){
	
	struct timeval cur_time;
	double cur_secs, init_secs;
	
	//pthread_mutex_lock(&start_time_mtex); you may need a lock here
	init_secs = (init_time.tv_sec + (double) init_time.tv_usec / 1000000);
	//pthread_mutex_unlock(&start_time_mtex);
	
	gettimeofday(&cur_time, NULL);
	cur_secs = (cur_time.tv_sec + (double) cur_time.tv_usec / 1000000);
	
	return cur_secs - init_secs;
}

void * customer_entry(void * cus_info);
void * clerk_entry(void * clerkNum);

int main(int argc, char *argv[]) {


	// initialize all the condition variable and thread lock will be used
    initializeQueue(&economyQueue);
    initializeQueue(&businessQueue);
    
	/** Read customer information from txt file and store them in the structure you created 
		
		1. Allocate memory(array, link list etc.) to store the customer information.
		2. File operation: fopen fread getline/gets/fread ..., store information in data structure you created
    
	*/
    if (argc < 2) {
        printf("Error: You must provide a filename\n");
        return 1;
    }
    FILE *inputfile = fopen(argv[1], "r");

    if (inputfile == NULL) {
        printf("Error: Unable to open file\n");
        return 1;
    }
    // read the first line of file and get the number of customers then store it in Ncustomers
    int NCustomers;
    int NClerks = 5;
    char line[256];
    fgets(line, sizeof(line), inputfile);
    sscanf(line, "%d",&NCustomers);

    struct customer_info customers[NCustomers]; //customer array
    int clerk_info[NClerks]; //clerk array

    int i = 0;

    //store customer info
    while(fgets(line, sizeof(line), inputfile)!= NULL && i < NCustomers){
        sscanf(line, "%d:%d,%d,%d", &customers[i].user_id, &customers[i].class_type, &customers[i].arrival_time, &customers[i].service_time);
        if (customers[i].arrival_time <=0 || customers[i].service_time <= 0) {
            printf("Error: Invalid input\n");
            return 1;
        }
        i++;
    }
    // store clerk info
    int k = 1;
    for (i = 0; i < NClerks; i++){
        clerk_info[i] = 1;
        k++;
    }

    //start simulation time
    gettimeofday(&init_time, NULL); // record simulation start time

    pthread_t customId[NCustomers]; // customer thread array
    pthread_t clerkId[NClerks]; //clerk thread array
    int rc;

	//create clerk thread (optional)
	for(i = 0; i < NClerks; i++){ // number of clerks
		if (rc = pthread_create(&clerkId[i], NULL, clerk_entry, (void *)&clerk_info[i])){ // clerk_info: passing the clerk information (e.g., clerk ID) to clerk thread
            printf("Error: pthread_create, rc: %d\n", rc);
            return 1;
        }
    }
	// //create customer thread
	for(i = 0; i < NCustomers; i++){ // number of customers
		if (rc = pthread_create(&customId[i], NULL, customer_entry, (void *)&customers[i])){ //custom_info: passing the customer information (e.g., customer ID, arrival time, service time, etc.) to customer thread
            printf("Error: pthread_create, rc: %d\n", rc);
            return 1;
        }
    }
    // wait for all customer threads to terminate
	for (i=0; i<NCustomers;i++){
		pthread_join(customId[i], NULL);
	}
	// destroy mutex & condition variable (optional)
	pthread_mutex_destroy(&economyQueue.lock);
    pthread_mutex_destroy(&businessQueue.lock);

	// calculate the average waiting time of all customers
    double allaverage_waitingtime = overall_waiting_time / NCustomers;
    fprintf(stdout, "The average waiting time for all customers in system is: %.2f. \n", allaverage_waitingtime);

    fclose(inputfile);
	return 0;
}

void * customer_entry(void * cus_info){
	
	struct customer_info * p_myInfo = (struct customer_info *) cus_info;
    double queue_enter_time;
	Queue *selectedQueue;
    int cur_queue;
    int selected_queue_id;
	usleep(p_myInfo->arrival_time * 100000); // sleep until arrival time of customer
	
	fprintf(stdout, "A customer arrives: customer ID %2d. \n", p_myInfo->user_id);
	
	/* Enqueue operation: get into either business queue or economy queue by using p_myInfo->class_type*/

	if (p_myInfo->class_type == 0){
        selectedQueue = &economyQueue;
        cur_queue = 0;
        selected_queue_id = 1;
    } else {
        selectedQueue = &businessQueue;
        cur_queue = 1;
        selected_queue_id = 2;
    }

	pthread_mutex_lock(&selectedQueue->lock); 
	{
		fprintf(stdout, "A customer enters a queue: the queue ID %1d, and length of the queue %2d. \n", selected_queue_id, selectedQueue->size);
		enQueue(selectedQueue, p_myInfo);
		queue_enter_time = getCurSystemTime();
	    selectedQueue->size++;
		
        //waits in the queue
		while (1) {
			pthread_cond_wait(&selectedQueue->cv, &selectedQueue->lock);
			if (selectedQueue->head->info == p_myInfo && !winner_selected[cur_queue]) {
				deQueue(selectedQueue);
				selectedQueue->size--;
				winner_selected[cur_queue] = 1; // update the winner_selected variable to indicate that the first customer has been selected from the queue
				break;
			}
		}
			
	}
	pthread_mutex_unlock(&selectedQueue->lock); //unlock mutex_lock such that other customers can enter into the queue
	
	/* Try to figure out which clerk awoken me, because you need to print the clerk Id information */
	usleep(10); // Add a usleep here to make sure that all the other waiting threads have already got back to call pthread_cond_wait. 10 us will not harm your simulation time.
	Clerk* clerk_woke_me_up = queue_status[cur_queue];
	queue_status[cur_queue] = NULL; // NULL = IDLE . 1/2/3/4/5 = CLERKID (BUSY)
	
	/* get the current machine time; updates the overall_waiting_time*/
	double startserve_time = getCurSystemTime();
    double waiting_time =startserve_time - queue_enter_time;
    overall_waiting_time += waiting_time;

    if (cur_queue = 0) {
        economy_time += 
    }
	fprintf(stdout, "A clerk starts serving a customer: start time %.2f, the customer ID %2d, the clerk ID %1d. \n", startserve_time, p_myInfo->user_id, clerk_woke_me_up->clerk_id);
	
	usleep(p_myInfo->service_time * 100000); // service time
	
	/* get the current machine time; */
    double end_time = getCurSystemTime();
	fprintf(stdout, "A clerk finishes serving a customer: end time %.2f, the customer ID %2d, the clerk ID %1d. \n", end_time, p_myInfo->user_id, clerk_woke_me_up->clerk_id);\
	
	pthread_cond_signal(&clerk_woke_me_up->clerk_cv); // Notify the clerk that service is finished, it can serve another customer
	
	pthread_exit(NULL);
	
	return NULL;
}

// function entry for clerk threads
void *clerk_entry(void * clerkNum){
	
    Clerk* clerk = createClerk(clerkNum);

	while(1){
		
        Queue *selectedQueue;
		int selected_queue_ID;// = Select the queue based on the priority and current customers number */
		if (businessQueue.size == 0){
            if (economyQueue.size > 0){
                selectedQueue = &economyQueue;
                selected_queue_ID = 0;
            }
        } else{
            selectedQueue = &businessQueue;
            selected_queue_ID = 1;
        }
		pthread_mutex_lock(&selectedQueue->lock);
		
		queue_status[selected_queue_ID] = clerk; // The current clerk (clerkID) is signaling this queue
		
		pthread_cond_broadcast(&selectedQueue->cv); // Awake the customer (the one enter into the queue first) from the selected queue
		
		winner_selected[selected_queue_ID] = 0; // 0=FALSE set the initial value as the customer has not selected from the queue.
		
        //release mutex and wait for the customer to finish its service
        pthread_cond_wait(&clerk->clerk_cv,&selectedQueue->lock); // when it returns wait for customer, or check queue again

		pthread_mutex_unlock(&selectedQueue->lock);
	}
	
	pthread_exit(NULL);
	
	return NULL;
}