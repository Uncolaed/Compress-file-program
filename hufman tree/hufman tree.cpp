#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>
#include <windows.h>
#include <thread>
#include <mutex>
#include <vector>
#include <queue>
#include <condition_variable>
#include <functional>
#include <future>
#include "CC212SGL.h"
#include "tinyfiledialogs.h"
#include "tinyfd_moredialogs.h"
#pragma comment(lib, "CC212SGL.lib")


#define MAX_LEVELS 2
#define FILE_SIZE 1024LL * 1024LL *102ll // 2 GB in bytes
#define BUFFER_SIZE 1024LL*1024LL // 1 MB buffer size
#define Bit_compression_size 8
#define MAX_HUFFMAN_CODE_SIZE 256 // Define the maximum size of a Huffman code



// to use fseeko and ftello on windows due to fseek and ftell only being able to take long numbers 
#ifdef _WIN32

#define fseeko _fseeki64
#define ftello _ftelli64
#else
#include <stdio.h>
#endif

// thermal pooling to limit the number of threads to the number of cores in the system
// this is to avoid the overheat of creating too many threads
//uses library from queue and condition variable to create a thread pool
class ThreadPool
{
public:
	ThreadPool(size_t numThreads)
	{
		// create threads and assign them to the vector
		for (size_t i = 0; i < numThreads; ++i)
		{
			workers.emplace_back([this]
				{
					while (true)
					{
						std::function<void()> task;
						{
							// lock the queue and wait for the condition to be true
							std::unique_lock<std::mutex> lock(queueMutex);
							condition.wait(lock, [this] { return stop || !tasks.empty(); });
							if (stop && tasks.empty()) return;
							task = std::move(tasks.front());
							tasks.pop();
						}
						task();
					}
				});
		}
	}

	// enqueue the task to the thread pool
	template<class F, class... Args>
	auto enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>
	{
		using return_type = typename std::result_of<F(Args...)>::type;

		// create a task and assign it to the thread pool

		auto task = std::make_shared<std::packaged_task<return_type()>>
			(
				std::bind(std::forward<F>(f), std::forward<Args>(args)...)
			);

		std::future<return_type> res = task->get_future();
		{
			std::unique_lock<std::mutex> lock(queueMutex);
			if (stop) throw std::runtime_error("enqueue on stopped ThreadPool");
			tasks.emplace([task]() { (*task)(); });
		}
		condition.notify_one();
		return res;
	}

	~ThreadPool()
	{
		{
			std::unique_lock<std::mutex> lock(queueMutex);
			stop = true;
		}
		condition.notify_all();
		for (std::thread& worker : workers) worker.join();
	}

private:
	std::vector<std::thread> workers;
	std::queue<std::function<void()>> tasks;
	std::mutex queueMutex;
	std::condition_variable condition;
	bool stop = false;
};



// graphics 

CC212SGL graphics;


// Loading bar structure

struct LoadingBar
{
	int x, y, width, height;
	float progress;
	const char* label;
};

// loading bars for all actions 

LoadingBar frequencyBar = { 100, 100, 400, 20, 0.0f, "Generating Frequency Table" };
LoadingBar compressionBar = { 100, 140, 400, 20, 0.0f, "Compressing File" };
LoadingBar fileGenBar = { 100, 180, 400, 20, 0.0f, "Generating Random Text File" };

// draw empty loading bars

void drawLoadingBar(LoadingBar* bar)
{
	graphics.setDrawingColor(WHITE);
	graphics.drawRectangle(bar->x, bar->y, bar->width, bar->height);
	graphics.setDrawingColor(BLUE);
	graphics.drawSolidRectangle(bar->x, bar->y, (int)(bar->width * bar->progress), bar->height);
	graphics.setDrawingColor(WHITE);
	graphics.drawText(bar->x, bar->y - 20, bar->label);

	// Display the percentage of completion
	char progressText[50];
	sprintf(progressText, "%.0f%%", bar->progress * 100);
	graphics.drawText(bar->x + bar->width + 10, bar->y, progressText);
}

// update loading bars 

void updateLoadingBar(LoadingBar* bar, float progress)
{
	bar->progress = progress;
	graphics.beginDraw();
	graphics.fillScreen(BLACK);
	drawLoadingBar(&fileGenBar);
	drawLoadingBar(&frequencyBar);
	drawLoadingBar(&compressionBar);
	graphics.endDraw();
}

// Function to show completion screen after compression

void showCompletionScreen(const char* outputFilename)
{
	graphics.beginDraw();
	graphics.fillScreen(BLACK);
	graphics.setDrawingColor(GREEN);
	graphics.setFontSizeAndBoldness(24, true);
	graphics.drawText(100, 100, "Compression complete!");
	graphics.drawText(100, 140, "Output file:");
	graphics.drawText(100, 180, outputFilename);
	graphics.endDraw();
	Sleep(10); // Display the message for 5 seconds
}

int intro;

int* level;

void loadIntro()
{
	level = new int[0];
	level[0] = graphics.loadImage("Assests\\intro.png");
}


// Function to show the welcome screen with an intro image
void showWelcomeScreen()
{
	graphics.beginDraw();
	graphics.fillScreen(BLACK);
	graphics.drawImage(level[0], 0, 0, RGB(0, 0, 0)); // Assuming intro.png is your intro image
	graphics.resizeImage(level[0], graphics.getWindowWidth(), graphics.getWindowHeight());
	graphics.endDraw();
}

// Define a structure for the Huffman code lookup table

typedef struct
{
	char* codes[256];

}HuffmanCodeTable;


//Note : this Part was made by Omar Saleh

// Structure for a node in the binary tree
struct TreeNode
{
	int frequency;
	char data;
	TreeNode* left;
	TreeNode* right;
};



// Structure for a node in the priority queue
struct PQNode
{
	TreeNode* treeNode;
	PQNode* next;
};


// Function to read the user-specified text file and generate the frequency table modified to use buffer
void generateFrequencyTable(const char* filename, int* frequencyTable, LoadingBar* bar)
{
	FILE* file = fopen(filename, "rb");
	if (file == NULL)
	{
		printf("Error opening file.\n");
		return;
	}

	fseeko(file, 0, SEEK_END);
	long long fileSize = ftello(file);
	if (fileSize == 0)
	{
		printf("The file is empty.\n");
		fclose(file);
		return;
	}
	fseeko(file, 0, SEEK_SET);

	char* buffer = (char*)malloc(BUFFER_SIZE);
	if (buffer == NULL)
	{
		printf("Memory allocation failed.\n");
		fclose(file);
		return;
	}

	long long number = BUFFER_SIZE;

	size_t bytesRead;
	long long totalSteps = (fileSize / number) + 1;  // Correct calculation of total steps
	int step = 0;
	bar->progress = 0.0f;  // Reset progress

	ThreadPool pool(std::thread::hardware_concurrency());
	std::mutex mtx;

	while ((bytesRead = fread(buffer, 1, BUFFER_SIZE, file)) > 0)
	{
		char* chunk = (char*)malloc(bytesRead);
		if (chunk == NULL)
		{
			printf("Memory allocation failed.\n");
			free(buffer);
			fclose(file);
			return;
		}
		memcpy(chunk, buffer, bytesRead);

		pool.enqueue([chunk, bytesRead, &frequencyTable, &mtx]
			{
				int localFrequencyTable[256] = { 0 };
				for (size_t i = 0; i < bytesRead; i++)
				{
					unsigned char c = chunk[i];
					localFrequencyTable[c]++;
				}
				delete[] chunk;

				std::lock_guard<std::mutex> lock(mtx);
				for (int i = 0; i < 256; i++)
				{
					frequencyTable[i] += localFrequencyTable[i];
				}
			});

		updateLoadingBar(bar, static_cast<float>(step++) / totalSteps);
	}

	free(buffer);
	fclose(file);
}




// Function to create a new priority Queue Node
PQNode* CreatePQNode(TreeNode* treeNode)
{
	PQNode* newNode = (PQNode*)malloc(sizeof(PQNode));

	if (newNode == NULL)
	{
		printf("Memory allocation failed.");
		return NULL;
	}
	newNode->treeNode = treeNode;
	newNode->next = NULL;
	return newNode;
}


// Function to insert a priority queue node into the priority queue // by mazen 
void insertPQNode(PQNode** head, PQNode* newNode)
{
	PQNode* current;
	if (*head == NULL || (*head)->treeNode->frequency >= newNode->treeNode->frequency)
	{
		newNode->next = *head;
		*head = newNode;
	}
	else
	{
		current = *head;
		while (current->next != NULL && current->next->treeNode->frequency < newNode->treeNode->frequency)
		{
			current = current->next;
		}
		newNode->next = current->next;
		current->next = newNode;
	}
}

// Function to remove the first node from the priority queue

PQNode* removePQNode(PQNode** head)
{
	PQNode* temp = *head;
	*head = (*head)->next;
	return temp;
}

// Note : this Part was made by Mazen Belal


// Function to create a new tree node

TreeNode* CreateTreeNode(int frequency, char data, TreeNode* left, TreeNode* right)
{
	TreeNode* newNode = (TreeNode*)malloc(sizeof(TreeNode));

	// allocaote first tree node if there was none
	if (newNode == NULL)
	{
		printf("Memory allocation failed.");
		return NULL;
	}
	// variables in tree node 
	newNode->frequency = frequency;
	newNode->data = data;
	newNode->left = left;
	newNode->right = right;

	return newNode;
}


// Function to build the Huffman tree
TreeNode* buildHuffmanTree(int* frequencyTable)
{
	// pointers declarations

	PQNode* head = NULL;
	TreeNode* left;
	TreeNode* right;
	TreeNode* top;

	// for graphics uses only

	void generateDot(TreeNode * root, FILE * stream);
	void createDotFile(TreeNode * root, const char* filename);

	// ensure the tree is empty
	void freeHuffmanTree(TreeNode * node);

	// Create a priority queue of tree nodes
	for (int i = 0; i < 256; i++)
	{
		if (frequencyTable[i] > 0)
		{
			// inserts new PQnode and creates a new PQnode with null left and right child 
			insertPQNode(&head, CreatePQNode(CreateTreeNode(frequencyTable[i], (char)i, NULL, NULL)));
		}
	}

	// Build the Huffman tree
	while (head->next != NULL)
	{
		// removes first new nodes in PQ
		left = removePQNode(&head)->treeNode;
		right = removePQNode(&head)->treeNode;
		// adds thier frequancy in a parent node and sets them and its childeren 
		top = CreateTreeNode(left->frequency + right->frequency, '\0', left, right);
		// then puts it back into the PQ
		insertPQNode(&head, CreatePQNode(top));
	}
	return removePQNode(&head)->treeNode;
}


// Function build a table for each character and its huffmancode
void buildHuffmanCodeTable(TreeNode* root, int arr[], int top, HuffmanCodeTable* table)
{

	if (root->left)
	{
		arr[top] = 0;
		buildHuffmanCodeTable(root->left, arr, top + 1, table);
	}
	if (root->right)
	{
		arr[top] = 1;
		buildHuffmanCodeTable(root->right, arr, top + 1, table);
	}
	if (root->left == NULL && root->right == NULL)
	{
		char* code = (char*)malloc(top + 1);
		for (int i = 0; i < top; i++)
		{
			code[i] = '0' + arr[i];
		}
		code[top] = '\0';
		table->codes[(unsigned char)root->data] = code;
	}
}


// Function to intialize the huffman code table
HuffmanCodeTable* generateHuffmanCodeTable(TreeNode* root)
{
	HuffmanCodeTable* table = (HuffmanCodeTable*)malloc(sizeof(HuffmanCodeTable));
	for (int i = 0; i < 256; i++)
	{
		table->codes[i] = NULL;
	}
	int arr[256];
	int top = 0;
	buildHuffmanCodeTable(root, arr, top, table);
	return table;
}


// save huffman tree and code table
void saveHuffmanTree(TreeNode* node, FILE* file)
{
	if (node == NULL)
	{
		fputc('0', file); // NULL marker
		return;
	}
	fputc('1', file); // Non-NULL marker
	fwrite(&node->data, sizeof(char), 1, file);
	fwrite(&node->frequency, sizeof(int), 1, file);
	saveHuffmanTree(node->left, file);
	saveHuffmanTree(node->right, file);
}

// Function to save the Huffman code table to the .dat file
void saveHuffmanTreeAndCodeTable(TreeNode* root, HuffmanCodeTable* table, const char* filename)
{
	FILE* file = fopen(filename, "wb");
	if (file == NULL)
	{
		printf("Error opening file to save Huffman tree and code table.\n");
		return;
	}

	// Save Huffman tree
	saveHuffmanTree(root, file);

	// Save Huffman code tabl
	for (int i = 0; i < 256; i++)
	{
		if (table->codes[i] != NULL)
		{
			fputc('1', file); // Non-NULL marker
			fputc((char)i, file);
			int length = strlen(table->codes[i]);
			fwrite(&length, sizeof(int), 1, file);
			fwrite(table->codes[i], sizeof(char), length, file);
		}
		else
		{
			fputc('0', file); // NULL marker
		}
	}

	fclose(file);
}

// load huffman tree and code table

TreeNode* loadHuffmanTree(FILE* file)
{
	if (fgetc(file) == '0')
	{
		return NULL;
	}
	TreeNode* node = (TreeNode*)malloc(sizeof(TreeNode));
	fread(&node->data, sizeof(char), 1, file);
	fread(&node->frequency, sizeof(int), 1, file);
	node->left = loadHuffmanTree(file);
	node->right = loadHuffmanTree(file);
	return node;
}

// Function to load the Huffman code table from the .dat file
HuffmanCodeTable* loadHuffmanCodeTable(FILE* file)
{
	HuffmanCodeTable* table = (HuffmanCodeTable*)malloc(sizeof(HuffmanCodeTable));
	for (int i = 0; i < 256; i++)
	{
		table->codes[i] = NULL;
	}

	for (int i = 0; i < 256; i++)
	{
		if (fgetc(file) == '1')
		{ // Non-NULL marker
			char index = fgetc(file);
			int length;
			fread(&length, sizeof(int), 1, file);
			table->codes[(unsigned char)index] = (char*)malloc(length + 1);
			fread(table->codes[(unsigned char)index], sizeof(char), length, file);
			table->codes[(unsigned char)index][length] = '\0';
		}
	}

	return table;
}

//function to free huffman table
void freeHuffmanCodeTable(HuffmanCodeTable* table)
{
	for (int i = 0; i < 256; i++)
	{
		if (table->codes[i] != NULL)
		{
			free(table->codes[i]);
		}
	}
	free(table);
}

//function to free nodes in the Huffman tree

void freeHuffmanTree(TreeNode* node)
{
	if (node != NULL)
	{
		freeHuffmanTree(node->left);
		freeHuffmanTree(node->right);
		free(node);
	}
}

// graphics testing function and visualization of hunffman tree




// dot file 

void generateDot(TreeNode* root, FILE* stream, int level)
{
	if (root == NULL)
	{
		return;
	}

	unsigned long id = (unsigned long)root;

	if (root->left)
	{
		unsigned long left_id = (unsigned long)root->left;
		fprintf(stream, "    \"%lu\" -> \"%lu\";\n", id, left_id);
		generateDot(root->left, stream, level + 1);
	}
	if (root->right)
	{
		unsigned long right_id = (unsigned long)root->right;
		fprintf(stream, "    \"%lu\" -> \"%lu\";\n", id, right_id);
		generateDot(root->right, stream, level + 1);
	}

	if (root->left == NULL && root->right == NULL)
	{
		fprintf(stream, "    \"%lu\" [label=\"%c(%d)\", shape=ellipse, style=filled, fillcolor=lightblue];\n", id, root->data, root->frequency);
	}
	else
	{
		fprintf(stream, "    \"%lu\" [label=\"%d\", shape=circle];\n", id, root->frequency);
	}
}

void createDotFile(TreeNode* root, const char* filename)
{
	printf("Creating dot file: %s\n", filename); // Debug statement
	FILE* file = fopen(filename, "w");
	if (file == NULL)
	{
		perror("Failed to open file");
		return;
	}
	fprintf(file, "digraph G {\n");
	fprintf(file, "    node [fontname=\"Helvetica\"];\n");
	fprintf(file, "    edge [fontname=\"Helvetica\"];\n");
	fprintf(file, "    graph [fontsize=10];\n");
	fprintf(file, "    node [fontsize=10];\n");
	fprintf(file, "    edge [fontsize=10];\n");
	fprintf(file, "    rankdir=TB;\n");
	fprintf(file, "    size=\"8,10!\"; ratio=fill;\n");
	generateDot(root, file, 0);
	fprintf(file, "}\n");
	fclose(file);
	printf("Dot file created successfully.\n"); // Debug statement
}


//dot -Tpng "C:\Users\mazen\OneDrive\Desktop\AAST\term 3\data structure\project\hufman tree\hufman tree\huffman_tree.dot" -o "C:\Users\mazen\OneDrive\Desktop\AAST\term 3\data structure\project\hufman tree\hufman tree\huffman_tree.png"

// Function to get the output filename by appending "_compressed" to the input filename

void getOutputFilename(const char* inputFilename, char* outputFilename)
{
	strcpy(outputFilename, inputFilename);
	char* dot = strrchr(outputFilename, '.');
	if (dot)
	{
		*dot = '\0'; // Remove the extension
	}
	strcat(outputFilename, "_compressed.txt");
}

void getOutputdecompress(const char* inputFilename, char* outputFilename)
{
	strcpy(outputFilename, inputFilename);
	char* dot = strrchr(outputFilename, '_');
	if (dot)
	{
		*dot = '\0'; // Remove the extension
	}
	strcat(outputFilename, "_decompressed.txt");
}



// Function to check if the input filename is a text file
bool isTextFile(const char* filename)
{
	const char* dot = strrchr(filename, '.');
	return dot && strcmp(dot, ".txt") == 0;
}

// Function to check if the input filename is a Com file
bool isComFile(const char* filename)
{
	const char* dot = strrchr(filename, '.');
	return dot && strcmp(dot, ".txt") == 0;
}

// Function to get the .dat filename by appending ".dat" to the input filename
void getComFilename(const char* filename, char* ComFilename)
{
	strcpy(ComFilename, filename);

	// Check if the filename ends with "_compressed.txt"
	char* compressedExt = strstr(ComFilename, "_compressed.txt");
	if (compressedExt != NULL && strcmp(compressedExt, "_compressed.txt") == 0)
	{
		*compressedExt = '\0'; // Remove "_compressed.txt"
	}
	else {
		// Check if the filename ends with ".txt"
		char* txtExt = strrchr(ComFilename, '.');
		if (txtExt != NULL && strcmp(txtExt, ".txt") == 0)
		{
			*txtExt = '\0'; // Remove ".txt"
		}
	}

	strcat(ComFilename, ".cod");
}

// Function to get the padding bits filename
void getPaddingFilename(const char* filename, char* paddingFilename) {
	strcpy(paddingFilename, filename);
	// Check if the filename ends with "_compressed.txt"
	char* compressedExt = strstr(paddingFilename, "_compressed.txt");
	if (compressedExt != NULL && strcmp(compressedExt, "_compressed.txt") == 0)
	{
		*compressedExt = '\0'; // Remove "_compressed.txt"
	}
	else {
		// Check if the filename ends with ".txt"
		char* txtExt = strrchr(paddingFilename, '.');
		if (txtExt != NULL && strcmp(txtExt, ".txt") == 0)
		{
			*txtExt = '\0'; // Remove ".txt"
		}
	}
	strcat(paddingFilename, "_padding.txt");
}

// Function to save the number of padding bits to a file
void savePaddingBits(const char* filename, unsigned char paddingBits) {
	char paddingFilename[1024];
	getPaddingFilename(filename, paddingFilename);
	FILE* paddingFile = fopen(paddingFilename, "w");
	if (paddingFile) {
		fprintf(paddingFile, "%u", paddingBits);
		fclose(paddingFile);
	}
	else {
		printf("Error opening padding bits file.\n");
	}
}

// Function to read the number of padding bits from a file
unsigned char readPaddingBits(const char* filename) {
	char paddingFilename[1024];
	getPaddingFilename(filename, paddingFilename);
	FILE* paddingFile = fopen(paddingFilename, "r");
	unsigned char paddingBits = 0;
	if (paddingFile) {
		fscanf(paddingFile, "%hhu", &paddingBits);
		fclose(paddingFile);
	}
	else {
		printf("Error opening padding bits file.\n");
	}
	return paddingBits;
}


#include <thread>
#include <vector>
#include <string>
#include <mutex>

// Mutex for synchronizing access to buffersArr
std::mutex buffersMutex;

void compressorThread(int idx, std::string* buffersArr, int bytesRead, char* chunk, HuffmanCodeTable* table) {
	std::string localCompressed;
	int localbitIndex = 0;
	unsigned char localbyte = 0;

	for (size_t i = 0; i < bytesRead; i++) {
		unsigned char c = chunk[i];
		const char* code = table->codes[c];
		if (code != NULL) {
			for (size_t j = 0; j < strlen(code); j++) {
				if (code[j] == '1') {
					localbyte |= (1 << (7 - localbitIndex));
				}
				localbitIndex++;
				if (localbitIndex == 8) {
					localCompressed.push_back(localbyte);
					localbitIndex = 0;
					localbyte = 0;
				}
			}
		}
	}

	delete[] chunk;  // Assuming chunk was dynamically allocated

	// Synchronize access to buffersArr
	{
		std::lock_guard<std::mutex> lock(buffersMutex);
		buffersArr[idx] = localCompressed;
	}

	return;
}

void compressFile_intermediate_multithread(const char* inputFilename, TreeNode* root, HuffmanCodeTable* table, LoadingBar* bar) {
	char outputFilename[1024];
	getOutputFilename(inputFilename, outputFilename);

	FILE* inputFile = fopen(inputFilename, "rb");
	if (inputFile == NULL) {
		printf("Error opening input file.\n");
		return;
	}

	FILE* outputFile = fopen(outputFilename, "wb");
	if (outputFile == NULL) {
		printf("Error opening output file.\n");
		fclose(inputFile);
		return;
	}

	// Generate a unique .dat file name based on the input file name
	char ComFilename[1024];
	getComFilename(inputFilename, ComFilename);

	// Save Huffman tree and code table to the .dat file
	FILE* ComFile = fopen(ComFilename, "wb");
	if (ComFile == NULL) {
		printf("Error opening .dat file to save Huffman tree and code table.\n");
		fclose(inputFile);
		fclose(outputFile);
		return;
	}
	saveHuffmanTreeAndCodeTable(root, table, ComFilename);
	fclose(ComFile);

	// Create a .txt file to store the number of padding bits
	char paddingFilename[1024];
	getPaddingFilename(inputFilename, paddingFilename);
	FILE* paddingFile = fopen(paddingFilename, "w");
	if (paddingFile == NULL) {
		printf("Error opening padding bits file.\n");
		fclose(inputFile);
		fclose(outputFile);
		return;
	}

	char* buffer = (char*)malloc(BUFFER_SIZE);
	if (!buffer) {
		printf("Memory allocation failed.\n");
		fclose(inputFile);
		fclose(outputFile);
		fclose(paddingFile);
		return;
	}

	fseeko(inputFile, 0, SEEK_END);
	long long fileSize = ftello(inputFile);
	fseeko(inputFile, 0, SEEK_SET);

	// for loading bar 
	long long number = BUFFER_SIZE;
	int totalSteps = fileSize / number + 1;
	int step = 0;

	unsigned char paddingBits = 0;

	size_t bytesRead;

	std::vector<std::thread> workers;
	std::string* buffersArr = new std::string[std::thread::hardware_concurrency()];
	int currWorker = -1;

	while ((bytesRead = fread(buffer, 1, BUFFER_SIZE, inputFile)) > 0) {
		char* chunk = new char[bytesRead];
		memcpy(chunk, buffer, bytesRead);

		// Enqueue a task to process this chunk
		currWorker++;
		workers.emplace_back(compressorThread, currWorker, buffersArr, bytesRead, chunk, table);

		// Update the loading bar after processing each buffer
		updateLoadingBar(bar, static_cast<float>(++step) / totalSteps);

		if (currWorker >= std::thread::hardware_concurrency() - 1) {
			for (auto& worker : workers)
				if (worker.joinable())worker.join();
			for (int i = 0; i <= currWorker; ++i)
				fwrite(buffersArr[i].data(), 1, buffersArr[i].size(), outputFile);
			currWorker = 0;
		}
	}

	for (auto& worker : workers)
		if (worker.joinable())worker.join();
	for (int i = 0; i <= currWorker; ++i)
		fwrite(buffersArr[i].data(), 1, buffersArr[i].size(), outputFile);


	// Write the number of padding bits to the .txt file
	fprintf(paddingFile, "%u", paddingBits);
	fclose(paddingFile);

	free(buffer);
	fclose(inputFile);
	fclose(outputFile);

	printf("File '%s' compressed to '%s'.\n", inputFilename, outputFilename);
	showCompletionScreen(outputFilename);

	// Clean up
	delete[] buffersArr;
}



// version 2 of compress file function
void compressFile_intermediate(const char* inputFilename, TreeNode* root, HuffmanCodeTable* table, LoadingBar* bar) {
	char outputFilename[1024];
	getOutputFilename(inputFilename, outputFilename);

	FILE* inputFile = fopen(inputFilename, "rb");
	if (inputFile == NULL) {
		printf("Error opening input file.\n");
		return;
	}

	FILE* outputFile = fopen(outputFilename, "wb");
	if (outputFile == NULL) {
		printf("Error opening output file.\n");
		fclose(inputFile);
		return;
	}

	// Generate a unique .dat file name based on the input file name
	char ComFilename[1024];
	getComFilename(inputFilename, ComFilename);

	// Save Huffman tree and code table to the .dat file
	FILE* ComFile = fopen(ComFilename, "wb");
	if (ComFile == NULL) {
		printf("Error opening .dat file to save Huffman tree and code table.\n");
		fclose(inputFile);
		fclose(outputFile);
		return;
	}
	saveHuffmanTreeAndCodeTable(root, table, ComFilename);
	fclose(ComFile);

	// Create a .txt file to store the number of padding bits
	char paddingFilename[1024];
	getPaddingFilename(inputFilename, paddingFilename);
	FILE* paddingFile = fopen(paddingFilename, "w");
	if (paddingFile == NULL) {
		printf("Error opening padding bits file.\n");
		fclose(inputFile);
		fclose(outputFile);
		return;
	}

	char* buffer = (char*)malloc(BUFFER_SIZE);
	if (!buffer) {
		printf("Memory allocation failed.\n");
		fclose(inputFile);
		fclose(outputFile);
		fclose(paddingFile);
		return;
	}

	char bitBuffer[Bit_compression_size + 1] = { 0 };
	int bitIndex = 0;
	unsigned char byte = 0;

	size_t bytesRead;
	unsigned char paddingBits = 0;

	fseeko(inputFile, 0, SEEK_END);
	long long fileSize = ftello(inputFile);
	fseeko(inputFile, 0, SEEK_SET);

	// for loading bar 
	int totalSteps = fileSize / BUFFER_SIZE + 1;
	int step = 0;

	while ((bytesRead = fread(buffer, 1, BUFFER_SIZE, inputFile)) > 0) {
		for (size_t i = 0; i < bytesRead; i++) {
			unsigned char c = buffer[i];
			const char* code = table->codes[c];
			if (code != NULL) {
				for (size_t j = 0; j < strlen(code); j++) {
					if (code[j] == '1') {
						byte |= (1 << (7 - bitIndex));
					}
					bitIndex++;
					if (bitIndex == 8) {
						fputc(byte, outputFile);
						bitIndex = 0;
						byte = 0;
					}
				}
			}
		}
		updateLoadingBar(bar, static_cast<float>(++step) / totalSteps); // Update loading bar here
	}

	// Write any remaining bits as the last byte and calculate padding bits
	if (bitIndex > 0) {
		paddingBits = 8 - bitIndex;
		fputc(byte, outputFile);
	}

	fprintf(paddingFile, "%u", paddingBits);
	fclose(paddingFile);

	free(buffer);
	fclose(inputFile);
	fclose(outputFile);

	printf("File '%s' compressed to '%s'.\n", inputFilename, outputFilename);
	 showCompletionScreen(outputFilename);
}








void byteToBits(unsigned char byte, char* bits)
{
	for (int i = 0; i < Bit_compression_size; i++)
	{
		bits[i] = (byte & (1 << (Bit_compression_size - i - 1))) ? '1' : '0';
	}
}

void decompressFile(const char* inputFilename, const char* outputFilename, LoadingBar* bar)
{
	FILE* inputFile = fopen(inputFilename, "rb");
	if (inputFile == NULL) {
		printf("Error opening input file.\n");
		return;
	}

	FILE* outputFile = fopen(outputFilename, "wb");
	if (outputFile == NULL) {
		printf("Error opening output file.\n");
		fclose(inputFile);
		return;
	}

	// Generate the corresponding .dat file name based on the input compressed file name
	char ComFilename[1024];
	getComFilename(inputFilename, ComFilename);

	// Load the Huffman tree and code table from the .dat file
	FILE* ComFile = fopen(ComFilename, "rb");
	if (ComFile == NULL) {
		printf("Error opening .dat file to load Huffman tree and code table.\n");
		fclose(inputFile);
		fclose(outputFile);
		return;
	}

	TreeNode* root = loadHuffmanTree(ComFile);
	HuffmanCodeTable* table = loadHuffmanCodeTable(ComFile);
	fclose(ComFile);

	// Load the number of padding bits from the .txt file
	char paddingFilename[1024];
	getPaddingFilename(inputFilename, paddingFilename);
	FILE* paddingFile = fopen(paddingFilename, "r");
	if (paddingFile == NULL) {
		printf("Error opening padding bits file.\n");
		fclose(inputFile);
		fclose(outputFile);
		return;
	}

	unsigned char paddingBits;
	fscanf(paddingFile, "%hhu", &paddingBits);
	fclose(paddingFile);

	// Get file size
	fseeko(inputFile, 0, SEEK_END);
	long long fileSize = ftello(inputFile);
	fseeko(inputFile, 0, SEEK_SET);

	// For loading bar 
	long long number = BUFFER_SIZE;
	int totalSteps = fileSize / number + 1;
	int step = 0;

	char* buffer = (char*)malloc(BUFFER_SIZE);
	if (!buffer) {
		printf("Memory allocation failed.\n");
		fclose(inputFile);
		fclose(outputFile);
		return;
	}

	size_t bytesRead;
	char bitBuffer[Bit_compression_size + 1];
	bitBuffer[Bit_compression_size] = '\0';
	size_t bitIndex = 0;
	TreeNode* currentNode = root;
	long remainingBits = (fileSize * 8) - paddingBits; // Total bits minus padding bits
	long processedBits = 0;

	while ((bytesRead = fread(buffer, 1, BUFFER_SIZE, inputFile)) > 0) {
		for (size_t i = 0; i < bytesRead && processedBits < remainingBits; i++) {
			unsigned char byte = buffer[i];
			byteToBits(byte, bitBuffer);

			for (int j = 0; j < Bit_compression_size && processedBits < remainingBits; j++) {
				if (bitBuffer[j] == '0') {
					currentNode = currentNode->left;
				}
				else {
					currentNode = currentNode->right;
				}

				if (currentNode->left == NULL && currentNode->right == NULL) {
					fwrite(&currentNode->data, sizeof(char), 1, outputFile);
					currentNode = root;
				}
				processedBits++;
			}
		}
		updateLoadingBar(bar, static_cast<float>(++step) / totalSteps);
	}

	fclose(inputFile);
	fclose(outputFile);
	printf("File '%s' decompressed to '%s'.\n", inputFilename, outputFilename);

	// Free the Huffman tree and code table
	free(buffer);
	freeHuffmanTree(root);
	freeHuffmanCodeTable(table);
	 showCompletionScreen(outputFilename);
}



//function to load intro pic
void mainLoop()
{
	loadIntro();
	const char* filterPatterns[1] = { "*.txt" };
	const char* filterPatterns1[1] = { "*_compressed.txt" };
	int frequencyTable[256] = { 0 };
	bool multithread = true;

	graphics.setup();
	graphics.hideCursor();

	bool running = true;
	bool fileGenerated = false;
	bool fileCompressed = false;
	bool fileDecompressed = false;

	while (running)
	{
		showWelcomeScreen();

		if (GetAsyncKeyState('S') & 0x8000)
		{
			const char* inputFilename = "input.txt";
			//generateRandomTextFile(inputFilename, FILE_SIZE, &fileGenBar);
			generateFrequencyTable(inputFilename, frequencyTable, &frequencyBar);
			TreeNode* root = buildHuffmanTree(frequencyTable);

			createDotFile(root, "huffman_tree.dot");

			HuffmanCodeTable* table = generateHuffmanCodeTable(root);
			compressFile_intermediate(inputFilename, root, table, &compressionBar);

			freeHuffmanCodeTable(table);
			freeHuffmanTree(root);
			fileCompressed = true;
		}
		else if (GetAsyncKeyState('C') & 0x8000)
		{
			const char* inputFilename = tinyfd_openFileDialog(
				"Select a text file",
				"",
				1,
				filterPatterns,
				"Text files",
				0
			);

			if (inputFilename)
			{
				if (isTextFile(inputFilename))
				{
					generateFrequencyTable(inputFilename, frequencyTable, &frequencyBar);
					TreeNode* root = buildHuffmanTree(frequencyTable);

					createDotFile(root, "huffman_tree.dot");

					HuffmanCodeTable* table = generateHuffmanCodeTable(root);

					if (multithread == false)
					{
						compressFile_intermediate(inputFilename, root, table, &compressionBar);
					}
					else
					{
						compressFile_intermediate_multithread(inputFilename, root, table, &compressionBar);
					}

					freeHuffmanCodeTable(table);
					freeHuffmanTree(root);
					fileCompressed = true;
				}
				else
				{
					tinyfd_messageBox(
						"Error",
						"The selected file is not a text file.",
						"ok",
						"error",
						1
					);
				}
			}
			else {
				tinyfd_messageBox(
					"Error",
					"No file was selected.",
					"ok",
					"error",
					1
				);
			}
		}
		else if (GetAsyncKeyState('D') & 0x8000)
		{  // Decompression option
			const char* inputFilename = tinyfd_openFileDialog
			(
				"Select a text file",
				"",
				1,
				filterPatterns1,
				"Text files",
				0
			);

			if (inputFilename)
			{
				if (isComFile(inputFilename))
				{
					char outputFilename[1024];
					getOutputdecompress(inputFilename, outputFilename);
					decompressFile(inputFilename, outputFilename, &compressionBar);
					fileDecompressed = true;
				}
				else
				{
					tinyfd_messageBox
					(
						"Error",
						"The selected file is not a com file.",
						"ok",
						"error",
						1
					);
				}
			}
			else {
				tinyfd_messageBox
				(
					"Error",
					"No file was selected.",
					"ok",
					"error",
					1
				);
			}
		}
		else if (GetAsyncKeyState('Q') & 0x8000)
		{
			running = false;
		}

		if (fileCompressed)
		{
			fileCompressed = false;
			Sleep(10);
		}

		if (fileDecompressed)
		{
			showCompletionScreen("decompressed_output.txt");
			fileDecompressed = false;
			Sleep(10);
		}
	}

	graphics.showCursor();
}

void writeHuffmanEncodedFile(const char* inputFilename, HuffmanCodeTable* table, const char* encodedFilename) {
	FILE* inputFile = fopen(inputFilename, "rb");
	if (inputFile == NULL) {
		printf("Error opening input file.\n");
		return;
	}

	FILE* encodedFile = fopen(encodedFilename, "w");
	if (encodedFile == NULL) {
		printf("Error opening encoded file.\n");
		fclose(inputFile);
		return;
	}

	char* buffer = (char*)malloc(BUFFER_SIZE);
	if (buffer == NULL) {
		printf("Memory allocation failed.\n");
		fclose(inputFile);
		fclose(encodedFile);
		return;
	}

	size_t bytesRead;

	while ((bytesRead = fread(buffer, 1, BUFFER_SIZE, inputFile)) > 0) {
		for (size_t i = 0; i < bytesRead; i++) {
			unsigned char c = buffer[i];
			const char* code = table->codes[c];
			if (code != NULL) {
				fprintf(encodedFile, "%s", code);
			}
		}
	}

	free(buffer);
	fclose(inputFile);
	fclose(encodedFile);

	printf("Huffman encoded file written to '%s'.\n", encodedFilename);
}

void printHuffmanCodeTable(HuffmanCodeTable* table, const char* filename) {
	FILE* file = fopen(filename, "w");
	if (file == NULL) {
		printf("Error opening file to print Huffman code table.\n");
		return;
	}

	char* buffer = (char*)malloc(MAX_HUFFMAN_CODE_SIZE + 50); // Allocate enough space for the largest possible entry
	if (buffer == NULL) {
		printf("Memory allocation failed.\n");
		fclose(file);
		return;
	}

	for (int i = 0; i < 256; i++) {
		if (table->codes[i] != NULL) {
			snprintf(buffer, MAX_HUFFMAN_CODE_SIZE + 50, "%c: %s\n", i, table->codes[i]);
			fprintf(file, "%s", buffer);
		}
	}

	free(buffer);
	fclose(file);
}

// Main function

int main()
{
	bool devmode = false;

	if (!devmode)
	{
		mainLoop();
		return 0;
	}
	const char* inputFilename = "a.txt";

	// Initialize the frequency table
	int frequencyTable[256] = { 0 };

	// Generate the frequency table
	generateFrequencyTable(inputFilename, frequencyTable, &frequencyBar);

	// Build the Huffman tree
	TreeNode* root = buildHuffmanTree(frequencyTable);
	if (!root)
	{
		printf("Failed to build Huffman Tree. Exiting program.\n");
		return -1; // Exit if tree construction failed
	}

	// Print Huffman codes to console

	HuffmanCodeTable* table = generateHuffmanCodeTable(root);

	printf("Huffman Codes:\n");
	for (int i = 0; i < 256; i++)
	{
		if (table->codes[i] != NULL)
		{
			printf("Character: %c, Huffman Code: %s\n", i, table->codes[i]);
		}
	}

	writeHuffmanEncodedFile(inputFilename, table, "huffman_encoded.txt");// correct

	printHuffmanCodeTable(table, "huffman_codes.txt");// correct 

	// Compress the input file using the Huffman codes

	compressFile_intermediate_multithread(inputFilename, root, table, &compressionBar);


	// Decompress the output file using the Huffman codes

	const char* outputFilename = "a_compressed.txt";
	decompressFile(outputFilename, "a_decompressed.txt", &compressionBar);

	// Free the Huffman code table
	freeHuffmanCodeTable(table);

	// Free the Huffman Tree to prevent memory leaks
	freeHuffmanTree(root);

	return 0;
}