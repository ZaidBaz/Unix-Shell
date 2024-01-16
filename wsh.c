#include "wsh.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

#define MAX_LENGTH 256
#define MAX_JOBS 256

void fork_pipes(int n, struct command *cmd) {
    
	pid_t pid;
    int fd[2];
    int in_fd = 0;

    for (int i = 0; i < n; i++) {
        pipe(fd);

        if ((pid = fork()) == -1) {
            perror("Failed to fork child process");
            exit(1);
        }

        if (pid == 0) {
            
            char *cmndArgs[MAX_LENGTH];
            int totalArgs = 0;
            char *token;
            char *command_copy = strdup(cmd->argv[i]);

            while((token = strtok(command_copy, " \t\n"))) {
                cmndArgs[totalArgs++] = token;
                command_copy = NULL;
            }

            cmndArgs[totalArgs] = NULL;

            // If not the first command, then redirect standard input
            if (i != 0) {
                dup2(in_fd, STDIN_FILENO);
            }
            
            // If not the last command, then redirect standard output
            if (i != n - 1) {
                dup2(fd[1], STDOUT_FILENO);
            }

            // Close the original file descriptors
            close(fd[0]);
            close(fd[1]);

            // Stop other commands from reading from pipe
            if (i != 0) {
                close(in_fd);
            }

            execvp(cmndArgs[0], cmndArgs);
            perror("execvp failed\n");
            exit(1);
        } else {
            // Parent process
            wait(NULL);

            close(fd[1]);

            in_fd = fd[0];
        }
    }
}


char *foregroundArgs;
pid_t foreground_pid = 0;
int currSize = 0;
int currAvailableID = 1;
pid_t recentlyStopped = 0;

struct Job jobs[MAX_JOBS];

int findJob(pid_t pid) {
	
	for(int i = 0; i < currSize; i++) {

		if(jobs[i].pid == pid) {
			return i;
		}
	
	}

	return -1;

}

void SIGCHILD_Handler(int sig) {

	int status;
	pid_t pid;

	while((pid = waitpid(-1, &status, WNOHANG | WUNTRACED) > 0)) {
		
		if(WIFEXITED(status) || WIFSIGNALED(status)) {

			int searchIndex = findJob(pid);

			jobs[searchIndex].wasTerminated = 1;

			free(jobs[searchIndex].commandArgs);

		}

	}

}

void SIGINT_Handler(int sig) {

	if(foreground_pid != 0) {

		kill(foreground_pid, SIGINT);
	
	}

}

void SIGTSTP_Handler(int sig) {

	if(foreground_pid != 0) {

		recentlyStopped = foreground_pid;
		kill(foreground_pid, SIGTSTP);
		int searchIndex = findJob(foreground_pid);
		jobs[searchIndex].wasStopped = 1;
		foreground_pid = 0;

	}

}


void listJobs() {

	for(int i = 0; i < currSize; i++) {
		
		if(jobs[i].wasTerminated == 0) {
			
			printf("%d:", (int) jobs[i].id);

			for(int j = 0; j < jobs[i].numArgs; j++) {

				printf(" %s", jobs[i].commandArgs[j]);

			}
		
			if(jobs[i].isBg == 1) {
				printf(" &\n");
			}
			else {
				printf("\n");
			}

		}

	}

	return;
}


pid_t idToPid(int id) {

	for(int i = 0; i < currSize; i++) {
		
		if(jobs[i].id == id) {

			return jobs[i].pid;
		
		}
		
	}

	return -1;

}


pid_t findNextRecentlyStopped() {

	int currMaxIndex = -1;
	int currMax = 0;

	for(int i = 0; i < currSize; i++) {

		if(jobs[i].wasTerminated == 0 && jobs[i].isBg != 1 && jobs[i].id > currMax) {
			
			currMax = jobs[i].id;
			currMaxIndex = i;
		
		}

	}

	return jobs[currMaxIndex].pid;

}

int findNextPositive() {

	if(currSize == 0) {
		return 1;
	}
	else {
		
		int usedIds[currSize];
		int i, nextId = 1;

		for(i = 0; i < currSize; i++) {

			usedIds[i] = 0;

		}

		for(i = 0; i < currSize; i++) {

			int id = jobs[i].id;
			if(id >= 1 && id <= currSize && jobs[i].wasTerminated == 0) {

				usedIds[id - 1] = 1;

			}

		}

		while(usedIds[nextId - 1] == 1) {
			
			nextId++;

		}
		return nextId;
	}

}

int main(int argc, char *argv[]) {
	
	char *input;

	signal(SIGCHLD, SIGCHILD_Handler);
    signal(SIGINT, SIGINT_Handler);
    signal(SIGTSTP, SIGTSTP_Handler);
    signal(SIGTTOU, SIG_IGN);

	if(argc == 1) {

		memset(jobs, 0, sizeof(jobs));

		while(1) {

			int totalArgs = 0;
			char *cmndWithArgs[MAX_LENGTH];
			char cmnd[MAX_LENGTH] = "";
			size_t len = MAX_LENGTH;

			printf("wsh> ");
			
			input = NULL;
			if(getline(&input, &len, stdin) == -1)
			{
				printf("\n");
				exit(0);	
			}

			input[strlen(input) - 1] = '\0';

			strcpy(cmnd, input);

			char *token;

			while((token = strsep(&input, " ")) != NULL)
			{
				cmndWithArgs[totalArgs++] = token;
			}

			cmndWithArgs[totalArgs] = NULL;

			char *pipe_check = strstr(cmnd, "|");
			if (pipe_check) {
				
				struct command cmd;
				memset(&cmd, 0, sizeof(cmd));
				int cmd_count = 0;
				char *cmd_ptr = cmnd;

				while ((token = strsep(&cmd_ptr, "|")) != NULL) {

					cmd.argv[cmd_count] = malloc(sizeof(char *) * 15); 
					cmd.argv[cmd_count] = token;
					cmd_count++;
				}
				fork_pipes(cmd_count, &cmd);
			} else {

				if(strncmp(cmndWithArgs[0], "exit", 4) == 0) {
					break;
				}
				else if(strcmp(cmndWithArgs[0], "cd") == 0) {
					
					if(totalArgs == 2) {
						
						if(chdir(cmndWithArgs[1]) != 0) {
							perror("chdir error\n");
						}
					
					}
					else if(totalArgs == 1 || totalArgs > 2) {
						
						exit(1);			
		
					}
				}
				else if(strcmp(cmndWithArgs[0], "jobs") == 0) {
					
					listJobs();

				}
				else if(strcmp(cmndWithArgs[0], "bg") == 0) {
			
					if(totalArgs == 2) {

						kill(idToPid(atoi(cmndWithArgs[1])), SIGCONT);
					
					}
					else if(totalArgs == 1) {
					
						kill(recentlyStopped, SIGCONT);
						recentlyStopped = findNextRecentlyStopped();

					}
				
				}
				else if(strcmp(cmndWithArgs[0], "fg") == 0) {

					if(totalArgs == 2) {
					
						pid_t pidToFg = idToPid(atoi(cmndWithArgs[1]));
						foreground_pid = pidToFg;
						tcsetpgrp(STDIN_FILENO, pidToFg);
						kill(pidToFg, SIGCONT);
						int status;
						waitpid(pidToFg, &status, WUNTRACED);
						tcsetpgrp(STDIN_FILENO, getpgrp());
						if(WIFSTOPPED(status)) {
							jobs[findJob(pidToFg)].wasStopped = 1;
						}
						else {
							jobs[findJob(pidToFg)].wasTerminated = 1;
							foreground_pid = 0;
						}

					}
					else if(totalArgs == 1) {

						//pid_t pidToFg = 0;
						int currMax = 0;
						int currMaxIndex = -1;

						for(int i = 0; i < currSize; i++) {
							
							if(jobs[i].id > currMax && jobs[i].wasTerminated == 0) {
								
								currMax = jobs[i].id;
								currMaxIndex = i;

							}

						}
						
						foreground_pid = jobs[currMaxIndex].pid;
						tcsetpgrp(STDIN_FILENO, foreground_pid);
						kill(foreground_pid, SIGCONT);
						int status;
						waitpid(foreground_pid, &status, WUNTRACED);
						tcsetpgrp(STDIN_FILENO, getpgrp());
						if(WIFSTOPPED(status)) {
							jobs[findJob(foreground_pid)].wasStopped = 1;
						}
						else {
							jobs[findJob(foreground_pid)].wasTerminated = 1;
							foreground_pid = 0;
						}
						
					}					

				}
				else {

					int is_bg = 0;

					if(cmndWithArgs[totalArgs - 1][0] == '&') {
					
						is_bg = 1;
						cmndWithArgs[totalArgs - 1] = NULL;				
						totalArgs--;

					}

					pid_t pid = fork();

					if(pid == 0) {

						setpgid(0, 0);
						execvp(cmndWithArgs[0], cmndWithArgs);
						exit(1);

					}
					else {

						setpgid(pid, pid);
						
						if(!is_bg) {
							tcsetpgrp(STDIN_FILENO, pid);
							foreground_pid = pid;
							int status;
							waitpid(pid, &status, WUNTRACED);
							tcsetpgrp(STDIN_FILENO, getpid());
							
							if(WIFSTOPPED(status)) {

								jobs[currSize].id = findNextPositive();
								jobs[currSize].wasStopped = 1;
								jobs[currSize].isBg = is_bg;
								jobs[currSize].wasTerminated = 0;
								jobs[currSize].pid = pid;
								jobs[currSize].numArgs = totalArgs;

								jobs[currSize].commandArgs = malloc((totalArgs + 1) * sizeof(char *));
								if(jobs[currSize].commandArgs == NULL) {
									perror("Memory alloc failed\n");
									exit(1);
								}							

								for(int i = 0; i < totalArgs; i++)
								{
									jobs[currSize].commandArgs[i] = malloc(strlen(cmndWithArgs[i]) * sizeof(char));
									if (jobs[currSize].commandArgs[i] == NULL) {
										perror("Memory allocation failed\n");
										exit(1);
									}
									strcpy(jobs[currSize].commandArgs[i], cmndWithArgs[i]);
								}

								currSize++;	
							
							}
							foreground_pid = 0;
						}
						else {
							
							jobs[currSize].id = findNextPositive();
							jobs[currSize].wasStopped = 0;
							jobs[currSize].isBg = is_bg;
							jobs[currSize].wasTerminated = 0;
							jobs[currSize].pid = pid;
							jobs[currSize].numArgs = totalArgs;
							
							jobs[currSize].commandArgs = malloc((totalArgs + 1) * sizeof(char *));
							if(jobs[currSize].commandArgs == NULL) {
								perror("Memory alloc failed\n");
								exit(1);
							}

							for(int i = 0; i < totalArgs; i++)
							{
								jobs[currSize].commandArgs[i] = malloc(strlen(cmndWithArgs[i]) * sizeof(char));
								if (jobs[currSize].commandArgs[i] == NULL) {
									perror("Memory allocation failed\n");
									exit(1);
								}
								strcpy(jobs[currSize].commandArgs[i], cmndWithArgs[i]);
							}
							
							currSize++;						
		
						}
					}
				}
			}
		}
	}
	else if(argc == 2) {
	
		memset(jobs, 0, sizeof(jobs));
	
		FILE *file = fopen(argv[1], "r");
		if(file == NULL) {
			perror("Error opening the file\n");
			exit(1);
		}

		char *input = NULL;
		size_t len = 0;
		ssize_t read;

		while((read = getline(&input, &len, file)) != -1) {

			int totalArgs = 0;
			char cmnd[MAX_LENGTH] = "";
			input[strlen(input) - 1] = '\0';
			char *cmndWithArgs[MAX_LENGTH];
			strcpy(cmnd, input);

			char *pipe_check = strstr(cmnd, "|");
			if (pipe_check) {
				
				struct command cmd;
				memset(&cmd, 0, sizeof(cmd));
				int cmd_count = 0;
				char *cmd_ptr = cmnd;
				char *token;

				while ((token = strsep(&cmd_ptr, "|")) != NULL) {

					cmd.argv[cmd_count] = malloc(sizeof(char *) * 15); 
					cmd.argv[cmd_count] = token;
					cmd_count++;
				}
				fork_pipes(cmd_count, &cmd);
				continue;
			}

			char *token;

            while((token = strsep(&input, " ")) != NULL)
            {
                cmndWithArgs[totalArgs++] = token;
            }

            cmndWithArgs[totalArgs] = NULL;

			if(strncmp(cmndWithArgs[0], "exit", 4) == 0) {
				break;
			}
			else if(strcmp(cmndWithArgs[0], "cd") == 0) {
				
				if(totalArgs == 2) {
					
					if(chdir(cmndWithArgs[1]) != 0) {
						perror("chdir error\n");
					}
				
				}
				else if(totalArgs == 1 || totalArgs > 2) {
					
					exit(1);			
	
				}
			}
			else if(strcmp(cmndWithArgs[0], "jobs") == 0) {
				
				listJobs();

			}
			else if(strcmp(cmndWithArgs[0], "bg") == 0) {
		
				if(totalArgs == 2) {

					kill(idToPid(atoi(cmndWithArgs[1])), SIGCONT);
				
				}
				else if(totalArgs == 1) {
				
					kill(recentlyStopped, SIGCONT);
					recentlyStopped = findNextRecentlyStopped();

				}
			
			}
			else if(strcmp(cmndWithArgs[0], "fg") == 0) {

				if(totalArgs == 2) {
				
					pid_t pidToFg = idToPid(atoi(cmndWithArgs[1]));
					foreground_pid = pidToFg;
					tcsetpgrp(STDIN_FILENO, pidToFg);
					kill(pidToFg, SIGCONT);
					int status;
					waitpid(pidToFg, &status, WUNTRACED);
					tcsetpgrp(STDIN_FILENO, getpgrp());
					if(WIFSTOPPED(status)) {
						jobs[findJob(pidToFg)].wasStopped = 1;
					}
					else {
						jobs[findJob(pidToFg)].wasTerminated = 1;
						foreground_pid = 0;
					}

				}
				else if(totalArgs == 1) {

					//pid_t pidToFg = 0;
					int currMax = 0;
					int currMaxIndex = -1;

					for(int i = 0; i < currSize; i++) {
						
						if(jobs[i].id > currMax && jobs[i].wasTerminated == 0) {
							
							currMax = jobs[i].id;
							currMaxIndex = i;

						}

					}
					
					foreground_pid = jobs[currMaxIndex].pid;
					tcsetpgrp(STDIN_FILENO, foreground_pid);
					kill(foreground_pid, SIGCONT);
					int status;
					waitpid(foreground_pid, &status, WUNTRACED);
					tcsetpgrp(STDIN_FILENO, getpgrp());
					if(WIFSTOPPED(status)) {
						jobs[findJob(foreground_pid)].wasStopped = 1;
					}
					else {
						jobs[findJob(foreground_pid)].wasTerminated = 1;
						foreground_pid = 0;
					}
					
				}					

			}
			else {

				int is_bg = 0;

				if(cmndWithArgs[totalArgs - 1][0] == '&') {
				
					is_bg = 1;
					cmndWithArgs[totalArgs - 1] = NULL;				
					totalArgs--;

				}

				pid_t pid = fork();

				if(pid == 0) {

					setpgid(0, 0);
					execvp(cmndWithArgs[0], cmndWithArgs);
					exit(1);

				}
				else {

					setpgid(pid, pid);
					
					if(!is_bg) {
						tcsetpgrp(STDIN_FILENO, pid);
						foreground_pid = pid;
						int status;
						waitpid(pid, &status, WUNTRACED);
						tcsetpgrp(STDIN_FILENO, getpid());
						
						if(WIFSTOPPED(status)) {

							jobs[currSize].id = findNextPositive();
							jobs[currSize].wasStopped = 1;
							jobs[currSize].isBg = is_bg;
							jobs[currSize].wasTerminated = 0;
							jobs[currSize].pid = pid;
							jobs[currSize].numArgs = totalArgs;

							jobs[currSize].commandArgs = malloc((totalArgs + 1) * sizeof(char *));
							if(jobs[currSize].commandArgs == NULL) {
								perror("Memory alloc failed\n");
								exit(1);
							}							

							for(int i = 0; i < totalArgs; i++)
							{
								jobs[currSize].commandArgs[i] = malloc(strlen(cmndWithArgs[i]) * sizeof(char));
								if (jobs[currSize].commandArgs[i] == NULL) {
									perror("Memory allocation failed\n");
									exit(1);
								}
								strcpy(jobs[currSize].commandArgs[i], cmndWithArgs[i]);
							}

							currSize++;	
						
						}
						foreground_pid = 0;
					}
					else {
						
						jobs[currSize].id = findNextPositive();
						jobs[currSize].wasStopped = 0;
						jobs[currSize].isBg = is_bg;
						jobs[currSize].wasTerminated = 0;
						jobs[currSize].pid = pid;
						jobs[currSize].numArgs = totalArgs;
						
						jobs[currSize].commandArgs = malloc((totalArgs + 1) * sizeof(char *));
						if(jobs[currSize].commandArgs == NULL) {
							perror("Memory alloc failed\n");
							exit(1);
						}

						for(int i = 0; i < totalArgs; i++)
						{
							jobs[currSize].commandArgs[i] = malloc(strlen(cmndWithArgs[i]) * sizeof(char));
							if (jobs[currSize].commandArgs[i] == NULL) {
								perror("Memory allocation failed\n");
								exit(1);
							}
							strcpy(jobs[currSize].commandArgs[i], cmndWithArgs[i]);
						}
						
						currSize++;						
	
					}
				}
			}
		}

	}

	exit(0);

}
