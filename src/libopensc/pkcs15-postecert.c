/*
 * PKCS15 emulation layer for Postecert and Cnipa card.
 * To see how this works, run p15dump on your Postecert or Cnipa Card.
 *
 * Copyright (C) 2004, Antonino Iacono <ant_iacono@tin.it>
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
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
#include <opensc/log.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int sc_pkcs15emu_postecert_init_ex(sc_pkcs15_card_t *, sc_pkcs15emu_opt_t *);

static int (*set_security_env) (sc_card_t *, const sc_security_env_t *, int);

static int set_sec_env(sc_card_t * card, const sc_security_env_t *env,
		       int se_num)
{
	sc_security_env_t tenv = *env;
	if (tenv.operation == SC_SEC_OPERATION_SIGN)
		tenv.operation = SC_SEC_OPERATION_DECIPHER;
	return set_security_env(card, &tenv, se_num);
}

static int do_sign(sc_card_t * card, const u8 * in, size_t inlen, u8 * out,
		   size_t outlen)
{
	return card->ops->decipher(card, in, inlen, out, outlen);
}

static void set_string(char **strp, const char *value)
{
	if (*strp)
		free(*strp);
	*strp = value ? strdup(value) : NULL;
}

static int sc_pkcs15emu_postecert_init(sc_pkcs15_card_t * p15card)
{
	static int prkey_usage = SC_PKCS15_PRKEY_USAGE_NONREPUDIATION;
	static int authprkey_usage = SC_PKCS15_PRKEY_USAGE_SIGN
	    | SC_PKCS15_PRKEY_USAGE_SIGNRECOVER
	    | SC_PKCS15_PRKEY_USAGE_ENCRYPT | SC_PKCS15_PRKEY_USAGE_DECRYPT;

	sc_card_t *card = p15card->card;
	sc_path_t path;
	sc_pkcs15_id_t id, auth_id;
	unsigned char certlen[2];
	unsigned char *certi = NULL;
	int index_cert[4];
	int count_cert[4];
	int flags;
	int authority;
	size_t i, count;
	int r;
	int o = 0;

	const char *label = "User Non-repudiation Certificate";
	const char *calabel = "CA Certificate";
	const char *catmslabel = "CA TimeStamper Certificate";
	const char *authlabel = "User Authentication Certificate";

	const char *postecert_auth_cert_path = "504B0001";

	const char *authPIN = "Authentication PIN";
	const char *nonrepPIN = "Non-repudiation PIN";

	const char *authPRKEY = "Authentication Key";
	const char *nonrepPRKEY = "Non repudiation Key";

	/* Get the non-repudiation certificate length */
	sc_format_path(postecert_auth_cert_path, &path);

	if (sc_select_file(card, &path, NULL) < 0) {
		r = SC_ERROR_WRONG_CARD;
		goto failed;
	}

	set_string(&p15card->label, "Postecert & Cnipa Card");
	set_string(&p15card->manufacturer_id, "Postecert");
	set_string(&p15card->serial_number, "0000");

	sc_read_binary(card, 0, certlen, 2, 0);

	/* Now set the certificate offset/len */
	count = (certlen[0] << 8) + certlen[1];
	if (count < 256)
		return SC_ERROR_INTERNAL;

	certi = (unsigned char *) malloc(count);

	if (!certi)
		return SC_ERROR_OUT_OF_MEMORY;

	sc_read_binary(card, 0, certi, count - 500, 0);

	for (i = 2; i < (count - 256); i++) {
		/* this file contain more than one certificate */
		if (*(certi + i) == 0x30 && *(certi + i + 1) == 0x82
		    && *(certi + i + 4) == 0x30 && *(certi + i + 5) == 0x82
		    && *(certi + i + 2) > 1 && *(certi + i + 2) < 8
		    && *(certi + i + 6) <= *(certi + i + 2)) {
			index_cert[o] = i;
			count_cert[o] =
			    (*(certi + i + 2) << 8) + *(certi + i + 3) + 4;
			o++;
			if (o > 4)
				break;
			i += (*(certi + i + 2) << 8) + *(certi + i + 3);
		}
	}

	free(certi);

	path.index = index_cert[0];
	path.count = count_cert[0];

	id.value[0] = 1;
	id.len = 1;

	authority = 1;

	sc_pkcs15emu_add_cert(p15card,
			      SC_PKCS15_TYPE_CERT_X509, authority,
			      &path, &id, calabel, SC_PKCS15_CO_FLAG_MODIFIABLE);

	path.index = index_cert[1];
	path.count = count_cert[1];

	id.value[0] = 2;
	id.len = 1;

	authority = 1;

	sc_pkcs15emu_add_cert(p15card,
			      SC_PKCS15_TYPE_CERT_X509, authority,
			      &path, &id, catmslabel, SC_PKCS15_CO_FLAG_MODIFIABLE);

	path.index = index_cert[2];
	path.count = count_cert[2];

	id.value[0] = 3;
	id.len = 1;

	authority = 0;

	sc_pkcs15emu_add_cert(p15card,
			      SC_PKCS15_TYPE_CERT_X509, authority,
			      &path, &id, label, SC_PKCS15_CO_FLAG_MODIFIABLE);

	path.index = index_cert[3];
	path.count = count_cert[3];

	id.value[0] = 4;
	id.len = 1;

	sc_pkcs15emu_add_cert(p15card,
			      SC_PKCS15_TYPE_CERT_X509, authority,
			      &path, &id, authlabel, SC_PKCS15_CO_FLAG_MODIFIABLE);


	flags = SC_PKCS15_PIN_FLAG_CASE_SENSITIVE |
	    SC_PKCS15_PIN_FLAG_INITIALIZED | SC_PKCS15_PIN_FLAG_NEEDS_PADDING;

	/* add authentication PIN */
	sc_format_path("3F00504B", &path);
	id.value[0] = 1;
	sc_pkcs15emu_add_pin(p15card, &id,
			     authPIN, &path, 0x82,
			     SC_PKCS15_PIN_TYPE_ASCII_NUMERIC,
			     6, 14, flags, 3, 0,
			     SC_PKCS15_CO_FLAG_MODIFIABLE |
			     SC_PKCS15_CO_FLAG_PRIVATE);

	/* add authentication private key */
	id.value[0] = 4;
	auth_id.value[0] = 1;
	auth_id.len = 1;
	sc_pkcs15emu_add_prkey(p15card, &id,
			       authPRKEY,
			       SC_PKCS15_TYPE_PRKEY_RSA,
			       1024, authprkey_usage,
			       &path, 0x06, &auth_id, SC_PKCS15_CO_FLAG_PRIVATE);

	/* add non repudiation PIN */
	sc_format_path("3F00504B", &path);
	id.value[0] = 2;
	sc_pkcs15emu_add_pin(p15card, &id,
			     nonrepPIN, &path, 0x82,
			     SC_PKCS15_PIN_TYPE_ASCII_NUMERIC,
			     6, 14, flags, 3, 0,
			     SC_PKCS15_CO_FLAG_MODIFIABLE |
			     SC_PKCS15_CO_FLAG_PRIVATE);


	/* add non repudiation private key */
	id.value[0] = 3;
	auth_id.value[0] = 2;
	sc_pkcs15emu_add_prkey(p15card, &id,
			       nonrepPRKEY,
			       SC_PKCS15_TYPE_PRKEY_RSA,
			       1024, prkey_usage,
			       &path, 0x01, &auth_id, SC_PKCS15_CO_FLAG_PRIVATE);

	/* return to MF */
	sc_format_path("3F00", &path);
	sc_select_file(card, &path, NULL);
	{
		/* save old signature funcs */
		set_security_env = card->ops->set_security_env;
		/* set new one              */
		card->ops->set_security_env  = set_sec_env;
		card->ops->compute_signature = do_sign;
	}
	return 0;

failed:
	sc_error(card->ctx,
		 "Failed to initialize Postecert and Cnipa emulation: %s\n",
		 sc_strerror(r));
	return r;
}

static int postecert_detect_card(sc_pkcs15_card_t * p15card)
{
	sc_card_t *card = p15card->card;

	/* check if we have the correct card OS */
	if (strcmp(card->name, "CardOS M4"))
		return SC_ERROR_WRONG_CARD;
	return SC_SUCCESS;
}

int sc_pkcs15emu_postecert_init_ex(sc_pkcs15_card_t * p15card,
				   sc_pkcs15emu_opt_t * opts)
{
	if (opts && opts->flags & SC_PKCS15EMU_FLAGS_NO_CHECK)
		return sc_pkcs15emu_postecert_init(p15card);
	else {
		int r = postecert_detect_card(p15card);
		if (r)
			return SC_ERROR_WRONG_CARD;
		return sc_pkcs15emu_postecert_init(p15card);
	}
}
