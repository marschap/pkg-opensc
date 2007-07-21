/*
 * pkcs15-tool.c: Tool for poking with PKCS #15 smart cards
 *
 * Copyright (C) 2001  Juha Yrjölä <juha.yrjola@iki.fi>
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
#ifdef HAVE_OPENSSL
#ifdef _WIN32
typedef unsigned __int32 uint32_t;
#else
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#else
#warning no uint32_t type available, please contact opensc-devel@opensc-project.org
#endif
#endif
#include <openssl/bn.h>
#include <openssl/crypto.h>
#endif
#include <limits.h>
#include <opensc/pkcs15.h>
#include "util.h"

static const char *app_name = "pkcs15-tool";

static int opt_reader = -1, opt_wait = 0;
static int opt_no_cache = 0;
static char * opt_auth_id;
static char * opt_cert = NULL;
static char * opt_data = NULL;
static char * opt_pubkey = NULL;
static char * opt_outfile = NULL;
static u8 * opt_newpin = NULL;
static u8 * opt_pin = NULL;
static u8 * opt_puk = NULL;

static int	verbose = 0;

enum {
	OPT_CHANGE_PIN = 0x100,
	OPT_LIST_PINS,
	OPT_READER,
	OPT_PIN_ID,
	OPT_NO_CACHE,
	OPT_LIST_PUB,
	OPT_READ_PUB,
#if defined(HAVE_OPENSSL) && (defined(_WIN32) || defined(HAVE_INTTYPES_H))
	OPT_READ_SSH,
#endif
	OPT_PIN,
	OPT_NEWPIN,
	OPT_PUK,
};

#define NELEMENTS(x)	(sizeof(x)/sizeof((x)[0]))

static int	authenticate(sc_pkcs15_object_t *obj);
static int	pem_encode(int, sc_pkcs15_der_t *, sc_pkcs15_der_t *);

static const struct option options[] = {
	{ "learn-card",		no_argument, NULL, 	'L' },
	{ "read-certificate",	required_argument, NULL, 	'r' },
	{ "list-certificates",	no_argument, NULL,		'c' },
	{ "read-data-object",	required_argument, NULL, 	'R' },
	{ "list-data-objects",	no_argument, NULL,		'C' },
	{ "list-pins",		no_argument, NULL,		OPT_LIST_PINS },
	{ "dump",		no_argument, NULL,		'D' },
	{ "unblock-pin",	no_argument, NULL,		'u' },
	{ "change-pin",		no_argument, NULL,		OPT_CHANGE_PIN },
	{ "list-keys",          no_argument, NULL,         'k' },
	{ "list-public-keys",	no_argument, NULL,		OPT_LIST_PUB },
	{ "read-public-key",	required_argument, NULL,	OPT_READ_PUB },
#if defined(HAVE_OPENSSL) && (defined(_WIN32) || defined(HAVE_INTTYPES_H))
	{ "read-ssh-key",	required_argument, NULL,	OPT_READ_SSH },
#endif
	{ "reader",		required_argument, NULL,	OPT_READER },
	{ "pin",                required_argument, NULL,   OPT_PIN },
	{ "new-pin",		required_argument, NULL,	OPT_NEWPIN },
	{ "puk",		required_argument, NULL,	OPT_PUK },
	{ "output",		required_argument, NULL,	'o' },
	{ "no-cache",		no_argument, NULL,		OPT_NO_CACHE },
	{ "auth-id",		required_argument, NULL,	'a' },
	{ "wait",		no_argument, NULL,		'w' },
	{ "verbose",		no_argument, NULL,		'v' },
	{ NULL, 0, NULL, 0 }
};

static const char *option_help[] = {
	"Stores card info to cache",
	"Reads certificate with ID <arg>",
	"Lists certificates",
	"Reads data object with applicationName or OID <arg>",
	"Lists data objects",
	"Lists PIN codes",
	"Dump card objects",
	"Unblock PIN code",
	"Changes the PIN code",
	"Lists private keys",
	"Lists public keys",
	"Reads public key with ID <arg>",
	"Reads public key with ID <arg>, outputs ssh format",
	"Uses reader number <arg>",
	"Specify PIN",
	"Specify New PIN (when changing or unblocking)",
	"Specify Unblock PIN",
	"Outputs to file <arg>",
	"Disable card caching",
	"The auth ID of the PIN to use",
	"Wait for card insertion",
	"Verbose operation. Use several times to enable debug output.",
};

static sc_context_t *ctx = NULL;
static sc_card_t *card = NULL;
static struct sc_pkcs15_card *p15card = NULL;

static void print_cert_info(const struct sc_pkcs15_object *obj)
{
	struct sc_pkcs15_cert_info *cert = (struct sc_pkcs15_cert_info *) obj->data;

	printf("X.509 Certificate [%s]\n", obj->label);
	printf("\tFlags    : %d\n", obj->flags);
	printf("\tAuthority: %s\n", cert->authority ? "yes" : "no");
	printf("\tPath     : %s\n", sc_print_path(&cert->path));
	printf("\tID       : %s\n", sc_pkcs15_print_id(&cert->id));
}


static int list_certificates(void)
{
	int r, i;
	struct sc_pkcs15_object *objs[32];

	r = sc_pkcs15_get_objects(p15card, SC_PKCS15_TYPE_CERT_X509, objs, 32);
	if (r < 0) {
		fprintf(stderr, "Certificate enumeration failed: %s\n", sc_strerror(r));
		return 1;
	}
	if (verbose)
		printf("Card has %d certificate(s).\n\n", r);
	for (i = 0; i < r; i++) {
		print_cert_info(objs[i]);
		printf("\n");
	}
	return 0;
}

static int
print_pem_object(const char *kind, const u8*data, size_t data_len)
{
	FILE		*outf;
	unsigned char	*buf = NULL;
	size_t		buf_len = 1024;
	int		r;

	/* With base64, every 3 bytes yield 4 characters, and with
	 * 64 chars per line we know almost exactly how large a buffer we
	 * will need. */
	buf_len = (data_len + 2) / 3 * 4;
	buf_len += 2 * (buf_len / 64 + 2); /* certain platforms use CRLF */
	buf_len += 64;			   /* slack for checksum etc */

	if (!(buf = (unsigned char *) malloc(buf_len))) {
		perror("print_pem_object");
		return 1;
	}

	r = sc_base64_encode(data, data_len, buf, buf_len, 64);
	if (r < 0) {
		fprintf(stderr, "Base64 encoding failed: %s\n", sc_strerror(r));
		free(buf);
		return 1;
	}

	if (opt_outfile != NULL) {
		outf = fopen(opt_outfile, "w");
		if (outf == NULL) {
			fprintf(stderr, "Error opening file '%s': %s\n",
				opt_outfile, strerror(errno));
			free(buf);
			return 2;
		}
	} else
		outf = stdout;
	fprintf(outf,
		"-----BEGIN %s-----\n"
		"%s"
		"-----END %s-----\n",
		kind, buf, kind);
	if (outf != stdout)
		fclose(outf);
	free(buf);
	return 0;
}

static int
list_data_object(const char *kind, const u8*data, size_t data_len)
{
	size_t i;
	
	printf("%s (%lu bytes): <", kind, (unsigned long) data_len);
	for (i = 0; i < data_len; i++)
		printf(" %02X", data[i]);
	printf(" >\n");

	return 0;
}

static int
print_data_object(const char *kind, const u8*data, size_t data_len)
{
	size_t i;
	
	if (opt_outfile != NULL) {
		FILE *outf;
		outf = fopen(opt_outfile, "w");
		if (outf == NULL) {
			fprintf(stderr, "Error opening file '%s': %s\n",
				opt_outfile, strerror(errno));
			return 2;
			}
		for (i=0; i < data_len; i++)
			fprintf(outf, "%c", data[i]);
		printf("Dumping (%lu bytes) to file <%s>: <",
			(unsigned long) data_len, opt_outfile);
		for (i=0; i < data_len; i++)
			printf(" %02X", data[i]);
		printf(" >\n");
		fclose(outf);
	} else {
		printf("%s (%lu bytes): <",
			kind, (unsigned long) data_len);
		for (i=0; i < data_len; i++)
			printf(" %02X", data[i]);
		printf(" >\n");
	}
	return 0;
}

static int read_certificate(void)
{
	int r, i, count;
	struct sc_pkcs15_id id;
	struct sc_pkcs15_object *objs[32];

	id.len = SC_PKCS15_MAX_ID_SIZE;
	sc_pkcs15_hex_string_to_id(opt_cert, &id);
	
	r = sc_pkcs15_get_objects(p15card, SC_PKCS15_TYPE_CERT_X509, objs, 32);
	if (r < 0) {
		fprintf(stderr, "Certificate enumeration failed: %s\n", sc_strerror(r));
		return 1;
	}
	count = r;
	for (i = 0; i < count; i++) {
		struct sc_pkcs15_cert_info *cinfo = (struct sc_pkcs15_cert_info *) objs[i]->data;
		struct sc_pkcs15_cert *cert;

		if (sc_pkcs15_compare_id(&id, &cinfo->id) != 1)
			continue;
			
		if (verbose)
			printf("Reading certificate with ID '%s'\n", opt_cert);
		r = sc_pkcs15_read_certificate(p15card, cinfo, &cert);
		if (r) {
			fprintf(stderr, "Certificate read failed: %s\n", sc_strerror(r));
			return 1;
		}
		r = print_pem_object("CERTIFICATE", cert->data, cert->data_len);
		sc_pkcs15_free_certificate(cert);
		return r;
	}
	fprintf(stderr, "Certificate with ID '%s' not found.\n", opt_cert);
	return 2;
}

static int read_data_object(void)
{
	int    r, i, count, oid_len = 0;
	struct sc_pkcs15_object *objs[32];
	struct sc_object_id      oid;

	r = sc_pkcs15_get_objects(p15card, SC_PKCS15_TYPE_DATA_OBJECT, objs, 32);
	if (r < 0) {
		fprintf(stderr, "Data object enumeration failed: %s\n", sc_strerror(r));
		return 1;
	}
	count = r;

	r = sc_format_oid(&oid, opt_data);
	if (r == SC_SUCCESS) {
		while (oid.value[oid_len] >= 0) oid_len++;
	}

	for (i = 0; i < count; i++) {
		struct sc_pkcs15_data_info *cinfo = (struct sc_pkcs15_data_info *) objs[i]->data;
		struct sc_pkcs15_data *data_object;

		if (oid_len) {
			if (memcmp(oid.value, cinfo->app_oid.value, sizeof(int) * oid_len))
				continue;
		} else {
			if (memcmp(opt_data, &cinfo->app_label, strlen(opt_data)))
				continue;
		}
			
		if (verbose)
			printf("Reading data object with label '%s'\n", opt_data);
		r = authenticate(objs[i]);
		if (r >= 0) {
			r = sc_pkcs15_read_data_object(p15card, cinfo, &data_object);
			if (r) {
				fprintf(stderr, "Data object read failed: %s\n", sc_strerror(r));
				if (r == SC_ERROR_FILE_NOT_FOUND)
					continue; /* DEE emulation may say there is a file */
				return 1;
			}
			r = print_data_object("Data Object", data_object->data, data_object->data_len);
			sc_pkcs15_free_data_object(data_object);
			return r;
		} else {
			fprintf(stderr, "Authentication error: %s\n", sc_strerror(r));
			return 1;
		}
	}
	fprintf(stderr, "Data object with label '%s' not found.\n", opt_data);
	return 2;
}

static int list_data_objects(void)
{
	int r, i, count;
	struct sc_pkcs15_object *objs[32];

	r = sc_pkcs15_get_objects(p15card, SC_PKCS15_TYPE_DATA_OBJECT, objs, 32);
	if (r < 0) {
		fprintf(stderr, "Data object enumeration failed: %s\n", sc_strerror(r));
		return 1;
	}
	count = r;
	for (i = 0; i < count; i++) {
		int idx;
		struct sc_pkcs15_data_info *cinfo = (struct sc_pkcs15_data_info *) objs[i]->data;

		printf("Reading data object <%i>\n", i);
		printf("applicationName: %s\n", cinfo->app_label);
		printf("applicationOID:  ");
		if (cinfo->app_oid.value[0] >= 0) {
			printf("%i", cinfo->app_oid.value[0]);
			idx = 1;
			while (idx < SC_MAX_OBJECT_ID_OCTETS) {
				if (cinfo->app_oid.value[idx] < 0)
					break;
				printf(".%i", cinfo->app_oid.value[idx++]);
			}
			printf("\n");
		} else
			printf("NONE\n");
		printf("Path:            %s\n", sc_print_path(&cinfo->path));
		printf("Auth ID:         %s\n", sc_pkcs15_print_id(&objs[i]->auth_id));
		if (objs[i]->auth_id.len == 0) {
			struct sc_pkcs15_data *data_object;
			r = sc_pkcs15_read_data_object(p15card, cinfo, &data_object);
			if (r) {
				fprintf(stderr, "Data object read failed: %s\n", sc_strerror(r));
				if (r == SC_ERROR_FILE_NOT_FOUND)
					 continue; /* DEE emulation may say there is a file */
				return 1;
			}
			r = list_data_object("Data Object", data_object->data, data_object->data_len);
			sc_pkcs15_free_data_object(data_object);
		}
	}
	return 0;
}

static void print_prkey_info(const struct sc_pkcs15_object *obj)
{
	unsigned int i;
	struct sc_pkcs15_prkey_info *prkey = (struct sc_pkcs15_prkey_info *) obj->data;
	const char *usages[] = {
		"encrypt", "decrypt", "sign", "signRecover",
		"wrap", "unwrap", "verify", "verifyRecover",
		"derive", "nonRepudiation"
	};
	const size_t usage_count = sizeof(usages)/sizeof(usages[0]);
	const char *access_flags[] = {
		"sensitive", "extract", "alwaysSensitive",
		"neverExtract", "local"
	};
	const unsigned int af_count = NELEMENTS(access_flags);

	printf("Private RSA Key [%s]\n", obj->label);
	printf("\tCom. Flags  : %X\n", obj->flags);
	printf("\tUsage       : [0x%X]", prkey->usage);
	for (i = 0; i < usage_count; i++)
   	if (prkey->usage & (1 << i)) {
   		printf(", %s", usages[i]);
   	}
	printf("\n");
	printf("\tAccess Flags: [0x%X]", prkey->access_flags);
	for (i = 0; i < af_count; i++)
		if (prkey->access_flags & (1 << i)) {
			printf(", %s", access_flags[i]);   
		}
	printf("\n");
	printf("\tModLength   : %lu\n", (unsigned long)prkey->modulus_length);
	printf("\tKey ref     : %d\n", prkey->key_reference);
	printf("\tNative      : %s\n", prkey->native ? "yes" : "no");
	printf("\tPath        : %s\n", sc_print_path(&prkey->path));
	printf("\tAuth ID     : %s\n", sc_pkcs15_print_id(&obj->auth_id));
	printf("\tID          : %s\n", sc_pkcs15_print_id(&prkey->id));
}


static int list_private_keys(void)
{
	int r, i;
        struct sc_pkcs15_object *objs[32];
	
	r = sc_pkcs15_get_objects(p15card, SC_PKCS15_TYPE_PRKEY_RSA, objs, 32);
	if (r < 0) {
		fprintf(stderr, "Private key enumeration failed: %s\n", sc_strerror(r));
		return 1;
	}
	if (verbose)
		printf("Card has %d private key(s).\n\n", r);
	for (i = 0; i < r; i++) {
		print_prkey_info(objs[i]);
		printf("\n");
	}
	return 0;
}

static void print_pubkey_info(const struct sc_pkcs15_object *obj)
{
	unsigned int i;
	const struct sc_pkcs15_pubkey_info *pubkey = (const struct sc_pkcs15_pubkey_info *) obj->data;
	const char *usages[] = {
		"encrypt", "decrypt", "sign", "signRecover",
		"wrap", "unwrap", "verify", "verifyRecover",
		"derive", "nonRepudiation"
	};
	const unsigned int usage_count = NELEMENTS(usages);
	const char *access_flags[] = {
		"sensitive", "extract", "alwaysSensitive",
		"neverExtract", "local"
	};
	const unsigned int af_count = NELEMENTS(access_flags);

	printf("Public RSA Key [%s]\n", obj->label);
	printf("\tCom. Flags  : %X\n", obj->flags);
	printf("\tUsage       : [0x%X]", pubkey->usage);
	for (i = 0; i < usage_count; i++)
		if (pubkey->usage & (1 << i)) {
			printf(", %s", usages[i]);
	}
	printf("\n");
	printf("\tAccess Flags: [0x%X]", pubkey->access_flags);
	for (i = 0; i < af_count; i++)
		if (pubkey->access_flags & (1 << i)) {
			printf(", %s", access_flags[i]);   
		}
	printf("\n");
	printf("\tModLength   : %lu\n", (unsigned long)pubkey->modulus_length);
	printf("\tKey ref     : %d\n", pubkey->key_reference);
	printf("\tNative      : %s\n", pubkey->native ? "yes" : "no");
	printf("\tPath        : %s\n", sc_print_path(&pubkey->path));
	printf("\tAuth ID     : %s\n", sc_pkcs15_print_id(&obj->auth_id));
	printf("\tID          : %s\n", sc_pkcs15_print_id(&pubkey->id));
}

static int list_public_keys(void)
{
	int r, i;
	struct sc_pkcs15_object *objs[32];
	
	r = sc_pkcs15_get_objects(p15card, SC_PKCS15_TYPE_PUBKEY_RSA, objs, 32);
	if (r < 0) {
		fprintf(stderr, "Public key enumeration failed: %s\n", sc_strerror(r));
		return 1;
	}
	if (verbose)
		printf("Card has %d public key(s).\n\n", r);
	for (i = 0; i < r; i++) {
		print_pubkey_info(objs[i]);
		printf("\n");
	}
	return 0;
}

static int read_public_key(void)
{
	int r;
	struct sc_pkcs15_id id;
	struct sc_pkcs15_object *obj;
	sc_pkcs15_pubkey_t *pubkey = NULL;
	sc_pkcs15_cert_t *cert = NULL;
	sc_pkcs15_der_t pem_key;

	id.len = SC_PKCS15_MAX_ID_SIZE;
	sc_pkcs15_hex_string_to_id(opt_pubkey, &id);

	r = sc_pkcs15_find_pubkey_by_id(p15card, &id, &obj);
	if (r >= 0) {
		if (verbose)
			printf("Reading public key with ID '%s'\n", opt_pubkey);
		r = authenticate(obj);
		if (r >= 0)
			r = sc_pkcs15_read_pubkey(p15card, obj, &pubkey);
	} else if (r == SC_ERROR_OBJECT_NOT_FOUND) {
		/* No pubkey - try if there's a certificate */
		r = sc_pkcs15_find_cert_by_id(p15card, &id, &obj);
		if (r >= 0) {
			if (verbose)
				printf("Reading certificate with ID '%s'\n", opt_pubkey);
			r = sc_pkcs15_read_certificate(p15card,
				(sc_pkcs15_cert_info_t *) obj->data,
				&cert);
		}
		if (r >= 0)
			pubkey = &cert->key;
	}

	if (r == SC_ERROR_OBJECT_NOT_FOUND) {
		fprintf(stderr, "Public key with ID '%s' not found.\n", opt_pubkey);
		return 2;
	}
	if (r < 0) {
		fprintf(stderr, "Public key enumeration failed: %s\n", sc_strerror(r));
		return 1;
	}
	if (!pubkey) {
		fprintf(stderr, "Public key not available\n");
		return 1;
	}

	r = pem_encode(pubkey->algorithm, &pubkey->data, &pem_key);
	if (r < 0) {
		fprintf(stderr, "Error encoding PEM key: %s\n",
				sc_strerror(r));
		r = 1;
	} else {
		r = print_pem_object("PUBLIC KEY", pem_key.value, pem_key.len);
	}

	free(pem_key.value);
	if (cert)
		sc_pkcs15_free_certificate(cert);
	else if (pubkey)
		sc_pkcs15_free_pubkey(pubkey);

	return r;
}

#if defined(HAVE_OPENSSL) && (defined(_WIN32) || defined(HAVE_INTTYPES_H))
static int read_ssh_key(void)
{
	int r;
	struct sc_pkcs15_id id;
	struct sc_pkcs15_object *obj;
	sc_pkcs15_pubkey_t *pubkey = NULL;
	sc_pkcs15_cert_t *cert = NULL;

	id.len = SC_PKCS15_MAX_ID_SIZE;
	sc_pkcs15_hex_string_to_id(opt_pubkey, &id);

	r = sc_pkcs15_find_pubkey_by_id(p15card, &id, &obj);
	if (r >= 0) {
		if (verbose)
			printf("Reading ssh key with ID '%s'\n", opt_pubkey);
		r = authenticate(obj);
		if (r >= 0)
			r = sc_pkcs15_read_pubkey(p15card, obj, &pubkey);
	} else if (r == SC_ERROR_OBJECT_NOT_FOUND) {
		/* No pubkey - try if there's a certificate */
		r = sc_pkcs15_find_cert_by_id(p15card, &id, &obj);
		if (r >= 0) {
			if (verbose)
				printf("Reading certificate with ID '%s'\n", opt_pubkey);
			r = sc_pkcs15_read_certificate(p15card,
				(sc_pkcs15_cert_info_t *) obj->data,
				&cert);
		}
		if (r >= 0)
			pubkey = &cert->key;
	}

	if (r == SC_ERROR_OBJECT_NOT_FOUND) {
		fprintf(stderr, "Public key with ID '%s' not found.\n", opt_pubkey);
		return 2;
	}
	if (r < 0) {
		fprintf(stderr, "Public key enumeration failed: %s\n", sc_strerror(r));
		return 1;
	}

	/* rsa1 keys */
	if (pubkey->algorithm == SC_ALGORITHM_RSA) {
		int bits;
		BIGNUM *bn;
		char *exp,*mod;

		bn = BN_new();
		BN_bin2bn((unsigned char*)pubkey->u.rsa.modulus.data,
				pubkey->u.rsa.modulus.len, bn);
		bits = BN_num_bits(bn);
		exp =  BN_bn2dec(bn);
		BN_free(bn);

		bn = BN_new();
		BN_bin2bn((unsigned char*)pubkey->u.rsa.exponent.data,
				pubkey->u.rsa.exponent.len, bn);
		mod = BN_bn2dec(bn);
		BN_free(bn);

		if (bits && exp && mod) {
			printf("%u %s %s\n", bits,mod,exp);
		} else {
			printf("decoding rsa key failed!\n");
		}
		OPENSSL_free(exp);
		OPENSSL_free(mod);
	}
	
	/* rsa and des keys - ssh2 */
	/* key_to_blob */

	if (pubkey->algorithm == SC_ALGORITHM_RSA) {
		unsigned char buf[2048];
		unsigned char *uu;
		uint32_t len;
		uint32_t n;

		buf[0]=0;
		buf[1]=0;
		buf[2]=0;
		buf[3]=7;

		len = sprintf((char *) buf+4,"ssh-rsa");
		len+=4;

		if (sizeof(buf)-len < 4+pubkey->u.rsa.exponent.len)
			goto fail;
		n = pubkey->u.rsa.exponent.len;
		if (pubkey->u.rsa.exponent.data[0] & 0x80) n++;
		buf[len++]=(n >>24) & 0xff;
		buf[len++]=(n >>16) & 0xff;
		buf[len++]=(n >>8) & 0xff;
		buf[len++]=(n) & 0xff;
		if (pubkey->u.rsa.exponent.data[0] & 0x80) 
			buf[len++]= 0;

		memcpy(buf+len,pubkey->u.rsa.exponent.data,
			pubkey->u.rsa.exponent.len);
		len += pubkey->u.rsa.exponent.len;

		if (sizeof(buf)-len < 5+pubkey->u.rsa.modulus.len)
			goto fail;
		n = pubkey->u.rsa.modulus.len;
		if (pubkey->u.rsa.modulus.data[0] & 0x80) n++;
		buf[len++]=(n >>24) & 0xff;
		buf[len++]=(n >>16) & 0xff;
		buf[len++]=(n >>8) & 0xff;
		buf[len++]=(n) & 0xff;
		if (pubkey->u.rsa.modulus.data[0] & 0x80) 
			buf[len++]= 0;

		memcpy(buf+len,pubkey->u.rsa.modulus.data,
			pubkey->u.rsa.modulus.len);
		len += pubkey->u.rsa.modulus.len;

		uu = malloc(len*2);
		r = sc_base64_encode(buf, len, uu, 2*len, 2*len);

		printf("ssh-rsa %s", uu);
		free(uu);

	}

	if (pubkey->algorithm == SC_ALGORITHM_DSA) {
		unsigned char buf[2048];
		unsigned char *uu;
		uint32_t len;
		uint32_t n;

		buf[0]=0;
		buf[1]=0;
		buf[2]=0;
		buf[3]=7;

		len = sprintf((char *) buf+4,"ssh-dss");
		len+=4;

		if (sizeof(buf)-len < 5+pubkey->u.dsa.p.len)
			goto fail;
		n = pubkey->u.dsa.p.len;
		if (pubkey->u.dsa.p.data[0] & 0x80) n++;
		buf[len++]=(n >>24) & 0xff;
		buf[len++]=(n >>16) & 0xff;
		buf[len++]=(n >>8) & 0xff;
		buf[len++]=(n) & 0xff;
		if (pubkey->u.dsa.p.data[0] & 0x80) 
			buf[len++]= 0;

		memcpy(buf+len,pubkey->u.dsa.p.data,
			pubkey->u.dsa.p.len);
		len += pubkey->u.dsa.p.len;

		if (sizeof(buf)-len < 5+pubkey->u.dsa.q.len)
			goto fail;
		n = pubkey->u.dsa.q.len;
		if (pubkey->u.dsa.q.data[0] & 0x80) n++;
		buf[len++]=(n >>24) & 0xff;
		buf[len++]=(n >>16) & 0xff;
		buf[len++]=(n >>8) & 0xff;
		buf[len++]=(n) & 0xff;
		if (pubkey->u.dsa.q.data[0] & 0x80) 
			buf[len++]= 0;

		memcpy(buf+len,pubkey->u.dsa.q.data,
			pubkey->u.dsa.q.len);
		len += pubkey->u.dsa.q.len;

		if (sizeof(buf)-len < 5+pubkey->u.dsa.g.len)
			goto fail;
		n = pubkey->u.dsa.g.len;
		if (pubkey->u.dsa.g.data[0] & 0x80) n++;
		buf[len++]=(n >>24) & 0xff;
		buf[len++]=(n >>16) & 0xff;
		buf[len++]=(n >>8) & 0xff;
		buf[len++]=(n) & 0xff;
		if (pubkey->u.dsa.g.data[0] & 0x80) 
			buf[len++]= 0;

		memcpy(buf+len,pubkey->u.dsa.g.data,
			pubkey->u.dsa.g.len);
		len += pubkey->u.dsa.g.len;

		if (sizeof(buf)-len < 5+pubkey->u.dsa.pub.len)
			goto fail;
		n = pubkey->u.dsa.pub.len;
		if (pubkey->u.dsa.pub.data[0] & 0x80) n++;
		buf[len++]=(n >>24) & 0xff;
		buf[len++]=(n >>16) & 0xff;
		buf[len++]=(n >>8) & 0xff;
		buf[len++]=(n) & 0xff;
		if (pubkey->u.dsa.pub.data[0] & 0x80) 
			buf[len++]= 0;

		memcpy(buf+len,pubkey->u.dsa.pub.data,
			pubkey->u.dsa.pub.len);
		len += pubkey->u.dsa.pub.len;

		uu = malloc(len*2);
		r = sc_base64_encode(buf, len, uu, 2*len, 2*len);

		printf("ssh-dss %s", uu);
		free(uu);

	}

	if (cert)
		sc_pkcs15_free_certificate(cert);
	else if (pubkey)
		sc_pkcs15_free_pubkey(pubkey);

	return 0;
fail:
		printf("can't convert key: buffer too small\n");
	if (cert)
		sc_pkcs15_free_certificate(cert);
	else if (pubkey)
		sc_pkcs15_free_pubkey(pubkey);
	return SC_ERROR_OUT_OF_MEMORY;
}
#endif

static sc_pkcs15_object_t *
get_pin_info(void)
{
	sc_pkcs15_object_t *objs[32], *obj;
	int r;
	
	if (opt_auth_id == NULL) {
		r = sc_pkcs15_get_objects(p15card, SC_PKCS15_TYPE_AUTH_PIN, objs, 32);
		if (r < 0) {
			fprintf(stderr, "PIN code enumeration failed: %s\n", sc_strerror(r));
			return NULL;
		}
		if (r == 0) {
			fprintf(stderr, "No PIN codes found.\n");
			return NULL;
		}
		obj = objs[0];
	} else {
		struct sc_pkcs15_id auth_id;
		
		sc_pkcs15_hex_string_to_id(opt_auth_id, &auth_id);
		r = sc_pkcs15_find_pin_by_auth_id(p15card, &auth_id, &obj);
		if (r) {
			fprintf(stderr, "Unable to find PIN code: %s\n", sc_strerror(r));
			return NULL;
		}
	}

	return obj;
}

static u8 * get_pin(const char *prompt, sc_pkcs15_object_t *pin_obj)
{
	sc_pkcs15_pin_info_t *pinfo = (sc_pkcs15_pin_info_t *) pin_obj->data;
	char buf[80];
	char *pincode;
	
	sprintf(buf, "%s [%s]: ", prompt, pin_obj->label);
	while (1) {
		pincode = getpass(buf);
		if (strlen(pincode) == 0)
			return NULL;
		if (strlen(pincode) < pinfo->min_length) {
			printf("PIN code too short, try again.\n");
			continue;
		}
		if (strlen(pincode) > pinfo->max_length) {
			printf("PIN code too long, try again.\n");
			continue;
		}
		return (u8 *) strdup(pincode);
	}
}

static int authenticate(sc_pkcs15_object_t *obj)
{
	sc_pkcs15_pin_info_t	*pin_info;
	sc_pkcs15_object_t	*pin_obj;
	u8			*pin;
	int			r;

	if (obj->auth_id.len == 0)
		return 0;
	r = sc_pkcs15_find_pin_by_auth_id(p15card, &obj->auth_id, &pin_obj);
	if (r)
		return r;

	pin_info = (sc_pkcs15_pin_info_t *) pin_obj->data;
	if (opt_pin != NULL)
		pin = opt_pin;
	else
		pin = get_pin("Please enter PIN", pin_obj);

	return sc_pkcs15_verify_pin(p15card, pin_info,
			pin, pin? strlen((char *) pin) : 0);
}

static void print_pin_info(const struct sc_pkcs15_object *obj)
{
	const char *pin_flags[] = {
		"case-sensitive", "local", "change-disabled",
		"unblock-disabled", "initialized", "needs-padding",
		"unblockingPin", "soPin", "disable_allowed",
		"integrity-protected", "confidentiality-protected",
		"exchangeRefData"
	};
	const char *pin_types[] = {"bcd", "ascii-numeric", "UTF-8",
		"halfnibble bcd", "iso 9664-1"}; 
	const struct sc_pkcs15_pin_info *pin = (const struct sc_pkcs15_pin_info *) obj->data;
	const size_t pf_count = sizeof(pin_flags)/sizeof(pin_flags[0]);
	size_t i;

	printf("PIN [%s]\n", obj->label);
	printf("\tCom. Flags: 0x%X\n", obj->flags);
	printf("\tID        : %s\n", sc_pkcs15_print_id(&pin->auth_id));
	printf("\tFlags     : [0x%02X]", pin->flags);
	for (i = 0; i < pf_count; i++)
		if (pin->flags & (1 << i)) {
			printf(", %s", pin_flags[i]);
		}
	printf("\n");
	printf("\tLength    : min_len:%lu, max_len:%lu, stored_len:%lu\n",
		(unsigned long)pin->min_length, (unsigned long)pin->max_length,
		(unsigned long)pin->stored_length);
	printf("\tPad char  : 0x%02X\n", pin->pad_char);
	printf("\tReference : %d\n", pin->reference);
	if (pin->type >= 0 && pin->type < sizeof(pin_types)/sizeof(pin_types[0]))
		printf("\tType      : %s\n", pin_types[pin->type]);
	else
		printf("\tType      : [encoding %d]\n", pin->type);
	printf("\tPath      : %s\n", sc_print_path(&pin->path));
	if (pin->tries_left >= 0)
		printf("\tTries left: %d\n", pin->tries_left);
}

static int list_pins(void)
{
	int r, i;
	struct sc_pkcs15_object *objs[32];
	
	r = sc_pkcs15_get_objects(p15card, SC_PKCS15_TYPE_AUTH_PIN, objs, 32);
	if (r < 0) {
		fprintf(stderr, "PIN enumeration failed: %s\n", sc_strerror(r));
		return 1;
	}
	if (verbose)
		printf("Card has %d PIN code(s).\n\n", r);
	for (i = 0; i < r; i++) {
		print_pin_info(objs[i]);
		printf("\n");
	}
	return 0;
}

static int dump(void)
{

	const char *flags[] = {
		"Read-only",
		"Login required",
		"PRN generation",
		"EID compliant"
	};

	int i, count = 0;
	sc_lock(card);

	printf("PKCS#15 Card [%s]:\n", p15card->label);
	printf("\tVersion        : %d\n", p15card->version);
	printf("\tSerial number  : %s\n", p15card->serial_number);
	printf("\tManufacturer ID: %s\n", p15card->manufacturer_id);
	if (p15card->last_update)
		printf("\tLast update    : %s\n", p15card->last_update);
	if (p15card->preferred_language)
		printf("\tLanguage       : %s\n", p15card->preferred_language);
	printf("\tFlags          : ");
	for (i = 0; i < 4; i++) {
		if ((p15card->flags >> i) & 1) {
			if (count)
				printf(", ");
			printf("%s", flags[i]);
			count++;
		}
	}
	printf("\n\n");

	list_pins();
	list_private_keys();
	list_public_keys();
	list_certificates();
	list_data_objects();

	sc_unlock(card);
	return 0;
}

static int unblock_pin(void)
{
	struct sc_pkcs15_pin_info *pinfo = NULL;
	sc_pkcs15_object_t *pin_obj;
	u8 *pin, *puk;
	int r;
	
	if (!(pin_obj = get_pin_info()))
		return 2;
	pinfo = (sc_pkcs15_pin_info_t *) pin_obj->data;

	if ((puk = opt_puk) == NULL) {
		puk = get_pin("Enter PUK", pin_obj);
		if (puk == NULL)
			return 2;
	}

	if ((pin = opt_pin) == NULL)
		pin = opt_newpin;
	while (pin == NULL) {
		u8 *pin2;
	
		pin = get_pin("Enter new PIN", pin_obj);
		if (pin == NULL || strlen((char *) pin) == 0)
			return 2;
		pin2 = get_pin("Enter new PIN again", pin_obj);
		if (pin2 == NULL || strlen((char *) pin2) == 0)
			return 2;
		if (strcmp((char *) pin, (char *) pin2) != 0) {
			printf("PIN codes do not match, try again.\n");
			free(pin);
			pin = NULL;
		}
		free(pin2);
	}

	r = sc_pkcs15_unblock_pin(p15card, pinfo, puk, strlen((char *) puk),
				 pin, strlen((char *) pin));
	if (r == SC_ERROR_PIN_CODE_INCORRECT) {
		fprintf(stderr, "PUK code incorrect; tries left: %d\n", pinfo->tries_left);
		return 3;
	} else if (r) {
		fprintf(stderr, "PIN unblocking failed: %s\n", sc_strerror(r));
		return 2;
	}
	if (verbose)
		printf("PIN successfully unblocked.\n");
	return 0;
}

static int change_pin(void)
{
	sc_pkcs15_object_t *pin_obj;
	sc_pkcs15_pin_info_t *pinfo = NULL;
	u8 *pincode, *newpin;
	int r;

	if (!(pin_obj = get_pin_info()))
		return 2;
	pinfo = (sc_pkcs15_pin_info_t *) pin_obj->data;

	if ((pincode = opt_pin) == NULL) {
		pincode = get_pin("Enter old PIN", pin_obj);
		if (pincode == NULL)
			return 2;
	}

	if (strlen((char *) pincode) == 0) {
		fprintf(stderr, "No PIN code supplied.\n");
		return 2;
	}

	newpin = opt_newpin;
	while (newpin == NULL) {
		u8 *newpin2;
		
		newpin = get_pin("Enter new PIN", pin_obj);
		if (newpin == NULL || strlen((char *) newpin) == 0)
			return 2;
		newpin2 = get_pin("Enter new PIN again", pin_obj);
		if (newpin2 == NULL || strlen((char *) newpin2) == 0)
			return 2;
		if (strcmp((char *) newpin, (char *) newpin2) == 0) {
			free(newpin2);
			break;
		}
		printf("PIN codes do not match, try again.\n");
		free(newpin);
		free(newpin2);
		newpin=NULL;
	}
	r = sc_pkcs15_change_pin(p15card, pinfo, pincode, strlen((char *) pincode),
				 newpin, strlen((char *) newpin));
	if (r == SC_ERROR_PIN_CODE_INCORRECT) {
		fprintf(stderr, "PIN code incorrect; tries left: %d\n", pinfo->tries_left);
		return 3;
	} else if (r) {
		fprintf(stderr, "PIN code change failed: %s\n", sc_strerror(r));
		return 2;
	}
	if (verbose)
		printf("PIN code changed successfully.\n");
	return 0;
}

static int read_and_cache_file(const sc_path_t *path)
{
	sc_file_t *tfile;
	const sc_acl_entry_t *e;
	u8 *buf;
	int r;

	if (verbose) {
		printf("Reading file ");
		hex_dump(stdout, path->value, path->len, "");
		printf("...\n");
	}
	r = sc_select_file(card, path, &tfile);
	if (r != 0) {
		fprintf(stderr, "sc_select_file() failed: %s\n", sc_strerror(r));
		return -1;
	}
	e = sc_file_get_acl_entry(tfile, SC_AC_OP_READ);
	if (e != NULL && e->method != SC_AC_NONE) {
		if (verbose)
			printf("Skipping; ACL for read operation is not NONE.\n");
		return -1;
	}
	buf = malloc(tfile->size);
	if (!buf) {
		printf("out of memory!");
		return -1;
	}
	r = sc_read_binary(card, 0, buf, tfile->size, 0);
	if (r < 0) {
		fprintf(stderr, "sc_read_binary() failed: %s\n", sc_strerror(r));
		free(buf);
		return -1;
	}
	r = sc_pkcs15_cache_file(p15card, path, buf, r);
	if (r) {
		fprintf(stderr, "Unable to cache file: %s\n", sc_strerror(r));
		free(buf);
		return -1;
	}
	sc_file_free(tfile);
	free(buf);
	return 0;
}

static int learn_card(void)
{
	char dir[PATH_MAX];
	int r, i, cert_count;
	struct sc_pkcs15_object *certs[32];
	struct sc_pkcs15_df *df;

	r = sc_get_cache_dir(ctx, dir, sizeof(dir)); 
	if (r) {
		fprintf(stderr, "Unable to find cache directory: %s\n", sc_strerror(r));
		return 1;
	}

	printf("Using cache directory '%s'.\n", dir);
	r = sc_pkcs15_get_objects(p15card, SC_PKCS15_TYPE_CERT_X509, certs, 32);
	if (r < 0) {
		fprintf(stderr, "Certificate enumeration failed: %s\n", sc_strerror(r));
		return 1;
	}
	cert_count = r;
	r = sc_pkcs15_get_objects(p15card, SC_PKCS15_TYPE_PRKEY_RSA, NULL, 0);
	if (r < 0) {
		fprintf(stderr, "Private key enumeration failed: %s\n", sc_strerror(r));
		return 1;
	}
	r = sc_pkcs15_get_objects(p15card, SC_PKCS15_TYPE_AUTH_PIN, NULL, 0);
	if (r < 0) {
		fprintf(stderr, "PIN code enumeration failed: %s\n", sc_strerror(r));
		return 1;
	}

	/* Cache all relevant DF files. The cache
	 * directory is created automatically. */
	for (df = p15card->df_list; df != NULL; df = df->next)
		read_and_cache_file(&df->path);
	printf("Caching %d certificate(s)...\n", cert_count);
	for (i = 0; i < cert_count; i++) {
		sc_path_t tpath;
		struct sc_pkcs15_cert_info *cinfo = (struct sc_pkcs15_cert_info *) certs[i]->data;
		
		printf("[%s]\n", certs[i]->label);

		tpath = cinfo->path;
		if (tpath.type == SC_PATH_TYPE_FILE_ID) {
			/* prepend application DF path in case of a file id */
			r = sc_concatenate_path(&tpath, &p15card->file_app->path, &tpath);
			if (r != SC_SUCCESS)
				return r;
		}

		read_and_cache_file(&tpath);
	}

	return 0;
}

int main(int argc, char * const argv[])
{
	int err = 0, r, c, long_optind = 0;
	int do_read_cert = 0;
	int do_list_certs = 0;
	int do_read_data_object = 0;
	int do_list_data_objects = 0;
	int do_list_pins = 0;
	int do_dump = 0;
	int do_list_prkeys = 0;
	int do_list_pubkeys = 0;
	int do_read_pubkey = 0;
#if defined(HAVE_OPENSSL) && (defined(_WIN32) || defined(HAVE_INTTYPES_H))
	int do_read_sshkey = 0;
#endif
	int do_change_pin = 0;
	int do_unblock_pin = 0;
	int do_learn_card = 0;
	int action_count = 0;
	sc_context_param_t ctx_param;

	while (1) {
		c = getopt_long(argc, argv, "r:cuko:va:LR:CwD", options, &long_optind);
		if (c == -1)
			break;
		if (c == '?')
			print_usage_and_die(app_name, options, option_help);
		switch (c) {
		case 'r':
			opt_cert = optarg;
			do_read_cert = 1;
			action_count++;
			break;
		case 'c':
			do_list_certs = 1;
			action_count++;
			break;
		case 'R':
			opt_data = optarg;
			do_read_data_object = 1;
			action_count++;
			break;
		case 'C':
			do_list_data_objects = 1;
			action_count++;
			break;
		case OPT_CHANGE_PIN:
			do_change_pin = 1;
			action_count++;
			break;
		case 'u':
			do_unblock_pin = 1;
			action_count++;
			break;
		case OPT_LIST_PINS:
			do_list_pins = 1;
			action_count++;
			break;
		case 'D':
			do_dump = 1;
			action_count++;
			break;
		case 'k':
			do_list_prkeys = 1;
			action_count++;
			break;
		case OPT_LIST_PUB:
			do_list_pubkeys = 1;
			action_count++;
			break;
		case OPT_READ_PUB:
			opt_pubkey = optarg;
			do_read_pubkey = 1;
			action_count++;
			break;
#if defined(HAVE_OPENSSL) && (defined(_WIN32) || defined(HAVE_INTTYPES_H))
		case OPT_READ_SSH:
			opt_pubkey = optarg;
			do_read_sshkey = 1;
			action_count++;
			break;
#endif
		case 'L':
			do_learn_card = 1;
			action_count++;
			break;
		case OPT_READER:
			opt_reader = atoi(optarg);
			break;
		case OPT_PIN:
			opt_pin = (u8 *) optarg;
			break;
		case OPT_NEWPIN:
			opt_newpin = (u8 *) optarg;
			break;
		case OPT_PUK:
			opt_puk = (u8 *) optarg;
			break;
		case 'o':
			opt_outfile = optarg;
			break;
		case 'v':
			verbose++;
			break;
		case 'a':
			opt_auth_id = optarg;
			break;
		case OPT_NO_CACHE:
			opt_no_cache++;
			break;
		case 'w':
			opt_wait = 1;
			break;
		}
	}
	if (action_count == 0)
		print_usage_and_die(app_name, options, option_help);

	memset(&ctx_param, 0, sizeof(ctx_param));
	ctx_param.ver      = 0;
	ctx_param.app_name = app_name;

	r = sc_context_create(&ctx, &ctx_param);
	if (r) {
		fprintf(stderr, "Failed to establish context: %s\n", sc_strerror(r));
		return 1;
	}
	if (verbose > 1 )
		ctx->debug = verbose-1;

	err = connect_card(ctx, &card, opt_reader, 0, opt_wait, verbose);
	if (err)
		goto end;

	if (verbose)
		fprintf(stderr, "Trying to find a PKCS#15 compatible card...\n");
	r = sc_pkcs15_bind(card, &p15card);
	if (r) {
		fprintf(stderr, "PKCS#15 initialization failed: %s\n", sc_strerror(r));
		err = 1;
		goto end;
	}
	if (opt_no_cache)
		p15card->opts.use_cache = 0;
	if (verbose)
		fprintf(stderr, "Found %s!\n", p15card->label);
	if (do_learn_card) {
		if ((err = learn_card()))
			goto end;
		action_count--;
	}
	if (do_list_certs) {
		if ((err = list_certificates()))
			goto end;
		action_count--;
	}
	if (do_read_cert) {
		if ((err = read_certificate()))
			goto end;
		action_count--;
	}
	if (do_list_data_objects) {
		if ((err = list_data_objects()))
			goto end;
		action_count--;
	}
	if (do_read_data_object) {
		if ((err = read_data_object()))
			goto end;
		action_count--;
	}
	if (do_list_prkeys) {
		if ((err = list_private_keys()))
			goto end;
		action_count--;
	}
	if (do_list_pubkeys) {
		if ((err = list_public_keys()))
			goto end;
		action_count--;
	}
	if (do_read_pubkey) {
		if ((err = read_public_key()))
			goto end;
		action_count--;
	}
#if defined(HAVE_OPENSSL) && (defined(_WIN32) || defined(HAVE_INTTYPES_H))
	if (do_read_sshkey) {
		if ((err = read_ssh_key()))
			goto end;
		action_count--;
	}
#endif
	if (do_list_pins) {
		if ((err = list_pins()))
			goto end;
		action_count--;
	}
	if (do_dump) {
		if ((err = dump()))
			goto end;
		action_count--;
	}
	if (do_change_pin) {
		if ((err = change_pin()))
			goto end;
		action_count--;
	}
	if (do_unblock_pin) {
		if ((err = unblock_pin()))
			goto end;
		action_count--;
	}
end:
	if (p15card)
		sc_pkcs15_unbind(p15card);
	if (card) {
		sc_unlock(card);
		sc_disconnect_card(card, 0);
	}
	if (ctx)
		sc_release_context(ctx);
	return err;
}

/*
 * Helper function for PEM encoding public key
 */
#include "opensc/asn1.h"
static const struct sc_asn1_entry	c_asn1_pem_key_items[] = {
	{ "algorithm",	SC_ASN1_ALGORITHM_ID, SC_ASN1_CONS| SC_ASN1_TAG_SEQUENCE, 0, NULL, NULL},
	{ "key",	SC_ASN1_BIT_STRING_NI, SC_ASN1_TAG_BIT_STRING, 0, NULL, NULL },
	{ NULL, 0, 0, 0, NULL, NULL }
};
static const struct sc_asn1_entry	c_asn1_pem_key[] = {
	{ "publicKey",	SC_ASN1_STRUCT, SC_ASN1_CONS | SC_ASN1_TAG_SEQUENCE, 0, NULL, NULL},
	{ NULL, 0, 0, 0, NULL, NULL }
};

static int pem_encode(int alg_id, sc_pkcs15_der_t *key, sc_pkcs15_der_t *out)
{
	struct sc_asn1_entry	asn1_pem_key[2],
				asn1_pem_key_items[3];
	struct sc_algorithm_id algorithm;
	int key_len;

	memset(&algorithm, 0, sizeof(algorithm));
	algorithm.algorithm = alg_id;

	sc_copy_asn1_entry(c_asn1_pem_key, asn1_pem_key);
	sc_copy_asn1_entry(c_asn1_pem_key_items, asn1_pem_key_items);
	sc_format_asn1_entry(asn1_pem_key + 0, asn1_pem_key_items, NULL, 1);
	sc_format_asn1_entry(asn1_pem_key_items + 0,
			&algorithm, NULL, 1);
	key_len = 8 * key->len;
	sc_format_asn1_entry(asn1_pem_key_items + 1,
			key->value, &key_len, 1);

	return sc_asn1_encode(ctx, asn1_pem_key, &out->value, &out->len);
}
