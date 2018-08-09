#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

// Program Name: smallsh (Small Shell)
// Programmer: Daniel Rotenberg
// Class: OSU 344 - Operating Systems
// Assignment # 3

// SOURCES USED: 
// Lecture Notes
// https://www.geeksforgeeks.org/strtok-strtok_r-functions-c-examples/
// https://stackoverflow.com/questions/23456374/why-do-we-use-null-in-strtok
// https://brennan.io/2015/01/16/write-a-shell-in-c/

// Your shell must support command lines with a maximum length of 2048 characters, and a maximum of 512 arguments. 
#define MAX_LEN 2048
#define MAX_ARG 512
#define TOKEN_DELIMS " \n"

// Global Variables
int isBackground = 0; //0 is foreground, 1 is background
int argCount = 0; // to count the arguments for each command
int exitShell = 0; // to control main loop
char* line; // to use when reading in lines
char* token; // to be used when splitting up the string
char** argsToUse; //to store the arguments that will be used for exec calls
int fileDescriptor = -3; // to use when opening/closing files
unsigned long maxBuffer = MAX_LEN;
pid_t spawnPid = -3; // to capture child process IDs
int childExitStatus = -3; // to test how child exited
char* inRedirFile; // to store input redirection filename
char* outRedirFile; // to store output redirection filename

// int totalChildPIDs = 0; // to store total amount of child PIDs

// function declarations
void runSmallSh();
void revealStatus(int status);
void catchSIGTSTP(int signo);

int main() {
  runSmallSh();
  return 0;
}

// A CTRL-Z command from the keyboard will send a SIGTSTP signal to your parent shell 
// process and all children at the same time. For this assignment, when this signal is received by your shell, 
// your shell must display an informative message
void catchSIGTSTP(int signo){
  char* message = "\nSIGTSTP Signal Has been Received.\n"; //35 character message
  write(STDOUT_FILENO, message, 35);
  exit(0);
}

void revealStatus(int status) {
  if (WIFEXITED(status)) {
    // printf("The process exited normally\n");
    int exitStatus = WEXITSTATUS(status);
    printf("exit status was %d\n", exitStatus);
    fflush(stdout);
  } else if (WIFSIGNALED(status)){
      // printf("Child terminated by a signal\n");
      int termSignal = WTERMSIG(status);
      printf("Term Signal status was %d\n", termSignal);
      fflush(stdout);      
    }
}

void runSmallSh() {
  struct sigaction action1 = {{0}};
  struct sigaction action2 = {{0}};  
  // signal handlers
  action1.sa_handler = SIG_IGN; //set to ignore
  action2.sa_flags = 0;
  sigaction(SIGINT, &action1, NULL);

  action2.sa_handler = catchSIGTSTP;           
  action2.sa_flags = 0;                    
  sigaction(SIGTSTP, &action2, NULL);

  while(!exitShell) {
    argCount = 0; // reset argCount for each command
    isBackground = 0; // reset background argument
    printf(": "); // the prompt should be ": " according to HW Specs
    getline(&line, &maxBuffer, stdin);
    // printf("Line Read In: %s", line);
    fflush(stdout);

    // need to 'tokenize' the line being read to break apart the string
    argsToUse = malloc(MAX_LEN * sizeof(char));
    assert(argsToUse != NULL);
    // printf("asset passed");
    int tokIdx = 0;
    token = strtok(line, TOKEN_DELIMS); // get first token before looping
    while (token != NULL) { //loop until end of line
      if (strcmp(token, "&") == 0) {
        isBackground = 1;
        break;
      } else if (strcmp(token, ">") == 0) { // if token = '>' then get next token and save name for output redirection
          token = strtok(NULL, TOKEN_DELIMS);
          outRedirFile = strdup(token);
          // strcpy(outRedirFile,token);
          token = strtok(NULL, TOKEN_DELIMS);
      } else if (strcmp(token, "<") == 0) { // if token = '<' then get next token and save name for output redirection
          token = strtok(NULL, TOKEN_DELIMS);
          inRedirFile = strdup(token);
          // strcpy(inRedirFile, token)
          token = strtok(NULL, TOKEN_DELIMS);        
      } else { // if not a special case, store token in the argument array
          argsToUse[tokIdx] = token;
          token = strtok(NULL, TOKEN_DELIMS);
          tokIdx++;
          argCount++;
      }
    }
    argsToUse[argCount] = NULL; // to end the argument array

    // HW SPECS: your shell should allow blank lines and comments.  
    // Any line that begins with the # character is a comment line and should be ignored
    // A blank line (one without any commands) should also do nothing. 
    // Your shell should just re-prompt for another command when it receives either a blank line or a comment line.

    // The exit command exits your shell. It takes no arguments. When this command is run, your shell must kill any other 
    // processes or jobs that your shell has started before it terminates itself.

    // The cd command changes the working directory of your shell. By itself - with no arguments - 
    // it changes to the directory specified in the HOME environment variable
    // This command can also take one argument: the path of a directory to change to. 
    // Your cd command should support both absolute and relative paths.

    // The status command prints out either the exit status or the terminating signal of the last foreground process 
    // (not both, processes killed by signals do not have exit statuses!) ran by your shell.    

    if (argsToUse[0] == NULL) {
        continue; // re-prompt user
    } else if (strncmp(argsToUse[0], "#", 1) == 0) { // comments are skipped
        continue; // re-prompt user
    } else if (strcmp(argsToUse[0], "status") == 0) { // reveal status
      revealStatus(childExitStatus);
    } else if (strcmp(argsToUse[0], "exit") == 0) { // exit program
        exitShell = 1;
        fflush(stdout);
        exit(0);      
    } else if (strcmp(argsToUse[0], "cd") == 0) { // built in change directory
        if (argCount > 2) {
          printf("cd: Too Many Arguments!\n");
          fflush(stdout);
        } else if (argsToUse[1] != NULL) {
          if (chdir(argsToUse[1]) != 0) { // chdir returns 0 if successful
            printf("%s does not exist!\n", argsToUse[1]);
            fflush(stdout);
          } else {
            chdir(getenv("HOME"));
          }
        }
    } else { // If not build in command, comment or blank line, fork new child and 
      // **********BEGIN CHILD PROCESSES*****************
      spawnPid = fork();
      switch(spawnPid) {
        case -1: { //error case
          perror("Fork Failed!\n");
          exit(1);
          break;
        } // end of error case
        case 0: { // child creation success

// You will use fork(), exec(), and waitpid() to execute commands. From a conceptual perspective, 
// consider setting up your shell to run in this manner: let the parent process (your shell) continue running. 
// Whenever a non-built in command is received, have the parent fork() off a child. 
// This child then does any needed input/output redirection before running exec() on the command given.          

// After the fork() but before the exec() you must do any input and/or output redirection with dup2(). 
// An input file redirected via stdin should be opened for reading only; if your shell cannot open the file for reading, 
// it should print an error message and set the exit status to 1 (but don't exit the shell). 
// Similarly, an output file redirected via stdout should be opened for writing only; 
// it should be truncated if it already exists or created if it does not exist. If your shell cannot open the output file 
// it should print an error message and set the exit status to 1 (but don't exit the shell).                    
          // printf("CHILD(%d): Sleeping for 1 second\n", getpid());
          // sleep(1);

          // check for file input/output redirection 
          // used lecture 3.4 Redirecting stdout & stdin with execlp() for reference

    // The parent should not attempt to terminate the foreground child process when the parent receives a SIGINT
    // signal: instead, the foreground child (if any) must terminate itself on receipt of this signal.

          // if foreground mode reset SIGINT so the child can terminate 
          if (!isBackground) {
            action1.sa_handler = SIG_DFL; //set back to default mode
            action1.sa_flags = SA_RESTART;
            sigaction(SIGINT, &action1, NULL);
          }

          // Background commands should have their standard input redirected from /dev/null 
          // if the user did not specify some other file to take standard input from.

          if (isBackground) {
            fileDescriptor = open("/dev/null", O_RDONLY);
            if (fileDescriptor == -1) {
              perror("inRedirFile Open()");
              fflush(stdout);
              exit(1);
            } else if (dup2(fileDescriptor, 0) == -1) {
              perror("inRedirFile dup2()");
              fflush(stdout);
              exit(1);
            }
            close(fileDescriptor);
          } else if (inRedirFile != NULL) {
            fileDescriptor = open(inRedirFile, O_RDONLY);
            if (fileDescriptor == -1) {
              perror("inRedirFile Open()");
              fflush(stdout);
              exit(1);
            } else if (dup2(fileDescriptor, 0) == -1) {
              perror("inRedirFile dup2()");
              fflush(stdout);
              exit(1);
            }
            close(fileDescriptor);
          }

          if (outRedirFile != NULL) {
            fileDescriptor = open(outRedirFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fileDescriptor == -1) {
              perror("outRedirFile Open()");
              fflush(stdout);
              exit(1);
            } else if (dup2(fileDescriptor, 1) == -1) {
              perror("outRedirFile dup2()");
              fflush(stdout);
              exit(1);
            }
            close(fileDescriptor);
          }

          //after file redirection, time to call exec on the commands in the argsToUse array
          if(execvp(argsToUse[0], argsToUse)) {
            perror("Error: Fork not created! ");
            fflush(stdout);
            exit(1);
          }
          // printf("CHILD(%d): Converting into \'ls -a\'\n", getpid());
          // execlp("ls", "ls", "-a", NULL);
          // perror("CHILD: exec failure!\n");
          // exit(1);
          break;
        } // end of child case
        default: {
          // printf("PARENT(%d)\n", getpid());
          // sleep(2);
          // printf("PARENT(%d): Wait()ing for child(%d) to terminate\n", getpid(), spawnPid);
          // isBackground = 1;
          if (isBackground) {
            printf("PID for Background Process: %d\n", spawnPid);
            pid_t childPid = waitpid(spawnPid, &childExitStatus, 0);
            printf("PARENT(%d): Child(%d) terminated!\n", getpid(), childPid);
          }
            pid_t childPid = waitpid(spawnPid, &childExitStatus, 0);
            // printf("PARENT(%d): Child(%d) terminated!\n", getpid(), childPid);
           // } 
           // else {
          //   pid_t childPid = waitpid(spawnPid, &childExitStatus, WNOHANG);
          // }
          // return;
          break;
        } // end of default case
      } // end of fork Switch
    }

    // reset redirection file path names
    outRedirFile = NULL;
    inRedirFile = NULL;

    for (int i = 0; i < argCount; i++) { // reset args
      argsToUse[i] = NULL;
    }

  } // closing bracket that ends the shell loop
}

