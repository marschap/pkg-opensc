/*
 * pkcs15-cert.c: PKCS #15 certificate functions
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
#include "pkcs15.h"
#include "asn1.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <assert.h>

static int parse_x509_cert(struct sc_context *ctx, const u8 *buf, size_t buflen, struct sc_pkcs15_cert *cert)
{
	int r;
	struct sc_algorithm_id pk_alg, sig_alg;
	sc_pkcs15_der_t pk = { NULL, 0 };
	struct sc_asn1_entry asn1_version[] = {
		{ "version",		SC_ASN1_INTEGER,   ASN1_INTEGER, 0, &cert->version },
		{ NULL }
	};
	struct sc_asn1_entry asn1_pkinfo[] = {
		{ "algorithm",		SC_ASN1_ALGORITHM_ID,  ASN1_SEQUENCE | SC_ASN1_CONS, 0, &pk_alg },
		{ "subjectPublicKey",	SC_ASN1_BIT_STRING_NI, ASN1_BIT_STRING, SC_ASN1_ALLOC, &pk.value, &pk.len },
		{ NULL }
	};
	struct sc_asn1_entry asn1_x509v3[] = {
		{ "certificatePolicies",	SC_ASN1_OCTET_STRING, SC_ASN1_SEQUENCE | SC_ASN1_CONS, SC_ASN1_OPTIONAL, NULL },
		{ "subjectKeyIdentifier",	SC_ASN1_OCTET_STRING, SC_ASN1_SEQUENCE | SC_ASN1_CONS, SC_ASN1_OPTIONAL, NULL },
		{ "crlDistributionPoints",	SC_ASN1_OCTET_STRING, SC_ASN1_SEQUENCE | SC_ASN1_CONS, SC_ASN1_OPTIONAL | SC_ASN1_ALLOC, &cert->crl, &cert->crl_len },
		{ "authorityKeyIdentifier",	SC_ASN1_OCTET_STRING, SC_ASN1_SEQUENCE | SC_ASN1_CONS, SC_ASN1_OPTIONAL, NULL },
		{ "keyUsage",			SC_ASN1_BOOLEAN, SC_ASN1_SEQUENCE | SC_ASN1_CONS, SC_ASN1_OPTIONAL, NULL },
		{ NULL }
	};
	struct sc_asn1_entry asn1_extensions[] = {
		{ "x509v3",		SC_ASN1_STRUCT,    ASN1_SEQUENCE | SC_ASN1_CONS, SC_ASN1_OPTIONAL, asn1_x509v3 },
		{ NULL }
	};
	struct sc_asn1_entry asn1_tbscert[] = {
		{ "version",		SC_ASN1_STRUCT,    SC_ASN1_CTX | 0 | SC_ASN1_CONS, SC_ASN1_OPTIONAL, asn1_version },
		{ "serialNumber",	SC_ASN1_OCTET_STRING, ASN1_INTEGER, SC_ASN1_ALLOC, &cert->serial, &cert->serial_len },
		{ "signature",		SC_ASN1_STRUCT,    ASN1_SEQUENCE | SC_ASN1_CONS, 0, NULL },
		{ "issuer",		SC_ASN1_OCTET_STRING, ASN1_SEQUENCE | SC_ASN1_CONS, SC_ASN1_ALLOC, &cert->issuer, &cert->issuer_len },
		{ "validity",		SC_ASN1_STRUCT,    ASN1_SEQUENCE | SC_ASN1_CONS, 0, NULL },
		{ "subject",		SC_ASN1_OCTET_STRING, ASN1_SEQUENCE | SC_ASN1_CONS, SC_ASN1_ALLOC, &cert->subject, &cert->subject_len },
		{ "subjectPublicKeyInfo",SC_ASN1_STRUCT,   ASN1_SEQUENCE | SC_ASN1_CONS, 0, asn1_pkinfo },
		{ "extensions",		SC_ASN1_STRUCT,    SC_ASN1_CTX | 3 | SC_ASN1_CONS, SC_ASN1_OPTIONAL, asn1_extensions },
		{ NULL }
	};
	struct sc_asn1_entry asn1_cert[] = {
		{ "tbsCertificate",	SC_ASN1_STRUCT,    ASN1_SEQUENCE | SC_ASN1_CONS, 0, asn1_tbscert },
		{ "signatureAlgorithm",	SC_ASN1_ALGORITHM_ID, ASN1_SEQUENCE | SC_ASN1_CONS, 0, &sig_alg },
		{ "signatureValue",	SC_ASN1_BIT_STRING,ASN1_BIT_STRING, 0, NULL, 0 },
		{ NULL }
	};
	const u8 *obj;
	size_t objlen;
	
	memset(cert, 0, sizeof(*cert));
	obj = sc_asn1_verify_tag(ctx, buf, buflen, ASN1_SEQUENCE | SC_ASN1_CONS,
				 &objlen);
	if (obj == NULL) {
		sc_error(ctx, "X.509 certificate not found\n");
		return SC_ERROR_INVALID_ASN1_OBJECT;
	}
	cert->data_len = objlen + (obj - buf);
	r = sc_asn1_decode(ctx, asn1_cert, obj, objlen, NULL, NULL);
	SC_TEST_RET(ctx, r, "ASN.1 parsing of certificate failed");

	cert->version++;

	cert->key.algorithm = pk_alg.algorithm;
	pk.len >>= 3;	/* convert number of bits to bytes */
	cert->key.data = pk;

	r = sc_pkcs15_decode_pubkey(ctx, &cert->key, pk.value, pk.len);
	if (r < 0)
		free(pk.value);
	sc_asn1_clear_algorithm_id(&pk_alg);
	sc_asn1_clear_algorithm_id(&sig_alg);

	return r;
}

int sc_pkcs15_read_certificate(struct sc_pkcs15_card *p15card,
			       const struct sc_pkcs15_cert_info *info,
			       struct sc_pkcs15_cert **cert_out)
{
	int r;
	struct sc_pkcs15_cert *cert;
	u8 *data = NULL;
	size_t len;
	
	assert(p15card != NULL && info != NULL && cert_out != NULL);
	SC_FUNC_CALLED(p15card->card->ctx, 1);

	if (info->path.len) {
		r = sc_pkcs15_read_file(p15card, &info->path, &data, &len, NULL);
		if (r)
			return r;
	} else {
		sc_pkcs15_der_t copy;

		sc_der_copy(&copy, &info->value);
		data = copy.value;
		len = copy.len;
	}

	cert = (struct sc_pkcs15_cert *) malloc(sizeof(struct sc_pkcs15_cert));
	if (cert == NULL) {
		free(data);
		return SC_ERROR_OUT_OF_MEMORY;
	}
	memset(cert, 0, sizeof(struct sc_pkcs15_cert));
	if (parse_x509_cert(p15card->card->ctx, data, len, cert)) {
		free(data);
		free(cert);
		return SC_ERROR_INVALID_ASN1_OBJECT;
	}
	cert->data = data;
	*cert_out = cert;
	return 0;
}

static const struct sc_asn1_entry c_asn1_cred_ident[] = {
	{ "idType",	SC_ASN1_INTEGER,      ASN1_INTEGER, 0, NULL },
	{ "idValue",	SC_ASN1_OCTET_STRING, ASN1_OCTET_STRING, 0, NULL },
	{ NULL }
};
static const struct sc_asn1_entry c_asn1_com_cert_attr[] = {
	{ "iD",         SC_ASN1_PKCS15_ID, ASN1_OCTET_STRING, 0, NULL },
	{ "authority",  SC_ASN1_BOOLEAN,   ASN1_BOOLEAN, SC_ASN1_OPTIONAL, NULL },
	{ "identifier", SC_ASN1_STRUCT,    ASN1_SEQUENCE | SC_ASN1_CONS, SC_ASN1_OPTIONAL, NULL },
	/* FIXME: Add rest of the optional fields */
	{ NULL }
};
static const struct sc_asn1_entry c_asn1_x509_cert_value_choice[] = {
	{ "path",	SC_ASN1_PATH,	   ASN1_SEQUENCE | SC_ASN1_CONS, SC_ASN1_OPTIONAL, NULL },
	{ "direct",	SC_ASN1_OCTET_STRING, SC_ASN1_CTX | 0 | SC_ASN1_CONS, SC_ASN1_OPTIONAL | SC_ASN1_ALLOC, NULL },
	{ NULL }
};
static const struct sc_asn1_entry c_asn1_x509_cert_attr[] = {
	{ "value",	SC_ASN1_CHOICE, 0, 0, NULL },
	{ NULL }
};
static const struct sc_asn1_entry c_asn1_type_cert_attr[] = {
	{ "x509CertificateAttributes", SC_ASN1_STRUCT, ASN1_SEQUENCE | SC_ASN1_CONS, 0, NULL },
	{ NULL }
};
static const struct sc_asn1_entry c_asn1_cert[] = {
	{ "x509Certificate", SC_ASN1_PKCS15_OBJECT, ASN1_SEQUENCE | SC_ASN1_CONS, 0, NULL },
	{ NULL }
};

int sc_pkcs15_decode_cdf_entry(struct sc_pkcs15_card *p15card,
			       struct sc_pkcs15_object *obj,
			       const u8 ** buf, size_t *buflen)
{
        struct sc_context *ctx = p15card->card->ctx;
	struct sc_pkcs15_cert_info info;
	struct sc_asn1_entry	asn1_cred_ident[3], asn1_com_cert_attr[4],
				asn1_x509_cert_attr[2], asn1_type_cert_attr[2],
				asn1_cert[2], asn1_x509_cert_value_choice[3];
	struct sc_asn1_pkcs15_object cert_obj = { obj, asn1_com_cert_attr, NULL,
					     asn1_type_cert_attr };
	sc_pkcs15_der_t *der = &info.value;
	u8 id_value[128];
	int id_type;
	size_t id_value_len = sizeof(id_value);
	int r;

	sc_copy_asn1_entry(c_asn1_cred_ident, asn1_cred_ident);
	sc_copy_asn1_entry(c_asn1_com_cert_attr, asn1_com_cert_attr);
	sc_copy_asn1_entry(c_asn1_x509_cert_attr, asn1_x509_cert_attr);
	sc_copy_asn1_entry(c_asn1_x509_cert_value_choice, asn1_x509_cert_value_choice);
	sc_copy_asn1_entry(c_asn1_type_cert_attr, asn1_type_cert_attr);
	sc_copy_asn1_entry(c_asn1_cert, asn1_cert);
	
	sc_format_asn1_entry(asn1_cred_ident + 0, &id_type, NULL, 0);
	sc_format_asn1_entry(asn1_cred_ident + 1, &id_value, &id_value_len, 0);
	sc_format_asn1_entry(asn1_com_cert_attr + 0, &info.id, NULL, 0);
	sc_format_asn1_entry(asn1_com_cert_attr + 1, &info.authority, NULL, 0);
	sc_format_asn1_entry(asn1_com_cert_attr + 2, asn1_cred_ident, NULL, 0);
	sc_format_asn1_entry(asn1_x509_cert_attr + 0, asn1_x509_cert_value_choice, NULL, 0);
	sc_format_asn1_entry(asn1_x509_cert_value_choice + 0, &info.path, NULL, 0);
	sc_format_asn1_entry(asn1_x509_cert_value_choice + 1, &der->value, &der->len, 0);
	sc_format_asn1_entry(asn1_type_cert_attr + 0, asn1_x509_cert_attr, NULL, 0);
	sc_format_asn1_entry(asn1_cert + 0, &cert_obj, NULL, 0);

        /* Fill in defaults */
        memset(&info, 0, sizeof(info));
	info.authority = 0;
	
	r = sc_asn1_decode(ctx, asn1_cert, *buf, *buflen, buf, buflen);
	/* In case of error, trash the cert value (direct coding) */
	if (r < 0 && der->value)
		free(der->value);
	if (r == SC_ERROR_ASN1_END_OF_CONTENTS)
		return r;
	SC_TEST_RET(ctx, r, "ASN.1 decoding failed");
	obj->type = SC_PKCS15_TYPE_CERT_X509;
	obj->data = malloc(sizeof(info));
	if (obj->data == NULL)
		SC_FUNC_RETURN(ctx, 0, SC_ERROR_OUT_OF_MEMORY);
	memcpy(obj->data, &info, sizeof(info));

	return 0;
}

int sc_pkcs15_encode_cdf_entry(struct sc_context *ctx,
			       const struct sc_pkcs15_object *obj,
			       u8 **buf, size_t *bufsize)
{
	struct sc_asn1_entry	asn1_cred_ident[3], asn1_com_cert_attr[4],
				asn1_x509_cert_attr[2], asn1_type_cert_attr[2],
				asn1_cert[2], asn1_x509_cert_value_choice[3];
	struct sc_pkcs15_cert_info *infop = (sc_pkcs15_cert_info_t *) obj->data;
	sc_pkcs15_der_t *der = &infop->value;
	struct sc_asn1_pkcs15_object cert_obj = { (struct sc_pkcs15_object *) obj,
							asn1_com_cert_attr, NULL,
							asn1_type_cert_attr };
	int r;

	sc_copy_asn1_entry(c_asn1_cred_ident, asn1_cred_ident);
	sc_copy_asn1_entry(c_asn1_com_cert_attr, asn1_com_cert_attr);
	sc_copy_asn1_entry(c_asn1_x509_cert_attr, asn1_x509_cert_attr);
	sc_copy_asn1_entry(c_asn1_x509_cert_value_choice, asn1_x509_cert_value_choice);
	sc_copy_asn1_entry(c_asn1_type_cert_attr, asn1_type_cert_attr);
	sc_copy_asn1_entry(c_asn1_cert, asn1_cert);
	
	sc_format_asn1_entry(asn1_com_cert_attr + 0, (void *) &infop->id, NULL, 1);
	if (infop->authority)
		sc_format_asn1_entry(asn1_com_cert_attr + 1, (void *) &infop->authority, NULL, 1);
	if (infop->path.len || !der->value) {
		sc_format_asn1_entry(asn1_x509_cert_value_choice + 0, &infop->path, NULL, 1);
	} else {
		sc_format_asn1_entry(asn1_x509_cert_value_choice + 1, der->value, &der->len, 1);
	}
	sc_format_asn1_entry(asn1_type_cert_attr + 0, &asn1_x509_cert_value_choice, NULL, 1);
	sc_format_asn1_entry(asn1_cert + 0, (void *) &cert_obj, NULL, 1);

	r = sc_asn1_encode(ctx, asn1_cert, buf, bufsize);

	return r;
}

void sc_pkcs15_free_certificate(struct sc_pkcs15_cert *cert)
{
	assert(cert != NULL);

	sc_pkcs15_erase_pubkey(&cert->key);
	free(cert->subject);
	free(cert->issuer);
	free(cert->serial);
	free(cert->data);
	free(cert->crl);
	free(cert);
}
