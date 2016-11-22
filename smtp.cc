/*Headers*/

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <getopt.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <openssl/md5.h>
#include <ctype.h>
#include <openssl/md5.h>
#include <time.h>

bool printToStderr = false;

int commfdArray[100];
int indexToFd;
int indexToPthread;

pthread_t thread[100];
int accountCount;
char accounts[1000][1000];
char directoryPath[100];

enum SMTPCommands {
	HELO = 1, MAIL_FROM, RCPT_TO, DATA, QUIT, RSET, NOOP, INVALID = -1
};

typedef struct perThreadStruct {
	FILE* fp[100];
	bool dataReceptionFlag;
	char readingBuffer[10000];
	int CommandSequence;
	int clientCount;
	char* recipients[100];
	char mailFrom[100];
	char dataBuffer[10000];
	char time[200];

} perThreadStruct_t;

void howToUse();
void convertToLower(char* str);
bool DoesStringMatch(char* str1, char*str2);
int compStringSMTP(char* str);
void INThandler(int sig);
void printStrFromIndex(char* str, int index);
void parseCommand(char* str, int comm_fd, perThreadStruct_t* infoStruct);
void populateAccounts(char* DirName);
void parseTheReadBuffer(char* readString, int comm_fd,
		perThreadStruct_t* infoStruct);
void parseTheDataBuffer(char* readString, int comm_fd,
		perThreadStruct_t* infoStruct);
void* worker(void * arg);
bool isAccountpresent(char* buffer, perThreadStruct_t* infoStruct, int comm_fd);
void writeBadSequence(int fd);
FILE* openMboxFile(char* buffer, char* Dir, perThreadStruct_t* infoStruct);
int extractUserName(char* str, char* tmp2);
void clearInfo(perThreadStruct_t* infoStruct);

int main(int argc, char *argv[]) {
	char str[100];
	char welcomeMessage[200] =
			"220 localhost Simple Mail Transfer service ready\r\n";
	int listen_fd = 0;
	int option = 0;
	int port;
	struct sockaddr_in servaddr;
	int comm_fd = -1;

	port = 2500; //Default port for SMTP

	if (argc < 2) {
		fprintf(stderr,
				"*** Author: Surya Anirudh Chelluri Venkata(anisurya)\n");
		exit(1);
	}

	/************ Getting the option from command line ******************/

	while ((option = getopt(argc, argv, "p:av")) != -1) {
		switch (option) {
		case 'p':
			port = atoi(optarg);
			break;
		case 'a':
			fprintf(stderr,
					"*** Author: Surya Anirudh Chelluri Venkata(anisurya)\n");
			break;
		case 'v':
			printToStderr = true;
			break;
		default:
			howToUse();
			exit(EXIT_FAILURE);
		}
	}
	if(argv[optind] != NULL){
		strcpy(directoryPath, argv[optind]);
	}else{
		howToUse();
		exit(EXIT_FAILURE);
	}
	signal(SIGINT, INThandler);

	populateAccounts(directoryPath); //replace this with the read directory path

	listen_fd = socket(AF_INET, SOCK_STREAM, 0);

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htons(INADDR_ANY);
	servaddr.sin_port = htons(port);

	bind(listen_fd, (struct sockaddr *) &servaddr, sizeof(servaddr)); //Binding the socket fd (listen_fd) to the server address.
	listen(listen_fd, 1000); //Allowing a maximum of 1000 connections for now.
	while (true) {
		struct sockaddr_in clientaddr;
		socklen_t clientaddrlen = sizeof(clientaddr);
		comm_fd = accept(listen_fd, (struct sockaddr*) &clientaddr,
				&clientaddrlen);
		write(comm_fd, welcomeMessage, strlen(welcomeMessage));
		if (printToStderr)
			fprintf(stderr, "[%d] New connection\n", comm_fd); //print this only if stderr print is enabled

		pthread_create(&thread[indexToPthread], NULL, worker, &comm_fd);
		indexToPthread++;
		pthread_detach(thread[indexToPthread]); //Detaching the worker threads. Take care of exit in client
	}
}

void* worker(void * arg) {
	char str[1000];
	bzero(str, 1000);
	int*tmp = (int *) arg;
	int comm_fd;
	comm_fd = *tmp;
	perThreadStruct_t infoStruct;

	infoStruct.dataReceptionFlag = false;
	infoStruct.clientCount = 0;
	infoStruct.CommandSequence = 0;

	char strClose[] = "Server shutting down !";
	commfdArray[indexToFd] = comm_fd;
	indexToFd++;
	char okString[1000];
	char byeMessage[] = "+OK Goodbye!";
	char errmsg[] = "-ERR Unknown command\n";
	char lookFor[] = "\n";
	char emptyString[] = "";
	while (1) {
		bzero(str, 1000);
		if (strstr(infoStruct.readingBuffer, lookFor) != NULL
				&& infoStruct.dataReceptionFlag == false) {
			parseTheReadBuffer(emptyString, comm_fd, &infoStruct);
		}
		read(comm_fd, str, 1000);
		if (!infoStruct.dataReceptionFlag) {
			parseTheReadBuffer(str, comm_fd, &infoStruct);
		} else {
			parseTheDataBuffer(str, comm_fd, &infoStruct);
		}
	}

	return NULL;
}

void howToUse() {
	printf("Usage: -p <port number> -a -v <directory path>\n");
}

void convertToLower(char* str) {
	int i = 0;
	while (str[i]) {
		str[i] = tolower(str[i]);
		i++;
	}
}

bool DoesStringMatch(char* str1, char*str2) {
	if (!strncmp(str1, str2, strlen(str2))) {
		return true;
	}
	return false;
}

int compStringSMTP(char* str) {
	char helo[] = "helo";
	char mail[] = "mail from:";
	char rcpt[] = "rcpt to:";
	char data[] = "data";
	char quit[] = "quit";
	char rset[] = "rset";
	char noop[] = "noop";

	convertToLower(str);

	if (DoesStringMatch(str, helo)) {
		return HELO;
	}
	if (DoesStringMatch(str, mail)) { // fix this
		return MAIL_FROM;
	}
	if (DoesStringMatch(str, rcpt)) {
		return RCPT_TO;
	}
	if (DoesStringMatch(str, data)) {
		return DATA;
	}
	if (DoesStringMatch(str, quit)) {
		return QUIT;
	}
	if (DoesStringMatch(str, rset)) {
		return RSET;
	}
	if (DoesStringMatch(str, noop)) {
		return NOOP;
	} else {
		return INVALID;
	}
}

void INThandler(int sig) {
	char c;
	char str[] = "Server shutting down !";
	signal(sig, SIG_IGN);
	printf("Quit? [y/n] ");
	c = getchar();
	if (c == 'y' || c == 'Y') {
		for (int i = 0; i < indexToFd; i++) {
			write(commfdArray[i], str, strlen(str));
			close(commfdArray[i]);
		}
		for (int i = 0; i < indexToPthread; i++) {
			pthread_kill(thread[i], SIGTERM);
		}
		exit(0);
	} else
		signal(SIGINT, INThandler);
	getchar();
}

void printStrFromIndex(char* str, int index) {
	int len = strlen(str);
	int i;
	for (i = index; i < len; i++) {
		fprintf(stderr, "%c", str[i]);
	}
}
void writeBadSequence(int fd) {
	char seqErrMsg[] = "250 : Invalid sequence of Commands!";
	write(fd, seqErrMsg, strlen(seqErrMsg));
}

void parseCommand(char* str, int comm_fd, perThreadStruct_t* infoStruct) {
	char okString[1000];
	char byeMessage[] = "221 \r\n";
	char errmsg[] = "500 -ERR Unknown command\n";
	char DataMsg[] = "354 Start mail input; end with <CRLF>.<CRLF> \r\n";
	char ackMsg[] = "250 OK\r\n";
	int ret = compStringSMTP(str);
	char parsedAccountName[100];
	bzero(okString, 1000);
	if (ret == QUIT) {
		if (printToStderr)
			fprintf(stderr, "[%d] S: +OK Goodbye!\n", comm_fd);
		write(comm_fd, byeMessage, strlen(byeMessage));
		close(comm_fd);
		if (printToStderr)
			fprintf(stderr, "[%d] Connection closed\n", comm_fd);
		pthread_exit(NULL);
		return;
	} else if (ret == HELO) {
		if (infoStruct->CommandSequence == 0) {
			infoStruct->CommandSequence = HELO;
			write(comm_fd, ackMsg, strlen(ackMsg));
			return;
		} else {
			writeBadSequence(comm_fd);
			return;
		}
	} else if (ret == MAIL_FROM) {
		if (infoStruct->CommandSequence == HELO) {
			infoStruct->CommandSequence = MAIL_FROM;
			write(comm_fd, ackMsg, strlen(ackMsg));
			extractUserName(strstr(str, ":"), parsedAccountName);
			bzero(infoStruct->mailFrom, 100);
			strcpy(infoStruct->mailFrom, parsedAccountName);
			return;
		} else {
			writeBadSequence(comm_fd);
			return;
		}

	} else if (ret == RCPT_TO) {
		if (infoStruct->CommandSequence == MAIL_FROM
				|| infoStruct->CommandSequence == RCPT_TO) {
			infoStruct->CommandSequence = RCPT_TO;
			extractUserName(strstr(str, ":"), parsedAccountName);
			char *recipient = (char*) malloc(sizeof(char) * 100);
			strcpy(recipient, parsedAccountName);
			infoStruct->recipients[infoStruct->clientCount] = recipient;
			/*check if the recipient is one of the accounts */
			if (!(isAccountpresent(recipient, infoStruct, comm_fd))) {
				return;
			}
			write(comm_fd, ackMsg, strlen(ackMsg));
			openMboxFile(recipient, directoryPath, infoStruct);
			infoStruct->clientCount++;
		} else {
			writeBadSequence(comm_fd);
			return;
		}

	} else if (ret == DATA) {
		if (infoStruct->CommandSequence == RCPT_TO) {
			infoStruct->CommandSequence = DATA;
			write(comm_fd, DataMsg, strlen(DataMsg));
			infoStruct->dataReceptionFlag = true;
			return;
		} else {
			writeBadSequence(comm_fd);
			return;
		}
	} else if (ret == RSET) {
		clearInfo(infoStruct);
		write(comm_fd, ackMsg, strlen(ackMsg));
		return;
	} else if (ret == NOOP) {
		write(comm_fd, ackMsg, strlen(ackMsg));
		return;
		//Do Nothing
	} else if (ret == INVALID) {
		fprintf(stderr, "-ERR : Unknown command\n");
		write(comm_fd, errmsg, strlen(errmsg));
		return;
	}
}

void populateAccounts(char* DirName) {
	DIR *dp;
	struct dirent *ep;
	accountCount = 0;
	dp = opendir(directoryPath); // change this to DirName
	char invalidFileName1[] = ".";
	char invalidFileName2[] = "..";
	char temp[256];
	if (dp != NULL) {
		while ((ep = readdir(dp)) != NULL) {
			strcpy(temp, ep->d_name);
			if ((!strcmp(invalidFileName1, temp)
					|| !strcmp(invalidFileName2, temp))) {

			} else {
				strcpy(accounts[accountCount], temp);
				accountCount++;
			}
		}
		(void) closedir(dp);
	} else
		perror("Couldn't open the directory");
}

void clearInfo(perThreadStruct_t* infoStruct) {
	infoStruct->CommandSequence = 0;
	infoStruct->clientCount = 0;
	infoStruct->dataReceptionFlag = false;
	bzero(infoStruct->readingBuffer, 10000);
	bzero(infoStruct->recipients, 100);
	bzero(infoStruct->mailFrom, 100);
	bzero(infoStruct->dataBuffer, 10000);
	bzero(infoStruct->time, 200);

}

int extractUserName(char* str, char* tmp2) {
	char* index1;
	if ((index1 = strstr(str, "<")) == NULL)
		return -1;

	char* index2;
	if ((index2 = strstr(index1, ">")) == NULL)
		return -1;
	bzero(tmp2, 100);
	memcpy(tmp2, index1 + 1, index2 - index1 - 1);
	return 0;
}

bool isAccountpresent(char* buffer, perThreadStruct_t* infoStruct,
		int comm_fd) {
	int i = accountCount - 1;
	char atString[] = "@";
	char recievedRecipient[100];
	char domainName[] = "@localhost";
	char errmsg[] = "551 -ERR Not localhost\r\n";
	char errmsg2[] = "550 -ERR Not a valid User\r\n";

	char* indexOfAt = strstr(buffer, atString);
	if (strncmp(indexOfAt, domainName, strlen(domainName)) != 0) {
		write(comm_fd, errmsg, strlen(errmsg));
		return false;
	}
	char parsedUser[50];
	bzero(parsedUser, 50);
	memcpy(parsedUser, buffer, strlen(buffer) - strlen(indexOfAt));
	strcat(parsedUser, ".mbox");
	while (i >= 0) {
		if (!(strcmp(parsedUser, accounts[i]))) {
			return true;
		}
		i--;
	}
	write(comm_fd, errmsg2, strlen(errmsg2));
	return false;
}
/* Open a mbox file and store the pointer in the infostruct. use this pointer to write data to these files on recieving data command. */
FILE* openMboxFile(char* buffer, char* Dir, perThreadStruct_t* infoStruct) {
	char atString[] = "@";
	char* indexOfAt = strstr(buffer, atString);
	FILE* fp;
	char tmp[100];
	bzero(tmp, 100);
	char nameOfAccount[50];
	bzero(nameOfAccount, 50);
	memcpy(nameOfAccount, buffer, strlen(buffer) - strlen(indexOfAt));
	strcat(nameOfAccount, ".mbox");
	strcat(tmp, Dir);
	strcat(tmp, nameOfAccount);
	if ((fp = fopen(tmp, "a"))) {
		int index = infoStruct->clientCount;
		infoStruct->fp[index] = fp;
	}
	return NULL;
}

void writeToMbox(perThreadStruct_t* infoStruct) {
	char msg[10000];
	bzero(msg, 10000);
	char outstr[200];
	bzero(outstr, 200);
	time_t t;
	struct tm *tmp;

	t = time(NULL);
	tmp = localtime(&t);
	if (tmp == NULL) {
		perror("localtime");
		exit(EXIT_FAILURE);
	}
	strcat(msg, "From <");
	strcat(msg, infoStruct->mailFrom);
	strftime(outstr, sizeof(outstr), "%c", tmp);
	strcat(msg, "> ");
	strcat(msg, outstr);
	strcat(msg, "\n");
	strcat(msg, infoStruct->dataBuffer);
	while (infoStruct->clientCount) {
		FILE* fp = infoStruct->fp[(infoStruct->clientCount) - 1];
		if (fp) {
			fputs(msg, fp);
			fclose(fp);
		}
		infoStruct->clientCount--;
	}
}

void parseTheReadBuffer(char* readString, int comm_fd,
		perThreadStruct_t* infoStruct) {
	char lookFor[] = "\n";
	char* IndexOfBackSlashN = NULL;
	char command[1000];
	bzero(command, 1000);
	char tmp[1000];
	bzero(tmp, 1000);
	strcat(infoStruct->readingBuffer, readString); // adding the read string to the reading buffer.
	if ((IndexOfBackSlashN = strstr(infoStruct->readingBuffer, lookFor)) != NULL) { // if the reading buffer has \n
		memcpy(command, infoStruct->readingBuffer,
				strlen(infoStruct->readingBuffer) - strlen(IndexOfBackSlashN)
						+ 1); // we have received a command.
		memcpy(tmp, IndexOfBackSlashN + 1, strlen(IndexOfBackSlashN + 1));
		bzero(infoStruct->readingBuffer, 10000);
		strcpy(infoStruct->readingBuffer, tmp);
		parseCommand(command, comm_fd, infoStruct);
	} else {
		return;
	}
}

void parseTheDataBuffer(char* readString, int comm_fd,
		perThreadStruct_t* infoStruct) {
	char lookFor[] = "\r\n.\r\n";
	char* IndexOfEndOfData = NULL;
	bzero(infoStruct->dataBuffer, 10000);
	char tmp[1000];
	char ackMsg[] = "250 OK\r\n";
	bzero(tmp, 1000);
	strcat(infoStruct->readingBuffer, readString); // adding the read string to the reading buffer.
	if ((IndexOfEndOfData = strstr(infoStruct->readingBuffer, lookFor)) != NULL) { // if the reading buffer has \n
		memcpy(infoStruct->dataBuffer, infoStruct->readingBuffer,
				strlen(infoStruct->readingBuffer) - strlen(IndexOfEndOfData)
						+ 2); // we have received the data
		memcpy(tmp, IndexOfEndOfData + 5, strlen(IndexOfEndOfData + 5));
		bzero(infoStruct->readingBuffer, 10000);
		strcpy(infoStruct->readingBuffer, tmp);
		infoStruct->dataReceptionFlag = false;
		writeToMbox(infoStruct);
		write(comm_fd, ackMsg, strlen(ackMsg));
		return;
	} else {
		return;
	}
}
