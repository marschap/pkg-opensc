/*
 * $Id: scldap.c,v 1.20 2003/12/03 14:09:15 aet Exp $
 *
 * Copyright (C) 2001, 2002
 *  Antti Tapaninen <aet@cc.hut.fi>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef HAVE_LBER_H
#include <lber.h>
#endif
#ifdef HAVE_LDAP_H
#include <ldap.h>
#endif
#ifdef HAVE_LDAP_SSL_H
#include <ldap_ssl.h>
#endif
#include "scldap.h"

extern char **environ;

typedef struct _cb_data {
	scldap_context *ctx;
	char *cardprefix;
} cb_data;

static int attrs_cb(scconf_context * config, const scconf_block * block, scconf_entry * entry, int depth)
{
	scldap_param_entry *lentry = (scldap_param_entry *) entry->arg;
	const scconf_list *list = scconf_find_list(block, entry->name);

	for (; list; list = list->next) {
		if (lentry->numattrs >= SCLDAP_MAX_ATTRIBUTES) {
			break;
		}
		lentry->attributes = (char **) realloc(lentry->attributes, (lentry->numattrs + 2) * sizeof(char *));
		if (!lentry->attributes)
			return 1;
		lentry->attributes[lentry->numattrs] = strdup(list->data);
		lentry->numattrs++;
		lentry->attributes[lentry->numattrs] = NULL;
	}
	return 0;		/* 0 for ok, 1 for error */
}

static int ldap_cb(scconf_context * config, const scconf_block * block, scconf_entry * entry, int depth)
{
	cb_data *trans = (cb_data *) entry->arg;
	scldap_context *ctx = trans->ctx;
	char *cardprefix = trans->cardprefix;
	scldap_param_entry *lentry = (scldap_param_entry *) &ctx->entry[ctx->entries];
	scconf_entry centry[] =
	{
		{"ldaphost", SCCONF_STRING, SCCONF_ALLOC, &lentry->ldaphost, NULL},
		{"ldapport", SCCONF_INTEGER, SCCONF_ALLOC, &lentry->ldapport, NULL},
		{"scope", SCCONF_INTEGER, SCCONF_ALLOC, &lentry->scope, NULL},
		{"binddn", SCCONF_STRING, SCCONF_ALLOC, &lentry->binddn, NULL},
		{"passwd", SCCONF_STRING, SCCONF_ALLOC, &lentry->passwd, NULL},
		{"base", SCCONF_STRING, SCCONF_ALLOC, &lentry->base, NULL},
		{"attributes", SCCONF_CALLBACK, 0, (void *) attrs_cb, lentry},
		{"filter", SCCONF_STRING, SCCONF_ALLOC, &lentry->filter, NULL},
		{NULL}
	};
	char *ldapsuffix = NULL;
	size_t len = 0;

	if (ctx->entries >= SCLDAP_MAX_ENTRIES)
		return 0;	/* Hard limit reached, just return OK */
	ldapsuffix = scconf_list_strdup(block->name, " ");
	if (!ldapsuffix) {
		return 1;
	}
	if (cardprefix) {
		len = strlen(cardprefix) + 1;
	}
	len += strlen(ldapsuffix) + 1;
	lentry->entry = (char *) malloc(len);
	if (!lentry->entry) {
		free(ldapsuffix);
		return 1;
	}
	memset(lentry->entry, 0, len);
	snprintf(lentry->entry, len, "%s%s%s", cardprefix ? cardprefix : "", cardprefix ? " " : "", ldapsuffix);
	free(ldapsuffix);
	if (scconf_parse_entries(config, block, centry) != 0) {
		return 1;
	}
	ctx->entries++;
	ctx->entry = (scldap_param_entry *) realloc(ctx->entry, (ctx->entries + 2) * sizeof(scldap_param_entry));
	if (!ctx->entry)
		return 1;
	memset(&ctx->entry[ctx->entries], 0, sizeof(scldap_param_entry));

	return 0;		/* 0 for ok, 1 for error */
}

static int card_cb(scconf_context * config, const scconf_block * block, scconf_entry * entry, int depth)
{
	cb_data *trans = (cb_data *) entry->arg;
	scconf_entry card_entry[] =
	{
		{"ldap", SCCONF_CALLBACK, SCCONF_ALL_BLOCKS, (void *) ldap_cb, trans},
		{NULL}
	};

	trans->cardprefix = scconf_list_strdup(block->name, " ");
	if (scconf_parse_entries(config, block, card_entry) != 0) {
		free(trans->cardprefix);
		trans->cardprefix = NULL;
		return 1;
	}
	free(trans->cardprefix);
	trans->cardprefix = NULL;
	return 0;		/* 0 for ok, 1 for error */
}

scldap_context *scldap_parse_parameters(const char *filename)
{
	scldap_context *ctx = NULL;

	ctx = (scldap_context *) malloc(sizeof(scldap_context));
	if (!ctx) {
		return NULL;
	}
	memset(ctx, 0, sizeof(scldap_context));
	ctx->entry = (scldap_param_entry *) realloc(ctx->entry, (ctx->entries + 2) * sizeof(scldap_param_entry));
	if (!ctx->entry) {
		scldap_free_parameters(ctx);
		return NULL;
	}
	memset(&ctx->entry[ctx->entries], 0, sizeof(scldap_param_entry));

	if (filename) {
		cb_data trans = {ctx, NULL};
		scconf_entry entry[] =
		{
			{"ldap", SCCONF_CALLBACK, SCCONF_ALL_BLOCKS, (void *) ldap_cb, &trans},
			{"card", SCCONF_CALLBACK, SCCONF_ALL_BLOCKS, (void *) card_cb, &trans},
			{NULL}
		};
		ctx->conf = scconf_new(filename);
		if (!ctx->conf) {
			scldap_free_parameters(ctx);
			return NULL;
		}
		if (scconf_parse(ctx->conf) < 1) {
			scldap_free_parameters(ctx);
			return NULL;
		}
		if (scconf_parse_entries(ctx->conf, NULL, entry) != 0) {
			scldap_free_parameters(ctx);
			return NULL;
		}
	}
	ctx->entries++;
	ctx->active = 0;
	return ctx;
}

void scldap_show_parameters(scldap_context * ctx)
{
	unsigned int i, j;

	if (!ctx)
		return;
	for (i = 0; i < ctx->entries; i++) {
		if (ctx->entry[i].entry)
			printf("[%i]->entry=%s\n", i, ctx->entry[i].entry);
		if (ctx->entry[i].ldaphost)
			printf("[%i]->ldaphost=%s\n", i, ctx->entry[i].ldaphost);
		printf("[%i]->ldapport=%i\n", i, ctx->entry[i].ldapport);
		printf("[%i]->scope=%i\n", i, ctx->entry[i].scope);
		if (ctx->entry[i].binddn)
			printf("[%i]->binddn=%s\n", i, ctx->entry[i].binddn);
		if (ctx->entry[i].passwd)
			printf("[%i]->passwd=%s\n", i, ctx->entry[i].passwd);
		if (ctx->entry[i].base)
			printf("[%i]->base=%s\n", i, ctx->entry[i].base);
		for (j = 0; j < ctx->entry[i].numattrs; j++) {
			if (ctx->entry[i].attributes[j])
				printf("[%i]->attribute[%i]=%s\n", i, j, ctx->entry[i].attributes[j]);
		}
		if (ctx->entry[i].filter)
			printf("[%i]->filter=%s\n\n", i, ctx->entry[i].filter);
	}
}

void scldap_free_parameters(scldap_context * ctx)
{
	unsigned int i, j;

	if (!ctx)
		return;
	if (ctx) {
		for (i = 0; i < ctx->entries; i++) {
			if (ctx->entry[i].entry) {
				free(ctx->entry[i].entry);
			}
			ctx->entry[i].entry = NULL;
			if (ctx->entry[i].ldaphost) {
				free(ctx->entry[i].ldaphost);
			}
			ctx->entry[i].ldaphost = NULL;
			ctx->entry[i].ldapport = 0;
			ctx->entry[i].scope = 0;
			if (ctx->entry[i].binddn) {
				free(ctx->entry[i].binddn);
			}
			ctx->entry[i].binddn = NULL;
			if (ctx->entry[i].passwd) {
				free(ctx->entry[i].passwd);
			}
			ctx->entry[i].passwd = NULL;
			if (ctx->entry[i].base) {
				free(ctx->entry[i].base);
			}
			ctx->entry[i].base = NULL;
			for (j = 0; j < ctx->entry[i].numattrs; j++) {
				free(ctx->entry[i].attributes[j]);
				ctx->entry[i].attributes[j] = NULL;
			}
			if (ctx->entry[i].attributes) {
				free(ctx->entry[i].attributes);
			}
			ctx->entry[i].attributes = NULL;
			ctx->entry[i].numattrs = 0;
			if (ctx->entry[i].filter) {
				free(ctx->entry[i].filter);
			}
			ctx->entry[i].filter = NULL;
		}
		if (ctx->entry) {
			free(ctx->entry);
		}
		ctx->entry = NULL;
		ctx->entries = 0;
		if (ctx->conf) {
			scconf_free(ctx->conf);
		}
		ctx->conf = NULL;
		free(ctx);
		ctx = NULL;
	}
}

void scldap_parse_arguments(scldap_context ** ctx, int argc, const char **argv)
{
	scldap_context *ptr = *ctx;
	int i;

	if (!ptr || !argv || argc < 0)
		return;
	for (i = 0; i < argc; i++) {
		if (argv[i][0] == '-') {
			char *optarg = (char *) argv[i + 1];
			if (!optarg)
				continue;
			switch (argv[i][1]) {
#define ADD(x) \
{ \
  if (x) { \
    free(x); \
    x = NULL; \
  } \
  x = ((optarg) ? strdup(optarg) : NULL); \
}
			case 'A':
				scldap_add_entry(ptr, optarg);
				break;
			case 'E':
				scldap_set_entry(ptr, optarg);
				break;
			case 'H':
				ADD(ptr->entry[ptr->active].ldaphost);
				break;
			case 'P':
				ptr->entry[ptr->active].ldapport = atoi(optarg);
				break;
			case 'S':
				ptr->entry[ptr->active].scope = atoi(optarg);
				break;
			case 'b':
				ADD(ptr->entry[ptr->active].binddn);
				break;
			case 'p':
				ADD(ptr->entry[ptr->active].passwd);
				break;
			case 'B':
				ADD(ptr->entry[ptr->active].base);
				break;
			case 'a':
				if (ptr->entry[ptr->active].numattrs >= SCLDAP_MAX_ATTRIBUTES) {
					break;
				}
				ptr->entry[ptr->active].attributes = (char **) realloc(ptr->entry[ptr->active].attributes, (ptr->entry[ptr->active].numattrs + 2) * sizeof(char *));
				if (!ptr->entry[ptr->active].attributes)
					break;
				memset(&ptr->entry[ptr->active].attributes[ptr->entry[ptr->active].numattrs], 0, sizeof(char *));
				ADD(ptr->entry[ptr->active].attributes[ptr->entry[ptr->active].numattrs]);
				ptr->entry[ptr->active].numattrs++;
				ptr->entry[ptr->active].attributes[ptr->entry[ptr->active].numattrs] = NULL;
				break;
			case 'f':
				ADD(ptr->entry[ptr->active].filter);
#undef ADD
				break;
			case 'L':
				{
					scldap_context *tmp = scldap_parse_parameters(optarg);
					if (tmp) {
						scldap_free_parameters(ptr);
						ptr = tmp;
					}
				}
				break;
			}
		}
	}
	*ctx = ptr;
}

const char *scldap_show_arguments(void)
{
	static char buf[250];

	memset(buf, 0, 250);
	snprintf(buf, 250,
		 " -L ldap.conf	Configuration file to load\n"
		 " -A entry	Add new entry\n"
		 " -E entry	Set current entry\n"
		 "  LDAP entry specific options:\n"
		 "   -H hostname\n"
		 "   -P port\n"
		 "   -S scope\n"
		 "   -b binddn\n"
		 "   -p passwd\n"
		 "   -B base\n"
		 "   -a attribute(s)\n"
		 "   -f filter\n");
	return &buf[0];
}

int scldap_add_entry(scldap_context * ctx, const char *entry)
{
	unsigned int i;

	if (!ctx)
		return 0;
	if (entry) {
		for (i = 0; i < ctx->entries; i++) {
			if (!ctx->entry[i].entry) {
				ctx->entry[i].entry = strdup(entry);
				ctx->active = i;
				return i;
			}
		}
		i = ctx->entries;
		ctx->entry = (scldap_param_entry *) realloc(ctx->entry, (i + 2) * sizeof(scldap_param_entry));
		if (!ctx->entry)
			return 0;
		memset(&ctx->entry[i], 0, sizeof(scldap_param_entry));
		ctx->entry[i].entry = strdup(entry);
		ctx->active = i;
		ctx->entries++;
		return i;
	}
	return 0;
}

int scldap_get_entry(scldap_context * ctx, const char *entry)
{
	unsigned int i;

	if (!ctx)
		return 0;
	if (entry) {
		for (i = 0; i < ctx->entries; i++) {
			if (ctx->entry[i].entry) {
				if (!strcmp(ctx->entry[i].entry, entry)) {
					return i;
				}
			}
		}
	}
	return 0;
}

void scldap_set_entry(scldap_context * ctx, const char *entry)
{
	unsigned int i;

	if (!ctx)
		return;
	if (entry) {
		for (i = 0; i < ctx->entries; i++) {
			if (ctx->entry[i].entry) {
				if (!strcmp(ctx->entry[i].entry, entry)) {
					ctx->active = i;
					break;
				}
			}
		}
	}
}

void scldap_remove_entry(scldap_context * ctx, const char *entry)
{
	unsigned int i, j;

	if (!ctx)
		return;
	if (entry) {
		for (i = 0; i < ctx->entries; i++) {
			if (ctx->entry[i].entry) {
				if (!strcmp(ctx->entry[i].entry, entry)) {
					if (ctx->entry[i].entry) {
						free(ctx->entry[i].entry);
					}
					ctx->entry[i].entry = NULL;
					if (ctx->entry[i].ldaphost) {
						free(ctx->entry[i].ldaphost);
					}
					ctx->entry[i].ldaphost = NULL;
					ctx->entry[i].ldapport = 0;
					ctx->entry[i].scope = 0;
					if (ctx->entry[i].binddn) {
						free(ctx->entry[i].binddn);
					}
					ctx->entry[i].binddn = NULL;
					if (ctx->entry[i].passwd) {
						free(ctx->entry[i].passwd);
					}
					ctx->entry[i].passwd = NULL;
					if (ctx->entry[i].base) {
						free(ctx->entry[i].base);
					}
					ctx->entry[i].base = NULL;
					for (j = 0; j < ctx->entry[i].numattrs; j++) {
						free(ctx->entry[i].attributes[j]);
						ctx->entry[i].attributes[j] = NULL;
					}
					if (ctx->entry[i].attributes) {
						free(ctx->entry[i].attributes);
					}
					ctx->entry[i].attributes = NULL;
					ctx->entry[i].numattrs = 0;
					if (ctx->entry[i].filter) {
						free(ctx->entry[i].filter);
					}
					ctx->entry[i].filter = NULL;
					break;
				}
			}
		}
	}
}

int scldap_is_valid_url(const char *url)
{
	if (!url)
		return 0;
	return ldap_is_ldap_url((char *) url);
}

int scldap_url_to_entry(scldap_context * ctx, const char *entry, const char *url)
{
	LDAPURLDesc *ldapurl = NULL;
	int rv, i, j;

	if (!ctx || !entry || !url) {
		return -1;
	}
	rv = ldap_url_parse((char *) url, &ldapurl);
	if (rv) {
		switch (rv) {
#ifdef LDAP_URL_ERR_BADSCHEME
		case LDAP_URL_ERR_BADSCHEME:
			fprintf(stderr, "Not an LDAP URL: %s", url);
			break;
#endif
#ifdef LDAP_URL_ERR_BADENCLOSURE
		case LDAP_URL_ERR_BADENCLOSURE:
			fprintf(stderr, "Bad enclosure in URL: %s", url);
			break;
#endif
#ifdef LDAP_URL_ERR_BADURL
		case LDAP_URL_ERR_BADURL:
			fprintf(stderr, "Bad URL: %s", url);
			break;
#endif
#ifdef LDAP_URL_ERR_BADHOST
		case LDAP_URL_ERR_BADHOST:
			fprintf(stderr, "Host is invalid in URL: %s", url);
			break;
#endif
#ifdef LDAP_URL_ERR_BADATTRS
		case LDAP_URL_ERR_BADATTRS:
			fprintf(stderr, "Attributes are invalid in URL: %s", url);
			break;
#endif
#ifdef LDAP_URL_ERR_BADSCOPE
		case LDAP_URL_ERR_BADSCOPE:
			fprintf(stderr, "Scope is invalid in URL: %s", url);
			break;
#endif
#ifdef LDAP_URL_ERR_BADFILTER
		case LDAP_URL_ERR_BADFILTER:
			fprintf(stderr, "Filter is invalid in URL: %s", url);
			break;
#endif
#ifdef LDAP_URL_ERR_BADEXTS
		case LDAP_URL_ERR_BADEXTS:
			fprintf(stderr, "Extensions are invalid in URL: %s", url);
			break;
#endif
#ifdef LDAP_URL_ERR_MEM
		case LDAP_URL_ERR_MEM:
			fprintf(stderr, "Out of memory parsing URL: %s", url);
			break;
#endif
#ifdef LDAP_URL_ERR_PARAM
		case LDAP_URL_ERR_PARAM:
			fprintf(stderr, "Bad parameter parsing URL: %s", url);
			break;
#endif
		default:
			fprintf(stderr, "Unknown error %d parsing URL: %s", rv, url);
			break;
		}
		return -1;
	}
	if (ldapurl) {
		scldap_remove_entry(ctx, entry);
		scldap_add_entry(ctx, entry);

		i = scldap_get_entry(ctx, entry);
#define ADD(val) ((val) ? strdup(val) : NULL)
		ctx->entry[i].ldaphost = ADD(ldapurl->lud_host);
		ctx->entry[i].ldapport = ldapurl->lud_port;
		ctx->entry[i].scope = ldapurl->lud_scope;
		ctx->entry[i].base = ADD(ldapurl->lud_dn);

		for (j = 0; ldapurl->lud_attrs[j]; j++) {
			if (ctx->entry[i].numattrs >= SCLDAP_MAX_ATTRIBUTES) {
				break;
			}
			ctx->entry[i].attributes = (char **) realloc(ctx->entry[i].attributes, (ctx->entry[i].numattrs + 2) * sizeof(char *));
			if (!ctx->entry[i].attributes)
				break;
			memset(&ctx->entry[i].attributes[ctx->entry[i].numattrs], 0, sizeof(char *));
			ctx->entry[i].attributes[ctx->entry[i].numattrs] = strdup(ldapurl->lud_attrs[j]);
			ctx->entry[i].numattrs++;
			ctx->entry[i].attributes[ctx->entry[i].numattrs] = NULL;
		}

		ctx->entry[i].filter = ADD(ldapurl->lud_filter);
#undef ADD
		ldap_free_urldesc(ldapurl);
		ldapurl = NULL;
		return 0;
	}
	return -1;
}

int scldap_approx_base_by_dn(scldap_context * ctx, const char *entry, const char *dn, char **base)
{
	scldap_result *splitdn = NULL;
	unsigned int i = 0, j = 0, numdns = 0;
	char **founddns = NULL;

	if (!ctx || !entry || !dn) {
		return -1;
	}
	if (scldap_dn_to_result(dn, &splitdn, 0) < 0) {
		return -1;
	}
	for (i = 0; i < splitdn->results; i++) {
		scldap_result *result = NULL;
#if 0
		printf("%02i. %s [%li]\n", i + 1,
		       splitdn->result[i].data,
		       splitdn->result[i].datalen);
#endif
		if (scldap_search(ctx, entry, &result, 0, (const char *) splitdn->result[i].data) < 0) {
			continue;
		}
		if (result) {
			for (j = 0; j < result->results; j++) {
				founddns = (char **) realloc(founddns, (numdns + 2) * sizeof(char *));
				founddns[numdns] = strdup(result->result[j].dn);
				numdns++;
				founddns[numdns] = NULL;
			}
			scldap_free_result(result);
		}
	}
	scldap_free_result(splitdn);
	if (!numdns) {
		return -1;
	}
#if 0
	for (i = 0; i < numdns; i++) {
		printf("%02i. %s\n", i + 1, founddns[i]);
	}
#endif
	if (*base) {
		free(*base);
		*base = NULL;
	}
	/* FIXME: Add proper logic to this */
	*base = strdup(founddns[0]);
	for (i = 0; i < numdns; i++) {
		free(founddns[i]);
	}
	free(founddns);
	return 1;
}

int scldap_dn_to_result(const char *dn, scldap_result ** result, int notypes)
{
	scldap_result *_result = NULL;
	char *buf = NULL, **tmp = NULL;
	unsigned int i;

	if (!dn || *result)
		return -1;

	_result = (scldap_result *) malloc(sizeof(scldap_result));
	if (!_result) {
		return -1;
	}
	memset(_result, 0, sizeof(scldap_result));

#if 0
	printf("dn: %s\n", dn);
#endif
	buf = (char *) malloc((strlen(dn) + 1) * 2);
	if (!buf) {
		free(_result);
		return -1;
	}
	memset(buf, 0, (strlen(dn) + 1) * 2);

	if (dn[0] == '/') {
		unsigned int c = 0;

		for (i = 1; i < strlen(dn); i++) {
			if (dn[i] == '/') {
				buf[c++] = ',';
				buf[c++] = ' ';
			} else {
				buf[c++] = dn[i];
			}
		}
	} else {
		memcpy(buf, dn, strlen(dn));
	}
#if 0
	printf("buf: %s\n", buf);
#endif
	tmp = ldap_explode_dn(buf, notypes);
	for (i = 0; tmp[i]; i++) {
		_result->result = (scldap_result_entry *) realloc(_result->result, (_result->results + 2) * sizeof(scldap_result_entry));
		if (!_result->result)
			continue;
		memset(&_result->result[_result->results], 0, sizeof(scldap_result_entry));
		_result->result[_result->results].dn = strdup(buf);
		_result->result[_result->results].data = (unsigned char *) strdup(tmp[i]);
		_result->result[_result->results].datalen = strlen(tmp[i]);
		_result->results++;
		free(tmp[i]);
	}
	free(buf);
	free(tmp);
	if (!_result->results) {
		scldap_free_result(_result);
		return -1;
	}
	*result = _result;
	return 0;
}

static void scldap_get_result(LDAP * ld, LDAPMessage * res, scldap_param_entry * param, scldap_result * result, int attrsonly)
{
	struct berval **bvals = NULL;
	BerElement *ber = NULL;
	char *name = NULL;
	unsigned int i = 0, j, o;

	for (name = ldap_first_attribute(ld, res, &ber); name;
	     name = ldap_next_attribute(ld, res, ber)) {
#define ADD() \
{ \
  if (result->results < SCLDAP_MAX_RESULTS) { \
    result->result[result->results].name = strdup(name); \
    result->result[result->results].dn = ldap_get_dn(ld, res); \
    if (!attrsonly) { \
      result->result[result->results].datalen = bvals[i]->bv_len; \
      result->result[result->results].data = (unsigned char *) malloc(result->result[result->results].datalen + 1); \
      memset(result->result[result->results].data, 0, result->result[result->results].datalen + 1); \
      memcpy(result->result[result->results].data, bvals[i]->bv_val, result->result[result->results].datalen); \
      for (o = 0; o < bvals[i]->bv_len; o++) { \
        int k = bvals[i]->bv_val[o]; \
        if (!isascii(k)) { \
          result->result[result->results].binary = 1; \
          break; \
        } \
      } \
    } \
    result->results++; \
    result->result = (scldap_result_entry *) realloc(result->result, (result->results + 2) * sizeof(scldap_result_entry)); \
    memset(&result->result[result->results], 0, sizeof(scldap_result_entry)); \
  } \
}
		if (attrsonly) {
			if (param->numattrs) {
				for (j = 0; j < param->numattrs; j++) {
					if (!strncasecmp(param->attributes[j], name, strlen(param->attributes[j]))) {
						ADD();
					}
				}
			} else {
				ADD();
			}
		} else if ((bvals = ldap_get_values_len(ld, res, name))) {
			for (i = 0; bvals[i]; i++) {
				if (param->numattrs) {
					for (j = 0; j < param->numattrs; j++) {
						if (!strncasecmp(param->attributes[j], name, strlen(param->attributes[j]))) {
							ADD();
						}
					}
				} else {
					ADD();
#undef ADD
				}
			}
			ber_bvecfree(bvals);
		}
	}
}

static char *combinestr(char *str,...)
{
#define MAX_BUF_LEN 4096
	va_list ap;
	char *buf = NULL;

	if (!str) {
		return NULL;
	}
	buf = (char *) malloc(MAX_BUF_LEN);
	if (!buf) {
		return NULL;
	}
	memset(buf, 0, MAX_BUF_LEN);

	va_start(ap, str);
	vsnprintf(buf, MAX_BUF_LEN, str, ap);
	va_end(ap);
	return buf;
#undef MAX_BUF_LEN
}

int scldap_search(scldap_context * ctx, const char *entry,
		  scldap_result ** result, unsigned int numwantedresults,
		  const char *searchpattern)
{
	LDAPMessage *res, *e;
	LDAP *ld = NULL;
	scldap_result *_result = *result;
	int rc, entrynum = -1;
	char *pattern = NULL;
	char **keepenv = NULL;

	if (_result || !ctx) {
		return -1;
	}
	entrynum = scldap_get_entry(ctx, entry);
	if (entrynum < 0) {
		return -1;
	}
	if (!ctx->entry[entrynum].ldaphost) {
		return -1;
	}
	keepenv = environ;
	environ = NULL;
	if ((ld = ldap_init(ctx->entry[entrynum].ldaphost, ctx->entry[entrynum].ldapport)) == NULL) {
		environ = keepenv;
		perror("ldap_init");
		return -1;
	}
	environ = keepenv;
	if (ldap_bind_s(ld, ctx->entry[entrynum].binddn, ctx->entry[entrynum].passwd, LDAP_AUTH_SIMPLE) != LDAP_SUCCESS) {
		ldap_perror(ld, "ldap_bind");
		ldap_unbind(ld);
		return -1;
	}
	if (searchpattern && ctx->entry[entrynum].filter) {
		pattern = combinestr(ctx->entry[entrynum].filter, searchpattern);
	} else if (searchpattern && !ctx->entry[entrynum].filter) {
		pattern = strdup(searchpattern);
	} else if (!searchpattern && ctx->entry[entrynum].filter) {
		pattern = strdup(ctx->entry[entrynum].filter);
	}
	/* This ifdef is currently not actually used or probed in any way,
	 * older versions of OpenLDAP, eg. <=1.2.12.1 seem to need this
	 * sanity check. This check cannot be enabled at least for newer
	 * versions of OpenLDAP, otherwise it'll block certificate CRL
	 * fetches. scldap used to work at least with solaris built-in
	 * ldap library, nowadays untested, so beware. -aet
	 */
#ifdef HAVE_OLD_OPENLDAP
	/* Note: pattern *can* be empty but NOT NULL! Therefore, this is illegal. */
	if (!pattern) {
		ldap_unbind(ld);
		return -1;
	}
#endif
#if 0
	if (pattern)
		fprintf(stderr, "pattern: %s\n", pattern);
#endif
	if (ldap_search(ld, ctx->entry[entrynum].base, ctx->entry[entrynum].scope, pattern, ctx->entry[entrynum].attributes, ctx->entry[entrynum].attrsonly) == -1) {
		ldap_perror(ld, "ldap_search");
		if (pattern)
			free(pattern);
		ldap_unbind(ld);
		return -1;
	}
	if (pattern)
		free(pattern);
	_result = (scldap_result *) malloc(sizeof(scldap_result));
	if (!_result) {
		ldap_unbind(ld);
		return -1;
	}
	memset(_result, 0, sizeof(scldap_result));
	while ((rc = ldap_result(ld, LDAP_RES_ANY, 0, NULL, &res)) == LDAP_RES_SEARCH_ENTRY) {
		e = ldap_first_entry(ld, res);
		if (_result->results < SCLDAP_MAX_RESULTS) {
			_result->result = (scldap_result_entry *) realloc(_result->result, (_result->results + 2) * sizeof(scldap_result_entry));
			if (!_result->result)
				break;
			memset(&_result->result[_result->results], 0, sizeof(scldap_result_entry));
			scldap_get_result(ld, e, &ctx->entry[entrynum], _result, ctx->entry[entrynum].attrsonly);
		}
		ldap_msgfree(res);
	}
	if (rc == -1) {
		ldap_perror(ld, "ldap_result");
		ldap_msgfree(res);
		ldap_unbind(ld);
		scldap_free_result(_result);
		return rc;
	}
	if ((rc = ldap_result2error(ld, res, 0)) != LDAP_SUCCESS) {
		ldap_perror(ld, "ldap_search");
	}
	ldap_msgfree(res);
	ldap_unbind(ld);
	if (numwantedresults) {
		if (numwantedresults != _result->results) {
			scldap_free_result(_result);
			_result = NULL;
			rc = -1;
		}
	}
	*result = _result;
	return rc;
}

void scldap_free_result(scldap_result * result)
{
	unsigned int i;

	if (result) {
		for (i = 0; i < result->results; i++) {
			if (result->result[i].name) {
				free(result->result[i].name);
			}
			result->result[i].name = NULL;
			if (result->result[i].dn) {
				free(result->result[i].dn);
			}
			result->result[i].dn = NULL;
			if (result->result[i].data) {
				free(result->result[i].data);
			}
			result->result[i].data = NULL;
			result->result[i].datalen = 0;
		}
		if (result->result) {
			free(result->result);
		}
		result->result = NULL;
		result->results = 0;
		free(result);
		result = NULL;
	}
}
