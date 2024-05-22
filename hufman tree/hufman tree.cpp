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
#define BUFFER_SIZE 1000000ll // 1 MB buffer size
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




// create a text file with random data 
void generateRandomTextFile(const char* filename, long long size, LoadingBar* bar)
{
    FILE* file = fopen(filename, "w+");
    if (file == NULL) 
    {
        printf("Error opening file.\n");
        return;
    }

    srand(time(NULL));
    long long bytesWritten = 0;
    int totalSteps = size / BUFFER_SIZE + 1;
    int step = 0;

    auto generateChunk = [&](long long chunkSize) 
        {
        char buffer[BUFFER_SIZE];
        for (int i = 0; i < chunkSize; i++) 
        {
            buffer[i] = (rand() % 95) + 32; // Generate printable ASCII characters (32-126)
        }
        fwrite(buffer, 1, chunkSize, file);
        };

    // Create multiple threads to generate the file
    std::vector<std::thread> threads;
    while (bytesWritten < size) {
        long long bytesToWrite = BUFFER_SIZE;
        if (size - bytesWritten < BUFFER_SIZE) {
            bytesToWrite = size - bytesWritten;
        }

        threads.emplace_back(generateChunk, bytesToWrite);
        bytesWritten += bytesToWrite;

        // Update the loading bar
        updateLoadingBar(bar, static_cast<float>(++step) / totalSteps);
    }

    for (auto& th : threads) 
    {
        if (th.joinable()) 
        {
            th.join();
        }
    }

    fclose(file);
    printf("File '%s' generated with size %ld bytes.\n", filename, size);
}



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

    char buffer[BUFFER_SIZE];
    size_t bytesRead;
    long long totalSteps = fileSize / BUFFER_SIZE + 1;  // Correct calculation of total steps
    int step = 0;
    bar->progress = 0.0f;  // Reset progress

    ThreadPool pool(std::thread::hardware_concurrency());
    std::mutex mtx;

    while ((bytesRead = fread(buffer, 1, BUFFER_SIZE, file)) > 0) 
    {
        char* chunk = new char[bytesRead];
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

        updateLoadingBar(bar, static_cast<float>(++step) / totalSteps);
    }

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
    strcat(outputFilename, "_compressed.com");
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
    return dot && strcmp(dot, ".com") == 0;
}

// Function to get the .dat filename by appending ".dat" to the input filename
void getComFilename(const char* filename, char* ComFilename) 
{
    strcpy(ComFilename, filename);

    // Check if the filename ends with "_compressed.txt"
    char* compressedExt = strstr(ComFilename, "_compressed.com");
    if (compressedExt != NULL && strcmp(compressedExt, "_compressed.com") == 0) 
    {
        *compressedExt = '\0'; // Remove "_compressed.txt"
    }
    else {
        // Check if the filename ends with ".com"
        char* txtExt = strrchr(ComFilename, '.');
        if (txtExt != NULL && strcmp(txtExt, ".txt") == 0) 
        {
            *txtExt = '\0'; // Remove ".txt"
        }
    }

    strcat(ComFilename, ".cod");
}




// Function to compress the input file and save the compressed data to the output file

unsigned char bitsToReadableAsciiChar(const char* bits)
{
    unsigned char value = 0;
    for (int i = 0; i < Bit_compression_size; i++)
    {
        value <<= 1;
        if (bits[i] == '1')
        {
            value |= 1;
        }
    }
    return value;
}

void compressFile_intermediate(const char* inputFilename, TreeNode* root, HuffmanCodeTable* table, LoadingBar* bar) 
{
    char outputFilename[1024];
    getOutputFilename(inputFilename, outputFilename);

    FILE* inputFile = fopen(inputFilename, "rb");
    if (inputFile == NULL) 
    {
        printf("Error opening input file.\n");
        return;
    }

    FILE* outputFile = fopen(outputFilename, "wb");
    if (outputFile == NULL) 
    {
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

    char* buffer = (char*)malloc(BUFFER_SIZE);
    if (!buffer) {
        printf("Memory allocation failed.\n");
        fclose(inputFile);
        fclose(outputFile);
        return;
    }

    size_t bytesRead;
    fseeko(inputFile, 0, SEEK_END);
    long long fileSize = ftello(inputFile);
    fseeko(inputFile, 0, SEEK_SET);
    int totalSteps = fileSize / BUFFER_SIZE + 1;
    int step = 0;

    ThreadPool pool(std::thread::hardware_concurrency());
    std::mutex mtx;

    while ((bytesRead = fread(buffer, 1, BUFFER_SIZE, inputFile)) > 0) 
    {
        char* chunk = new char[bytesRead];
        memcpy(chunk, buffer, bytesRead);

        pool.enqueue([chunk, bytesRead, &outputFile, &table, &mtx] 
            {
            std::string localCompressed;
            char bitBuffer[Bit_compression_size + 1];
            bitBuffer[Bit_compression_size] = '\0';
            int bitIndex = 0;

            for (size_t i = 0; i < bytesRead; i++) 
            {
                unsigned char c = chunk[i];
                const char* code = table->codes[c];
                if (code != NULL) {
                    for (size_t j = 0; j < strlen(code); j++) 
                    {
                        bitBuffer[bitIndex] = code[j];
                        bitIndex++;
                        if (bitIndex == Bit_compression_size) 
                        {
                            unsigned char byte = bitsToReadableAsciiChar(bitBuffer);
                            localCompressed.push_back(byte);
                            bitIndex = 0;
                        }
                    }
                }
            }

            if (bitIndex > 0) {
                while (bitIndex < Bit_compression_size) 
                {
                    bitBuffer[bitIndex] = '0';
                    bitIndex++;
                }
                unsigned char byte = bitsToReadableAsciiChar(bitBuffer);
                localCompressed.push_back(byte);
            }

            delete[] chunk;

            std::lock_guard<std::mutex> lock(mtx);
            fwrite(localCompressed.data(), sizeof(unsigned char), localCompressed.size(), outputFile);
            });

        updateLoadingBar(bar, static_cast<float>(++step) / totalSteps);
    }

    pool.~ThreadPool();  // Ensure all tasks are finished before closing files

    free(buffer);
    fclose(inputFile);
    fclose(outputFile);

    printf("File '%s' compressed to '%s'.\n", inputFilename, outputFilename);
    showCompletionScreen(outputFilename);
}






// decompression code 
void byteToBits(unsigned char byte, char* bits) 
{
    for (int i = Bit_compression_size - 1; i >= 0; i--) 
    {
        bits[i] = (byte & 1) ? '1' : '0';
        byte >>= 1;
    }
}

void decompressFile(const char* inputFilename, const char* outputFilename, LoadingBar* bar)
{
    FILE* inputFile = fopen(inputFilename, "rb");
    if (inputFile == NULL) 
    {
        printf("Error opening input file.\n");
        return;
    }

    FILE* outputFile = fopen(outputFilename, "wb");
    if (outputFile == NULL) 
    {
        printf("Error opening output file.\n");
        fclose(inputFile);
        return;
    }

    // Generate the corresponding .dat file name based on the input compressed file name
    char ComFilename[1024];
    getComFilename(inputFilename, ComFilename);

    // Load the Huffman tree and code table from the .dat file
    FILE* ComFile = fopen(ComFilename, "rb");
    if (ComFile == NULL) 
    {
        printf("Error opening .dat file to load Huffman tree and code table.\n");
        fclose(inputFile);
        fclose(outputFile);
        return;
    }
    TreeNode* root = loadHuffmanTree(ComFile);
    HuffmanCodeTable* table = loadHuffmanCodeTable(ComFile);
    fclose(ComFile);

    // replace with char buffer
    char* buffer = (char*)malloc(BUFFER_SIZE);
    if (!buffer) 
    {
        printf("Memory allocation failed.\n");
        fclose(inputFile);
        fclose(outputFile);
        return;
    }

    size_t bytesRead;
    char bitBuffer[Bit_compression_size + 1];
    bitBuffer[Bit_compression_size] = '\0';
    int bitIndex = 0;
    TreeNode* currentNode = root;

    fseeko(inputFile, 0, SEEK_END);
    long long fileSize = ftello(inputFile);
    fseeko(inputFile, 0, SEEK_SET);
    int totalSteps = fileSize / BUFFER_SIZE + 1;
    int step = 0;

    while ((bytesRead = fread(buffer, 1, BUFFER_SIZE, inputFile)) > 0) 
    {
        for (size_t i = 0; i < bytesRead; i++) 
        {
            unsigned char byte = buffer[i];
            byteToBits(byte, bitBuffer);

            for (int j = 0; j < Bit_compression_size; j++) 
            {
                if (bitBuffer[j] == '0') 
                {
                    currentNode = currentNode->left;
                }
                else {
                    currentNode = currentNode->right;
                }

                if (currentNode->left == NULL && currentNode->right == NULL) 
                {
                    fwrite(&currentNode->data, sizeof(char), 1, outputFile);
                    currentNode = root;
                }
            }
        }
        updateLoadingBar(bar, static_cast<float>(++step) / totalSteps);
    }

    fclose(inputFile);
    fclose(outputFile);
    printf("File '%s' decompressed to '%s'.\n", inputFilename, outputFilename);

    // Free the Huffman tree and code table
    freeHuffmanTree(root);
    freeHuffmanCodeTable(table);
}






//function to load intro pic


void mainLoop() 
{
    loadIntro();
    const char* filterPatterns[1] = { "*.txt" };
    const char* filterPatterns1[1] = { "*_compressed.com" };
    int frequencyTable[256] = { 0 };

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
            generateRandomTextFile(inputFilename, FILE_SIZE, &fileGenBar);
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
                    compressFile_intermediate(inputFilename, root, table, &compressionBar);

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

int main() 
{
    mainLoop();
    return 0;
}