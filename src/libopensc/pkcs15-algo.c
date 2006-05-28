/*
 * pkc15-algo.c: ASN.1 handling for algorithm IDs and parameters
 *
 * Copyright (C) 2001, 2002  Olaf Kirch <okir@lst.de>
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
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <stdlib.h>

/*
 * AlgorithmIdentifier handling
 */
static struct sc_asn1_entry	c_asn1_des_iv[] = {
	{ "iv",	SC_ASN1_OCTET_STRING, SC_ASN1_TAG_OCTET_STRING, 0, NULL, NULL },
	{ NULL, 0, 0, 0, NULL, NULL }
};

static int
asn1_decode_des_params(sc_context_t *ctx, void **paramp,
				const u8 *buf, size_t buflen, int depth)
{
	struct sc_asn1_entry asn1_des_iv[2];
	u8	iv[8];
	int	ivlen = 8, r;

	sc_copy_asn1_entry(c_asn1_des_iv, asn1_des_iv);
	sc_format_asn1_entry(asn1_des_iv + 0, iv, &ivlen, 0);
	r = _sc_asn1_decode(ctx, asn1_des_iv, buf, buflen, NULL, NULL, 0, depth + 1);
	if (r < 0)
		return r;
	if (ivlen != 8)
		return SC_ERROR_INVALID_ASN1_OBJECT;
	*paramp = malloc(8);
	if (!*paramp)
		return SC_ERROR_OUT_OF_MEMORY;
	memcpy(*paramp, iv, 8);
	return 0;
}

static int
asn1_encode_des_params(sc_context_t *ctx, void *params,
				u8 **buf, size_t *buflen, int depth)
{
	struct sc_asn1_entry asn1_des_iv[2];
	int	ivlen = 8;

	sc_copy_asn1_entry(c_asn1_des_iv, asn1_des_iv);
	sc_format_asn1_entry(asn1_des_iv + 0, params, &ivlen, 1);
	return _sc_asn1_encode(ctx, asn1_des_iv, buf, buflen, depth + 1);
}

static const struct sc_asn1_entry	c_asn1_pbkdf2_params[] = {
	{ "salt",	SC_ASN1_OCTET_STRING, SC_ASN1_TAG_OCTET_STRING, 0, NULL, NULL },
	{ "count",	SC_ASN1_INTEGER, SC_ASN1_TAG_INTEGER, 0, NULL, NULL },
	{ "keyLength",	SC_ASN1_INTEGER, SC_ASN1_TAG_INTEGER, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "prf",	SC_ASN1_ALGORITHM_ID, SC_ASN1_TAG_SEQUENCE, SC_ASN1_OPTIONAL, NULL, NULL },
	{ NULL, 0, 0, 0, NULL, NULL }
};

static int
asn1_decode_pbkdf2_params(sc_context_t *ctx, void **paramp,
				const u8 *buf, size_t buflen, int depth)
{
	struct sc_pbkdf2_params info;
	struct sc_asn1_entry asn1_pbkdf2_params[5];
	int r;

	sc_copy_asn1_entry(c_asn1_pbkdf2_params, asn1_pbkdf2_params);
	sc_format_asn1_entry(asn1_pbkdf2_params + 0,
			info.salt, &info.salt_len, 0);
	sc_format_asn1_entry(asn1_pbkdf2_params + 1,
			&info.iterations, NULL, 0);
	sc_format_asn1_entry(asn1_pbkdf2_params + 2,
			&info.key_length, NULL, 0);
	sc_format_asn1_entry(asn1_pbkdf2_params + 3,
			&info.hash_alg, NULL, 0);

	memset(&info, 0, sizeof(info));
	info.salt_len = sizeof(info.salt);
	info.hash_alg.algorithm = SC_ALGORITHM_SHA1;

	r = _sc_asn1_decode(ctx, asn1_pbkdf2_params, buf, buflen, NULL, NULL, 0, depth + 1);
	if (r < 0)
		return r;

	*paramp = malloc(sizeof(info));
	if (!*paramp)
		return SC_ERROR_OUT_OF_MEMORY;
	memcpy(*paramp, &info, sizeof(info));
	return 0;
}

static int
asn1_encode_pbkdf2_params(sc_context_t *ctx, void *params,
				u8 **buf, size_t *buflen, int depth)
{
	struct sc_pbkdf2_params *info;
	struct sc_asn1_entry asn1_pbkdf2_params[5];

	info = (struct sc_pbkdf2_params *) params;

	sc_copy_asn1_entry(c_asn1_pbkdf2_params, asn1_pbkdf2_params);
	sc_format_asn1_entry(asn1_pbkdf2_params + 0,
			info->salt, &info->salt_len, 1);
	sc_format_asn1_entry(asn1_pbkdf2_params + 1,
			&info->iterations, NULL, 1);
	if (info->key_length > 0)
		sc_format_asn1_entry(asn1_pbkdf2_params + 2,
				&info->key_length, NULL, 1);
	if (info->hash_alg.algorithm != SC_ALGORITHM_SHA1)
		sc_format_asn1_entry(asn1_pbkdf2_params + 3,
				&info->hash_alg, NULL, 0);

	return _sc_asn1_encode(ctx, asn1_pbkdf2_params, buf, buflen, depth + 1);
}

static const struct sc_asn1_entry	c_asn1_pbes2_params[] = {
	{ "keyDerivationAlg", SC_ASN1_ALGORITHM_ID, SC_ASN1_TAG_SEQUENCE, 0, NULL, NULL },
	{ "keyEcnryptionAlg", SC_ASN1_ALGORITHM_ID, SC_ASN1_TAG_SEQUENCE, 0, NULL, NULL },
	{ NULL, 0, 0, 0, NULL, NULL }
};

static int
asn1_decode_pbes2_params(sc_context_t *ctx, void **paramp,
				const u8 *buf, size_t buflen, int depth)
{
	struct sc_asn1_entry asn1_pbes2_params[3];
	struct sc_pbes2_params info;
	int r;

	sc_copy_asn1_entry(c_asn1_pbes2_params, asn1_pbes2_params);
	sc_format_asn1_entry(asn1_pbes2_params + 0,
				&info.derivation_alg, NULL, 0);
	sc_format_asn1_entry(asn1_pbes2_params + 1,
				&info.key_encr_alg, NULL, 0);
	memset(&info, 0, sizeof(info));

	r = _sc_asn1_decode(ctx, asn1_pbes2_params, buf, buflen, NULL, NULL, 0, depth + 1);
	if (r < 0)
		return r;
	*paramp = malloc(sizeof(info));
	if (!*paramp)
		return SC_ERROR_OUT_OF_MEMORY;
	memcpy(*paramp, &info, sizeof(info));
	return 0;
}

static int
asn1_encode_pbes2_params(sc_context_t *ctx, void *params,
				u8 **buf, size_t *buflen, int depth)
{
	struct sc_asn1_entry asn1_pbes2_params[3];
	struct sc_pbes2_params *info;

	info = (struct sc_pbes2_params *) params;
	sc_copy_asn1_entry(c_asn1_pbes2_params, asn1_pbes2_params);
	sc_format_asn1_entry(asn1_pbes2_params + 0,
				&info->derivation_alg, NULL, 0);
	sc_format_asn1_entry(asn1_pbes2_params + 1,
				&info->key_encr_alg, NULL, 0);
	return _sc_asn1_encode(ctx, asn1_pbes2_params, buf, buflen, depth + 1);
}

static void
asn1_free_pbes2_params(void *ptr)
{
	struct sc_pbes2_params *params = (struct sc_pbes2_params *) ptr;

	sc_asn1_clear_algorithm_id(&params->derivation_alg);
	sc_asn1_clear_algorithm_id(&params->key_encr_alg);
	free(params);
}

static struct sc_asn1_pkcs15_algorithm_info algorithm_table[] = {
#ifdef SC_ALGORITHM_SHA1
	/* hmacWithSHA1 */
	{ SC_ALGORITHM_SHA1, {{ 1, 2, 840, 113549, 2, 7 }}, NULL, NULL, NULL },
	{ SC_ALGORITHM_SHA1, {{ 1, 3, 6, 1, 5, 5, 8, 1, 2 }}, NULL, NULL, NULL },
	/* SHA1 */
	{ SC_ALGORITHM_SHA1, {{ 1, 3, 14, 3, 2, 26, }}, NULL, NULL, NULL },
#endif
#ifdef SC_ALGORITHM_MD5
	{ SC_ALGORITHM_MD5, {{ 1, 2, 840, 113549, 2, 5, }}, NULL, NULL, NULL },
#endif
#ifdef SC_ALGORITHM_DSA
	{ SC_ALGORITHM_DSA, {{ 1, 2, 840, 10040, 4, 3 }}, NULL, NULL, NULL },
#endif
#ifdef SC_ALGORITHM_RSA /* really rsaEncryption */
	{ SC_ALGORITHM_RSA, {{ 1, 2, 840, 113549, 1, 1, 1 }}, NULL, NULL, NULL },
#endif
#ifdef SC_ALGORITHM_DH
	{ SC_ALGORITHM_DH, {{ 1, 2, 840, 10046, 2, 1 }}, NULL, NULL, NULL },
#endif
#ifdef SC_ALGORITHM_RC2_WRAP /* from CMS */
	{ SC_ALGORITHM_RC2_WRAP,  {{ 1, 2, 840, 113549, 1, 9, 16, 3, 7 }}, NULL, NULL, NULL },
#endif
#ifdef SC_ALGORITHM_RC2 /* CBC mode */
	{ SC_ALGORITHM_RC2, {{ 1, 2, 840, 113549, 3, 2 }},
			asn1_decode_rc2_params,
			asn1_encode_rc2_params },
#endif
#ifdef SC_ALGORITHM_DES /* CBC mode */
	{ SC_ALGORITHM_DES, {{ 1, 3, 14, 3, 2, 7 }},
			asn1_decode_des_params,
			asn1_encode_des_params,
			free },
#endif
#ifdef SC_ALGORITHM_3DES_WRAP /* from CMS */
	{ SC_ALGORITHM_3DES_WRAP, {{ 1, 2, 840, 113549, 1, 9, 16, 3, 6 }}, NULL, NULL, NULL },
#endif
#ifdef SC_ALGORITHM_3DES /* EDE CBC mode */
	{ SC_ALGORITHM_3DES, {{ 1, 2, 840, 113549, 3, 7 }},
			asn1_decode_des_params,
			asn1_encode_des_params,
			free },
#endif
/* We do not support PBES1 because the encryption is weak */
#ifdef SC_ALGORITHM_PBKDF2
	{ SC_ALGORITHM_PBKDF2, {{ 1, 2, 840, 113549, 1, 5, 12 }},
			asn1_decode_pbkdf2_params,
			asn1_encode_pbkdf2_params,
			free },
#endif
#ifdef SC_ALGORITHM_PBES2
	{ SC_ALGORITHM_PBES2, {{ 1, 2, 840, 113549, 1, 5, 13 }},
			asn1_decode_pbes2_params,
			asn1_encode_pbes2_params,
			asn1_free_pbes2_params },
	{ -1, {{ -1 }}, NULL, NULL, NULL }
#endif
};

static struct sc_asn1_pkcs15_algorithm_info *
sc_asn1_get_algorithm_info(const struct sc_algorithm_id *id)
{
	struct sc_asn1_pkcs15_algorithm_info *aip;

	aip = algorithm_table;
	if ((int) id->algorithm < 0) {
		while (aip->id >= 0) {
			const int	*oid1, *oid2;
			int		m;
			
			oid1 = aip->oid.value;
	                oid2 = id->obj_id.value;
			for (m = 0; m < SC_MAX_OBJECT_ID_OCTETS; m++) {
				if (oid1[m] == oid2[m])
					continue;
				if (oid1[m] > 0 || oid2[m] > 0)
					break;
				/* We have a match */
				return aip;
			}
			aip++;
		}
	} else {
		while (aip->id >= 0) {
			if (aip->id == (int)id->algorithm)
				return aip;
			aip++;
		}
	}
	return NULL;
}

static const struct sc_asn1_entry c_asn1_alg_id[6] = {
	{ "algorithm",  SC_ASN1_OBJECT, SC_ASN1_TAG_OBJECT, 0, NULL, NULL },
	{ "nullParam",  SC_ASN1_NULL, SC_ASN1_TAG_NULL, SC_ASN1_OPTIONAL, NULL, NULL },
	{ NULL, 0, 0, 0, NULL, NULL }
};

int
sc_asn1_decode_algorithm_id(sc_context_t *ctx, const u8 *in,
			    size_t len, struct sc_algorithm_id *id,
			    int depth)
{
	struct sc_asn1_pkcs15_algorithm_info *alg_info;
	struct sc_asn1_entry asn1_alg_id[3];
	int r;

	sc_copy_asn1_entry(c_asn1_alg_id, asn1_alg_id);
	sc_format_asn1_entry(asn1_alg_id + 0, &id->obj_id, NULL, 0);

	memset(id, 0, sizeof(*id));
	r = _sc_asn1_decode(ctx, asn1_alg_id, in, len, &in, &len, 0, depth + 1);
	if (r < 0)
		return r;

	/* See if we understand the algorithm, and if we do, check
	 * whether we know how to decode any additional parameters */
	id->algorithm = (unsigned int ) -1;
	if ((alg_info = sc_asn1_get_algorithm_info(id)) != NULL) {
		id->algorithm = alg_info->id;
		if (alg_info->decode) {
			if (asn1_alg_id[1].flags & SC_ASN1_PRESENT)
				return SC_ERROR_INVALID_ASN1_OBJECT;
			r = alg_info->decode(ctx, &id->params, in, len, depth);
		}
	}

	return r;
}

int
sc_asn1_encode_algorithm_id(sc_context_t *ctx,
			    u8 **buf, size_t *len,
			    const struct sc_algorithm_id *id,
			    int depth)
{
	struct sc_asn1_pkcs15_algorithm_info *alg_info;
	struct sc_algorithm_id temp_id;
	struct sc_asn1_entry asn1_alg_id[3];
	u8 *obj = NULL;
	size_t obj_len = 0;
	int r;
	u8 *tmp;

	alg_info = sc_asn1_get_algorithm_info(id);
	if (alg_info == NULL) {
		sc_error(ctx, "Cannot encode unknown algorithm %u.\n",
				id->algorithm);
		return SC_ERROR_INVALID_ARGUMENTS;
	}

	/* Set the oid if not yet given */
	if (id->obj_id.value[0] <= 0) {
		temp_id = *id;
		temp_id.obj_id = alg_info->oid;
		id = &temp_id;
	}

	sc_copy_asn1_entry(c_asn1_alg_id, asn1_alg_id);
	sc_format_asn1_entry(asn1_alg_id + 0, (void *) &id->obj_id, NULL, 1);

	/* no parameters, write NULL tag */
	if (!id->params || !alg_info->encode)
		asn1_alg_id[1].flags |= SC_ASN1_PRESENT;

	r = _sc_asn1_encode(ctx, asn1_alg_id, buf, len, depth + 1);
	if (r < 0)
		return r;

	/* Encode any parameters */
	if (id->params && alg_info->encode) {
		r = alg_info->encode(ctx, id->params, &obj, &obj_len, depth+1);
		if (r < 0) {
			if (obj)
				free(obj);
			return r;
		}
	}

	if (obj_len) {
		tmp = (u8 *) realloc(*buf, *len + obj_len);
		if (!tmp) {
			free(*buf);
			*buf = NULL;
			free(obj);
			return SC_ERROR_OUT_OF_MEMORY;
		}
		*buf = tmp;
		memcpy(*buf + *len, obj, obj_len);
		*len += obj_len;
		free(obj);
	}

	return 0;
}

void
sc_asn1_clear_algorithm_id(struct sc_algorithm_id *id)
{
	struct sc_asn1_pkcs15_algorithm_info *aip;

	if ((aip = sc_asn1_get_algorithm_info(id)) && aip->free)
		aip->free(id);
}
