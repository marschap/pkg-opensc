/*
 * dir.c: Stuff for handling EF(DIR)
 *
 * Copyright (C) 2001, 2002  Juha Yrj�l� <juha.yrjola@iki.fi>
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

#include "internal.h"
#include "asn1.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct app_entry {
	const u8 *aid;
	size_t aid_len;
	const char *desc;
};

static const struct app_entry apps[] = {
	{ (const u8 *) "\xA0\x00\x00\x00\x63PKCS-15", 12, "PKCS #15" },
	{ (const u8 *) "\xA0\x00\x00\x01\x77PKCS-15", 12, "Belgian eID" },
};

static const struct app_entry * find_app_entry(const u8 * aid, size_t aid_len)
{
	size_t i;

	for (i = 0; i < sizeof(apps)/sizeof(apps[0]); i++) {
		if (apps[i].aid_len == aid_len &&
		    memcmp(apps[i].aid, aid, aid_len) == 0)
			return &apps[i];
	}
	return NULL;
}

const struct sc_app_info * sc_find_pkcs15_app(struct sc_card *card)
{
	const struct sc_app_info *app = NULL;
	unsigned int i;

	i = sizeof(apps)/sizeof(apps[0]);
	while (!app && i--)
		app = sc_find_app_by_aid(card, apps[i].aid, apps[i].aid_len);

	return app;
}

static const struct sc_asn1_entry c_asn1_dirrecord[] = {
	{ "aid",   SC_ASN1_OCTET_STRING, SC_ASN1_APP | 15, 0, NULL },
	{ "label", SC_ASN1_UTF8STRING,   SC_ASN1_APP | 16, SC_ASN1_OPTIONAL, NULL },
	{ "path",  SC_ASN1_OCTET_STRING, SC_ASN1_APP | 17, SC_ASN1_OPTIONAL, NULL },
	{ "ddo",   SC_ASN1_OCTET_STRING, SC_ASN1_APP | 19 | SC_ASN1_CONS, SC_ASN1_OPTIONAL, NULL },
	{ NULL }
};

static const struct sc_asn1_entry c_asn1_dir[] = {
	{ "dirRecord", SC_ASN1_STRUCT, SC_ASN1_APP | 1 | SC_ASN1_CONS, 0, NULL },
	{ NULL }
};

static int parse_dir_record(struct sc_card *card, u8 ** buf, size_t *buflen,
			    int rec_nr)
{
	struct sc_asn1_entry asn1_dirrecord[5], asn1_dir[2];
	struct sc_app_info *app = NULL;
	const struct app_entry *ae;
	int r;
	u8 aid[128], label[128], path[128];
	u8 ddo[128];
	size_t aid_len = sizeof(aid), label_len = sizeof(label),
	       path_len = sizeof(path), ddo_len = sizeof(ddo);

	sc_copy_asn1_entry(c_asn1_dirrecord, asn1_dirrecord);
	sc_copy_asn1_entry(c_asn1_dir, asn1_dir);
	sc_format_asn1_entry(asn1_dir + 0, asn1_dirrecord, NULL, 0);
	sc_format_asn1_entry(asn1_dirrecord + 0, aid, &aid_len, 0);
	sc_format_asn1_entry(asn1_dirrecord + 1, label, &label_len, 0);
	sc_format_asn1_entry(asn1_dirrecord + 2, path, &path_len, 0);
	sc_format_asn1_entry(asn1_dirrecord + 3, ddo, &ddo_len, 0);
	
	r = sc_asn1_decode(card->ctx, asn1_dir, *buf, *buflen, (const u8 **) buf, buflen);
	if (r == SC_ERROR_ASN1_END_OF_CONTENTS)
		return r;
	if (r) {
		sc_error(card->ctx, "EF(DIR) parsing failed: %s\n",
		      sc_strerror(r));
		return r;
	}
	if (aid_len > SC_MAX_AID_SIZE) {
		sc_error(card->ctx, "AID is too long.\n");
		return SC_ERROR_INVALID_ASN1_OBJECT;
	}
	app = (struct sc_app_info *) malloc(sizeof(struct sc_app_info));
	if (app == NULL)
		return SC_ERROR_OUT_OF_MEMORY;
	
	memcpy(app->aid, aid, aid_len);
	app->aid_len = aid_len;
	if (asn1_dirrecord[1].flags & SC_ASN1_PRESENT)
		app->label = strdup((char *) label);
	else
		app->label = NULL;
	if (asn1_dirrecord[2].flags & SC_ASN1_PRESENT) {
		if (path_len > SC_MAX_PATH_SIZE) {
			sc_error(card->ctx, "Application path is too long.\n");
			return SC_ERROR_INVALID_ASN1_OBJECT;
		}
		memcpy(app->path.value, path, path_len);
		app->path.len = path_len;	
		app->path.type = SC_PATH_TYPE_PATH;
	} else if (aid_len < sizeof(app->path.value)) {
		memcpy(app->path.value, aid, aid_len);
		app->path.len = aid_len;
		app->path.type = SC_PATH_TYPE_DF_NAME;
	} else
		app->path.len = 0;
	if (asn1_dirrecord[3].flags & SC_ASN1_PRESENT) {
		app->ddo = (u8 *) malloc(ddo_len);
		if (app->ddo == NULL)
			return SC_ERROR_OUT_OF_MEMORY;
		memcpy(app->ddo, ddo, ddo_len);
		app->ddo_len = ddo_len;
	} else {
		app->ddo = NULL;
		app->ddo_len = 0;
	}
	ae = find_app_entry(aid, aid_len);
	if (ae != NULL)
		app->desc = ae->desc;
	else
		app->desc = NULL;
	app->rec_nr = rec_nr;
	card->app[card->app_count] = app;
	card->app_count++;
	
	return 0;
}

int sc_enum_apps(struct sc_card *card)
{
	struct sc_path path;
	int ef_structure;
	size_t file_size;
	int r;

	if (card->app_count < 0)
		card->app_count = 0;
	sc_format_path("3F002F00", &path);
	if (card->ef_dir != NULL) {
		sc_file_free(card->ef_dir);
		card->ef_dir = NULL;
	}
	card->ctx->suppress_errors++;
	r = sc_select_file(card, &path, &card->ef_dir);
	card->ctx->suppress_errors--;
	if (r)
		return r;
	if (card->ef_dir->type != SC_FILE_TYPE_WORKING_EF) {
		sc_error(card->ctx, "EF(DIR) is not a working EF.\n");
		sc_file_free(card->ef_dir);
		card->ef_dir = NULL;
		return SC_ERROR_INVALID_CARD;
	}
	ef_structure = card->ef_dir->ef_structure;
	file_size = card->ef_dir->size;
	if (file_size == 0)
		return 0;
	if (ef_structure == SC_FILE_EF_TRANSPARENT) {
		u8 *buf = NULL, *p;
		size_t bufsize;
		
		buf = (u8 *) malloc(file_size);
		if (buf == NULL)
			return SC_ERROR_OUT_OF_MEMORY;
		p = buf;
		r = sc_read_binary(card, 0, buf, file_size, 0);
		if (r < 0) {
			free(buf);
			SC_TEST_RET(card->ctx, r, "read_binary() failed");
		}
		bufsize = r;
		while (bufsize > 0) {
			if (card->app_count == SC_MAX_CARD_APPS) {
				sc_error(card->ctx, "Too many applications on card");
				break;
			}
			r = parse_dir_record(card, &p, &bufsize, -1);
			if (r)
				break;
		}
		if (buf)
			free(buf);

	} else {	/* record structure */
		u8 buf[256], *p;
		unsigned int rec_nr;
		size_t       rec_size;
		
		for (rec_nr = 1; ; rec_nr++) {
			card->ctx->suppress_errors++;
			r = sc_read_record(card, rec_nr, buf, sizeof(buf), 
						SC_RECORD_BY_REC_NR);
			card->ctx->suppress_errors--;
			if (r == SC_ERROR_RECORD_NOT_FOUND)
				break;
			SC_TEST_RET(card->ctx, r, "read_record() failed");
			if (card->app_count == SC_MAX_CARD_APPS) {
				sc_error(card->ctx, "Too many applications on card");
				break;
			}
			rec_size = r;
			p = buf;
			parse_dir_record(card, &p, &rec_size, (int)rec_nr);
		}
	}
	return card->app_count;
}

void sc_free_apps(struct sc_card *card)
{
	int	i;

	for (i = 0; i < card->app_count; i++) {
		if (card->app[i]->label)
			free(card->app[i]->label);
		if (card->app[i]->ddo)
			free(card->app[i]->ddo);
		free(card->app[i]);
	}
	card->app_count = -1;
}

const struct sc_app_info * sc_find_app_by_aid(struct sc_card *card,
					      const u8 *aid, size_t aid_len)
{
	int i;

	assert(card->app_count > 0);
	for (i = 0; i < card->app_count; i++) {
		if (card->app[i]->aid_len == aid_len &&
		    memcmp(card->app[i]->aid, aid, aid_len) == 0)
			return card->app[i];
	}
	return NULL;
}

static int encode_dir_record(struct sc_context *ctx, const struct sc_app_info *app,
			     u8 **buf, size_t *buflen)
{
	struct sc_asn1_entry asn1_dirrecord[5], asn1_dir[2];
	struct sc_app_info   tapp = *app;
	int r;
	size_t label_len;

	sc_copy_asn1_entry(c_asn1_dirrecord, asn1_dirrecord);
	sc_copy_asn1_entry(c_asn1_dir, asn1_dir);
	sc_format_asn1_entry(asn1_dir + 0, asn1_dirrecord, NULL, 1);
	sc_format_asn1_entry(asn1_dirrecord + 0, (void *) tapp.aid, (void *) &tapp.aid_len, 1);
	if (tapp.label != NULL) {
		label_len = strlen(tapp.label);
		sc_format_asn1_entry(asn1_dirrecord + 1, tapp.label, &label_len, 1);
	}
	if (tapp.path.len)
		sc_format_asn1_entry(asn1_dirrecord + 2, (void *) tapp.path.value,
				     (void *) &tapp.path.len, 1);
	if (tapp.ddo != NULL)
		sc_format_asn1_entry(asn1_dirrecord + 3, (void *) tapp.ddo,
				     (void *) &tapp.ddo_len, 1);
	r = sc_asn1_encode(ctx, asn1_dir, buf, buflen);
	if (r) {
		sc_error(ctx, "sc_asn1_encode() failed: %s\n",
		      sc_strerror(r));
		return r;
	}
	return 0;
}

static int update_transparent(struct sc_card *card, struct sc_file *file)
{
	u8 *rec, *buf = NULL, *tmp;
	size_t rec_size, buf_size = 0;
	int i, r;

	for (i = 0; i < card->app_count; i++) {
		r = encode_dir_record(card->ctx, card->app[i], &rec, &rec_size);
		if (r) {
			if (rec)
				free(rec);
			if (buf)
				free(buf);
			return r;
		}
		tmp = (u8 *) realloc(buf, buf_size + rec_size);
		if (!tmp) {
			if (rec)
				free(rec);
			if (buf)
				free(buf);
			return SC_ERROR_OUT_OF_MEMORY;
		}
		buf = tmp;
		memcpy(buf + buf_size, rec, rec_size);
		buf_size += rec_size;
		free(rec);
	}
	if (file->size > buf_size) {
		tmp = (u8 *) realloc(buf, file->size);
		if (!tmp) {
			free(buf);
			return SC_ERROR_OUT_OF_MEMORY;
		}
		buf = tmp;
		memset(buf + buf_size, 0, file->size - buf_size);
		buf_size = file->size;
	}
	r = sc_update_binary(card, 0, buf, buf_size, 0);
	free(buf);
	SC_TEST_RET(card->ctx, r, "Unable to update EF(DIR)");
	
	return 0;
}

static int update_single_record(struct sc_card *card, struct sc_file *file,
				struct sc_app_info *app)
{
	u8 *rec;
	size_t rec_size;
	int r;
	
	r = encode_dir_record(card->ctx, app, &rec, &rec_size);
	if (r)
		return r;
	r = sc_update_record(card, (unsigned int)app->rec_nr, rec, rec_size, 0);
	free(rec);
	SC_TEST_RET(card->ctx, r, "Unable to update EF(DIR) record");
	return 0;
}

static int update_records(struct sc_card *card, struct sc_file *file)
{
	int i, r;

	for (i = 0; i < card->app_count; i++) {
		r = update_single_record(card, file, card->app[i]);
		if (r)
			return r;
	}
	return 0;
}

int sc_update_dir(struct sc_card *card, struct sc_app_info *app)
{
	struct sc_path path;
	struct sc_file *file;
	int r;
	
	sc_format_path("3F002F00", &path);

	r = sc_select_file(card, &path, &file);
	SC_TEST_RET(card->ctx, r, "unable to select EF(DIR)");
	if (file->ef_structure == SC_FILE_EF_TRANSPARENT)
		r = update_transparent(card, file);
	else if (app == NULL)
		r = update_records(card, file);
	else
		r = update_single_record(card, file, app);
	sc_file_free(file);
	return r;
}
