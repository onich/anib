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
#define _ADDRESS 0
#define _PORT 1
#define _NICK 2
#define _USERNAME 3
#define _REAL_NAME 4
#define _CHANNELS 5
#define PARAMS_COUNT 6

char **phrases;
int line_count;

static inline void get_phrases(void)
{
	FILE *phrases_file = fopen("phrases.txt", "r");
	char buff;
	int i = 0, max_line = 0, str_size;
	char *line_buff;

	line_count = 1;
	while (1 == fread(&buff, 1, 1, phrases_file)) {
		if (buff != '\n') {
			++i;
		} else {
			max_line = (i > max_line) ? i : max_line;
			i = 0;
			++line_count;
		}
	}
	fseek(phrases_file, 0, SEEK_SET);
	if (i == 0) {
		--line_count;
	}

	phrases = (char **) malloc(sizeof(char *) * line_count);
	line_buff = (char *) malloc(sizeof(char) * (max_line + 2));

	i = 0;
	while (NULL != fgets(line_buff, max_line + 2, phrases_file)) {
		str_size = strlen(line_buff);
		if (line_buff[str_size - 1] == '\n') {
			line_buff[--str_size] = '\0';
		}

		phrases[i] = (char *) malloc(sizeof(char) * (str_size + 1));
		strcpy(phrases[i++], line_buff);
	}

	free(line_buff);
	fclose(phrases_file);
}

static inline void time_machine(char *middle, char out_msg[MAX_LEN])
{
	int r_num = random() % line_count;

	sprintf(out_msg, "%s :%s", middle, phrases[r_num]);
}

static inline void write_to_log(char *log_str, char *log_name)
{
	char timestamp[MAX_LEN];
	time_t timer = time(NULL);
	struct tm *cur_time = localtime(&timer);
	char log_filename[MAX_LEN];
	FILE *log_file;

	strcpy(log_filename, log_name);
	strcat(log_filename, ".log");
	log_file = fopen(log_filename, "a");

	/* This check is redundant */
	if (log_file == NULL) {
		fprintf(stderr, "Failed to log. No .log file initialized.");
		exit(EXIT_FAILURE);
	}

    strftime(timestamp, MAX_LEN, "%d-%m-%Y %I:%M:%S %p", cur_time);
    fprintf(log_file, "[%s] %s\n", timestamp, log_str);
	printf("%s\n", log_str);
	fclose(log_file);
}

static inline void get_config(char config[PARAMS_COUNT][MAX_LEN])
{
	char cur_string[MAX_LEN];
	char value[MAX_LEN];
	int i = 0;
	FILE *config_file;

	if (NULL == (config_file = fopen("config.txt", "r"))) {
		perror("Failed to read config.txt. get_config():fopen()");
		exit(EXIT_FAILURE);
	}

	while (EOF != fscanf(config_file, "%[^ =] = %[^\n]", value, value)
		   && i < 6) {
		fgetc(config_file);
		strcpy(config[i], value);
		++i;
	}

	if (i < 6) {
		fprintf(stderr, "Not enough options in config.txt. get_config()");
		exit(EXIT_FAILURE);
	}

	fclose(config_file);
}

static inline void read_in_msg(int sockfd, char *in_msg)
{
	int status, i = 0;
	char cur_char;

	while (i < MAX_LEN) {
		status = recv(sockfd, &cur_char, 1, 0);

		if (status <= 0) {
			perror("Connection closed. read_in_msg():recv()");
			exit(EXIT_FAILURE);
		}

		if (cur_char == '\n') {
			in_msg[i - 1] = '\0';
			return;
		}

		in_msg[i] = cur_char;
		++i;
	}

	fprintf(stderr, "in_msg too long. exit() from read_in_msg\n");
	exit(EXIT_FAILURE);
}

static inline void parse_in_msg(char *in_msg, char *prefix, char *command,
								char *params)
{
	prefix[0] = '\0';
	command[0] = '\0';
	params[0] = '\0';
	char *token = strtok(in_msg, " ");

	if (token[0] == ':') {
		strcpy(prefix, token + 1);
		token = strtok(NULL, " ");
	}

	strcpy(command, token);

	token = strtok(NULL, "");
	if (token != NULL)
		strcpy(params, token);
}

static inline void send_out_msg(int sockfd, char *prefix, char *command,
								char *params)
{
	int status;
	char out_msg[MAX_LEN];

	sprintf(out_msg, "%s %s %s\r\n", prefix, command, params);
	status = send(sockfd, out_msg, strlen(out_msg), 0);

	if (status < 0) {
		perror("Failed to send message. send_out_msg():send()");
		exit(EXIT_FAILURE);
	}
}

/* Introduce our bot to the server and connect to channels */
static inline void irc_connect(int sockfd, char config[PARAMS_COUNT][MAX_LEN])
{
	char log_str[MAX_LEN], user_params[MAX_LEN];

	send_out_msg(sockfd, "", "NICK", config[_NICK]);
	sprintf(log_str, "Set nickname \"%s\"", config[_NICK]);
	write_to_log(log_str, "bot");

	sprintf(user_params, "%s 0 * :%s", config[_USERNAME], config[_REAL_NAME]);
	send_out_msg(sockfd, "", "USER", user_params);
	sprintf(log_str, "Set username \"%s\" and real name \"%s\"",
			config[_USERNAME], config[_REAL_NAME]);
	write_to_log(log_str, "bot");

	send_out_msg(sockfd, "", "JOIN", config[_CHANNELS]);
	sprintf(log_str, "Join to channel(s) \"%s\"", config[_CHANNELS]);
	write_to_log(log_str, "bot");
}

int main(int argc, char *argv[])
{
	/* hints is a template for sorting out connections */
	struct addrinfo hints;
	/* results contain all addresses available for connection,
	   given irc_serv_addr and port */
	struct addrinfo *results;
	/* Just iterator for results */
	struct addrinfo *cur_addr;
	/* Our config from file config.txt */
	char config[PARAMS_COUNT][MAX_LEN];
	char user_params[MAX_LEN];
	/* Socket for connection to server */
	int sockfd;
	/* Stores a message from IRC server */
	char in_msg[MAX_LEN];
	/* Stores parsed components of in_msg */
	char prefix[MAX_LEN], command[MAX_LEN], params[MAX_LEN];
	char *nickname, *middle, *trailing;
	char log_str[MAX_LEN];
	char out_msg[MAX_LEN];
	int status;

	get_config(config);

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET; 	/* Allow IPv4 only */
	hints.ai_socktype = SOCK_STREAM; 	/* TCP connection */
	hints.ai_flags = 0;
	hints.ai_protocol = 0; 	/* Any protocol */

	/* Get list of adresses corresponding our hints template */
	if (0 != (status = getaddrinfo(config[_ADDRESS], config[_PORT],
								   &hints, &results))
	) {
		fprintf(stderr,
				"Failed to get address info. main():getaddrinfo(): %s\n",
				gai_strerror(status));
		exit(EXIT_FAILURE);
	}

	/* Try each address in results for connection. */
	for (cur_addr = results;
		 cur_addr != NULL;
		 cur_addr = cur_addr->ai_next
	) {
		sockfd = socket(cur_addr->ai_family,
						cur_addr->ai_socktype,
						cur_addr->ai_protocol);
		if (sockfd == -1)
			continue;

		if (-1 != connect(sockfd, cur_addr->ai_addr, cur_addr->ai_addrlen))
			break; 	/* Successfull connection */

		close(sockfd);
	}
	freeaddrinfo(results);

	if (cur_addr == NULL) { 	/* Failed to connect */
		perror("Could not connect. main():connect()");
		exit(EXIT_FAILURE);
	}

	sprintf(log_str, "Connected to %s:%s", config[_ADDRESS], config[_PORT]);
	write_to_log(log_str, "bot");

	irc_connect(sockfd, config);
	get_phrases();

	while (1){
		read_in_msg(sockfd, in_msg);
		parse_in_msg(in_msg, prefix, command, params);
		nickname = (NULL == strchr(prefix, '!'))? ""
				   : (strtok(prefix, "!"));
		if (NULL == (trailing = strchr(params, ':'))) {
			trailing = "";
			middle = params;
		} else if (trailing == params) {
			++trailing;
			middle = "";
		} else {
			*(trailing - 1) = '\0';
			++trailing;
			middle = params;
		}

		if (!strcmp(command, "PING")) {
			send_out_msg(sockfd, "", "PONG", trailing);
			sprintf(log_str, "Send PONG to \"%s\"", trailing);
			write_to_log(log_str, "bot");
		} else if (!strcmp(command, "PRIVMSG")) {
			sprintf(log_str, "%s: %s", nickname, trailing);
			write_to_log(log_str, middle);
			if (NULL != strstr(trailing, config[_NICK])) {
				time_machine(middle, out_msg);
				send_out_msg(sockfd, "", "PRIVMSG", out_msg);
			}
		} else if (!strcmp(command, "JOIN")) {
			sprintf(log_str, "%s joined channel", nickname);
			write_to_log(log_str, middle);
		} else if (!strcmp(command, "PART")) {
			sprintf(log_str, "%s left channel", nickname);
			write_to_log(log_str, middle);
		}
	}
}
