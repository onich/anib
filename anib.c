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
#define CONF_ADDR 0
#define CONF_PORT 1
#define CONF_NICK 2
#define CONF_USRNM 3
#define CONF_RLNM 4
#define CONF_CHNS 5

#define COMPS_COUNT 6
#define COMPS_PREF
#define COMPS_CMD
#define COMPS_PARS
#define COMPS_NICK
#define COMPS_MID
#define COMPS_TRAIL

static char **phrases;
static int line_count;

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

static inline void time_machine(char out_trailing[MAX_LEN])
{
	int r_num = random() % line_count;

	strcpy(out_trailing, phrases[r_num]);
}

static inline void write_to_log(char *log_str, char *log_name)
{
	/* Things to check the time */
	char timestamp[MAX_LEN];
	time_t timer = time(NULL);
	struct tm *cur_time = localtime(&timer);
	/* Where to log */
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

static inline void get_config(char config[CONF_COUNT][MAX_LEN])
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

static inline void get_msg(int sockfd, char in_msg[COMPS_COUNT][MAX_LEN])
{
	char recvd_msg[MAX_LEN];
	char recvd_char;
	int status, i = 0;

	while (i < MAX_LEN) {
		if (0 >= (status = recv(sockfd, &recvd_char, 1, 0))) {
			perror("Connection closed. get_msg():recv()");
			exit(EXIT_FAILURE);
		}

		if (recvd_char == '\n') {
			recvd_msg[i - 1] = '\0';
			parse_msg(recvd_msg, in_msg);
			return;
		}

		recvd_msg[i] = recvd_char;
		++i;
	}

	fprintf(stderr, "recvd_msg too long. exit() from get_msg\n");
	exit(EXIT_FAILURE);
}

static inline void parse_msg(char *recvd_msg, char in_msg[COMPS_COUNT][MAX_LEN])
{
	char *token = strtok(recvd_msg, " ");

	if (token[0] == ':') {
		strcpy(in_msg[COMPS_PREF], token + 1);
		token = strtok(NULL, " ");
	}

	strcpy(in_msg[COMPS_CMD], token);

	token = strtok(NULL, "");
	if (token != NULL)
		strcpy(in_msg[COMPS_PARS], token);

	nickname = (NULL == strchr(in_msg[COMPS_PREF], '!'))? ""
			   : (strtok(in_msg[COMPS_PREF], "!"));
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

/* Introduce our bot to the server and connect to channel(s) */
static inline void irc_connect(int sockfd, char config[CONF_COUNT][MAX_LEN])
{
	char log_str[MAX_LEN], user_params[MAX_LEN];

	send_out_msg(sockfd, "", "NICK", config[CONF_NICK]);
	sprintf(log_str, "Set nickname \"%s\"", config[CONF_NICK]);
	write_to_log(log_str, "bot");

	sprintf(user_params, "%s 0 * :%s", config[CONF_USRNM], config[CONF_RLNM]);
	send_out_msg(sockfd, "", "USER", user_params);
	sprintf(log_str, "Set username \"%s\" and real name \"%s\"",
			config[CONF_USRNM], config[CONF_RLNM]);
	write_to_log(log_str, "bot");

	send_out_msg(sockfd, "", "JOIN", config[CONF_CHNS]);
	sprintf(log_str, "Join to channel(s) \"%s\"", config[CONF_CHNS]);
	write_to_log(log_str, "bot");
}

int main(int argc, char *argv[])
{
	/* To connect to server */
	struct addrinfo hints;
	struct addrinfo *results;
	struct addrinfo *res_i;
	int sockfd;
	int status;
	/* Congig */
	char config[CONF_COUNT][MAX_LEN];
	/* Recieved message */
	char in_msg[COMPS_COUNT][MAX_LEN];
	/* Bot's functionality */
	char out_msg[COMPS_COUNT][MAX_LEN];
	char log_str[MAX_LEN];

	/* hints is a template for sorting out connections */
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET; 	/* Allow IPv4 only */
	hints.ai_socktype = SOCK_STREAM; 	/* TCP connection */
	hints.ai_flags = 0;
	hints.ai_protocol = 0; 	/* Any protocol */

	get_config(config);
	/* Get list of adresses corresponding to our hints template */
	if (0 != (status = getaddrinfo(config[CONF_ADDR], config[CONF_PORT],
								   &hints, &results))
	) {
		fprintf(stderr,
				"Failed to get address info. main():getaddrinfo(): %s\n",
				gai_strerror(status));
		exit(EXIT_FAILURE);
	}

	/* Try each address in results for connection. */
	for (res_i = results;
		 res_i != NULL;
		 res_i = res_i->ai_next
	) {
		sockfd = socket(res_i->ai_family,
						res_i->ai_socktype,
						res_i->ai_protocol);
		if (sockfd == -1)
			continue;

		if (-1 != connect(sockfd, res_i->ai_addr, res_i->ai_addrlen))
			break; 	/* Successfull connection */

		close(sockfd);
	}
	freeaddrinfo(results);
	/* Check if connected */
	if (res_i == NULL) {
		perror("Could not connect. main():connect()");
		exit(EXIT_FAILURE);
	}

	sprintf(log_str, "Connected to %s:%s", config[CONF_ADDR], config[CONF_PORT]);
	write_to_log(log_str, "bot");

	irc_connect(sockfd, config);
	get_phrases();

	while (1){
		memset(in_msg, 0, sizeof(in_msg));
		memset(out_msg, 0, sizeof(out_msg));
		get_msg(sockfd, in_msg);

		if (!strcmp(command, "PING")) {
			send_out_msg(sockfd, "", "PONG", trailing);
			sprintf(log_str, "Send PONG to \"%s\"", trailing);
			write_to_log(log_str, "bot");
		} else if (!strcmp(command, "PRIVMSG")) {
			sprintf(log_str, "%s: %s", nickname, trailing);
			write_to_log(log_str, middle);

			if (NULL != strstr(trailing, config[CONF_NICK])) {
				time_machine(out_trailing);
				sprintf(out_params, "%s :%s", middle, out_trailing);
				send_out_msg(sockfd, "", "PRIVMSG", out_params);

				sprintf(log_str, "%s: %s", config[CONF_NICK], out_trailing);
				write_to_log(log_str, middle);
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
