/*Headers*/

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <dirent.h>
#include<string.h>
#include <stdlib.h>
#include <signal.h>
#include <getopt.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <openssl/md5.h>
#include <ctype.h>

bool printToStderr = false;

int commfdArray[100];
int indexToFd;
int indexToPthread;
char readingBuffer[1000];
pthread_t thread[100];
int accountCount;
char accounts[1000][1000];
char directoryPath[100];

enum Commands {
	USER, PASS, STAT, UIDL, RETR, DELE, QUIT, LIST, RSET, NOOP, INVALID
};

typedef struct mailStruct {
	int messageLength;
	bool markedForDelete;
	char UIDL[1000];
} mailStruct_t;

typedef struct accountStruct {
	char username[100];
	int numberOfMessages;
	int sizeOfMessages;
	int MarkedCount;
} accountStruct_t;

typedef struct perThreadStruct {
	FILE* fp;
	char readingBuffer[10000];
	int CommandSequence;
	accountStruct_t account;
} perThreadStruct_t;

void computeDigest(char *data, int dataLengthBytes,
		unsigned char *digestBuffer);
void howToUse();
int compStringPOP3(char* str);
void parseCommand(char* str, int comm_fd, perThreadStruct_t* infoStruct,
		mailStruct_t* mails);
bool DoesStringMatch(char* str1, char*str2);
void parseTheReadBuffer(char* readString, int comm_fd,
		perThreadStruct_t* infoStruct, mailStruct_t* mails);
void INThandler(int sig);
void* worker(void * arg);
bool isAccountpresent(char* buffer, perThreadStruct_t* infoStruct, int comm_fd);
void populateAccounts(char* DirName);
int extractUserName(char* str, char* tmp2);
void printMailstruct(mailStruct_t* mails, int msgCount);
void updateMailBoxStats(mailStruct_t* mailsRecieve,
		perThreadStruct_t* infoStruct);
void computeStats(mailStruct_t* mails, perThreadStruct_t* infoStruct,
		int comm_fd);
void retrieveMail(int msgID, perThreadStruct_t* infoStruct, mailStruct_t* mails,
		int comm_fd);
void markedForDelete(int msgID, perThreadStruct_t* infoStruct,
		mailStruct_t* mails, int comm_fd);
void listMessages(int msgID, perThreadStruct_t* infoStruct, mailStruct_t* mails,
		int comm_fd);
void resetCommand(perThreadStruct_t* infoStruct, mailStruct_t* mails,
		int comm_fd);

void commitOnQuit(perThreadStruct_t* infoStruct, mailStruct_t* mails);
void computeUIDL(perThreadStruct_t* infoStruct, mailStruct_t* mails);
void printUIDLforAll(int msgID, perThreadStruct_t* infoStruct,
		mailStruct_t* mails, int comm_fd);
int main(int argc, char *argv[]) {

	char str[100];
	char welcomeMessage[] = "+OK POP3 ready [localhost]\r\n";
	int listen_fd = 0;
	int option = 0;
	int port;
	struct sockaddr_in servaddr;
	int comm_fd = -1;

	port = 11000; //Default value for port

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
	populateAccounts(directoryPath);
	listen_fd = socket(AF_INET, SOCK_STREAM, 0);

	bzero(&servaddr, sizeof(servaddr));

	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htons(INADDR_ANY);
	servaddr.sin_port = htons(port);

	bind(listen_fd, (struct sockaddr *) &servaddr, sizeof(servaddr)); //Binding the socket fd (listen_fd) to the server address.
	listen(listen_fd, 1000); //Allowing a maximum of 1000 conenctions for now.
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
	infoStruct.CommandSequence = 0;
	mailStruct_t mails[1000];

	char strClose[] = "Server shutting down !";
	commfdArray[indexToFd] = comm_fd; // May be a lock for the two operations ?
	indexToFd++;
	char okString[1000];
	char byeMessage[] = "+OK Goodbye!";
	char errmsg[] = "-ERR\r\n";
	char lookFor[] = "\n";
	char emptyString[] = "";
	while (1) {
		bzero(str, 1000);
		if (strstr(infoStruct.readingBuffer, lookFor) != NULL) {
			parseTheReadBuffer(emptyString, comm_fd, &infoStruct, mails);
		}
		read(comm_fd, str, 1000);
		parseTheReadBuffer(str, comm_fd, &infoStruct, mails);
	}

	return NULL;
}
void howToUse() {
	printf("Usage: -p <port number> -a -v <path to directory>\n");
}

void computeDigest(char *data, int dataLengthBytes,
		unsigned char *digestBuffer) {
	/* The digest will be written to digestBuffer, which must be at least MD5_DIGEST_LENGTH bytes long */

	MD5_CTX c;
	MD5_Init(&c);
	MD5_Update(&c, data, dataLengthBytes);
	MD5_Final(digestBuffer, &c);
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
void parseTheReadBuffer(char* readString, int comm_fd,
		perThreadStruct_t* infoStruct, mailStruct_t* mails) {
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
		parseCommand(command, comm_fd, infoStruct, mails);
	} else {
		return;
	}
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

int compStringPOP3(char* str) {
	char user[] = "user";
	char pass[] = "pass";
	char stat[] = "stat";
	char uidl[] = "uidl";
	char retr[] = "retr";
	char dele[] = "dele";
	char quit[] = "quit";
	char list[] = "list";
	char rset[] = "rset";
	char noop[] = "noop";
	convertToLower(str);

	if (DoesStringMatch(str, user)) {
		return USER;
	}
	if (DoesStringMatch(str, pass)) {
		return PASS;
	}
	if (DoesStringMatch(str, stat)) {
		return STAT;
	}
	if (DoesStringMatch(str, uidl)) {
		return UIDL;
	}
	if (DoesStringMatch(str, retr)) {
		return RETR;
	}
	if (DoesStringMatch(str, dele)) {
		return DELE;
	}
	if (DoesStringMatch(str, quit)) {
		return QUIT;
	}
	if (DoesStringMatch(str, list)) {
		return LIST;
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

void parseCommand(char* str, int comm_fd, perThreadStruct_t* infoStruct,
		mailStruct_t* mails) {
	char byeMessage[] = "+OK Goodbye!\r\n";
	char errmsg[] = "-ERR \r\n";
	char ackMsg[] = "+OK\r\n";
	char passwd[] = " cis505";
	int ret = compStringPOP3(str);
	char parsedAccountName[100];
	bzero(parsedAccountName, 100);
	char* tmp = NULL;
	int msgID = 0;
	if (ret == USER) {
		if ((tmp = strstr(str, " ")) == NULL) {
			write(comm_fd, errmsg, strlen(errmsg));
		}
		extractUserName(str, parsedAccountName);
		parsedAccountName[strlen(parsedAccountName) - 1] = '\0';
		strcat(parsedAccountName, ".mbox");
		if (isAccountpresent(parsedAccountName, infoStruct, comm_fd)) {
			write(comm_fd, ackMsg, strlen(ackMsg));
			infoStruct->CommandSequence = USER;
			bzero(infoStruct->account.username, 100);
			strcpy(infoStruct->account.username, parsedAccountName);
			return;
		}

	} else if (ret == PASS) {
		if (infoStruct->CommandSequence == USER) {
			if ((tmp = strstr(str, " ")) != NULL) {
				if (strncmp(tmp, passwd, strlen(passwd)) == 0) {
					write(comm_fd, ackMsg, strlen(ackMsg));
					infoStruct->CommandSequence = PASS;
					updateMailBoxStats(mails, infoStruct);
					computeUIDL(infoStruct, mails);
				}
			} else {
				write(comm_fd, errmsg, strlen(errmsg));
			}
		} else {
			write(comm_fd, errmsg, strlen(errmsg));
		}
	} else if (ret == STAT) {
		if (infoStruct->CommandSequence == PASS) {
			computeStats(mails, infoStruct, comm_fd);
		}

	} else if (ret == UIDL) {
//		computeUIDL(infoStruct, mails);
		if ((tmp = strstr(str, " ")) == NULL) {
			if (strlen(str) == 6) {
				msgID = 0;
				printUIDLforAll(msgID, infoStruct, mails, comm_fd);
			} else {
				write(comm_fd, errmsg, strlen(errmsg));
			}
		} else {
			msgID = atoi((tmp + 1));
			if (msgID <= infoStruct->account.numberOfMessages) {
				printUIDLforAll(msgID, infoStruct, mails, comm_fd);
			} else {
				write(comm_fd, errmsg, strlen(errmsg));
			}
		}

	} else if (ret == RETR) {
		if ((tmp = strstr(str, " ")) == NULL) {
			write(comm_fd, errmsg, strlen(errmsg));
		} else {
			msgID = atoi((tmp + 1));
			if (msgID <= infoStruct->account.numberOfMessages) {
				retrieveMail(msgID, infoStruct, mails, comm_fd);
			} else {
				write(comm_fd, errmsg, strlen(errmsg));
			}
		}
	} else if (ret == DELE) {
		if ((tmp = strstr(str, " ")) == NULL) {
			write(comm_fd, errmsg, strlen(errmsg));
		} else {
			msgID = atoi((tmp + 1));
			if (msgID <= infoStruct->account.numberOfMessages) {
				markedForDelete(msgID, infoStruct, mails, comm_fd);
			} else {
				write(comm_fd, errmsg, strlen(errmsg));
			}
		}
		updateMailBoxStats(mails, infoStruct);
	} else if (ret == QUIT) {
		commitOnQuit(infoStruct, mails);
		if (printToStderr)
			fprintf(stderr, "[%d] S: +OK Goodbye!\n", comm_fd);
		write(comm_fd, byeMessage, strlen(byeMessage));
		close(comm_fd);
		if (printToStderr)
			fprintf(stderr, "[%d] Connection closed\n", comm_fd);
		pthread_exit(NULL);
		return;
	} else if (ret == LIST) {
		if ((tmp = strstr(str, " ")) == NULL) {
			if (strlen(str) == 6) {
				msgID = 0;
				listMessages(msgID, infoStruct, mails, comm_fd);
			} else {
				write(comm_fd, errmsg, strlen(errmsg));
			}
		} else {
			msgID = atoi((tmp + 1));
			if (msgID <= infoStruct->account.numberOfMessages) {
				listMessages(msgID, infoStruct, mails, comm_fd);
			} else {
				write(comm_fd, errmsg, strlen(errmsg));
			}
		}
	} else if (ret == RSET) {
		resetCommand(infoStruct, mails, comm_fd);
		updateMailBoxStats(mails, infoStruct);
	} else if (ret == NOOP) {
		write(comm_fd, ackMsg, strlen(ackMsg));
	} else if (ret == INVALID) {
		fprintf(stderr, "-ERR : Unknown command\n");
		write(comm_fd, errmsg, strlen(errmsg));
		return;
	}
}

bool isAccountpresent(char* parsedUser, perThreadStruct_t* infoStruct,
		int comm_fd) {
	int i = accountCount - 1;
	char errmsg[] = "-ERR\n";
	while (i >= 0) {
		if (!(strcmp(parsedUser, accounts[i]))) {
			return true;
		}
		i--;
	}
	write(comm_fd, errmsg, strlen(errmsg));
	return false;
}

int extractUserName(char* str, char* tmp2) {
	char* index1;
	if ((index1 = strstr(str, "er ")) == NULL)
		return -1;

	char* index2;
	if ((index2 = strstr(index1, "\n")) == NULL)
		return -1;
	bzero(tmp2, 100);
	memcpy(tmp2, index1 + 3, index2 - index1 - 3);
	return 0;
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

void markedForDelete(int msgID, perThreadStruct_t* infoStruct,
		mailStruct_t* mails, int comm_fd) {
	char file_name[256];
	sprintf(file_name, "%s%s", directoryPath, infoStruct->account.username);
	char buf[512];
	int temp = 0;
	int messageCount = 0;
	int mID = 1;
	FILE *fp = fopen(file_name, "r");
	int i = 0;
	bool copyMessage = false;
	bool copied = false;
	char retrMsg[1000];
	bzero(retrMsg, 1000);
	if (!fp) {
		exit(0);
	}
	while (fgets(buf, sizeof buf, fp)) {
		if ((temp = strncmp(buf, "From <", 6)) == 0) {
			msgID--;
			if (msgID == 0) {
				if ((&mails[i])->markedForDelete == false) {
					(&mails[i])->markedForDelete = true;
					write(comm_fd, "+OK\r\n", 5);
				} else {
					write(comm_fd, "-ERR\r\n", 6);
				}
			}
			i++;
		}

	}
}

void updateMailBoxStats(mailStruct_t* mailsRecieve,
		perThreadStruct_t* infoStruct) {
	char file_name[256];
	sprintf(file_name, "%s%s", directoryPath, infoStruct->account.username);
	char buf[512];
	int temp = 0;
	int messageCount = 0;
	int mID = 1;
	mailStruct_t* mails = mailsRecieve;
	int markedCount = 0;
	FILE *fp = fopen(file_name, "r");
	if (!fp) {
		exit(0);
	}
	while (fgets(buf, sizeof buf, fp)) {
		if ((temp = strncmp(buf, "From <", 6)) == 0) {
			messageCount++;
			if ((&mails[messageCount - 1])->markedForDelete == true) {
				markedCount++;
			}
			mails = &mailsRecieve[messageCount - 1];
			mails->messageLength = 0;
		} else {
			mails->messageLength += strlen(buf);
		}
	}
	infoStruct->account.numberOfMessages = messageCount;
	infoStruct->account.MarkedCount += markedCount;
	fclose(fp);
}

void printMailstruct(mailStruct_t* mails, int msgCount) {
	int i = 0;
	while (i < msgCount) {
		mailStruct_t* tmp = &mails[i];
		printf("length is : %d, marked for delete is :%d\n", tmp->messageLength,
				tmp->markedForDelete);
		i++;
	}
}

void computeStats(mailStruct_t* mails, perThreadStruct_t* infoStruct,
		int comm_fd) {
	int i = 0;
	int totalSize = 0;
	char statMsg[100];
	while (i < infoStruct->account.numberOfMessages) {
		if ((&mails[i])->markedForDelete == false) {
			totalSize += (&mails[i])->messageLength;
		}
		i++;
	}
	sprintf(statMsg, "+OK %d %d\r\n",
			infoStruct->account.numberOfMessages
					- infoStruct->account.MarkedCount, totalSize);
	write(comm_fd, statMsg, strlen(statMsg));
}

void printUIDLforAll(int msgID, perThreadStruct_t* infoStruct,
		mailStruct_t* mails, int comm_fd) {
	int count = infoStruct->account.numberOfMessages;
	int i = 0;
	char msg[1000];
	if (msgID > 0) {
		if ((&mails[msgID - 1])->markedForDelete == false) {
			sprintf(msg, "+OK %d %s\r\n", msgID, (&mails[msgID - 1])->UIDL);
			write(comm_fd, msg, strlen(msg));
		} else {
			write(comm_fd, "-ERR\r\n", 6);
		}
	} else {

		sprintf(msg, "+OK\r\n");
		write(comm_fd, msg, strlen(msg));
		bzero(msg, 100);
		while (i < count) {
			if ((&mails[i])->markedForDelete == false) {
				bzero(msg, 100);
				sprintf(msg, "%d %s\r\n", i + 1, (&mails[i])->UIDL);
				write(comm_fd, msg, strlen(msg));
			}
			i++;
		}
		write(comm_fd, "\r\n.\r\n", 5);
	}
}

void listMessages(int msgID, perThreadStruct_t* infoStruct, mailStruct_t* mails,
		int comm_fd) {
	int count = infoStruct->account.numberOfMessages;
	int i = 0;
	char msg[100];
	if (msgID > 0) {
		if ((&mails[msgID - 1])->markedForDelete == false) {
			sprintf(msg, "+OK %d %d\r\n", msgID,
					(&mails[msgID - 1])->messageLength);
			write(comm_fd, msg, strlen(msg));
		} else {
			write(comm_fd, "-ERR\r\n", 6);
		}
	} else {
		sprintf(msg, "+OK %d\r\n",
				infoStruct->account.numberOfMessages
						- infoStruct->account.MarkedCount);
		write(comm_fd, msg, strlen(msg));
		bzero(msg, 100);
		while (i < count) {
			if ((&mails[i])->markedForDelete == false) {
				bzero(msg, 100);
				sprintf(msg, "%d %d\r\n", i + 1, (&mails[i])->messageLength);
				write(comm_fd, msg, strlen(msg));
			}
			i++;
		}
		write(comm_fd, "\r\n.\r\n", 5);
	}
}

void resetCommand(perThreadStruct_t* infoStruct, mailStruct_t* mails,
		int comm_fd) {
	int i = 0;
	int count = infoStruct->account.numberOfMessages;
	while (i < count) {
		(&mails[i])->markedForDelete = false;
		i++;
	}
	printMailstruct(mails, infoStruct->account.numberOfMessages);
	infoStruct->account.MarkedCount = 0;
	write(comm_fd, "+OK\r\n", 5);
}

void retrieveMail(int msgID, perThreadStruct_t* infoStruct, mailStruct_t* mails,
		int comm_fd) {
	char file_name[256];
	sprintf(file_name, "%s%s", directoryPath, infoStruct->account.username);
	char buf[2000];
	int temp = 0;
	int messageCount = 0;
	int mID = 1;
	FILE *fp = fopen(file_name, "r");
	int i = 0;
	bool copyMessage = false;
	bool copied = false;
	char retrMsg[1000];
	bzero(retrMsg, 1000);
	if (!fp) {
		exit(0);
	}
	while (fgets(buf, sizeof buf, fp)) {
		if ((temp = strncmp(buf, "From <", 6)) == 0) {
//			if ((&mails[i])->markedForDelete == false){
			msgID--;
//			}

			if (msgID == 0) {
				if ((&mails[i])->markedForDelete == false) {
					copyMessage = true;
				} else {
					write(comm_fd, "-ERR\r\n", 6);
				}
			} else if (msgID < 0) {
				break;
			}
			i++;
		} else {
			if (copyMessage) {
				strcat(retrMsg, buf);
				copied = true;
			}
		}
	}
	if (copied) {
		write(comm_fd, "+OK\n", 4);
		write(comm_fd, retrMsg, strlen(retrMsg));
		write(comm_fd, ".\r\n", 3);
	}
}
void convertToHex(char* digestBuffer, char* hexDigestBuffer) {
	int i, len;

	len = strlen(digestBuffer);

	for (i = 0; i < len; i++) {
		sprintf(hexDigestBuffer + i * 2, "%02X", digestBuffer[i]);
	}
	return;
}

void computeUIDL(perThreadStruct_t* infoStruct, mailStruct_t* mails) {
	char file_name[256];
	sprintf(file_name, "%s%s", directoryPath, infoStruct->account.username);
	char buf[2000];
	int temp = 0;
	int messageCount = 0;
	FILE *fp = fopen(file_name, "r");
	int i = 0;
	int msgID = 0;
	bool copyMessage = false;
	bool copied = false;
	char retrMsg[1000];
	unsigned char digestBuffer[100];
	char hexDigestBuffer[100];

	bzero(digestBuffer, 100);
	bzero(retrMsg, 1000);
	if (!fp) {
		exit(0);
	}
	while (fgets(buf, sizeof buf, fp)) {
		if ((temp = strncmp(buf, "From <", 6)) == 0) {
			if (copyMessage) {
				computeDigest(retrMsg, (&mails[msgID - 1])->messageLength,
						digestBuffer);
				convertToHex((char *) digestBuffer, hexDigestBuffer);
				strcpy((&mails[i - 1])->UIDL, hexDigestBuffer);
				bzero(hexDigestBuffer, 33);
				bzero(retrMsg, 1000);
			}
			msgID++;
			copyMessage = true;
			i++;
		} else {
			if (copyMessage) {
				strcat(retrMsg, buf);
				copied = true;
			}
		}
	}
	if (copyMessage) {
		computeDigest(retrMsg, (&mails[msgID - 1])->messageLength,
				digestBuffer);
		convertToHex((char *) digestBuffer, hexDigestBuffer);
		strcpy((&mails[i - 1])->UIDL, hexDigestBuffer);
		bzero(hexDigestBuffer, 33);
		bzero(retrMsg, 1000);
	}
	if (copied) {
	}
}

void commitOnQuit(perThreadStruct_t* infoStruct, mailStruct_t* mails) {
	char file_name[256];
	sprintf(file_name, "%s%s", directoryPath, infoStruct->account.username);
	char tmp[100];
	sprintf(tmp, "%s%s", directoryPath, "tmp.mbox");
	FILE* fp;
	FILE* tmpfp;
	char buf[2000];
	int temp = 0;
	int msgID = 0;
	bool copyMessage = false;
	bool copied = false;
	char retrMsg[10000];
	bzero(retrMsg, 10000);
	fp = fopen(file_name, "r");
	tmpfp = fopen(tmp, "a");

	if (!fp || !tmpfp) {
		exit(0);
	}
	while (fgets(buf, sizeof buf, fp)) {
		if ((temp = strncmp(buf, "From <", 6)) == 0) {
			msgID++;
			if ((&mails[msgID - 1])->markedForDelete == false) {
				copyMessage = true;
				strcat(retrMsg, buf);
			} else {

				copyMessage = false;
				continue;
			}
		} else {
			if (copyMessage) {
				strcat(retrMsg, buf);
				copied = true;
			}
		}
	}
	fprintf(tmpfp, "%s", retrMsg);
	fclose(fp);
	remove(file_name);
	rename(tmp, file_name);
	fclose(tmpfp);
}
