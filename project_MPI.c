
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#include <mpi.h>

////////////////////////////////////////////////////////////////////////////////
// MPI PROJECT - SHEA KITSON - 40202515
////////////////////////////////////////////////////////////////////////////////

/*
A fine grain approach has been adopted in this MPI program, the master process will do some
admin work at the start, including generating the output file and reading in the control file.
Then the master process will then divide the text file up into portions and share it among the sub-processes.
Each sub-process will search their portion of text for the pattern, and then report their results back to the master
at the end using a MPI_Reduce.

STATEGIES TO OPTIMISE PERFORMANCE:
The fine grain approach is better for searches made up of mixed large and small inputs. Each thread is searching through an
even amount of text no matter how large the text is. 

If either the text or pattern file is empty, or if the text file is shorter than the pattern file, then the search is skipped as it 
is impossible for the pattern to be found when any of these things are true. When this happens I report it as pattern not found.

The linked list structure that is used to hold all occurances of a pattern will dynamically allocate its memory each time a new element 
is added to the list. Although this does not directly affect how fast the program executes, it means I am not statically allocating more
memory than necessary to hold the results.
*/


MPI_Status status;

typedef struct linkedList 
{ //define linked list structure which is used to store all positions that pattern is found
    int index;
    struct linkedList* next;
}linkedList_;

void pushToList(linkedList_ ** head, int value)
{ //This method will push a value to the front of the linked list
	linkedList_* foundAt;
	foundAt = (linkedList_*) malloc(sizeof(linkedList_));
	foundAt->index = value;
	foundAt->next= *head;
	*head = foundAt;
}

char *textData;
int textLength;

char *patternData;
int patternLength;

char *controlData; //stores the data from the control file
int controlLength; //stores length of control file

char typeOfRead; //stores the type of search to be done, i.e. '0' to find if pattern exists or '1' to find all occurances of pattern
char* textNumber; //stores text number to be searched
char* patternNumber; //stores pattern number that the program is searching for

unsigned int combinedResult; //used to reduce the results from all processes in a type '0' search to either -2 (found) or -1 (not found)
linkedList_* allResults; //used to reduce the results from all processes in a type '1' search, will contain every position a pattern is found

char *textPortion; //separate array to store portions of text so that the master process can divide it up between processes
int portionLength; //variable to store length of a portion of text
int startIndex; //used by each process to store their starting index to search in the main textData
int numPatternsFound; //used to count the number of patterns found by each process
int exists; //will be set by each process to -2 if they find the pattern or -1 if they dont
linkedList_* foundAt; //will contain all the positions of where the pattern is found by each process

int worldRank; //used by each process to query their world rank
int worldSize; //common accross all process, set using the -np flag in the run script

bool controlRead = false; //indicates if the control file read has been started or not
int allSearchesDone = 0; //indicates if all of the searches are complete, changes to 1 once they are all complete

FILE *outputFile; //File to output results

void extractPortion(int startIndex, int endIndex, int size) 
{//this method will extract a certain size portion from the textData from the given start index to the end index
	textPortion = (char *)realloc(textPortion, sizeof(char) * size); //allocate required amount of memory for array
	for (int i = startIndex; i <= endIndex; i++)
	{
		textPortion[i - startIndex] = textData[i]; 
	}
}

void sendPortionOfText(int sendTo, int startIndex, int maxEndIndex)
{ //this method will be used by the master process to send the allocated portion of text to all other processes
	int sizeToSend = maxEndIndex - startIndex;
	extractPortion(startIndex, maxEndIndex, sizeToSend);
	MPI_Send(&sizeToSend, 1, MPI_INT, sendTo, 100, MPI_COMM_WORLD);
	MPI_Send(textPortion, sizeToSend, MPI_CHAR, sendTo, 200, MPI_COMM_WORLD);
}

void sendPattern(int sendTo)
{ //this method will be used by the master process to send the current pattern to all other processes
	MPI_Send(&patternLength, 1, MPI_INT, sendTo, 100, MPI_COMM_WORLD);
	MPI_Send(patternData, patternLength, MPI_CHAR, sendTo, 200, MPI_COMM_WORLD);
}


void generateOutputFile() 
{ //This method will generate the output file to store results
	char fileName[1000];
	sprintf (fileName, "result_MPI.txt");
	outputFile = fopen(fileName, "w");
}

void insertLineInFile(int result) 
{ //This method will insert a line in the output file in the format textFile patternFile result
    fputs(textNumber, outputFile);
    fputc(' ', outputFile);
    fputs(patternNumber, outputFile);
    fputc(' ', outputFile);
    fprintf(outputFile, "%d", result);
    fputc('\n', outputFile);
}

/*
This method will remove all duplicate values from the unsorted linked list.
Before removeDuplicates:
4-->32-->32-->8-->9-->32-->8
After removeDuplicates:
4-->32-->8-->9
I took this removeDuplicates method from the following resource:
https://www.geeksforgeeks.org/remove-duplicates-from-an-unsorted-linked-list/
The author is listed as Gaurav Saxena.
*/
void removeDuplicates(linkedList_* start)
{
    linkedList_ *ptr1, *ptr2, *dup;
    ptr1 = start;
 
    while (ptr1->index != -1 && ptr1->next != NULL) {
        ptr2 = ptr1;

        while (ptr2->next != NULL) {
            if (ptr1->index == ptr2->next->index) {
                ptr2->next = ptr2->next->next;
                free (dup);
            }
            else
                ptr2 = ptr2->next;
        }
        ptr1 = ptr1->next;
    }
}

//standard method taken from searching_sequential.c from Blesson
void outOfMemory(int amount)
{
	printf("Tried to allocate %d\n", amount);
	fprintf (stderr, "Out of memory\n");
	//MPI_Finalize(); 
	exit (0);
}

//standard method taken from searching_sequential.c from Blesson
void readFromFile (FILE *f, char **data, int *length)
{
	int ch;
	int allocatedLength;
	char *result;
	int resultLength = 0;

	allocatedLength = 0;
	result = NULL;

	ch = fgetc (f);
	while (ch >= 0)
	{
		resultLength++;
		if (resultLength > allocatedLength)
		{
			allocatedLength += 10000;
			result = (char *) realloc (result, sizeof(char)*allocatedLength);
			if (result == NULL)
				outOfMemory(allocatedLength);
		}
		result[resultLength-1] = ch;
		ch = fgetc(f);
	}
	*data = result;
	*length = resultLength;
}

//standard method taken from searching_sequential.c from Blesson
int readData ()
{
	FILE *f;
	char fileName[1000];
#ifdef DOS
        sprintf (fileName, "inputs\\test%d\\text.txt", testNumber);
#else
	sprintf (fileName, "large_inputs/text%s.txt", textNumber);
#endif
	f = fopen (fileName, "r");
	if (f == NULL) {
		return 0;
	}
	readFromFile (f, &textData, &textLength);
	fclose (f);
#ifdef DOS
        sprintf (fileName, "inputs\\test%d\\pattern.txt", testNumber);
#else
	sprintf (fileName, "large_inputs/pattern%s.txt", patternNumber);
#endif
	f = fopen (fileName, "r");
	if (f == NULL) {
		return 0;
	}
	readFromFile (f, &patternData, &patternLength);
	fclose (f);

	return 1;
}


/*
This method is a modified version of the original hostMatch, it allows the processes to store
all positions at which they find a pattern match in a linked list, which is later reduced into 
a single linked list in the master process. This method is only used when the search type is 
a '1' ie. When all occurances of a pattern need to be found.
*/
linkedList_* hostMatchFindAll()
{
	int startPos,matchingCounter,currentPos, lastI;

	startPos=0;
	matchingCounter=0;
	currentPos=0;
	lastI = textLength-patternLength;

    linkedList_* foundAtList;
	foundAtList = (linkedList_*) realloc(foundAtList, sizeof(linkedList_)); //allocate memory for linked list object
    foundAtList->next = NULL; //set linked list initial values
    foundAtList->index = -1;

	if (worldRank == 0)
	{ //make sure master process only searches the first portion of the text
		lastI = (textLength / worldSize) + patternLength;
		if(lastI > textLength)
		{
			lastI = textLength;
		}
	}
	while (startPos<=lastI)
	{
		if (textData[currentPos] == patternData[matchingCounter])
		{
			currentPos++;
			matchingCounter++;
		}
		else
		{
			startPos++;
			currentPos=startPos;
			matchingCounter=0;
		}

        if (matchingCounter == patternLength)
        {
			//when a pattern is found, push the result to the list
            pushToList(&foundAtList, startPos);
			printf("process %d found a result at position %d\n", worldRank, startPos+startIndex);
			numPatternsFound++;
            startPos++;
            matchingCounter=0;
            currentPos=startPos;
        }
	}
	return foundAtList;
}

/*
This method is very similar to the original hostMatch, except each process searches only their
allocated portion of the fill text. This method is only used when the search type is 
a '0' ie. When finding if a pattern exists in the text.
*/
int hostMatchFindExists()
{
    int startPos,matchingCounter,currentPos, lastI, isFound;
	
	startPos=0;
	matchingCounter=0;
	currentPos=0;
	lastI = textLength-patternLength;
	isFound = 0;
    
	if (worldRank == 0)
	{ //make sure master process only searches the first portion of the text
		//printf("process 0 has started searching\n");
		lastI = (textLength / worldSize) + patternLength;
		if(lastI > textLength)
		{
			lastI = textLength;
		}
	}

	while (startPos<=lastI && matchingCounter<patternLength)
	{
		if (textData[currentPos] == patternData[matchingCounter])
		{
			currentPos++;
			matchingCounter++;
		}
		else
		{
			startPos++;
			currentPos=startPos;
			matchingCounter=0;
		}
	}

	if (matchingCounter == patternLength)
		return -2;
	else
		return -1;
}

/*
Checks to see if either of the files are empty, or if the text file is shorter than the pattern file
In all of these cases the pattern will never be found, so the program will skip the search
*/
bool checkForEmptyFiles() 
{
    if (textLength == 0 || patternLength == 0 || textLength < patternLength)
    { //pattern will not be found if text or pattern file is empty, or if the pattern is longer than the text
		if(worldRank == 0) 
		{ //master prints that search is being skipped
			printf("Search skipped due to an empty file, or text file is shorter than pattern file\n");
		}
        return true;
    }
    return false;
}

/*
This method will create a linked list to store all of the occurances that this process finds
then it will call the hostMatchFindAll method which will populate the linked list with the results
*/
linkedList_* processDataFindAll()
{
	linkedList_* allOccurances = (linkedList_*) realloc(allOccurances, sizeof(linkedList_));
	allOccurances->index = -1;
	allOccurances->next = NULL;
    if (checkForEmptyFiles())
    { //skip the search if there is an empty file of if the text is shorter than the pattern
        return allOccurances;
    }

    allOccurances = hostMatchFindAll();

	return allOccurances;
}

/*
This method will return -1 if a pattern does not exist in the text and -2 if it does.
*/
int processDataFindExists()
{
    if (checkForEmptyFiles())
    { //skip the search if there is an empty file of if the text is shorter than the pattern
        return -1;
    }

    //return -2 if pattern is found or -1 if pattern is not found
    return hostMatchFindExists();
}

/*
This method will divide the text into portions using the master process and send 
the allocated portion to each of the sub-processes. The sub-processes will recieve 
its allocated portion of text and the full pattern to search. The master process also
sends some meta data that is relevant to each search, such as the type of search to be
performed, whether one or both of the files are empty, and the start index of where the 
sub-process is starting its search in the main textData array.
*/
void partitionTextData()
{
	 if (worldRank == 0) //master process will create portions of text and send them to the other processes
	{
		readData(); //read the pattern and text file and print their lengths
        printf ("Text length = %d\n", textLength);
        printf ("Pattern length = %d\n", patternLength);
		int zero = 0;
		int one = 1;
		bool existsEmptyFile = checkForEmptyFiles(); //
		for (int i = 1; i < worldSize; i++) 
        { //work out start and end index for each process
		
			if(existsEmptyFile) 
			{ //if one of the files are empty, or if the text file is shorter than the pattern file 
				//then the master procress sends 
				MPI_Send(&one, 1, MPI_INT, i, 900, MPI_COMM_WORLD); //900 tag corresponds to the empty file tracker, it will send 1 here as existsEmptyFile is true
				MPI_Send(&typeOfRead, 1, MPI_CHAR, i, 600, MPI_COMM_WORLD); //600 tag corresponds to the type of search, 0 if checking if file exits, 1 if looking for all occurances
				MPI_Send(&zero, 1, MPI_INT, i, 100, MPI_COMM_WORLD); //sends 0 as both text length and pattern length if existsEmptyFile is true
				MPI_Send(&zero, 1, MPI_INT, i, 100, MPI_COMM_WORLD);
			} else 
			{
				MPI_Send(&zero, 1, MPI_INT, i, 900, MPI_COMM_WORLD); //900 tag corresponds to the empty file tracker, it will send 0 here as existsEmptyFile is false
				//calculate the portion size for each process
				int jump = textLength / worldSize;
				int startIndex = jump * i;
				int endIndex = startIndex + jump + patternLength;

				if (i == worldSize - 1 || endIndex > textLength)
				{ //set end index to the text length for the last process, or if the endIndex exceeds the text length
					endIndex = textLength;
				}

				sendPortionOfText(i, startIndex, endIndex); //send portion of text to process i
				sendPattern(i); //send pattern to process i
				MPI_Send(&startIndex, 1, MPI_INT, i, 500, MPI_COMM_WORLD); //send start index to process i
				MPI_Send(&typeOfRead, 1, MPI_CHAR, i, 600, MPI_COMM_WORLD); //600 tag corresponds to the type of search, 0 if checking if file exits, 1 if looking for all occurances
			}
			
		}	
	} else
	{//this else will be executed by all processes that are not the master
		int existsEmptyFile;
		MPI_Recv(&existsEmptyFile, 1, MPI_INT, 0, 900, MPI_COMM_WORLD, &status); //recieve either a 1 or 0, depending on if existsEmptyFile is true or false in the master process
		if (existsEmptyFile == 0) //if there are no empty files
		{
			MPI_Recv(&textLength, 1, MPI_INT, 0, 100, MPI_COMM_WORLD, &status); //receive length of portion from master process
			textData = (char *)realloc(textData, sizeof(char) * textLength); //allocate memory for portion of text data 
			MPI_Recv(textData, textLength, MPI_CHAR, 0, 200, MPI_COMM_WORLD, &status); //receive portion of text from master process
			MPI_Recv(&patternLength, 1, MPI_INT, 0, 100, MPI_COMM_WORLD, &status); //receive length of pattern from master process
			patternData = (char *)realloc(patternData, sizeof(char) * patternLength); //allocate memory for the pattern
			MPI_Recv(patternData, patternLength, MPI_CHAR, 0, 200, MPI_COMM_WORLD, &status); //receive pattern from master process
			MPI_Recv(&startIndex, 1, MPI_INT, 0, 500, MPI_COMM_WORLD, &status); //receive start index from master process
			MPI_Recv(&typeOfRead, 1, MPI_CHAR, 0, 600, MPI_COMM_WORLD, &status); //receive type of search to be executed
		}
		else //else there are empty files
		{
			MPI_Recv(&typeOfRead, 1, MPI_CHAR, 0, 600, MPI_COMM_WORLD, &status); //receive type of search to be executed
			MPI_Recv(&textLength, 1, MPI_INT, 0, 100, MPI_COMM_WORLD, &status); //receive 0 so that search does not go ahead
			MPI_Recv(&patternLength, 1, MPI_INT, 0, 100, MPI_COMM_WORLD, &status); //receive 0 so that search does not go ahead
		}
		
	}
}

/*
This method will partition the data among the processes and then carry out the corresponding search
*/
void doSearch() 
{
    partitionTextData();

    if (typeOfRead == '0') 
    {
        exists = processDataFindExists();
    } else if (typeOfRead == '1')
    {
        foundAt = processDataFindAll();
    } 
}


/*
This method will reduce the results back to the master process so it can print the results to the output file
*/
void reduceResults()
{
	if (typeOfRead == '0') 
    { //if checking if a pattern exists in the text file, reduce the results to the minimum value found, which will be either -2 or -1
		MPI_Reduce(&exists, &combinedResult, 1, MPI_INT, MPI_MIN, 0, MPI_COMM_WORLD); 
		if (worldRank == 0)
		{
			if(combinedResult == -1) 
			{
				printf("Pattern not found\n");
			} else {
				printf("Pattern found\n");
			}
		}
    } else if (typeOfRead == '1')
    { //if searching for all occurances of a pattern, reduce the results to a single linked list datastructure in the master process
        allResults = (linkedList_*) malloc(sizeof(linkedList_));
		allResults->index = -1;
		allResults->next = NULL;
		printf("process %d found %d patterns\n", worldRank, numPatternsFound);
		if (worldRank == 0)
		{
			while(foundAt->index != -1) 
			{ //push the masters results to the linked list
				pushToList(&allResults, foundAt->index);
				foundAt = foundAt->next;
			}
			for (int i = 1; i < worldSize; i++) 
			{ //iterate through all other processes and and add their results to the linked list
				int numResults = 0;
				MPI_Recv(&numResults, 1, MPI_INT, i, 50, MPI_COMM_WORLD, &status);
				for (int k = 0; k < numResults; k++) {
					int resultPos;
					MPI_Recv(&resultPos, 1, MPI_INT, i, 60, MPI_COMM_WORLD, &status);
					pushToList(&allResults, resultPos);
				}
			}
		} else 
		{ //sub-processes will send their results to the master process
			MPI_Send(&numPatternsFound, 1, MPI_INT, 0, 50, MPI_COMM_WORLD);
			for (int i = 0; i < numPatternsFound; i++) {
				int actualRes = foundAt->index + startIndex;
				MPI_Send(&actualRes, 1, MPI_INT, 0, 60, MPI_COMM_WORLD);
				foundAt = foundAt->next;
			}
		}
    } 
}

/*
This process will print the results of a single search to the output file. 
*/
void printResultsToFile() 
{
	if (worldRank == 0) 
	{ //only the master process prints the results
		if (typeOfRead == '0') 
    	{ //only one line is needed when checking if the file exists
			insertLineInFile(combinedResult);
		} else 
		{ //if search is finding all appearances of the pattern, then iterate through the linked
		  //list that is storing all the results and print each result to the output file.
			removeDuplicates(allResults);

			if(allResults->index == -1) 
			{
				insertLineInFile(allResults->index);
			}
			while (allResults->index != -1) 
			{
				insertLineInFile(allResults->index);
				allResults = allResults->next;
			}
			free(allResults);
		}
	}
	
}

/*
This method simply prints an entire file, it is used to print the control file to console
*/
void printFile(char *fileData, int fileLength) 
{
     for (int i = 0; i < fileLength; i++) {
        printf("%c", fileData[i]);
    }
    printf("\n\n");
}

/*
This method reads in the control file and prints it to the console.
The control file data is stored in the global variable controlData
*/
void readControlFile()
{
    //Initialise datastructures to hold information about control file
    FILE *f;
    char fileName[1000];

    //Specify control file path and open the file
    sprintf (fileName, "large_inputs/control.txt");
    f = fopen (fileName, "r");

    //If file doesnt exist or cant be opened, let the user know
	if (f == NULL)
		 printf("Unable to open control file");

    //read in the data from the control file
	readFromFile (f, &controlData, &controlLength);
    
    //Print the control file out to console
    printf("Control Length = %d\n", controlLength);
    printf("=====Control File=====\n");
    printFile(controlData, controlLength);
    printf("======================\n");
}

/*
This method will read the control file token by token and determine what type of search needs to be done, and what text and pattern to use
*/
void determineSearchType()
{
    char * controlToken;
    //The counter keeps track of what type of token we are reading, the tokens alternate between a type of search, what text file to use, and what pattern file to use
    int counter = 0;

	if (!controlRead)
	{ //The first time a token is read, the array (controlData) needs to be specified, all subsequent times strtok is called with NULL
 		//strtok splits the control data into tokens, separated by a space, new line, tab or return value
    	controlToken = strtok (controlData," \n\t\r");
		controlRead = true;
	} else {
		controlToken = strtok (NULL, " \n\t\r");
	}
   

    //iterate through the control file token by token
    while (counter != 3 && controlToken != NULL)
    {
        if (counter % 3 == 0) 
        {//The first token in a line is always the type of search to be done, it will always be either a '0' or '1'
            typeOfRead = controlToken[0];
            if(typeOfRead == '0') {
                printf ("\nFind if the pattern occurs ");
            } else if (typeOfRead == '1') {
                printf ("\nFind every position of pattern ");
            }
        } else if (counter % 3 == 1) 
        { //The second token specifies the text number to use 
			controlToken = strtok (NULL, " \n\t\r");
            textNumber = controlToken;
            printf ("using text file %s ", textNumber);
        } else if (counter % 3 ==2)
        { //the third token specifies the pattern number to use
			controlToken = strtok (NULL, " \n\t\r");
            patternNumber = controlToken;
            printf ("and pattern file %s\n", patternNumber);
        }
        //increment token counter
        counter ++;
    }

	if (controlToken == NULL)
	{ //if the control token is NULL, then the entire control file has been read, which means all searches have been completed
		allSearchesDone = 1;
	}
}

/*
Main method will initilise MPI environment and perform searches until all searches are completed.
*/
int main(int argc, char **argv)
{
    //initialize the MPI environment
	MPI_Init(NULL, NULL);

	//find out rank for each process and world size 
	MPI_Comm_rank(MPI_COMM_WORLD, &worldRank);
	MPI_Comm_size(MPI_COMM_WORLD, &worldSize);


	//master process will generate the output file, read in the control file, and determine what the first search is
	if (worldRank == 0)
	{ 
 		generateOutputFile();
		readControlFile();
		determineSearchType();
	}
   
    
	while (allSearchesDone != 1) 
	{ //this loop will perform searches until allSearchesDone is set to 1
		numPatternsFound = 0;
		doSearch();
		reduceResults();
		printResultsToFile();
		if (worldRank == 0 && controlRead) {
			free(textData);
			free(patternData);
			determineSearchType();
		}
		//Broadcast barrier will allow all processes to wait on the master to determine if another search needs to be performed
		MPI_Bcast(&allSearchesDone, 1, MPI_INT, 0, MPI_COMM_WORLD);
	}

	//master process closes the output file once all results have been written to it
	if (worldRank == 0)
	{
    	fclose(outputFile);
	}

	//finialise MPI and finish program
	printf("Process %d has terminated successfully\n", worldRank);
	MPI_Finalize(); 
    return 0;
}
