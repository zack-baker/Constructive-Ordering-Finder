#include <stdio.h> //used for input 
#include <pthread.h> //used for multithreading
#include <unistd.h> //used for sysconf, _SC_NPROCESSORS_ONLN, 
#include <mutex> //used for mutex
#include <chrono> //used for timing functions
#include <algorithm> //used for std::copy
#include <vector>

std::mutex mu;
//function prototypes
void print_arr(int*,int, FILE*); 
void* thread_ordering_creator(void*);
int verify_ordering(int*,int);
int* get_starting_baselist(int*, int, long int);
unsigned long factorial(int);
int* get_tuple(int, unsigned long);
int* get_list(int*, int, int);
void print_vector(std::vector<int>);

//Struct to hold the parameters to provide to the function each thread is processing
struct Thread_Param{
	int id;//the id of the thread
	int* baselist;// a pointer to the ordering to start calculation from
	int n;//the size of the group to calculate the permutations for; Z/nZ
	long int partition_size;//the number of permutations to calculate
	bool results;
	Thread_Param(){}
	Thread_Param(int i, int* b, int n_val, long int ps, bool res){
	 	id = i;
		n = n_val;
	 	baselist = new int[n-1];
	 	for(int i=0;i<n-1;i++){
	 		baselist[i] = b[i];
	 	}
	 	partition_size = ps;
	 	results = res;
	}
	Thread_Param(Thread_Param& t){//Copy Constructor for Thread_Param
	 	id = t.id;
	 	n = t.n;
	 	partition_size = t.partition_size;
	 	baselist = t.baselist;
	 }
	~Thread_Param(){
		delete [] baselist;
	 }
};

int main(int argc, char const *argv[])
{
	if(argc<2){//if usage is incorrect, exit
		fprintf(stderr, "USAGE: %s n [THREAD_MULT] [SAVE_FLAG]\n", argv[0]);
		_exit(1);
	}
	int n = atoi(argv[1]);//store the first command line argument, the value for n
	int thread_mult = 1;
	if(argc>2){//if there's more than one command-line argument, the second arg is the thread multiplier
		thread_mult = atoi(argv[2]);
	}
	int max_threads = thread_mult*sysconf(_SC_NPROCESSORS_ONLN);//get the number of on-line concurrent threads. The number of child processes created is calculated as a multiple of the number of concurrent threads as optionally given by the user on program initialization
	
	if(n<(max_threads/thread_mult)){
		fprintf(stderr, "[ERROR] Command Line argument too small; must be greater than the number of concurrent threads, %d\n", max_threads);
		_exit(3);
	}
	bool save_results = false;
	//if there is a third argument equal to 1, then save the list of constructive orderings
	if(argc>3){
		if(atoi(argv[3])==1){
			save_results = true;
		}
	}
	if(save_results){
		printf("Saving results..\n");
	}
	std::chrono::time_point<std::chrono::system_clock> start, end;//create the timekeeping variables
	start = std::chrono::system_clock::now();//initialize the first time variable
	
	long total_perms = factorial(n-1);//calculate the total number of permutations to verify. We use n-1 because the first value of a constructive ordering is known to be a fixed value (0), so we do not need to verify orderings that do not begin with 0
	unsigned long partition_size = (total_perms/max_threads)/2;//calculate the number of permutations each thread should process. We divide by two because each ordering of the form (0,k,...), where k is less than n/2, can be mapped to exactly one ordering of the form (0,l,...), where l is greater than n/2
	if(save_results){
		if(argc>4){//if we set this 4th parameter, use the full list, instead of the half list for full results
			if(atoi(argv[4])==1){
				partition_size = total_perms/max_threads + 1;
			}	
		}
		
	}
	printf("MAX THREADS: %d\n", max_threads);
	pthread_t threads[max_threads];//create the array of threads
	int* baselist = NULL;// pointer to point to the first ordering each thread begins with
	for(int i=0;i<max_threads;i++){//for each thread,
		//baselist = get_starting_baselist(baselist, n, partition_size); //generate the starting ordering for the thread
		baselist = get_list(get_tuple(n-1, partition_size*i),n,i);
		Thread_Param* tp = new Thread_Param(i,baselist, n, partition_size,save_results);//create the Thread_Param object to supply to the function provided to each thread//allocate it dynamically so each thread has its own struct
		int t_status = pthread_create(&threads[i], NULL, thread_ordering_creator,(void*)tp);//create the thread, having it compute the thread_ordering_creator function
		if(t_status!=0){//if there is an error creating the thread, exit
			fprintf(stderr, "[ERROR] Error creating thread %d; status is %d; exiting\n", i, t_status);
			_exit(2);
		}

	}
	long int total_good_perms = 0;//variable to hold the final result

	//loop to process each thread
	for(int i=0;i<max_threads;i++){
		void* results_ptr;
		pthread_join(threads[i], &results_ptr);//join each thread as it finishes
		mu.lock();
		total_good_perms+= (long)(results_ptr);//ensure the results variable is locked to be processed single-threadedly, to avoid write collisions
		mu.unlock();
	}

	total_good_perms*=2;
	end = std::chrono::system_clock::now();//get the ending time
	std::chrono::duration<double> time_taken = end-start;
	double total_time = time_taken.count();
	printf("FINISHED - Total good orderings: %ld - Time taken: %f\n", total_good_perms, total_time);
	printf("%d,%f\n", thread_mult, total_time);	
	//if we're saving the lists, we don't care about performance, because it will always be faster to not print the orderings. This helps clean up program output. Redirect stderr to a file to capture the results
	if(save_results){
		fprintf(stderr, "%d,%f\n", thread_mult, total_time);
	}
	return 0;


}



/**
*	This function confirms or denies if ord is a valid ordering
*	INPUT:
*		- ord: a pointer to an int array representing an ordering to verify
*		- size: the size of the ordering
*		- tid: the thread id of the thread ord belongs to
*		- results: a flag used to save results
*	OUTPUT:
		- returns 1 if ord is a constructive ordering, and 0 otherwise
*/

int verify_ordering(int* ord, int size, int tid, bool results){
	int total = 0;
	int n = size+1;
	bool* elements_seen = new bool[n]();//use an array to keep track of values already seen
	for(int i=0;i<size;i++){
		total += ord[i];
		total %= n;
		//if the element was found previously, or if the sum is 0, discard this ordering. 
		if(elements_seen[total] || total==0 || (total==n/2 && i!=size-1)){
			delete [] elements_seen;
			return 0;
		}
		else{//otherwise, set the flag at index total to true to indicate the value has been seen before.
			elements_seen[total]=true;
		}
	}
	//if we make it here, the ordering is a constructive ordering
	if(results){
		mu.lock();
		print_arr(ord, size, stderr);
		mu.unlock();
	}
	delete [] elements_seen;
	return 1;
}

/**
*	This method calculates the orderings for each thread to begin with. The thread then processes each ordering, deriving each successive ordering
*	INPUT:
*		- old_baselist: the ordering the previous thread began with
*		- n: the size of the group as provided by the user
*		- partition_size: the number of orderings to calculate per thread
*	OUTPUT:
*		Returns the ordering for the next thread to start verifying from
*/
int* get_starting_baselist(int* old_baselist, int n, long int partition_size){
	int v = n-1;
	long int list_count = 0;
	int* perm_list = new int[v];// array to return; the ordering for the thread to start with
	if(old_baselist==NULL){// if this is the first thread, use the natural ordering (0,1,2,...,n-1) for this thread
		for(int i=0;i<v;i++){
			perm_list[i] = i+1;
		}
		return perm_list;
	}
	//otherwise
	for(int i=0;i<v;i++){//create the first ordering
		perm_list[i] = old_baselist[i];
	}
	do{//iterate through successive orderings
		list_count++;
		if(list_count>=partition_size){
			break;
		}
	}while(std::next_permutation(perm_list, perm_list+v));
	delete old_baselist;
	return perm_list;
}

/**
*	This function prints the first size elements from arr
*	INPUT:
*		- arr: the array to print
*		- size: the number of elements in arr
*		- f: the file to print to. By default, this is set as stdout to print to the terminal
*/

void print_arr(int* arr, int size, FILE* f=stdout){
	for(int i=0;i<size;i++){
		fprintf(f, "%d",arr[i]);
		if(i!=size-1){
			fprintf(f, ",");
		}
	}
	fprintf(f,"\n");
	return;
}

/**
*	This function iterates through the orderings each thread is responsible for checking
*		INPUT:
*			- args: a pointer to a Thread_Param struct
*		OUTPUT:
*			- the number of constructive orderings in the bounds for this thread 	
*/
void* thread_ordering_creator(void* args){
	// Get the Thread_Param struct out of args
	Thread_Param* params = (Thread_Param*)(args);	
	int v = params->n-1;
	int* iterate_list = new int[v];//list used to iterate through
	for(int i=0;i<v;i++){
		iterate_list[i] = params->baselist[i];//deepcopy into iterate_list
	}
	long int good_orderings_calculated = 0;
	long int total_orderings_seen = 0;
	do{	
		int is_good_ordering = verify_ordering(iterate_list, v, params->id, params->results);//verify this ordering
		good_orderings_calculated += is_good_ordering;
		total_orderings_seen++;
		if(total_orderings_seen>params->partition_size-1){
			break;
		}
	}while(std::next_permutation(iterate_list, iterate_list+v));//iterate through each permutation

	delete [] iterate_list;
	delete params;
	return (void*)good_orderings_calculated;
}


/**
*	This is a simple recursive implementation of the factorial
*	INPUT:
*		- n: the value to calculate the factorial of
*	OUTPUT:
*		returns n!
*/
unsigned long factorial(int n){
	if(n<0)
		return -1;
	if(n==0 || n==1)
		return 1;
	else{
		return n*factorial(n-1);
	}
}

/**
*	This method generates a tuple representing the coefficients of tuple_num's representation in factorial base. This is used to calculate the tuple_numth lexicographical permutation.
*	INPUT:
		- m: the size of the group minus 1, i.e. n-1
		- tuple_num: the value to convert to factorial base.
	OUTPUT:
		- an array of size m representing a tuple of coefficients of the factorial representation of tuple_num
*/
int* get_tuple(int m, unsigned long tuple_num){
	int* tuple = new int[m];
	for(int i=0;i<m;i++){
		int v = tuple_num/factorial(m-i);
		tuple[i] = v;
		tuple_num=tuple_num-factorial(m-i)*v;
	}
	return tuple;
}

int* get_list(int* tuple, int n, int id){
	std::vector<int> ordered_list;
	for(int i=0;i<n;i++){
		ordered_list.push_back(i);
	}
	int* list = new int[n];
	for(int i=0;i<n-1;i++){
		list[i] = ordered_list.at(tuple[i]);
		ordered_list.erase(ordered_list.begin()+tuple[i]);
	}
	list[n-1] = ordered_list.front();

	int* nl = new int[n-1];
	for(int i=0;i<n-1;i++){
		nl[i] = list[i+1];
	}
	return nl;
	
}

void print_vector(std::vector<int> v){
	for(int i=0;i<v.size();i++){
		printf("v[%d]: %d\n", i, v.at(i));
	}
}