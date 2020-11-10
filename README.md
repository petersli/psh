# psh 🐚

Hello! This is a shell written in C. 

psh began its life as a project in Brown's Computer Systems (CS33) course, and the bottom of this README has some next steps outlined. Further features will be added as I conduct more user research and interviews on painpoints with current shells! 😁


## Features
**Built-in Commands:**


`cd`: change directory


`ln`: create hard link


`rm`: remove file


`fg`: resume job if suspended, run in foreground


`bg`: resume job if suspended, run in background


`exit`: quit the shell


`jobs`: lists all current jobs and their job ID, state (running/suspended), and the command used to execute them

**Forking Child Processes, I/O Redirection, Background Processes:**


The shell can `fork` and `execv` new child processes based on command line input. To run a process in the background, append `&` to your command. To redirect input, use `<`. To redirect output, use `>` to overwrite the output file or `>>` to append to it,

Examples:


 `psh: /bin/ls ` will print the directory contents


 `psh: sleep 40 &` will sleep for 40 seconds in the background


 `psh: /bin/echo hello > echoed.txt` will overwrite the contents of echoed.txt with "hello"

**Signal Handling:**


The shell responds to user signals as expected, only quitting when it reaches EOF (`CTRL-D`) or the user quits via `exit`. For instance, typing `CTRL-C` will terminate the shell's foreground child process via `SIGINT`.




## How to compile

 *Note: these executables require GNU/Linux to run. For a simple way (< 30 min.) to set up an Ubuntu VM, follow this [guide](http://cs.brown.edu/courses/csci1310/2020/assign/labs/lab0.html), using this [Vagrantfile](https://raw.githubusercontent.com/brown-cs0330/student-files/master/Vagrantfile)*

To compile the shell executable normally, run:

  

`$ make 33sh`

  

To compile the shell such that it doesn't print a prompt, run:

  

`$ make 33noprompt`

  

To delete old executables and compile both 33sh and 33noprompt, run:

  

`$ make clean all`




## Code structure

  

`parse()`

This function extracts tokens from the buffer using strtok and parses file redirection symbols and targets. `parse` handles errors that could stem from user input relating to file redirection (trying to redirect output twice, etc). Sets argv appropriately.

`handle_fg_process()`

Pass terminal control to child process, then call waitpid to
wait for the process to finish. Check status for terminating or suspending
signals, and take appropriate action.

 `exec_child()`

If in child process, sets child pgid, restores signal handlers, and does file redirection. Calls execv. If in parent process, does nothing.

`reap_jobs()`

Call `waitpid` on all jobs in job list, printing and updating job list as needed

`main()`:

A do-while loop continues until `exit` is called or `read` receives EOF.

Each iteration, the program prints the prompt (if applicable), resets the buffer, token, and argv arrays, then reads input from the user. The program calls`parse`, then checks for non-white-space input. If there is valid input, the program checks for built-in commands, and executes the appropriate system call if a built-in command is found. If no built-in commands are found, it will attempt to `fork` a new child process and `execv` the program whose path is specified by the first non-redirection token. Inside the child process, the program closes STDIN/STDOUT and opens files as needed for file redirection.

  
 
### File Redirection

  

Redirection logic is encoded in these global variables, which are set by the `parse` function and reset to `NULL` each iteration.

```

int input_redirect_code;

int output_redirect_code;

char* input_file;

char* output_file;

```

  
  

  
  

### Prompt

When compiled with prompt, the prompt contains your current work directory,

making `cd` and other commands easier to use.

  

Example prompt:

  

`psh: /vagrant/psh-src$`

## Moving Forward (Features to Add)

Shell features
- Smart autocomplete and autocorrect
- Make copy-paste in vim go to system clipboard, especially over ssh

Potential features for a new terminal
- Click to move cursor
- Chat / real-time collaboration


