the main file is huffmantee.cpp it has all the codes the rest are just libraries 

and 

Project Notes:
1)	I have used your custom library for a simple Gui to showcase the progress of the file compression and other actions being made also to allow the user to use the code as they wish.

2)	Also used another custom library called “tinydialogfiles” to allow users to browse for which files they wish to use.
   
https://sourceforge.net/projects/tinyfiledialogs/

4)	The dot file generation and creation are used to draw the hufman tree as a png using a graphics library called “Graphviz”

Enter this command into command prompt to turn dot file to png:

dot -Tpng "{path_file}\huffman_tree.dot" -o "{path_file}\huffman_tree.png"

https://graphviz.org/download/

4)	Finally, the main methods used to increase performance and speed of the program.
•	Used a buffer and to be honest  I didn’t fully understand exactly how it works until you gave us the water tank example and it finally made perfect since.

•	Used multithreading to allow parallel tasks to be made at the same time. 

•	Used thread pooling to limit the number of threads being used and to allow the reuse of the current set threads to the task.

Issues with the project:

1)	Too much memory usage when compressing files and CPU usage I tried my best to avoid memory leakages and I am sure that anything that isnt being used are being freed, there is a bigger issue when freeing the threads used in the ThreadPool it takes a while.

2)	I still can’t figure out how to save the missing bits from the last byte so that the decompression understands that there is padding.

3)	The graphics library is a tiny bit glitchy although i have used it in the past to make a puzzle platformer last semester with it worked perfectly, I assumed its due to some printfs in my code. 


Last Note 
The only part of the code that isnt mine is the priority queue functions credits to omar saleh my team member and the thread pooling, huge credits to the internet and some help from chatgpt to help me implement into my code since I was too far in 😊
