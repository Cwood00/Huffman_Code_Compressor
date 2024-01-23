#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>
#include<string.h>
#include<fcntl.h>
#include<stdbool.h>
#include<limits.h>
#include<stdint.h>

#define BUFFER_SIZE 2048
#define maxBytesPerEncoding ((UCHAR_MAX + 1) / 8)

//Node for forming the encoding tree. 
//Weights are used to minimize encoiding length of for common byte patters
struct weightedTreeNode
{
	unsigned long weight;
	unsigned char byte;
	bool isLeaf;
	struct weightedTreeNode* zeroTransition;
	struct weightedTreeNode* oneTransition;
};

//Node for the decoding tree
struct unweightedTreeNode
{
	unsigned char byte;
	bool isLeaf;
	struct unweightedTreeNode* zeroTransition;
	struct unweightedTreeNode* oneTransition;
};

//The encoding for a single char
//Length is messured in bits
struct charEncoding
{
	int length;
	unsigned char encoding[maxBytesPerEncoding];
};

//Wrapper function for open, that does error checking
int Open(char* fileName, int flags, mode_t mode)
{
	int fileDescriptor = open(fileName, flags, mode);

	if(fileDescriptor == -1)
	{
		printf("Could not open %s\nexiting...\n", fileName);
		exit(1);
	}

	return fileDescriptor;
}

//Wraper function for write that ensures all bytes are writen
int Write(int fileDescriptor, void* buffer, int size)
{
	int bytesWriten = 0;
	while (bytesWriten < size)
	{
		bytesWriten += write(fileDescriptor, buffer + bytesWriten, size - bytesWriten);
		if(bytesWriten == -1)
		{
			printf("Error wirting data\n");
			exit(1);
		}
	}
	return bytesWriten;
}

void swapptr(void** firstPtr, void** secondPtr)
{
	void* temp = *firstPtr;
	*firstPtr = *secondPtr;
	*secondPtr = temp;
}

void printBadUseError()
{
	printf("Invalid comand line arguments\n");
	printf("First argument must be -e to encode or -d to decode\n");
	printf("Second and thrid areguemnt must be source and destiation files respectively\n");
}

//Enqueues a weightedTreeNode pointer in to a min heep based priority queue
//Assumes there is room for the new node
void enqueue(struct weightedTreeNode* queue[], int* queueLength, struct weightedTreeNode* node)
{
	int newNodeIndex = *queueLength;
	int parentIndex = (newNodeIndex - 1) / 2;
	queue[newNodeIndex] = node;

	//Moves the node into the correct possition
	while(newNodeIndex > 0 && queue[newNodeIndex]->weight < queue[parentIndex]->weight)
	{
		swapptr((void**)&queue[newNodeIndex], (void**)&queue[parentIndex]);
		newNodeIndex = parentIndex;
		parentIndex = (newNodeIndex - 1) / 2;
	}

	*queueLength += 1;
}

//Dequeus a weightedTreeNode pointer from a min heep based priority queue
struct weightedTreeNode* dequeue(struct weightedTreeNode* queue[], int *queueLength)
{
	struct weightedTreeNode* retVal = queue[0];

	int rootNodeIndex = 0;
	int leftChildIndex = 1;
	int rightchildIndex = 2;
	int smallestChildIndex;
	//Moves last value to front of queue
	queue[0] = queue[*queueLength - 1];
	queue[*queueLength - 1] = NULL;

	*queueLength -= 1;
	//Find the smallest child
	if(rightchildIndex < *queueLength && queue[rightchildIndex]->weight < queue[leftChildIndex]->weight)
	{
		smallestChildIndex = rightchildIndex;
	}
	else if(leftChildIndex < *queueLength)
	{
		smallestChildIndex = leftChildIndex;
	}
	else
	{
		smallestChildIndex = -1;
	}
	//Swaps node in front of queue back, until it is in correct position
	while(smallestChildIndex != -1 && queue[smallestChildIndex]->weight < queue[rootNodeIndex]->weight)
	{
		swapptr((void**)&queue[smallestChildIndex], (void**)&queue[rootNodeIndex]);

		rootNodeIndex = smallestChildIndex;
		leftChildIndex = (rootNodeIndex * 2) + 1;
		rightchildIndex = (rootNodeIndex * 2) + 2;
		//Find the smallest child
		if(rightchildIndex < *queueLength && queue[rightchildIndex]->weight < queue[leftChildIndex]->weight)
		{
			smallestChildIndex = rightchildIndex;
		}
		else if(leftChildIndex < *queueLength)
		{
			smallestChildIndex = leftChildIndex;
		}
		else
		{
			smallestChildIndex = -1;
		}
	}

	return retVal;
}

//Rescursive helper function for populateEncodingTable
void _populateEncodingTable(struct charEncoding encodingTable[], struct weightedTreeNode* node, struct charEncoding encoding)
{
	if(node->isLeaf)
	{
		encodingTable[node->byte] = encoding;
	}
	else
	{
		unsigned char changedByte = encoding.length / 8;
		unsigned char changedBit = 1 << (encoding.length  % 8);

		encoding.length += 1;

		encoding.encoding[changedByte] &= ~changedBit;
		_populateEncodingTable(encodingTable, node->zeroTransition, encoding);

		encoding.encoding[changedByte] |= changedBit;
		_populateEncodingTable(encodingTable, node->oneTransition, encoding);
	}
}

//Uses encoding tree to populate array of encodings
void populateEncodingTable(struct charEncoding encodingTable[], struct weightedTreeNode* root)
{
	struct charEncoding encoding;
	memset(&encoding, 0, sizeof(encoding));
	_populateEncodingTable(encodingTable, root, encoding);
}

//Recusive helper function for writeTreeToFile
unsigned char* _writeTreeToFile(unsigned char* writeLocation, struct weightedTreeNode* node)
{
	if(node->isLeaf)
	{
		*writeLocation = true;
		writeLocation++;
		*writeLocation = node->byte;
	}
	else
	{
		*writeLocation = false;
		writeLocation = _writeTreeToFile(writeLocation + 1, node->zeroTransition);
		writeLocation = _writeTreeToFile(writeLocation + 1, node->oneTransition);
	}

	return writeLocation;
}

//Writes data needed to recrate encoding tree to file, by doing an preorder traversal of the tree, writing a
//byte of 0, if the node is not a leaf, and writing a 1 byte, followed by the byte encoded if the node is a leaf
void writeTreeToFile(int fileDescriptor, struct weightedTreeNode* root, int numLeafs)
{
	unsigned char buffer[numLeafs * 3 - 1];
	memset(buffer, 0, sizeof(buffer));

	_writeTreeToFile(buffer, root);

	Write(fileDescriptor, buffer, sizeof(buffer));
}

void freeWeightedTree(struct weightedTreeNode* root)
{
	if(!root->isLeaf)
	{
		freeWeightedTree(root->zeroTransition);
		freeWeightedTree(root->oneTransition);
	}
	free(root);
}

//Encodes source file into destination file
void encode(char* sourceFileName, char* destinationFileName)
{
	printf("Encoding %s into %s\n", sourceFileName, destinationFileName);

	int sourceFile = Open(sourceFileName, O_RDONLY, 0);

	unsigned long byteCounts[UCHAR_MAX + 1];
	memset(byteCounts, 0, sizeof(byteCounts));

	unsigned char readBuffer[BUFFER_SIZE];
	int bytesRead = 0;

	//Count the number of times each byte appers in the source file
	do{
		bytesRead = read(sourceFile, readBuffer, BUFFER_SIZE);
		for(int i = 0; i < bytesRead; i++)
		{
			byteCounts[readBuffer[i]]++;
		}
	} while(bytesRead == BUFFER_SIZE);

	close(sourceFile);

	//Prioroity queue, that uses a min heap data structure
	int queueLength = 0;
	struct weightedTreeNode* nodeQueue[UCHAR_MAX + 1];
	memset(nodeQueue, 0, sizeof(nodeQueue));

	//Place leaf nodes for the Huffman tree into the priority queue
	for(int i = 0; i <= UCHAR_MAX; i++)
	{
		if(byteCounts[i] > 0)
		{	
			struct weightedTreeNode* newNode = (struct weightedTreeNode*)malloc(sizeof(struct weightedTreeNode));
			memset(newNode, 0, sizeof(struct weightedTreeNode));
			newNode->byte = (unsigned char)i;
			newNode->weight = byteCounts[i];
			newNode->isLeaf = true;

			enqueue(nodeQueue, &queueLength, newNode);
		}
	}
	
	//Form the huffman tree
	int numLeafs = queueLength;
	while(queueLength > 1)
	{
		struct weightedTreeNode* lightestNode = dequeue(nodeQueue, &queueLength);
		struct weightedTreeNode* secondLightestNode = dequeue(nodeQueue, &queueLength);

		struct weightedTreeNode* newNode = (struct weightedTreeNode*)malloc(sizeof(struct weightedTreeNode));
		memset(newNode, 0, sizeof(struct weightedTreeNode));

		newNode->weight = lightestNode->weight + secondLightestNode->weight;
		newNode->zeroTransition = lightestNode;
		newNode->oneTransition = secondLightestNode;

		enqueue(nodeQueue, &queueLength, newNode);
	}

	struct weightedTreeNode* root = nodeQueue[0];

	//Array that contains the encoding for all byte patters in the original file
	//Index is byte pattern represented
	struct charEncoding encodingTable[UCHAR_MAX + 1];
	memset(encodingTable, 0, sizeof(encodingTable));

	populateEncodingTable(encodingTable, root);

	int destiationFile = Open(destinationFileName, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	writeTreeToFile(destiationFile, root, numLeafs);

	//Saves the number of bytes in the original file
	Write(destiationFile, &(root->weight), sizeof(root->weight));

	sourceFile = Open(sourceFileName, O_RDONLY, 0);
	unsigned char writeBuffer[BUFFER_SIZE];
	
	int writeIndex = 0;
	unsigned char writeBit = 1;

	//Encodes the file, and writes the encoded data to the destination file
	do{
		bytesRead = read(sourceFile, readBuffer, BUFFER_SIZE);
		for(int readIndex = 0; readIndex < BUFFER_SIZE; readIndex++)
		{
			struct charEncoding encoding = encodingTable[readBuffer[readIndex]];
			for(int i = 0; i < encoding.length; i++)
			{
				unsigned char encodingByte = i / 8;
				unsigned char encodingBit = 1 << (i % 8);
				if(encoding.encoding[encodingByte] & encodingBit)
				{
					writeBuffer[writeIndex] |= writeBit;
				}
				else
				{
					writeBuffer[writeIndex] &= ~writeBit;
				}

				if(writeBit != 1 << 7)
				{
					writeBit = writeBit << 1;
				}
				else
				{
					writeBit = 1;
					writeIndex += 1;
					if(writeIndex == BUFFER_SIZE)
					{
						writeIndex = 0;
						Write(destiationFile, writeBuffer, BUFFER_SIZE);
					}
				}
			}
		}

	}while(bytesRead == BUFFER_SIZE);
	Write(destiationFile, writeBuffer, writeIndex);

	close(sourceFile);
	close(destiationFile);
	freeWeightedTree(root);
}

//Recusive function that reads the encoding tree from the file, in the format that
//used by write tree to file
struct unweightedTreeNode* readTreeFromFile(int sourceFile)
{
	struct unweightedTreeNode* newNode = malloc(sizeof(struct unweightedTreeNode));
	memset(newNode, 0, sizeof(struct unweightedTreeNode));

	char byteRead;
	read(sourceFile, &(byteRead), 1);
	newNode->isLeaf = byteRead;

	if(byteRead == 1)
	{
		read(sourceFile, &(newNode->byte), 1);
	}
	else if(byteRead == 0)
	{
		newNode->zeroTransition = readTreeFromFile(sourceFile);
		newNode->oneTransition = readTreeFromFile(sourceFile);
	}
	else
	{
		printf("Bad source file\n");
		exit(1);
	}

	return newNode;
}

void freeUnweightedTree(struct unweightedTreeNode* root)
{
	if(!root->isLeaf)
	{
		freeUnweightedTree(root->zeroTransition);
		freeUnweightedTree(root->oneTransition);
	}
	free(root);
}

//Decodes encoded sourse file into destination file
void decode(char* sourceFileName, char* destinationFileName)
{
	printf("Decoding %s into %s\n", sourceFileName, destinationFileName);

	int sourceFile = Open(sourceFileName, O_RDONLY, 0);

	//Reads the encoding tree and the orignal non-encoded file length from the source file
	struct unweightedTreeNode* root = readTreeFromFile(sourceFile);
	unsigned long originalFileLength;
	read(sourceFile, &originalFileLength, sizeof(originalFileLength));

	int destiationFile = Open(destinationFileName, O_WRONLY | O_CREAT | O_TRUNC, 0644);

	unsigned char readBuffer[BUFFER_SIZE];
	unsigned char writeBuffer[BUFFER_SIZE];
	
	unsigned long totalBytesWriten = 0;
	int writeIndex = 0;
	int readByte = 0;
	unsigned char readBit = 1;

	//Decodes the file one byte per iteration of the loop
	read(sourceFile, readBuffer, BUFFER_SIZE);
	do{
		struct unweightedTreeNode* currentNode = root;
		while(!currentNode->isLeaf)
		{
			//Travers the encoding tree to find what byte is encoded
			if(readBuffer[readByte] & readBit)
			{
				currentNode = currentNode->oneTransition;
			}
			else
			{
				currentNode = currentNode->zeroTransition;
			}

			if(readBit != 1 << 7)
			{
				readBit = readBit << 1;
			}
			else
			{
				readBit = 1;
				readByte += 1;
				if(readByte == BUFFER_SIZE)
				{
					readByte = 0;
					read(sourceFile, readBuffer, BUFFER_SIZE);
				}
			}
		}
		writeBuffer[writeIndex] = currentNode->byte;
		writeIndex += 1;
		totalBytesWriten += 1;
		if(writeIndex == BUFFER_SIZE)
		{
			Write(destiationFile, writeBuffer, BUFFER_SIZE);
			writeIndex = 0;
		}

	}while(totalBytesWriten < originalFileLength);
	Write(destiationFile, writeBuffer, writeIndex);

	close(sourceFile);
	close(destiationFile);
	freeUnweightedTree(root);
}

int main(int argc, char* argv[])
{

	if(argc != 4)
	{
		printBadUseError();
		return 1;
	}

	if(!strcmp(argv[1], "-e"))
	{
		encode(argv[2], argv[3]);
	}
	else if(!strcmp(argv[1], "-d"))
	{
		decode(argv[2], argv[3]);
	}
	else
	{
		printBadUseError();
		return 1;
	}

	return 0;
}