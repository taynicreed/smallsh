#define _POSIX_C_SOURCE 200809L
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/types.h>    
#include <signal.h>    
#include <fcntl.h>
#include <stdint.h>



//*****************************************************
//  Function Declarationis and Global Variables
//*****************************************************

char *str_gsub(char *restrict *restrict haystack, char const *restrict needle, char const *restrict sub);
void exitCmd(int ec);
void cdCmd(char *fp);
void otherCmd(char *argv[], bool runBG, char *inputOp, char *outputOp, struct sigaction old_action);


int fgExit = 0;
pid_t bgPid = 0;

//*****************************************************
//  handle_SIGINT
//  dummy signal handler used during getline
//*****************************************************
void handle_SIGINT(int signo) {
}


//*****************************************************
//  main function
//*****************************************************

int main() {
  // initiating sigaction structs
  struct sigaction ignore_action = {0};
  struct sigaction no_action = {0};
  struct sigaction old_action = {0};
  ignore_action.sa_handler = SIG_IGN;
  no_action.sa_handler = handle_SIGINT; 
  sigfillset(&no_action.sa_mask);
  no_action.sa_flags = 0;

  sigaction(SIGINT, &ignore_action, &old_action);
  sigaction(SIGTSTP, &ignore_action, &old_action);

  char *line = NULL;
  size_t n = 0;

  // shell loop
 start:
  for(;;) {

    // managing background processes
    int bgChildStatus;
    pid_t bgChild;
    int bgExitStat;
    while ((bgChild = waitpid(0, &bgChildStatus, WNOHANG|WUNTRACED)) > 0) {
      if (WIFEXITED(bgChildStatus)) {
        bgExitStat = WEXITSTATUS(bgChildStatus);
        fprintf(stderr, "Child process %jd done. Exit status %d.\n", (intmax_t) bgChild, bgExitStat);
      }
      else if (WIFSIGNALED(bgChildStatus)) {
        bgExitStat = WTERMSIG(bgChildStatus);
        fprintf(stderr, "Child process %jd done. Signaled %d.\n", (intmax_t) bgChild, bgExitStat);
      }
      else if (WIFSTOPPED(bgChildStatus)) { 
       kill(bgChild, SIGCONT);
       fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t) bgChild);
     }              

    }

    // displaying prompt and reading input
    char *ps1 = getenv("PS1");
    if (ps1 == NULL)
      ps1 = "";
    fprintf(stderr, "%s", ps1);
    sigaction(SIGINT, &no_action, NULL);
    ssize_t line_length = getline(&line, &n, stdin);
    if(feof(stdin))
      exitCmd(fgExit);
    if (line_length == -1) {
      clearerr(stdin);
      fprintf(stderr, "\n");
      goto start;
    }
    sigaction(SIGINT, &ignore_action, NULL);


    // word splitting
    char *delim = getenv("IFS");
    if (delim == NULL) 
      delim = " \t\n";

    char *words[513];
    memset(words, '\0', 513);
    int wordCount = 0;
    char *token = strtok(line, delim);

    while (token) {
      words[wordCount] = strdup(token);
      token = strtok(NULL, delim);
      wordCount++;
    }

    words[wordCount] = NULL;
    free(token);

    // expansion
    pid_t smallsh_pid = getpid();
    char *pidsub = malloc(sizeof(8));
    char *fgExitSub = malloc(sizeof(8));
    char *bgPidSub = malloc(sizeof(8));

    sprintf(pidsub, "%d", smallsh_pid);
    sprintf(fgExitSub, "%d", fgExit);
    if (bgPid == 0)
      bgPidSub = "";
    else
      sprintf(bgPidSub, "%d", bgPid);
    
    // replacing ~/, $$, $?, $!
    for (int i = 0; i < wordCount; i++) {
        if (strncmp(words[i], "~/", 2) == 0)
          str_gsub(&words[i], "~", getenv("HOME"));

        str_gsub(&words[i], "$$", pidsub);

        str_gsub(&words[i], "$?", fgExitSub);

        str_gsub(&words[i], "$!", bgPidSub);
    }

    free(pidsub);
    free(fgExitSub);

    // parsing
    // NULLify comments, update wordCount variable if found
    for (int i = 0; i < wordCount; i++) {
      if (strcmp(words[i], "#") == 0) {
        words[i] = NULL;
        wordCount = i;
        break;
      }
    }
    
    // check if last word is &
    bool runBG = 0;
    if (strcmp(words[wordCount - 1], "&") == 0) {
      runBG = true;
      words[wordCount-1] = NULL;
      wordCount--;
    }

    // check for input and output commands
    char *inputOp = NULL,
        *outputOp = NULL;
    
    for (int i = wordCount - 2; i >= 0 && i > wordCount - 5; i -= 2) {
      if (strcmp(words[i], "<") == 0) {
        words[i] = NULL;
        inputOp = words[i+1];
        wordCount = i;
      }
      else if (strcmp(words[i], ">") == 0) {
        words[i] = NULL;
        outputOp = words[i+1];
        wordCount = i;
      }
      else
        break;
    }

    // execution
    // if no command, restart loop
    if (words[0] == NULL) goto start;

    // exit
    else if (strcmp(words[0], "exit") == 0) {
      // no argument, use exit status of last fg command
      if (wordCount == 1)
        exitCmd(0);
      // if there is one argument make sure it is a digit
      else if (wordCount == 2){
        char *exitArg = words[1];
        for (int i = 0; exitArg[i] != '\0'; i++) 
          if (!isdigit(exitArg[i])) goto exitErr;
        exitCmd(atoi(exitArg));
      }
      else {
     exitErr:
      fprintf(stderr, "exit command only accepts one integer argument\n");
      fgExit = 1;
      }
    }

    // cd
    else if (strcmp(words[0], "cd") == 0) {
      if (wordCount == 1) 
        cdCmd(getenv("HOME"));
      else if (wordCount == 2)
        cdCmd(words[1]);
      else {
        fprintf(stderr, "cd command only accepts one argument\n");
        fgExit = 1;
      }
    }
    
    // Non-Built-in commands
    else 
      otherCmd(words, runBG, inputOp, outputOp, old_action);
   
  }
  free(line);

  return 0;
}

//*****************************************************
//  str_gsub
//  String search and replace function
// code from https://www.youtube.com/watch?v=-3ty5W_6-IQ 
//*****************************************************

char *str_gsub(char *restrict *restrict haystack, char const *restrict needle, char const *restrict sub) {
  char *str = *haystack;
    size_t haystack_len = strlen(str); 
    size_t const needle_len = strlen(needle),
                 sub_len = strlen(sub);

    // find each occurence of needle in str and replace it with sub
    for (; (str = strstr(str, needle));) {  
        ptrdiff_t off = str - *haystack;
        if (sub_len > needle_len) {
            ptrdiff_t off = str - *haystack;
            str = realloc(*haystack, sizeof **haystack * (haystack_len + sub_len - needle_len +1)); 
            if (!str) goto exit;
            *haystack = str;
            str = *haystack + off; 
        }
        memmove(str + sub_len, str + needle_len, haystack_len + 1 - off - needle_len); 
        memcpy(str, sub, sub_len);
        haystack_len = haystack_len + sub_len - needle_len;
        str += sub_len;
        if(strcmp(needle, "~") == 0) break;
    }
    str = *haystack;
    if (sub_len < needle_len) {
        str = realloc(*haystack, sizeof **haystack + (haystack_len + 1)); 
        if (!str) goto exit;
        *haystack = str;
    }
  exit: 
    return str;
}

//*****************************************************
//  exitCmd function
//  Sends SIGINT sigal to all child processes in the 
//  proces group and then exits with given value, ec
//*****************************************************

void exitCmd(int ec) {
  fprintf(stderr, "\nexit\n");
  kill(0, SIGINT); 
  exit(ec);
}

//*****************************************************
//  cdCmd
//  Changes the working director to the given path, fp
//*****************************************************

void cdCmd(char *fp) {
  int cdErr = chdir(fp);
  if (cdErr != 0) {
    fprintf(stderr, "Cannot access %s\n", fp);
    fgExit = 1;
  }
  return;
}

//*****************************************************
//  otherCmd
//  code from Module 5
//*****************************************************

void otherCmd(char *argv[], bool runBG, char *inputOp, char *outputOp, struct sigaction old_action)
{

      int childStatus;
      pid_t spawnPid = fork();

      switch(spawnPid) {
        case -1:
                fprintf(stderr, "fork failed\n");
                fgExit = 1; 
                return; 
        case 0:
                // reset signals to original disposition
                 sigaction(SIGINT, &old_action, NULL);
                 sigaction(SIGTSTP, &old_action, NULL);

                // open inputOp for reading
                if (inputOp != NULL) {
                  int sourceFD = open(inputOp, O_RDONLY);
                  if (sourceFD == -1) {
                    perror("source open()");
                    exit(1);
                  }
                  int result = dup2(sourceFD, 0);
                  if (result == -1) {
                    perror("source dup2()");
                    exit(1);
                  }
                }
                // open outputOp for writing
                if (outputOp != NULL) {
                  int targetFD = open(outputOp, O_WRONLY | O_CREAT | O_TRUNC, 0777);
                  if (targetFD == -1) {
                    perror("target open()");
                    exit(1);
                  }
                  int result = dup2(targetFD, 1);
                  if (result == -1) {
                    perror("target dup2()");
                    exit(2);
                  }
                }
                // execute command with given arguments
                execvp(argv[0], argv);
                fprintf(stderr, "Error, could not execute %s\n", argv[0]);
                exit(2);
                break;
        default:
                // child is foreground process
                if (!runBG){
                  spawnPid = waitpid(spawnPid, &childStatus, 0);
                  if (WIFEXITED(childStatus)) 
                    fgExit = WEXITSTATUS(childStatus);
                  if (WIFSIGNALED(childStatus))
                    fgExit = 128 + WTERMSIG(childStatus); 
                  if (WIFSTOPPED(childStatus)) { 
                      kill(spawnPid, SIGCONT);
                      fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t) spawnPid);
                      bgPid = spawnPid;  
                  }
                }
                else
                  bgPid = spawnPid;
                }
      return;
    }

