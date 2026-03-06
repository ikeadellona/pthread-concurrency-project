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

/* global variables */
const int NQUEUE = 2;
const int NClerks = 5;
struct timeval init_time; // use this variable to record the simulation start time; No need to use mutex_lock when reading this variable since the value would not be changed by thread once the initial time was set.

//Variables to calculate the average waiting time. Every customer add their own waiting time to these variables, mutex_lock is necessary.
double overall_waiting_time; //A global variable to add up the overall waiting time for all customers
double economy_time; // records the total waiting time of customers in economy class
double business_time; // records the total waiting time of customers in business class
int total_business_customers; // records the number of customers in business class
int total_economy_customers; // records the number of customers in economy class

int queue_status[NQUEUE] = {0,0}; // variable to record the status of a queue, the value could be idle (not using by any clerk) or the clerk id (1 ~ 4), indicating that the corresponding clerk is now signaling this queue.
int winner_selected[NQUEUE]; // variable to record if the first customer in a queue has been successfully selected and left the queue.

pthread_cond_t clerk_cv[NClerks]; // condition variable for each clerks
pthread_mutex_t queues_lock; // condition variable for each queue
pthread_mutex_t stats_lock; // condition variable for variables that computes average waiting time

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

Queue* economyQueue;
Queue* businessQueue;

//Inititalizer
Queue* initializeQueue(Queue* q){
    q = (Queue*)malloc(sizeof(Queue));
    if (q == NULL){
        printf("Error: malloc failed");
        exit(1);
    }

    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
    if (pthread_mutex_init(&q->lock, NULL) != 0){ //mutex initialization
        printf("\n mutex init failed\n");
        exit(1);
    }
    if (pthread_cond_init(&q->cv, NULL) != 0){ //mutex initialization
        printf("\n queue convar init failed\n");
        exit(1);
    }
    return q;
}

// creates a customer
Customer* createCustomer(struct customer_info *cust){
    Customer* customer = (Customer*)malloc(sizeof(Customer));
    customer->info = cust;
    customer->next = NULL;
    return customer;
}

// to check if there are customers in the queue
int isEmpty(Queue* q){
    return q->head==NULL;
}
// put the customer at the last of the queue
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
    if (isEmpty(q)){
        exit(1);
    }

    Customer* front = q->head;
    // struct customer_info  removed_customer = front->info;
    q->head = q->head->next;
    if (q->head == NULL){
        q->tail=NULL;
    }
    free(front);
}

// time structure
double getCurSystemTime(){ //gets the time since the simulation started
	
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

    if (argc < 2) {
        printf("Error: You must provide a filename\n");
        return 1;
    }

	// initialize all the condition variable and thread lock will be used
    economyQueue = initializeQueue(economyQueue);
    businessQueue =  initializeQueue(businessQueue);
    
    for (int i = 0; i < NClerks; i++) {
        if (pthread_cond_init(&clerk_cv[i], NULL) != 0){ //mutex initialization
            printf("\n clerk convar init failed\n");
            return 1;
        }
    }
    if (pthread_mutex_init(&queues_lock, NULL) != 0){ //mutex initialization
        printf("\n mutex init failed\n");
        return 1;
    }
    if (pthread_mutex_init(&stats_lock, NULL) != 0){ //mutex initialization
        printf("\n mutex init failed\n");
        return 1;
    }
    
    // file handle
    FILE *inputfile = fopen(argv[1], "r");

    if (inputfile == NULL) {
        printf("Error: Unable to open file\n");
        return 1;
    }
    // read the first line of file and get the number of customers then store it in Ncustomers
    int NCustomers;
    char line[256];
    fgets(line, sizeof(line), inputfile);
    sscanf(line, "%d",&NCustomers);

    struct customer_info customers[NCustomers]; //customer array
    int clerk_info[NClerks]; //clerk array

    int i = 0;

    //store customer info
    while(fgets(line, sizeof(line), inputfile) && i < NCustomers){
        int n = sscanf(line, "%d:%d,%d,%d", &customers[i].user_id, &customers[i].class_type, &customers[i].arrival_time, &customers[i].service_time);
        if (customers[i].arrival_time <=0 || customers[i].service_time <= 0) {
            printf("Error: Invalid input\n");
            return 1;
        }
        if (n != 4){
            printf("Error: Invalid input\n");
            return 1;
        }
        i++;
    }
    // store clerk info
    int k = 1;
    for (i = 0; i < NClerks; i++){
        clerk_info[i] = k;
        k++;
    }

    //start simulation time
    gettimeofday(&init_time, NULL); // record simulation start time

    pthread_t customId[NCustomers]; // customer thread array
    pthread_t clerkId[NClerks]; //clerk thread array
    int rc;

	//create clerk thread (optional)
	for(i = 0; i < NClerks; i++){ // number of clerks
        rc = pthread_create(&clerkId[i], NULL, clerk_entry, (void *)&clerk_info[i]);
		if (rc != 0){ // clerk_info: passing the clerk information (e.g., clerk ID) to clerk thread
            printf("Error: pthread_create, rc: %d\n", rc);
            return 1;
        }
    }
	// //create customer thread
	for(i = 0; i < NCustomers; i++){ // number of customers
        rc = pthread_create(&customId[i], NULL, customer_entry, (void *)&customers[i]);
		if (rc != 0){ //custom_info: passing the customer information (e.g., customer ID, arrival time, service time, etc.) to customer thread
            printf("Error: pthread_create, rc: %d\n", rc);
            return 1;
        }
    }
    // wait for all customer threads to terminate
	for (i = 0; i < NCustomers; i++){
		pthread_join(customId[i], NULL);
	}

    fprintf(stdout, "All jobs done...\n");
	// calculate the average waiting time of all customers
    double allaverage_waitingtime = overall_waiting_time / NCustomers;
    double busaverage_waitingtime = 0.0; // calculate the average waiting time for business-class customers
    double ecoaverage_waitingtime = 0.0; // calculate the average waiting time for economy-class customers

    if (total_business_customers > 0){
        busaverage_waitingtime = business_time / total_business_customers;    
    }
    if (total_economy_customers > 0){
        ecoaverage_waitingtime = economy_time / total_economy_customers;
    }
    fprintf(stdout, "The average waiting time for all customers in the system is: %.2f seconds. \n", allaverage_waitingtime);
    fprintf(stdout, "The average waiting time for all business-class customers is: %.2f seconds. \n", busaverage_waitingtime);
    fprintf(stdout, "The average waiting time for all economy-class customers is: %.2f seconds. \n", ecoaverage_waitingtime);

    fclose(inputfile);
	return 0;
}

void * customer_entry(void * cus_info){
	
	struct customer_info * p_myInfo = (struct customer_info *) cus_info;
    double queue_enter_time;
	Queue *selectedQueue = NULL;
    int cur_queue;
    int selected_queue_id;
    int clerk_woke_me_up;
    
	usleep(p_myInfo->arrival_time * 100000); // sleep until arrival time of customer
	
	fprintf(stdout, "A customer arrives: customer ID %2d. \n", p_myInfo->user_id);
	
	/* Enqueue operation: get into either business queue or economy queue by using p_myInfo->class_type*/

	if (p_myInfo->class_type == 0){
        selectedQueue = economyQueue;
        cur_queue = 0;
        selected_queue_id = 1;
        pthread_mutex_lock(&stats_lock);
        total_economy_customers++;
        pthread_mutex_unlock(&stats_lock);

    } else {
        selectedQueue = businessQueue;
        cur_queue = 1;
        selected_queue_id = 2;
        pthread_mutex_lock(&stats_lock);
        total_business_customers++;
        pthread_mutex_unlock(&stats_lock);
    }

    

	pthread_mutex_lock(&selectedQueue->lock); 
	{
		enQueue(selectedQueue, p_myInfo);
		queue_enter_time = getCurSystemTime();
	    selectedQueue->size++;
		fprintf(stdout, "A customer enters a queue: the queue ID %1d, and length of the queue %2d. \n", selected_queue_id, selectedQueue->size);
		
        //waits in the queue
		while (1) {
			pthread_cond_wait(&selectedQueue->cv, &selectedQueue->lock);
			if (selectedQueue->head->info == p_myInfo && !winner_selected[cur_queue]) {
                clerk_woke_me_up = queue_status[cur_queue]; // store the clerkID that is serving the customer thread
				deQueue(selectedQueue);
				selectedQueue->size--;
                queue_status[cur_queue] = 0; // 0 = IDLE . 1/2/3/4/5 = CLERKID (BUSY)
				winner_selected[cur_queue] = 1; // update the winner_selected variable to indicate that the first customer has been selected from the queue
				break;
			}
		}
			
	}
	pthread_mutex_unlock(&selectedQueue->lock); //unlock mutex_lock such that other customers can enter into the queue
	
	usleep(10); // makes sure that all the other waiting threads have already got back to call pthread_cond_wait.
	
	/* get the current machine time; updates the overall_waiting_time*/
	double startserve_time = getCurSystemTime();
    double waiting_time =startserve_time - queue_enter_time; //only for this customer thread

    //handle shared time data
    pthread_mutex_lock(&stats_lock);
    overall_waiting_time += waiting_time;
    if (cur_queue == 0) {
        economy_time += waiting_time;
    } else {
        business_time += waiting_time;
    }
	fprintf(stdout, "A clerk starts serving a customer: start time %.2f, the customer ID %2d, the clerk ID %1d. \n", startserve_time, p_myInfo->user_id, clerk_woke_me_up);
    pthread_mutex_unlock(&stats_lock);

	usleep(p_myInfo->service_time * 100000); // service time
	
	/* get the current machine time; */
    double end_time = getCurSystemTime();
	fprintf(stdout, "-->>> A clerk finishes serving a customer: end time %.2f, the customer ID %2d, the clerk ID %1d. \n", end_time, p_myInfo->user_id, clerk_woke_me_up);\
	
	pthread_cond_signal(&clerk_cv[clerk_woke_me_up - 1]); // Notify the clerk that service is finished, it can serve another customer
	
	pthread_exit(NULL);
	
	return NULL;
}

// function entry for clerk threads
void *clerk_entry(void * clerkNum){
	
    int clerkID = *(int*) clerkNum;
    Queue* selectedQueue = NULL;
    int selected_queue_id;
	while(1){
		
        pthread_mutex_lock(&queues_lock);

        if (businessQueue->size > 0 && queue_status[1] == 0){
            selectedQueue = businessQueue;
            selected_queue_id = 1;
            queue_status[selected_queue_id] = clerkID;
        } else if (economyQueue->size > 0 && queue_status[0] == 0) {
            selectedQueue = economyQueue;
            selected_queue_id = 0;
            queue_status[selected_queue_id] = clerkID; // The current clerk (clerkID) is signaling this queue
        } else {
            pthread_mutex_unlock(&queues_lock);
            usleep(10);
            continue;
        }
        pthread_mutex_unlock(&queues_lock);

        if (selectedQueue != NULL) {
            pthread_mutex_lock(&selectedQueue->lock);
            winner_selected[selected_queue_id] = 0; //initialize that a customer has not been selected in the queue
            pthread_cond_broadcast(&selectedQueue->cv); // Awake the customer (the one enter into the queue first) from the selected queue
            pthread_cond_wait(&clerk_cv[clerkID-1],&selectedQueue->lock); // waits for customer to finish serving, when it returns wait for customer, or check queue again
            pthread_mutex_unlock(&selectedQueue->lock);
        }

	}
	
	pthread_exit(NULL);
	
	return NULL;
}