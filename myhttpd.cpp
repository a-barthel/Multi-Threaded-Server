/*
 * ------------------------------ AUTHOR --------------------------------------------
 *										   
 * 	Andrew Barthel								    
 * 	Purdue University							    
 * 	abarthe@purdue.edu							    
 *										    
 * 	CS252 Systems Programming						    
 * 	El Kindi Rezig								   
 * 	Lab 5 - Web Server							    
 *										    
 * ----------------------------------------------------------------------------------
 */




// TODO: After the semester obviously since you aren't finishing it in the next 3 hours - CGI 






// -------------------------------- HEADERS -----------------------------------------
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>								  
#include <stdio.h>								   
#include <time.h>								
#include <unistd.h>								  
#include <stdlib.h>								  
#include <errno.h>								   
#include <signal.h>								   
#include <pthread.h>								   
// ----------------------------------------------------------------------------------

// --------------------------- GLOBAL VARIABLES ------------------------------------- 
enum STATE { PROCESS, THREAD, POOL, DEFAULT };					 
STATE state = DEFAULT;								   
int queueLength = 5;								   
int port = 9010;								   
pthread_mutex_t mutex;								   
pthread_mutexattr_t mutexAttr;
const char* http = "HTTP/1.0 ";
const char* notFound = "404 File Not Found \r\n";
const char* server = "Server: CS252 Systems Programming\r\n";
const char* errContent = "Content-type: text/html\r\n\r\n";
const char* error = "Could not find the specified URL. The server returned an error. ";
const char* httpSuc = "HTTP/1.0 200 Document Follows\r\n";
const char* content = "Content-type: ";
// ----------------------------------------------------------------------------------

void processTimeRequest(int socket);
void processRequestThread(int socket);
void poolSlave(int socket);
char* processInput(char* input);
bool isValid(char* path, char* check);
bool hasEnding(char* has, char* ending);
char* openDir(char* input);

// This method is called after incorrect program start - prints the correct usage.
void printUsage() {

	// Print the correct calling syntax and exit.
	fprintf( stderr, "\n\t\tUsage:\t./myhttpd [-f|-t|-p] [<port>]\n\n");
	fprintf( stderr, "\t\t\t-f: This flag will fork a new process for every request.\n");
	fprintf( stderr, "\t\t\t-t: This flag will create a new thread for every request.\n");
	fprintf( stderr, "\t\t\t-p: This flag will create a pool of threads to handle requests.\n\n");
	fprintf( stderr, "\t\tport: This argument dictates which port to connect to.\n\n");
	exit(1);

}

// Kill the zombie processes.
extern "C" void killZombies(int signal) {

	int pid = wait3(0, 0, NULL);
	for (; waitpid(-1, NULL, WNOHANG) > 0;);

}

// Pipe Handler
void pipehandler(int signal) {
	perror("Broken pipe");
	//printf("Y");
}

int main (int argc, char** argv) {

	// WEB SERVER.
	
	// Check for correct syntax.
	
	// Check for too many arguments.
	if (argc > 3) printUsage();

	// Parse arguments to gather information on flags.
	if (argc == 2 && !strcmp(argv[1], "-f")) {

		state = PROCESS;

	} else if (argc == 2 && !strcmp(argv[1], "-t")) {

		state = THREAD;

	} else if (argc == 2 && !strcmp(argv[1], "-p")) {

		state = POOL;

	} else if (argc == 2) {

		port = atoi(argv[1]);

	} else if (argc == 3 && !strcmp(argv[1], "-f")) {

		state = PROCESS;
		port = atoi(argv[2]);

	} else if (argc == 3 && !strcmp(argv[1], "-t")) {

		state = THREAD;
		port = atoi(argv[2]);

	} else if (argc == 3 && !strcmp(argv[1], "-p")) {

		state = POOL;
		port = atoi(argv[2]);

	} else {

		// Don't do anything - user did not specify concurrency flag or port
		// Use defaults.

	}

	// Check for valid port.
	if (port < 1023 || port > 65535) printUsage(); 

	// Handle Zombies.
	struct sigaction zombieKiller;
	zombieKiller.sa_handler = killZombies;
	sigemptyset(&zombieKiller.sa_mask);
	zombieKiller.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &zombieKiller, NULL)) { perror("sigaction"); exit(1); }

	// Set up IP and port for server.
	struct sockaddr_in serverIPAddress;
	memset( &serverIPAddress, 0, sizeof(serverIPAddress));
	serverIPAddress.sin_family = AF_INET;
	serverIPAddress.sin_addr.s_addr = INADDR_ANY;
	serverIPAddress.sin_port = htons((u_short)port);

	// Allocate a socket.
	int masterSocket = socket(PF_INET, SOCK_STREAM, 0);
	if (masterSocket < 0) { perror("socket"); exit(-1); }

	// Set options to refuse port.
	int optval = 1;
	int err = setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(int));

	// Bind the socket.
	int error = bind(masterSocket, (struct sockaddr*)&serverIPAddress, sizeof(serverIPAddress));
	if (error) { perror("bind"); exit(-1); }

	// Start listening.
	error = listen(masterSocket, queueLength);
	if (error) { perror("listen"); exit(-1); }

	// Handle SIGPIPE
	struct sigaction signalpipe;
	signalpipe.sa_handler = pipehandler;
	sigemptyset(&signalpipe.sa_mask);
	signalpipe.sa_flags = SA_RESTART;
	if(sigaction(SIGPIPE, &signalpipe, NULL) == -1) {
		perror("sigpipe");
		exit(-1);
	}

	// Run different type of server depending on args.
	if(state==PROCESS) {

		// Spawn a new process.
		for(;1;) {
			struct sockaddr_in clientIPAddress;
			int alen = sizeof(clientIPAddress);
			int slaveSocket = accept(masterSocket, (struct sockaddr*)&clientIPAddress, (socklen_t*)&alen);
			if(slaveSocket<0) { perror("accept"); exit(-1); }
			pid_t ret = fork();
			if(!ret) {
				signal(SIGINT, SIG_DFL);
				processTimeRequest(slaveSocket);
				close(slaveSocket);
				exit(EXIT_SUCCESS);
			}
			close(slaveSocket);
		}

	} else if (state==THREAD) {

		// Spawn a new thread.
		for(;true;) {
			struct sockaddr_in clientIPAddress;
			int alen = sizeof(clientIPAddress);
			int slaveSocket = accept(masterSocket, (struct sockaddr*)&clientIPAddress, (socklen_t*)&alen);
			if(slaveSocket<0) { perror("accept"); exit(-1); }
			pthread_t thread;
			pthread_attr_t thrAttr;
			pthread_attr_init(&thrAttr);
			pthread_attr_setscope(&thrAttr, PTHREAD_SCOPE_SYSTEM);
			pthread_create(&thread, &thrAttr, (void*(*)(void*))processRequestThread,(void*)slaveSocket);
		}

	} else if (state==POOL) {

		// Spwan a pool of 5 threads.
		pthread_t threads[5];
		pthread_attr_t thrAttr;
		pthread_attr_init(&thrAttr);
		pthread_attr_setscope(&thrAttr,PTHREAD_SCOPE_SYSTEM);
		pthread_mutex_init(&mutex,NULL);
		int i = -1;
		for(;i++<5;) {
			pthread_create(&threads[i],&thrAttr,(void*(*)(void*))poolSlave,(void*)masterSocket);
		}
		pthread_join(threads[0],NULL);
		exit(1);

	} else {
		// Normal server.
		while(1) {
			struct sockaddr_in clientIPAddress;
			int alen = sizeof(clientIPAddress);
			int slaveSocket = accept(masterSocket, (struct sockaddr*)&clientIPAddress, (socklen_t*)&alen);
			if(slaveSocket<0) { perror("accept"); exit(-1); }
			if(slaveSocket==-1 && errno == EINTR) continue;
			processTimeRequest(slaveSocket);
			close(slaveSocket);
		} //close(slaveSocket);

	} 

}

void
processTimeRequest( int socket ) {
	char* initInput;
	char sort = 0;
	char* input = (char*)malloc(500);
	char newChar, oldChar;
	char* start = input;
	int length = 0; int n = 0;
	int getF = 0; int docF = 0; int clrf = 0;
	for(;(n=read(socket, &newChar, 1))>0;) {
		length++;
		if(!getF) {
			if(newChar==' ') {
				getF = 1;
				input = start;
				*input = 0;
			} else {
				*input = newChar;
				input++;
				*input = 0;
			}
		} else if(!docF) {
			if(newChar==' ') {
				if(*start==0) {
					*(start) = 47;
					*(start+1) = 0;
				}
				docF = 1;
				input = start;
				for(;*input;) {
					if(*input=='?') {
						*input = 0;
						sort = *(input+1);
						break;
					} input++;
				}
				input = start;
				initInput = strdup(input);
				start = processInput(input);
				input = start;
			} else {
				*input = newChar;
				input++;
				*input = 0;
			}
		}
		if(oldChar==13&&newChar==10) {
			if(clrf) break;
			clrf = 1;
			n = read(socket, &newChar, 1);
			oldChar = newChar;
		} else clrf = 0;
		oldChar = newChar;
	}
	printf("%s\n", input);
	char* ContentType = "text/plain";
	if(*input==0) {
		// Send 404
		write(socket, http, sizeof(http));
		write(socket, notFound, sizeof(notFound));
		write(socket, server, sizeof(server));
		write(socket, errContent, sizeof(errContent));
		write(socket, error, sizeof(error));
	} else {
		if(strstr(input, ".html")) {
			ContentType = "text/html";
		} else if(strstr(input, ".gif")) {
			ContentType = "image/gif";
		} else if(strstr(input, ".xbm")) {
			ContentType = "image/xbm";
		}
		write(socket, httpSuc, sizeof(httpSuc));
		write(socket, server, sizeof(server));
		write(socket, content, sizeof(content));
		write(socket, ContentType, sizeof(ContentType));
		write(socket, "\r\n", 2);
		write(socket, "\r\n", 2);
		FILE* document;
		document = fopen(input, "rb");
		char c;
		int no = fileno(document);
		int counter;
		for(;counter=read(no, &c, 1)>0;) write(socket, &c, 1);
	}
}

void processRequestThread(int socket) {
	processTimeRequest(socket);
	close(socket);
}

void poolSlave(int socket) {
	for(;1;) {
		pthread_mutex_lock(&mutex);
		struct sockaddr_in clientIPAddress;
		int alen = sizeof(clientIPAddress);
		int slaveSocket = accept(socket, (struct sockaddr*)&clientIPAddress, (socklen_t*)&alen);
		pthread_mutex_unlock(&mutex);
		if(slaveSocket<0) { perror("accept"); exit(-1); }
		processTimeRequest(slaveSocket);
		close(slaveSocket);
	}
}

char* processInput(char* input) {
	printf("%s\n", input);
	char* in = input;
	char* end = input+1;
	char* docPath = (char*)malloc(500);
	char* cwd = (char*)malloc(500);
	cwd = getcwd(cwd, 499);
	int i;
	if(strstr(input, "subdir1")) {
		i = sprintf(docPath, "%s/http-root-dir/htdocs/subdir1.html", cwd);
		free(cwd); free(input);
		return docPath;
	}
	if(strstr(input, "dir")) {
		i = sprintf(docPath, "%s/http-root-dir/htdocs/dir.html", cwd);
		free(cwd); free(input);
		return docPath;
	}
	if(!*end) {
		i = sprintf(docPath, "%s/http-root-dir/htdocs/index.html", cwd);
		free(cwd); free(input);
		return docPath;
	} else i = sprintf(cwd, "%s/http-root-dir", cwd);
	i = sprintf(docPath, "%s%s", cwd, input);
	in = realpath(docPath, NULL);
	if(in) {
		free(docPath); docPath = in;
		if(isValid(docPath, cwd)) { free(cwd); free(input); return docPath; }
	}
	i = sprintf(docPath,  "%s/htdocs%s", cwd, input);
	in = realpath(docPath, NULL);
	if(in) {
		free(docPath); docPath = in;
		if(isValid(docPath, cwd)) {free(cwd); free(input); return docPath; }
	}
	free(cwd); free(docPath);
	*input = 0;
	return input;
}

bool isValid(char* path, char* check) {
	for(;1;) {
		if(*path!=*check) return false;
		path++; check++;
		if(!*check) return true;
		if(!*path) return false;
	}
}

char* openDir(char* input) {
	char* html = NULL;
	return html;
}
