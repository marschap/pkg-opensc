/*
 * sc-padding.c: miscellaneous padding functions
 *
 * Copyright (C) 2001, 2002  Juha Yrj�l� <juha.yrjola@iki.fi>
 * Copyright (C) 2003	Nils Larsch <larsch@trustcenter.de>
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
#include <string.h>
#include <stdlib.h>
#include <assert.h>

/* TODO doxygen comments */

/*
 * Prefixes for pkcs-v1 signatures
 */
static const u8 hdr_md5[] = {
	0x30, 0x20, 0x30, 0x0c, 0x06, 0x08, 0x2a, 0x86, 0x48, 0x86, 0xf7,
	0x0d, 0x02, 0x05, 0x05, 0x00, 0x04, 0x10
};
static const u8 hdr_sha1[] = {
	0x30, 0x21, 0x30, 0x09, 0x06, 0x05, 0x2b, 0x0e, 0x03, 0x02, 0x1a,
	0x05, 0x00, 0x04, 0x14
};
static const u8 hdr_ripemd160[] = {
	0x30, 0x21, 0x30, 0x09, 0x06, 0x05, 0x2b, 0x24, 0x03, 0x02, 0x01,
	0x05, 0x00, 0x04, 0x14
};


#define DIGEST_INFO_COUNT 6
static const struct digest_info_prefix {
	unsigned int	algorithm;
	const u8 *	hdr;
	size_t		hdr_len;
	size_t		hash_len;
} digest_info_prefix[DIGEST_INFO_COUNT] = {
      { SC_ALGORITHM_RSA_HASH_NONE,     NULL,           0,                      -1      },
      {	SC_ALGORITHM_RSA_HASH_MD5,	hdr_md5,	sizeof(hdr_md5),	16	},
      { SC_ALGORITHM_RSA_HASH_SHA1,	hdr_sha1,	sizeof(hdr_sha1),	20	},
      { SC_ALGORITHM_RSA_HASH_RIPEMD160,hdr_ripemd160,	sizeof(hdr_ripemd160),	20	},
      { SC_ALGORITHM_RSA_HASH_MD5_SHA1,	NULL,		0,			36	},
      {	0,				NULL,		0,			-1	}
};

/* add/remove pkcs1 BT01 padding */

int sc_pkcs1_add_01_padding(const u8 *in, size_t in_len, u8 *out,
	size_t *out_len, size_t mod_length)
{
	size_t i;

	if (*out_len < mod_length)
		return SC_ERROR_BUFFER_TOO_SMALL;
	if (in_len + 11 > mod_length)
		return SC_ERROR_INVALID_ARGUMENTS;
	i = mod_length - in_len;
	memmove(out + i, in, in_len);
	*out++ = 0x00; /* XXX the leading zero octet does not really
			* belong to the pkcs1 BT01 padding -- Nils */
	*out++ = 0x01;
	
	memset(out, 0xFF, i - 3);
	out += i - 3;
	*out = 0x00;

	*out_len = mod_length;
	return SC_SUCCESS;
}

int sc_pkcs1_strip_01_padding(const u8 *in_dat, size_t in_len, u8 *out,
	size_t *out_len)
{
	const u8 *tmp = in_dat;
	size_t    len;

	if (in_dat == NULL || in_len < 10)
		return SC_ERROR_INTERNAL;
	/* ignore leading zero byte */
	if (*tmp == 0) {
		tmp++;
		in_len--;
	}
	len = in_len;
	if (*tmp != 0x01)
		return SC_ERROR_WRONG_PADDING;
	for (tmp++, len--; *tmp == 0xff && len != 0; tmp++, len--)
		;
	if (!len || (in_len - len) < 9 || *tmp++ != 0x00)
		return SC_ERROR_WRONG_PADDING;
	len--;
	if (out == NULL)
		/* just check the padding */
		return SC_SUCCESS;
	if (*out_len < len)
		return SC_ERROR_INTERNAL;
	memmove(out, tmp, len);
	*out_len = len;
	return SC_SUCCESS;
}

/* remove pkcs1 BT02 padding (adding BT02 padding is currently not
 * needed/implemented) */
int sc_pkcs1_strip_02_padding(const u8 *data, size_t len, u8 *out,
	size_t *out_len)
{
	unsigned int	n = 0;

	if (data == NULL || len < 3)
		return SC_ERROR_INTERNAL;
	/* skip leading zero octet (not part of the pkcs1 BT02 padding) */
	if (*data == 0) {
		data++;
		len--;
	}
	if (data[0] != 0x02)
		return SC_ERROR_WRONG_PADDING;
	/* skip over padding bytes */
	for (n = 1; n < len && data[n]; n++)
		;
	/* Must be at least 8 pad bytes */
	if (n >= len || n < 9)
		return SC_ERROR_WRONG_PADDING;
	n++;
	if (out == NULL)
		/* just check the padding */
		return SC_SUCCESS;
	/* Now move decrypted contents to head of buffer */
	if (*out_len < len -  n)
		return SC_ERROR_INTERNAL;
	memmove(out, data + n, len - n);
	return len - n;
}

/* add/remove DigestInfo prefix */
int sc_pkcs1_add_digest_info_prefix(unsigned int algorithm, const u8 *in,
	size_t in_len, u8 *out, size_t *out_len)
{
	int i;

	for (i = 0; i < DIGEST_INFO_COUNT; i++) {
		if (algorithm == digest_info_prefix[i].algorithm) {
			const u8 *hdr      = digest_info_prefix[i].hdr;
			size_t    hdr_len  = digest_info_prefix[i].hdr_len,
			          hash_len = digest_info_prefix[i].hash_len;
			if (in_len != hash_len ||
			    *out_len < (hdr_len + hash_len))
				return SC_ERROR_INTERNAL;
			memmove(out + hdr_len, in, hash_len);
			memmove(out, hdr, hdr_len);
			*out_len = hdr_len + hash_len;
			return SC_SUCCESS;
		}
	}

	return SC_ERROR_INTERNAL;
}

int sc_pkcs1_strip_digest_info_prefix(unsigned int *algorithm,
	const u8 *in_dat, size_t in_len, u8 *out_dat, size_t *out_len)
{
	int i;

	for (i = 0; i < DIGEST_INFO_COUNT; i++) {
		size_t    hdr_len  = digest_info_prefix[i].hdr_len,
		          hash_len = digest_info_prefix[i].hash_len;
		const u8 *hdr      = digest_info_prefix[i].hdr;
		
		if (in_len == (hdr_len + hash_len) &&
		    !memcmp(in_dat, hdr, hdr_len)) {
			if (algorithm)
				*algorithm = digest_info_prefix[i].algorithm;
			if (out_dat == NULL)
				/* just check the DigestInfo prefix */
				return SC_SUCCESS;
			if (*out_len < hash_len)
				return SC_ERROR_INTERNAL;
			memmove(out_dat, in_dat + hdr_len, hash_len);
			*out_len = hash_len;
			return SC_SUCCESS;
		}
	}
	return SC_ERROR_INTERNAL;
}

/* general PKCS#1 encoding function */
int sc_pkcs1_encode(struct sc_context *ctx, unsigned long flags,
	const u8 *in, size_t in_len, u8 *out, size_t *out_len, size_t mod_len)
{
	int    i;
	size_t tmp_len = *out_len;
	const u8    *tmp = in;
	unsigned int hash_algo, pad_algo;

	hash_algo = flags & (SC_ALGORITHM_RSA_HASHES | SC_ALGORITHM_RSA_HASH_NONE);
	pad_algo  = flags & SC_ALGORITHM_RSA_PADS;

	if (hash_algo != SC_ALGORITHM_RSA_HASH_NONE) {
		i = sc_pkcs1_add_digest_info_prefix(hash_algo, in, in_len,
						    out, &tmp_len);
		if (i != SC_SUCCESS) {
			sc_error(ctx, "Unable to add digest info 0x%x\n",
			      hash_algo);
			return i;
		}
		tmp = out;
	} else
		tmp_len = in_len;

	switch(pad_algo) {
	case SC_ALGORITHM_RSA_PAD_NONE:
		/* padding done by card => nothing to do */
		if (out != tmp)
			memcpy(out, tmp, tmp_len);
		*out_len = tmp_len;
		return SC_SUCCESS;
	case SC_ALGORITHM_RSA_PAD_PKCS1:
		/* add pkcs1 bt01 padding */
		return sc_pkcs1_add_01_padding(tmp, tmp_len, out, out_len,
					       mod_len);
	default:
		/* currently only pkcs1 padding is supported */
		sc_error(ctx, "Unsupported padding algorithm 0x%x\n", pad_algo);
		return SC_ERROR_NOT_SUPPORTED;
	}
}

/* strip leading zero padding (does only really work when a DigestInfo
 * value has been padded */
int sc_strip_zero_padding(const u8 *in, size_t in_len, u8 *out,
	size_t *out_len)
{
	while (*in == 0 && in_len) {
		in++;
		in_len--;
	}

	if (*out_len < in_len)
		return SC_ERROR_INTERNAL;

	memmove(out, in, in_len);
	*out_len = in_len;

	return SC_SUCCESS;
}
