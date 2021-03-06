/** 
 * CES-33 Final Project
 * 
 *  Memory Manager Simulator
 * 
 *  Felipe Tuyama de F. Barbosa
 *	Luiz Angel Rocha Rafael
 */
 
/**
 * 	Memory Manager Includes
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/**
 * 	Memory Manager Defines
 */
// Max Number Definitions
#define MaxStringLength 	7
#define NumThreads			2		//Versao 2: implementacao de threads

// Virtual Memory Pages
#define PagesAmount  		256
#define TLBEntriesAmount	16

// Physical Memory RAM
#define FramesAmount 		256		//Versao 2: 128 quadros de paginas
#define FrameBytesSize 		256

//Files
#define inputfile_default "addresses.txt"
#define backingStore_default "BACKING_STORE.bin"
#define result_default "result.txt"

/**
 * 	Memory Manager Structs
 */
// Page Table (256 pages)
typedef struct pageTable {
	int frameNumber[PagesAmount];
	unsigned int FIFO[FramesAmount];
} PageTable;

// Page (256 bytes)
typedef struct page {
	char PageContent[FrameBytesSize];
} Page;

// TLB - Maps Pages on Physical Memory (16 entries)
typedef struct tlb {
	int pageNumber[TLBEntriesAmount], frameNumber[TLBEntriesAmount];
	unsigned int FIFO[TLBEntriesAmount];
} TLB;

// Physical Memory (65.536 bytes)
typedef struct memory {
	Page frame[FramesAmount];
	int available[FramesAmount];
} Memory;

// Statistics
typedef struct statistics {
	int TranslatedAddressesCounter;
	int PageFaultsCounter;
	int TLBHitsCounter;
} Statistics;

/**
 * 	Threads
 */
pthread_t threads[NumThreads];
pthread_mutex_t mutex;
typedef struct {
	int pageNumber, frameNumber;
}thread_arg, *ptr_thread_arg;
int pageOnTLB;

/**
 * 	Program Global Variables
 */
FILE *addresses, *result, *backingStore;
char line[MaxStringLength] = "";

Statistics *_statistics;
PageTable *_pageTable;
Memory *_memory;
Page *_page;
TLB *_TLB;

/**
 * 	Output results methods
 */
// Write Output results
void writeOut(int virtualAddress, int realAddress, int value)
{
	fprintf(result, "Virtual address: %d ", virtualAddress);
    fprintf(result, "Physical address: %d ", realAddress);
    fprintf(result, "Value: %d\n", value);
    _statistics->TranslatedAddressesCounter++;
}

// Statistics Output Log
void statisticsLog()
{
	float pageFaultRate = _statistics->PageFaultsCounter;
		  pageFaultRate = pageFaultRate/_statistics->TranslatedAddressesCounter;
	float tlbHitsRate = _statistics->TLBHitsCounter;
		  tlbHitsRate = tlbHitsRate/_statistics->TranslatedAddressesCounter;
	fprintf(result, "Number of Translated Addresses = %d\n", _statistics->TranslatedAddressesCounter);
	fprintf(result, "Page Faults = %d\n", _statistics->PageFaultsCounter);
	fprintf(result, "Page Fault Rate = %.3f\n", pageFaultRate);
	fprintf(result, "TLB Hits = %d\n", _statistics->TLBHitsCounter);
	fprintf(result, "TLB Hit Rate = %.3f\n", tlbHitsRate);
}

/**
 * 	Initialization/Finalization methods
 */
// Initializing the Memory Manager
void initialize(char * inputfile)
{
	backingStore = fopen(backingStore_default, "r");
	addresses = fopen(inputfile, "r");
    result = fopen(result_default, "w");
    
    _statistics = (Statistics*)malloc(sizeof(Statistics));
	_pageTable = (PageTable*)malloc(sizeof(PageTable));
	_memory = (Memory*)malloc(sizeof(Memory));
	_page = (Page*)malloc(sizeof(Page));
	_TLB = (TLB*)malloc(sizeof(TLB));
	
	for (int i = 0; i < PagesAmount; i++) 
		_pageTable->frameNumber[i] = -1;
	
	for (int i = 0; i < TLBEntriesAmount; i++)
		_TLB->frameNumber[i] = _TLB->pageNumber[i] = _TLB->FIFO[i] = -1;
		
	for (int i = 0; i < FramesAmount; i++) {
		_memory->available[i] = 1;
		_pageTable->FIFO[i] = -1;
	}
		
	_statistics->TranslatedAddressesCounter = 0;
	_statistics->PageFaultsCounter = 0;
	_statistics->TLBHitsCounter = 0;
}

// Finalizing the Memory Manager
void finalize()
{
	statisticsLog();
	fclose(backingStore);
	fclose(addresses);
    fclose(result);
    
    free(_pageTable);
    free(_memory);
    free(_page);
    free(_TLB);
}

/**
 * 	Managing Page Table methods
 */

// Finding Requested Page on Page Table
int findPageOnPageTable(int pageNumber)
{
	//Return -1 if page is not present (Page Fault)
	return _pageTable->frameNumber[pageNumber];
}

void *thread_findOnPageTable(void *arg) 
{
	ptr_thread_arg targ = (ptr_thread_arg)arg;
	
	if (_pageTable->frameNumber[targ->pageNumber] != -1) {
		// FrameNumber found. Mutex to prevent errors
		pthread_mutex_lock(&mutex);
		if (pageOnTLB == 0) 
			targ->frameNumber = _pageTable->frameNumber[targ->pageNumber];
		pthread_mutex_unlock(&mutex);
		return NULL;
	}
	
	// Requested Page not found on Page Table.
	return NULL;
}

// Setting Used Page on Page Table
void setPageOnPageTable(int pageNumber, int frameNumber)
{
	_pageTable->frameNumber[pageNumber] = frameNumber;
}

/**
 * 	Managing TLB methods
 */

// Update TLB FIFO
int updateTLBFIFO()
{
	int slot = 0;

	//There is a free slot on TLB
	if (_TLB->FIFO[0] == -1) {

		//Move the FIFO
		for (int i = 0; i < TLBEntriesAmount-1; i++)
			_TLB->FIFO[i] = _TLB->FIFO[i+1];

		//Seeks for free slot on the TLB
		for (int i = 0; i < TLBEntriesAmount; i++) {
			if (_TLB->pageNumber[i] == -1) {
				slot = i;
				break;
			}
		}
	}

	//There is not a free slot on TLB
	else {
		//Picks the first slot on TLB
		slot = _TLB->FIFO[0];

		for (int i = 0; i < TLBEntriesAmount-1; i++)
			_TLB->FIFO[i] = _TLB->FIFO[i+1];
	}

	_TLB->FIFO[TLBEntriesAmount-1] = slot;
	return slot;
}

// Finding Requested Page on TLB
int findPageOnTLB(int pageNumber)
{
	for (int i = 0; i < TLBEntriesAmount; i++) {
		if (_TLB->pageNumber[i] == pageNumber) {
			_statistics->TLBHitsCounter++;
			//Requested Page is found on TLB
			return _TLB->frameNumber[i];
		}
	}

	// Requested Page not found on TLB.
	return -1;
}

void *thread_findOnTLB(void *arg) 
{
	ptr_thread_arg targ = (ptr_thread_arg)arg;

	for (int i = 0; i < TLBEntriesAmount; i++)
		if (_TLB->pageNumber[i] == targ->pageNumber) 
		{
			// FrameNumber found. Mutex to prevent errors
			pthread_mutex_lock(&mutex);
			targ->frameNumber = _TLB->frameNumber[i];
			pageOnTLB = 1;
			pthread_mutex_unlock(&mutex);
			
			_statistics->TLBHitsCounter++;
			
			return NULL;
		}
	
	// Requested Page not found on TLB.
	pageOnTLB = 0;
	return NULL;
}

// Setting Used Page on TLB
void setPageOnTLB(int pageNumber, int frameNumber)
{
	int newTLBindex = updateTLBFIFO();

	_TLB->frameNumber[newTLBindex] = frameNumber;
	_TLB->pageNumber[newTLBindex] = pageNumber;
}
/**
 * 	Managing Memory methods
 */

// Find Oldest Frame on memory (first element on FIFO)
int findOldestFrameOnMemory(int pageNumber)
{
	//The oldest page on the queue
	int switchedpage = _pageTable->FIFO[0];

	//Sets the switched page as unavailable
	int slot = _pageTable->frameNumber[switchedpage];
	_pageTable->frameNumber[switchedpage] = -1;

	//Updates the 
	for (int i = 0; i < FramesAmount - 1; i++) {
		_pageTable->FIFO[i] = _pageTable->FIFO[i+1];
	}

	_pageTable->FIFO[FramesAmount-1] = pageNumber;
	return slot;
}


//Updates values of Page Table FIFO
void updatePageTableFIFO(int pageNumber) {

	for (int i = 0; i < FramesAmount - 1; i++) {
		_pageTable->FIFO[i] = _pageTable->FIFO[i+1];
	}

	//Puts the most recent page as the last of queue
	_pageTable->FIFO[FramesAmount-1] = pageNumber;
}

// Find Available Frame on memory
int findAvailableFrameOnMemory(int pageNumber)
{
	for (int i = 0; i < FramesAmount; i++) {
		if (_memory->available[i] == 1) {
			_memory->available[i] = 0;
			updatePageTableFIFO(pageNumber);

			//There is available memory
			return i;
		}
	}

	//There is not available memory
	return -1;
}

// Find Frame on memory
int findFrameOnMemory(int pageNumber)
{
	int chosenFrame = findAvailableFrameOnMemory(pageNumber);

	if (chosenFrame == -1) {
		chosenFrame = findOldestFrameOnMemory(pageNumber);
	}
	return chosenFrame;
}

/**
 *  Managing Backing Store methods
 */
// Manage Backing_Store
void getBackingStorePage(int pageNumber, int frameNumber)
{
	_statistics->PageFaultsCounter++;
	fseek(backingStore, pageNumber*FrameBytesSize, SEEK_SET);
	fread(_memory->frame[frameNumber].PageContent, FrameBytesSize, 1, backingStore);
}

/**
 * 	Debug application methods
 */
 // Debug TLB
 void debugTLB()
 {
	printf("\nTLBf[");
	for (int i = 0; i < TLBEntriesAmount; i++)
		printf("%3d ", _TLB->frameNumber[i]);
	printf("]\n");
 }
 
// Debug Page Address
void debugPageAddress(int address, int pageNumber, int offset)
{
	printf ("Virtual Address: %5d ", address); 
	printf ("PageNumber : %3d ", pageNumber);
	printf ("PageOffset : %3d", offset);
	getchar();
}

// Debug Real Address
void debugFrameAddress(int address, int frameNumber, int offset)
{
	printf ("  Real  Address: %5d ", address); 
	printf ("FrameNumber: %3d ", frameNumber);
	printf ("FrameOffset: %3d", offset);
	getchar();
}

/**
 * 	Find Frame Number Assynchronous
 */
// Find frameNumber on TLB and PageTable on different threads
int findFrameNumberAssynchronous(int pageNumber)
{
	thread_arg arguments;
	arguments.pageNumber = pageNumber;
	arguments.frameNumber = -1;
	pageOnTLB = 0;
	
	pthread_mutex_init(&mutex, NULL);
	pthread_create(&threads[0], NULL, thread_findOnTLB, &(arguments));
	pthread_create(&threads[1], NULL, thread_findOnPageTable, &(arguments));
	pthread_join(threads[0], NULL);
	pthread_join(threads[1], NULL);
	
	int frameNumber = arguments.frameNumber;
	
	if (frameNumber == -1)
	{
		// Load on memory entire page of BACKING_STORE
		frameNumber = findFrameOnMemory(pageNumber);
		getBackingStorePage(pageNumber, frameNumber);
		
		// Set up accessed page on Page Table
		setPageOnPageTable(pageNumber, frameNumber);
	}
	if (pageOnTLB == 0)
		setPageOnTLB(pageNumber, frameNumber);
		
	return frameNumber;
}

/**
 * 	Find Frame Number Synchronous
 */
int findFrameNumberSynchronous(int pageNumber)
{
	// Find frameNumber on TLB
	int frameNumber = findPageOnTLB(pageNumber);
	
	// If TLB find fails
	if (frameNumber == -1)
	{
		// Find frameNumber on Page Table
		frameNumber = findPageOnPageTable(pageNumber);
		
		// If Page Fault
		if (frameNumber == -1)
		{
			// Load on memory entire page of BACKING_STORE
			frameNumber = findFrameOnMemory(pageNumber);
			getBackingStorePage(pageNumber, frameNumber);
			
			// Set up accessed page on Page Table
			setPageOnPageTable(pageNumber, frameNumber);
		}
		// Set up accessed page on TLB
		setPageOnTLB(pageNumber, frameNumber);
	}
	return frameNumber;
}

/**
 * 	Main Memory Manager
 */
int main(int arc, char** argv)
{
	if(arc == 1) 	initialize(inputfile_default);
	else 			initialize(argv[1]);
	
    while(fgets(line , MaxStringLength, addresses))
    {
		// Read new virtual Address
        int virtualAddress = atoi(line);
        int pageNumber =  virtualAddress/PagesAmount;
        int offset = virtualAddress & (PagesAmount-1);
        
        // Find frameNumber
        int frameNumber = findFrameNumberSynchronous(pageNumber);
        // int frameNumber = findFrameNumberAssynchronous(pageNumber);
        
		// Parse real Address
		int value = _memory->frame[frameNumber].PageContent[offset];
		int realAddress = frameNumber*PagesAmount + offset;
		writeOut(virtualAddress, realAddress, value);
		
		// Debuggind PageAddress and FrameAddress
        // debugPageAddress(virtualAddress, pageNumber, offset);
        // debugFrameAddress(realAddress, frameNumber, offset);
    }
    finalize();
    return 0;
}
