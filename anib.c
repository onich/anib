#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <time.h>

#define MAX_LEN 513
#define CONF_COUNT 6

static struct irc_conf {
	char address[MAX_LEN];
	char port[MAX_LEN];
	char nickname[MAX_LEN];
	char username[MAX_LEN];
	char realname[MAX_LEN];
	char channels[MAX_LEN];
} config;

struct irc_msg {
	char prefix[MAX_LEN];
	char command[MAX_LEN];
	char params[MAX_LEN];
	char nickname[MAX_LEN];
	char middle[MAX_LEN];
	char trailing[MAX_LEN];
};

static int sockfd;
static char **phrases;
static int ln_count = 1;

static void write_to_log(char *log_str, char *log_name)
{
	/* Things to check the time */
	char timestamp[MAX_LEN];
	time_t timer = time(NULL);
	struct tm *cur_time = localtime(&timer);
	char log_filename[MAX_LEN];
	FILE *log_file;

	strcpy(log_filename, log_name);
	strcat(log_filename, ".log");
	log_file = fopen(log_filename, "a");

    strftime(timestamp, MAX_LEN, "%d-%m-%Y %I:%M:%S %p", cur_time);
    fprintf(log_file, "[%s] %s\n", timestamp, log_str);
	/* May be removed if log to console isn't needed */
	printf("%s\n", log_str);

	fclose(log_file);
}

/* Sends string to server.
   Maybe should be rewrited to use sctruct irc_msg */
static void send_msg(char *msg)
{
	char crlf_str[] = "\r\n";

	memcpy(msg + strlen(msg), crlf_str, 3);
	int status = send(sockfd, msg, strlen(msg), 0);

	if (status < 0) {
		perror("Failed to send message. send_msg(): send()");
		exit(EXIT_FAILURE);
	}
}

static void some_magic(struct irc_msg *in_msg)
{
	int r_num = random() % ln_count;
	char msg[MAX_LEN], log_str[MAX_LEN];

	sprintf(msg, "PRIVMSG %s :%s", in_msg->middle, phrases[r_num]);
	send_msg(msg);

	sprintf(log_str, "%s: %s", config.nickname, phrases[r_num]);
	write_to_log(log_str, in_msg->middle);
}

static void parse_msg(char *recvd_str, struct irc_msg *in_msg)
{
	char buff_str[MAX_LEN];
	char *token = strtok(recvd_str, " ");

	memset(in_msg, 0, sizeof(*in_msg));

	if (token[0] == ':') {
		strcpy(in_msg->prefix, token + 1);
		token = strtok(NULL, " ");
	}

	strcpy(in_msg->command, token);

	token = strtok(NULL, "");
	if (token != NULL)
		strcpy(in_msg->params, token);

	strcpy(buff_str, in_msg->prefix);
	if (NULL != strchr(buff_str, '!')) {
		strtok(buff_str, "!");
	}
	strcpy(in_msg->nickname, buff_str);

	strcpy(buff_str, in_msg->params);
	if (buff_str[0] == ':') {
		strcpy(in_msg->trailing, buff_str + 1);
	}
	else {
		if (NULL != (token = strstr(buff_str, " :"))) {
			token[0] = 0;
			token += 2;
			strcpy(in_msg->trailing, token);
		}
		strcpy(in_msg->middle, buff_str);
	}
}

static void get_msg(struct irc_msg *in_msg)
{
	char recvd_str[MAX_LEN];
	char recvd_char;
	int status, i = 0;

	while (i < MAX_LEN) {
		if (0 >= (status = recv(sockfd, &recvd_char, 1, 0))) {
			perror("Connection closed. get_msg():recv()");
			exit(EXIT_FAILURE);
		}

		if (recvd_char == '\n') {
			recvd_str[i - 1] = '\0';
			parse_msg(recvd_str, in_msg);
			return;
		}

		recvd_str[i] = recvd_char;
		++i;
	}

	fprintf(stderr, "recvd_str too long. exit() from get_msg()\n");
	exit(EXIT_FAILURE);
}

static void get_phrases()
{
	char buff;
	char buff_str[MAX_LEN];
	int i = 0, len;
	FILE *phrs_file;

	if (NULL == (phrs_file = fopen("phrases.txt", "r"))) {
		perror("Failed to read phrases.txt. get_phrases(): fopen()");
		exit(EXIT_FAILURE);
	}

	while (EOF != (buff = fgetc(phrs_file))) {
		if (buff != '\n')
			++i;
		else {
			if (i > MAX_LEN - 1) {
				fprintf(stderr,
						"Phrase %i is too long. exit() from get_phrases()\n",
						ln_count);
				exit(EXIT_FAILURE);
			}
			++ln_count;
			i = 0;
		}
	}
	if (i == 0)
		--ln_count;

	phrases = (char **)malloc(sizeof(char *) * ln_count);

	i = 0;
	fseek(phrs_file, 0, SEEK_SET);
	while (NULL != fgets(buff_str, MAX_LEN, phrs_file)) {
		strtok(buff_str, "\n");
		phrases[i] = (char *)malloc(strlen(buff_str) + 1);
		strcpy(phrases[i++], buff_str);
	}

	fclose(phrs_file);
}

/* Introduce our bot to the server and connect to channel(s) */
static void irc_connect()
{
	char msg[MAX_LEN], log_str[MAX_LEN];

	sprintf(msg, "NICK %s", config.nickname);
	send_msg(msg);
	sprintf(log_str, "Set nickname to \"%s\"", config.nickname);
	write_to_log(log_str, "bot");

	sprintf(msg, "USER %s 0 * :%s", config.username, config.realname);
	send_msg(msg);
	sprintf(log_str, "Set username to \"%s\" and real name to \"%s\"",
			config.username, config.realname);
	write_to_log(log_str, "bot");

	sprintf(msg, "JOIN %s", config.channels);
	send_msg(msg);
	sprintf(log_str, "Join to channel(s) \"%s\"", config.channels);
	write_to_log(log_str, "bot");
}

static void server_connect() {
	struct addrinfo hints;
	struct addrinfo *results, *res_i;
	int status;
	char log_str[MAX_LEN];

	/* hints is a template for sorting out connections */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET; 	/* Allow IPv4 only */
	hints.ai_socktype = SOCK_STREAM; 	/* TCP connection */
	hints.ai_flags = 0;
	hints.ai_protocol = 0; 	/* Any protocol */

	/* Get list of adresses corresponding to our hints template */
	if (0 != (status = getaddrinfo(config.address, config.port,
								   &hints, &results)))
	{
		fprintf(stderr,
				"Failed to get address info."
				"server_connect(): getaddrinfo(): %s\n",
				gai_strerror(status));
		exit(EXIT_FAILURE);
	}

	/* Try each address in results for connection. */
	for (res_i = results;
		 res_i != NULL;
		 res_i = res_i->ai_next)
	{
		sockfd = socket(res_i->ai_family,
						res_i->ai_socktype,
						res_i->ai_protocol);
		if (sockfd == -1)
			continue;

		if (-1 != connect(sockfd, res_i->ai_addr, res_i->ai_addrlen))
			break; 	/* Successfull connection */

		close(sockfd);
	}

	/* Check if connected */
	if (res_i == NULL) {
		perror("Could not connect. main(): connect()");
		exit(EXIT_FAILURE);
	}

	sprintf(log_str, "Connected to %s:%s", config.address, config.port);
	write_to_log(log_str, "bot");

	freeaddrinfo(results);
}

static void get_config()
{
	char value[MAX_LEN];
	char read_conf[CONF_COUNT][MAX_LEN];
	int i = 0;
	FILE *conf_file;

	if (NULL == (conf_file = fopen("config.txt", "r"))) {
		perror("Failed to read config.txt. get_config(): fopen()");
		exit(EXIT_FAILURE);
	}

	/* Assume that config file structured right
	   TODO: more precise check*/
	while (EOF != fscanf(conf_file, "%[^ =] = %[^\n]", value, value)
		   && i < CONF_COUNT)
	{
		strcpy(read_conf[i], value);
		fgetc(conf_file);
		++i;
	}

	/* I don't know how to do it with more fashion */
	memset(&config, 0, sizeof(config));
	strcpy(config.address, read_conf[0]);
	strcpy(config.port, read_conf[1]);
	strcpy(config.nickname, read_conf[2]);
	strcpy(config.username, read_conf[3]);
	strcpy(config.realname, read_conf[4]);
	strcpy(config.channels, read_conf[5]);

	fclose(conf_file);
}

int main(int argc, char **argv)
{
	struct irc_msg in_msg;
	char msg[MAX_LEN], log_str[MAX_LEN];

	get_config();
	server_connect();
	irc_connect();
	get_phrases();
	srandom(time(NULL));

	while (1) {
		get_msg(&in_msg);

		if (!strcmp(in_msg.command, "PING")) {
			sprintf(msg, "PONG %s", in_msg.trailing);
			send_msg(msg);
			sprintf(log_str, "Send PONG to \"%s\"", in_msg.trailing);
			write_to_log(log_str, "bot");
		}
		else if (!strcmp(in_msg.command, "PRIVMSG")) {
			sprintf(log_str, "%s: %s", in_msg.nickname, in_msg.trailing);
			write_to_log(log_str, in_msg.middle);

			if (NULL != strstr(in_msg.trailing, config.nickname))
				some_magic(&in_msg);
		}
		else if (!strcmp(in_msg.command, "JOIN")) {
			sprintf(log_str, "%s joined channel", in_msg.nickname);
			write_to_log(log_str, in_msg.middle);
		}
		else if (!strcmp(in_msg.command, "PART")) {
			sprintf(log_str, "%s left channel", in_msg.nickname);
			write_to_log(log_str, in_msg.middle);
		}
	}
}
