/*
 * partial PKCS15 emulation for A-Trust ACOS cards
 *
 * Copyright (C) 2005  Franz Brandl <brandl@a-trust.at> based on work from
 *                     Nils Larsch  <larsch@trustcenter.de>, TrustCenter AG
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

#include <opensc/pkcs15.h>
#include <opensc/cardctl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MANU_ID		"A-Trust"
#define CARD_LABEL	"a.sign Premium a"

int sc_pkcs15emu_atrust_acos_init_ex(sc_pkcs15_card_t *, sc_pkcs15emu_opt_t *);

typedef struct cdata_st {
	const char *label;
	int	    authority;
	const char *path;
	const char *id;
	int         obj_flags;
} cdata;

typedef struct pdata_st {
	const char *id;
	const char *label;
	const char *path;
	int         ref;
	int         type;
	unsigned int maxlen;
	unsigned int minlen;
	unsigned int storedlen;
	int         flags;	
	int         tries_left;
	const char  pad_char;
	int         obj_flags;
} pindata; 

typedef struct prdata_st {
	const char *id;
	const char *label;
	unsigned int modulus_len;
	int         usage;
	const char *path;
	int         ref;
	const char *auth_id;
	int         obj_flags;
} prdata;

static int get_cert_len(sc_card_t *card, sc_path_t *path)
{
	int r;
	u8  buf[8];

	r = sc_select_file(card, path, NULL);
	if (r < 0)
		return 0;
	r = sc_read_binary(card, 0, buf, sizeof(buf), 0);
	if (r < 0)	
		return 0;
	if (buf[0] != 0x30 || buf[1] != 0x82)
		return 0;
	path->index = 0;
	path->count = ((((size_t) buf[2]) << 8) | buf[3]) + 4;
	return 1;
} 

static int acos_detect_card(sc_pkcs15_card_t *p15card)
{
	int       r;
	u8        buf[128];
	sc_path_t path;
	sc_card_t *card = p15card->card;

	/* check if we have the correct card OS */
	if (strcmp(card->name, "A-TRUST ACOS"))
		return SC_ERROR_WRONG_CARD;
	/* read EF_CIN_CSN file */
	sc_format_path("DF71D001", &path);
	sc_ctx_suppress_errors_on(card->ctx);
	r = sc_select_file(card, &path, NULL);
	sc_ctx_suppress_errors_off(card->ctx);
	if (r != SC_SUCCESS)
		return SC_ERROR_WRONG_CARD;
	r = sc_read_binary(card, 0, buf, 8, 0);
	if (r != 8)
		return SC_ERROR_WRONG_CARD;

	return SC_SUCCESS;
}

static int sc_pkcs15emu_atrust_acos_init(sc_pkcs15_card_t *p15card)
{
	const cdata certs[] = {
		{"C.CH.EKEY", 0, "DF71C001","1", 0},/* Decryption Certificate */
#if 0
		{"C.CH.DS",   0, "DF70C002","2", 0},/* Signature Certificate */
#endif
		{NULL, 0, NULL, 0, 0}
	};

	const pindata pins[] = {
		{ "01", "PIN.DEC", "3F00DF71", 0x81, /* Decryption PIN */
		  SC_PKCS15_PIN_TYPE_ASCII_NUMERIC,
		  4, 4, 8, SC_PKCS15_PIN_FLAG_NEEDS_PADDING |
		  SC_PKCS15_PIN_FLAG_LOCAL, -1, 0x00,
		  SC_PKCS15_CO_FLAG_MODIFIABLE | SC_PKCS15_CO_FLAG_PRIVATE },
#if 0
		{ "02", "PIN.SIG", "3F00DF70", 0x81, /* Signature PIN */
		  SC_PKCS15_PIN_TYPE_ASCII_NUMERIC,
		  6, 6, 8, SC_PKCS15_PIN_FLAG_NEEDS_PADDING |
		  SC_PKCS15_PIN_FLAG_LOCAL, -1, 0x00,
		  SC_PKCS15_CO_FLAG_MODIFIABLE | SC_PKCS15_CO_FLAG_PRIVATE }, 
		{ "03", "PIN.INF", "3F00DF71", 0x83, /* Infobox PIN */
		  SC_PKCS15_PIN_TYPE_ASCII_NUMERIC,
		  4, 4, 8, SC_PKCS15_PIN_FLAG_NEEDS_PADDING |
		  SC_PKCS15_PIN_FLAG_LOCAL, -1, 0x00,
		  SC_PKCS15_CO_FLAG_MODIFIABLE | SC_PKCS15_CO_FLAG_PRIVATE },
#endif
		{ NULL, NULL, NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0} 
	};

	const prdata prkeys[] = {
		{ "1", "SK.CH.EKEY", 1536,
			SC_PKCS15_PRKEY_USAGE_SIGN | SC_PKCS15_PRKEY_USAGE_DECRYPT | SC_PKCS15_PRKEY_USAGE_UNWRAP,
		  "", /* do not specify file here to prevent reset of security state */
		  0x88, "01", SC_PKCS15_CO_FLAG_PRIVATE},
#if 0
		{ "2", "SK.CH.DS", 192,
			SC_PKCS15_PRKEY_USAGE_SIGN,
		  "", /* do not specify file here to prevent reset of security state */
		  0x88, "02", SC_PKCS15_CO_FLAG_PRIVATE},
#endif
		{ NULL, NULL, 0, 0, NULL, 0, NULL, 0}
	};

	int    r, i;
	u8     buf[256];
	char   buf2[256];
	sc_path_t path;
	sc_file_t *file = NULL;
	sc_card_t *card = p15card->card;

	/* get serial number */

	/* read EF_CIN_CSN file */
	sc_format_path("DF71D001", &path);
	sc_ctx_suppress_errors_on(card->ctx);
	r = sc_select_file(card, &path, NULL);
	sc_ctx_suppress_errors_off(card->ctx);
	if (r != SC_SUCCESS)
		return SC_ERROR_INTERNAL;
	r = sc_read_binary(card, 0, buf, 8, 0);
	if (r != 8)
		return SC_ERROR_INTERNAL;
	r = sc_bin_to_hex(buf, 8, buf2, sizeof(buf2), 0);
	if (r != SC_SUCCESS)
		return SC_ERROR_INTERNAL;
	if (p15card->serial_number)
		free(p15card->serial_number);
	p15card->serial_number = (char *) malloc(strlen(buf2) + 1);
	if (!p15card->serial_number)
		return SC_ERROR_INTERNAL;
	strcpy(p15card->serial_number, buf2);

	/* the TokenInfo version number */
	p15card->version = 0;

	/* manufacturer ID */
	if (p15card->manufacturer_id)
		free(p15card->manufacturer_id);
	p15card->manufacturer_id = (char *) malloc(strlen(MANU_ID) + 1);
	if (!p15card->manufacturer_id)
		return SC_ERROR_INTERNAL;
	strcpy(p15card->manufacturer_id, MANU_ID);

	/* card label */
	if (p15card->label)
		free(p15card->label);
	p15card->label = (char *) malloc(strlen(CARD_LABEL) + 1);
	if (!p15card->label)
		return SC_ERROR_INTERNAL;
	strcpy(p15card->label, CARD_LABEL);

	/* set certs */
	for (i = 0; certs[i].label; i++) {
		struct sc_pkcs15_cert_info cert_info;
		struct sc_pkcs15_object    cert_obj;

		memset(&cert_info, 0, sizeof(cert_info));
		memset(&cert_obj,  0, sizeof(cert_obj));

		sc_pkcs15_format_id(certs[i].id, &cert_info.id);
		cert_info.authority = certs[i].authority;
		sc_format_path(certs[i].path, &cert_info.path);
		if (!get_cert_len(card, &cert_info.path))
			/* skip errors */
			continue;

		strncpy(cert_obj.label, certs[i].label, SC_PKCS15_MAX_LABEL_SIZE - 1);
		cert_obj.flags = certs[i].obj_flags;

		r = sc_pkcs15emu_add_x509_cert(p15card, &cert_obj, &cert_info);
		if (r < 0)
			return SC_ERROR_INTERNAL;
	}
	/* set pins */
	for (i = 0; pins[i].label; i++) {
		struct sc_pkcs15_pin_info pin_info;
		struct sc_pkcs15_object   pin_obj;

		memset(&pin_info, 0, sizeof(pin_info));
		memset(&pin_obj,  0, sizeof(pin_obj));

		sc_pkcs15_format_id(pins[i].id, &pin_info.auth_id);
		pin_info.reference     = pins[i].ref;
		pin_info.flags         = pins[i].flags;
		pin_info.type          = pins[i].type;
		pin_info.min_length    = pins[i].minlen;
		pin_info.stored_length = pins[i].storedlen;
		pin_info.max_length    = pins[i].maxlen;
		pin_info.pad_char      = pins[i].pad_char;
		sc_format_path(pins[i].path, &pin_info.path);
		pin_info.tries_left    = -1;

		strncpy(pin_obj.label, pins[i].label, SC_PKCS15_MAX_LABEL_SIZE - 1);
		pin_obj.flags = pins[i].obj_flags;

		r = sc_pkcs15emu_add_pin_obj(p15card, &pin_obj, &pin_info);
		if (r < 0)
			return SC_ERROR_INTERNAL;
	}
	/* set private keys */
	for (i = 0; prkeys[i].label; i++) {
		struct sc_pkcs15_prkey_info prkey_info;
		struct sc_pkcs15_object     prkey_obj;

		memset(&prkey_info, 0, sizeof(prkey_info));
		memset(&prkey_obj,  0, sizeof(prkey_obj));

		sc_pkcs15_format_id(prkeys[i].id, &prkey_info.id);
		prkey_info.usage         = prkeys[i].usage;
		prkey_info.native        = 1;
		prkey_info.key_reference = prkeys[i].ref;
		prkey_info.modulus_length= prkeys[i].modulus_len;
		sc_format_path(prkeys[i].path, &prkey_info.path);

		strncpy(prkey_obj.label, prkeys[i].label, SC_PKCS15_MAX_LABEL_SIZE - 1);
		prkey_obj.flags = prkeys[i].obj_flags;
		if (prkeys[i].auth_id)
			sc_pkcs15_format_id(prkeys[i].auth_id, &prkey_obj.auth_id);

		r = sc_pkcs15emu_add_rsa_prkey(p15card, &prkey_obj, &prkey_info);
		if (r < 0)
			return SC_ERROR_INTERNAL;
	}
		
	/* select the application DF */
	sc_format_path("DF71", &path);
	r = sc_select_file(card, &path, &file);
	if (r != SC_SUCCESS || !file)
		return SC_ERROR_INTERNAL;
	/* set the application DF */
	if (p15card->file_app)
		free(p15card->file_app);
	p15card->file_app = file;

	return SC_SUCCESS;
}

int sc_pkcs15emu_atrust_acos_init_ex(sc_pkcs15_card_t *p15card,
				  sc_pkcs15emu_opt_t *opts)
{

	if (opts && opts->flags & SC_PKCS15EMU_FLAGS_NO_CHECK)
		return sc_pkcs15emu_atrust_acos_init(p15card);
	else {
		int r = acos_detect_card(p15card);
		if (r)
			return SC_ERROR_WRONG_CARD;
		return sc_pkcs15emu_atrust_acos_init(p15card);
	}
}
