
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#include <omp.h>
#include <execinfo.h>
#include <stdbool.h>

////////////////////////////////////////////////////////////////////////////////
// OMP PROJECT - SHEA KITSON - 40202515
////////////////////////////////////////////////////////////////////////////////

/*
STATEGIES TO OPTIMISE PERFORMANCE:
I used a static parallel for loop schedule with no defined chunk size. This way the iterations are evenly divided (if possible) among the threads.
Not defining a chunk size allows the scheduler to more freely adapt to the large changes in the sizes of the text file to be searched.
For example, a chunk size of 10,000 would be no good for a text file with a size of 10. 

When checking if a pattern exists in the text, if any threads find the pattern they will update the shared 'isFound' variable.
Subsequent searches on different threads will see this and know that the pattern is already found, so they can skip the search.
This acts as an early stopping mechanism and is very effective if the pattern appears at the start of a segment that is being searched,
as it means all the other threads do not have to pointlessly search through their entire allocated portion of teh text.

If either the text or pattern file is empty, or if the text file is shorter than the pattern file, then the search is skipped as it 
is impossible for the pattern to be found when any of these things are true. When this happens I report it as pattern not found.

The linked list structure that is used to hold all occurances of a pattern will dynamically allocate its memory each time a new element 
is added to the list. Although this does not directly affect how fast the program executes, it means I am not statically allocating more
memory than necessary to hold the results.
*/

int num_threads = 4; //set number of threads

char *textData;
int textLength;

char *patternData;
int patternLength;

char *controlData; //stores the data from the control file
int controlLength; //stores length of control file

char typeOfRead; //stores the type of search to be done, i.e. '0' to find if pattern exists or '1' to find all occurances of pattern
char* textNumber; //stores text number to be searched
char* patternNumber; //stores pattern number that the program is searching for

FILE *outputFile; //File to output results

bool controlRead = false; //indicates if the control file read has been started or not
int allSearchesDone = 0; //indicates if all of the searches are complete, changes to 1 once they are all complete

typedef struct linkedList 
{  //define linked list structure which is used to store all positions that pattern is found
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

void generateOutputFile() 
{ //This method will generate the output file to store results
	char fileName[1000];
	sprintf (fileName, "result_OMP.txt");
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

//standard method taken from searching_sequential.c from Blesson
void outOfMemory()
{
	fprintf (stderr, "Out of memory\n");
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
				outOfMemory();
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
	if (f == NULL)
		return 0;
	readFromFile (f, &textData, &textLength);
	fclose (f);
#ifdef DOS
        sprintf (fileName, "inputs\\test%d\\pattern.txt", testNumber);
#else
	sprintf (fileName, "large_inputs/pattern%s.txt", patternNumber);
#endif
	f = fopen (fileName, "r");
	if (f == NULL)
		return 0;
	readFromFile (f, &patternData, &patternLength);
	fclose (f);

	return 1;

}

/*
This method is a modified version of the original hostMatch, it will store
all positions at which a pattern is found in a linked list, accessed via a 
critical section in the parallel for loop. This method is only used when the 
search type is a '1' ie. When all occurances of a pattern need to be found.
*/
linkedList_* hostMatchFindAll()
{
	//Declare variables that are needed for parallel execution
	int startPos,matchingCounter,currentPos, endPos, counter;
	bool isMatching;
	linkedList_* foundAtList;
	foundAtList = (linkedList_*) malloc(sizeof(linkedList_)); //allocate memory for linked list object
    foundAtList->next = NULL; //set linked list initial values
    foundAtList->index = -1;
	
	//Initialise all shared and firstprivate variables 
	matchingCounter=0;
	endPos = textLength-patternLength;
	isMatching = false;

	//Parallel openmp for loop 
	#pragma omp parallel for shared(foundAtList) firstprivate(endPos,patternLength, textData, patternData, isMatching, matchingCounter) private(startPos, currentPos) num_threads(num_threads) schedule(guided)
	for(startPos = 0; startPos<=endPos; startPos++)
	{
		if (textData[startPos] == patternData[0])
		{ //if the start position of this iteration finds a match for the first letter in the pattern 
			isMatching = true; 
			currentPos = startPos;
		}
		while (isMatching && matchingCounter < patternLength) 
		{ //while the text is matching the pattern, this while loop will iterate through the text until it finds a full pattern match
			if (textData[currentPos] == patternData[matchingCounter])
			{//checks if the text is still matching the pattern
				currentPos++;
				matchingCounter++;
				if (matchingCounter == patternLength) 
				{ //If a full pattern match is found the then the startPos is pushed to the linked list 
					#pragma omp critical (found)
					{//Needs to be in ciritical section as multiple occurances of the pattern may appear in the text
						pushToList(&foundAtList, startPos); //pushes the most recently found pattern position to the linked list
					}
					isMatching = false;
					matchingCounter = 0;
				}
			}
			else
			{ //The text is no longer matching the pattern, so the relevant variables are reset to 0/false
				currentPos=0;
				matchingCounter=0;
				isMatching = false;
			}
		}
	}
	return foundAtList;
}

/*
This method creates a parallel for loop which searches for a pattern, it will return -2
if the pattern exits in the text and -1 if it does now. This method is only used when the 
search type is '0' ie. When finding if a pattern exists in the text.
*/
int hostMatchFindExists()
{
    //Declare variables that are needed for parallel execution
	int startPos,matchingCounter,currentPos, endPos, isFound, counter;
	bool isMatching;
	
	//Initialise all shared and firstprivate variables 
	matchingCounter=0;
	isFound = -1;
	endPos = textLength-patternLength;
	isMatching = false;

	//Parallel openmp for loop 
	#pragma omp parallel for shared(isFound) firstprivate(endPos,patternLength, textData, patternData, isMatching, matchingCounter) private(startPos, currentPos) num_threads(num_threads) schedule(static)
	for(startPos = 0; startPos<=endPos; startPos++)
	{
		if (isFound == -1 && textData[startPos] == patternData[0])
		{ //if the start position of this iteration finds a match for the first letter in the pattern 
			isMatching = true; 
			currentPos = startPos;
		}
		while (isFound == -1  && isMatching && matchingCounter < patternLength) 
		{ //while the text is matching the pattern this while loop will iterate through the text until it finds a full pattern match or 
			if (textData[currentPos] == patternData[matchingCounter])
			{//checks if the text is still matching the pattern
				currentPos++;
				matchingCounter++;
				if (matchingCounter == patternLength) 
				{ //If a full pattern match is found the isFound shared variable is set to -2
                    #pragma omp critical (found)
		            { 
                        isFound = -2;
		            }
					isMatching = false;
					matchingCounter = 0;
				}
			}
			else
			{ //The text is no longer matching the pattern, so the relevant variables are reset to 0/false
				currentPos=0;
				matchingCounter=0;
				isMatching = false;
			}
		}
	}
	return isFound;
}


void processData()
{
	unsigned int result;

	printf ("Text length = %d\n", textLength);
    printf ("Pattern length = %d\n", patternLength);

	//checks to see if either of the files are empty, or if the pattern is longer than the text
	//if any of these are true, the search is not executed and instead -1 is written to the file as the result
    if (textLength == 0 || patternLength == 0 || textLength < patternLength)
    {
        result = -1;
        insertLineInFile(result);
		printf("Search skipped due to an empty file, or text file is shorter than pattern file\n");
        return;
    }

    if(typeOfRead == '0') 
    { //if checking if the pattern exists in the file
        result = hostMatchFindExists(); //do the search
		//report the results and write them to the output file
        if (result == -1)
        {
            printf ("Pattern not found\n");
            insertLineInFile(result);
        }
	    else
        {
            printf ("Pattern found \n");
            insertLineInFile(result);
        }
    } else if (typeOfRead == '1')
    { //if finding all occurrances of a pattern in the text
        linkedList_* allOccurances = hostMatchFindAll();
        if (allOccurances->index == -1)
        { //if no pattern was found then the index will still be -1 from the start
		    printf ("Pattern not found\n");
            insertLineInFile(allOccurances->index);
        } else {
            printf ("Pattern found at indexes:\n");
        }
        
        int counter = 0;
        while (allOccurances->index != -1) 
        { //iterate through the linked list and print the positions of all of the indexes
            counter++;
            printf("%d, ", allOccurances->index);
            insertLineInFile(allOccurances->index);
            allOccurances = allOccurances->next;
        }
    
        printf ("# of patterns found = %d\n", counter);
    }
}

void doSearch() 
{
    readData();
   	processData();
}

/*
This method simply prints an entire file, it is used to print the control file to console
*/
void printFile(char *fileData, int fileLength) 
{
     int i;
     for (i = 0; i < fileLength; i++) {
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
	{//if the control token is NULL, then the entire control file has been read, which means all searches have been completed
		allSearchesDone = 1;
	}
}

int main(int argc, char **argv)
{
	//set up the environment by generating the output file, reading the control file and determining the first search to be done
    generateOutputFile();	
    readControlFile();
	determineSearchType();

	while (allSearchesDone != 1) 
	{ //continue performing searches until all searches specified in the control file are complete
		doSearch();
		free(textData);
		free(patternData);
		determineSearchType();
	}

	//close the output file and terminate the program
    fclose(outputFile);
    return 0;
}
