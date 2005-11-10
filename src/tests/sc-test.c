/* Copyright (C) 2001  Juha Yrj�l� <juha.yrjola@iki.fi> 
 * All rights reserved.
 *
 * Common functions for test programs
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <opensc/opensc.h>
#include "sc-test.h"

sc_context_t *ctx;
sc_card_t *card;

const struct option	options[] = {
	{ "reader",             1, 0,           'r' },
	{ "driver",		1, 0,           'c' },
	{ "debug",              0, 0,           'd' },
	{ 0, 0, 0, 0 }
};

#if 0
const char *		option_help[] = {
	"Uses reader number <arg> [0]",
	"Forces the use of driver <arg> [auto-detect]",
	"Debug output -- may be supplied several times",
};
#endif

int sc_test_init(int *argc, char *argv[])
{
	char	*opt_driver = NULL, *app_name;
	int	opt_debug = 0, opt_reader = -1;
	int	i, c, rc;

	if  ((app_name = strrchr(argv[0], '/')) != NULL)
		app_name++;
	else
		app_name = argv[0];

	while ((c = getopt_long(*argc, argv, "r:c:d", options, NULL)) != -1) {
		switch (c) {
		case 'r':
			opt_reader = atoi(optarg);
			break;
		case 'c':
			opt_driver = optarg;
			break;
		case 'd':
			opt_debug++;
			break;
		default:
			fprintf(stderr,
				"usage: %s [-r reader] [-c driver] [-d]\n",
				app_name);
			exit(1);
		}
	}
	*argc = optind;

	printf("Using libopensc version %s.\n", sc_get_version());
	i = sc_establish_context(&ctx, app_name);
	if (i != SC_SUCCESS) {
		printf("Failed to establish context: %s\n", sc_strerror(i));
		return i;
	}
	ctx->debug = opt_debug;

	if (opt_reader >= ctx->reader_count) {
		fprintf(stderr, "Illegal reader number.\n"
				"Only %d reader(s) configured.\n",
				ctx->reader_count);
		exit(1);
	}

	while (1) {
		if (opt_reader >= 0) {
			rc = sc_detect_card_presence(
					ctx->reader[opt_reader], 0);
			printf("Card %s.\n", rc == 1 ? "present" : "absent");
			if (rc < 0)
				return rc;
		} else {
			for (i = rc = 0; rc != 1 && i < ctx->reader_count; i++)
				rc = sc_detect_card_presence(ctx->reader[i], 0);
			if (rc == 1)
				opt_reader = i - 1;
		}

		if (rc > 0) {
			printf("Card detected in reader '%s'\n",
					ctx->reader[opt_reader]->name);
			break;
		}
		if (rc < 0)
			return rc;

		printf("Please insert a smart card. Press return to continue");
		fflush(stdout);
		while (getc(stdin) != '\n')
			;
	}

	printf("Connecting... ");
	fflush(stdout);
	i = sc_connect_card(ctx->reader[opt_reader], 0, &card);
	if (i != SC_SUCCESS) {
		printf("Connecting to card failed: %s\n", sc_strerror(i));
		return i;
	}
	printf("connected.\n");
	{
		char tmp[SC_MAX_ATR_SIZE*3];
		sc_bin_to_hex(card->atr, card->atr_len, tmp, sizeof(tmp) - 1, ':');
		printf("ATR = %s\n",tmp);
	}

	if (opt_driver != NULL) {
		rc = sc_set_card_driver(ctx, opt_driver);
		if (rc != 0) {
			fprintf(stderr,
				"Driver '%s' not found!\n", opt_driver);
			return rc;
		}
	}

	return 0;
}

void sc_test_cleanup(void)
{
	sc_disconnect_card(card, 0);
	sc_release_context(ctx);
}
