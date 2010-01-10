/*
 * pkcs15-pin.c: PKCS #15 PIN functions
 *
 * Copyright (C) 2001, 2002  Juha Yrjölä <juha.yrjola@iki.fi>
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
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const struct sc_asn1_entry c_asn1_com_ao_attr[] = {
	{ "authId",       SC_ASN1_PKCS15_ID, SC_ASN1_TAG_OCTET_STRING, 0, NULL, NULL },
	{ NULL, 0, 0, 0, NULL, NULL }
};
static const struct sc_asn1_entry c_asn1_pin_attr[] = {
	{ "pinFlags",	  SC_ASN1_BIT_FIELD, SC_ASN1_TAG_BIT_STRING, 0, NULL, NULL },
	{ "pinType",      SC_ASN1_ENUMERATED, SC_ASN1_TAG_ENUMERATED, 0, NULL, NULL },
	{ "minLength",    SC_ASN1_INTEGER, SC_ASN1_TAG_INTEGER, 0, NULL, NULL },
	{ "storedLength", SC_ASN1_INTEGER, SC_ASN1_TAG_INTEGER, 0, NULL, NULL },
	{ "maxLength",    SC_ASN1_INTEGER, SC_ASN1_TAG_INTEGER, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "pinReference", SC_ASN1_INTEGER, SC_ASN1_CTX | 0, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "padChar",      SC_ASN1_OCTET_STRING, SC_ASN1_TAG_OCTET_STRING, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "lastPinChange",SC_ASN1_GENERALIZEDTIME, SC_ASN1_TAG_GENERALIZEDTIME, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "path",         SC_ASN1_PATH, SC_ASN1_TAG_SEQUENCE | SC_ASN1_CONS, SC_ASN1_OPTIONAL, NULL, NULL },
	{ NULL, 0, 0, 0, NULL, NULL }
};
static const struct sc_asn1_entry c_asn1_type_pin_attr[] = {
	{ "pinAttributes", SC_ASN1_STRUCT, SC_ASN1_TAG_SEQUENCE | SC_ASN1_CONS, 0, NULL, NULL },
	{ NULL, 0, 0, 0, NULL, NULL }
};
static const struct sc_asn1_entry c_asn1_pin[] = {
	{ "pin", SC_ASN1_PKCS15_OBJECT, SC_ASN1_TAG_SEQUENCE | SC_ASN1_CONS, 0, NULL, NULL },
	{ NULL, 0, 0, 0, NULL, NULL }
};

int sc_pkcs15_decode_aodf_entry(struct sc_pkcs15_card *p15card,
				struct sc_pkcs15_object *obj,
				const u8 ** buf, size_t *buflen)
{
	sc_context_t *ctx = p15card->card->ctx;
	struct sc_pkcs15_pin_info info;
	int r;
	size_t flags_len = sizeof(info.flags);
	size_t padchar_len = 1;
	struct sc_asn1_entry asn1_com_ao_attr[2], asn1_pin_attr[10], asn1_type_pin_attr[2];
	struct sc_asn1_entry asn1_pin[2];
	struct sc_asn1_pkcs15_object pin_obj = { obj, asn1_com_ao_attr, NULL, asn1_type_pin_attr };
	
	sc_copy_asn1_entry(c_asn1_pin, asn1_pin);
	sc_copy_asn1_entry(c_asn1_type_pin_attr, asn1_type_pin_attr);
	sc_copy_asn1_entry(c_asn1_pin_attr, asn1_pin_attr);
	sc_copy_asn1_entry(c_asn1_com_ao_attr, asn1_com_ao_attr);

	sc_format_asn1_entry(asn1_pin + 0, &pin_obj, NULL, 0);

	sc_format_asn1_entry(asn1_type_pin_attr + 0, asn1_pin_attr, NULL, 0);

	sc_format_asn1_entry(asn1_pin_attr + 0, &info.flags, &flags_len, 0);
	sc_format_asn1_entry(asn1_pin_attr + 1, &info.type, NULL, 0);
	sc_format_asn1_entry(asn1_pin_attr + 2, &info.min_length, NULL, 0);
	sc_format_asn1_entry(asn1_pin_attr + 3, &info.stored_length, NULL, 0);
	sc_format_asn1_entry(asn1_pin_attr + 4, &info.max_length, NULL, 0);
	sc_format_asn1_entry(asn1_pin_attr + 5, &info.reference, NULL, 0);
	sc_format_asn1_entry(asn1_pin_attr + 6, &info.pad_char, &padchar_len, 0);
	/* We don't support lastPinChange yet. */
	sc_format_asn1_entry(asn1_pin_attr + 8, &info.path, NULL, 0);

	sc_format_asn1_entry(asn1_com_ao_attr + 0, &info.auth_id, NULL, 0);

	/* Fill in defaults */
	memset(&info, 0, sizeof(info));
	info.reference = 0;
	info.tries_left = -1;

	r = sc_asn1_decode(ctx, asn1_pin, *buf, *buflen, buf, buflen);
	if (r == SC_ERROR_ASN1_END_OF_CONTENTS)
		return r;
	SC_TEST_RET(ctx, r, "ASN.1 decoding failed");
	info.magic = SC_PKCS15_PIN_MAGIC;
	obj->type = SC_PKCS15_TYPE_AUTH_PIN;
	obj->data = malloc(sizeof(info));
	if (obj->data == NULL)
		SC_FUNC_RETURN(ctx, 0, SC_ERROR_OUT_OF_MEMORY);
	if (info.max_length == 0) {
		if (p15card->card->max_pin_len != 0)
			info.max_length = p15card->card->max_pin_len;
		else if (info.stored_length != 0)
			info.max_length = info.type != SC_PKCS15_PIN_TYPE_BCD ?
				info.stored_length : 2 * info.stored_length;
		else
			info.max_length = 8; /* shouldn't happen */
	}

	/* OpenSC 0.11.4 and older encoded "pinReference" as a negative
	   value. Fixed in 0.11.5 we need to add a hack, so old cards
	   continue to work. */
	if (p15card->flags & SC_PKCS15_CARD_FLAG_FIX_INTEGERS) {
		if (info.reference < 0) {
			info.reference += 256;
		}
	}

	memcpy(obj->data, &info, sizeof(info));

	return 0;
}

int sc_pkcs15_encode_aodf_entry(sc_context_t *ctx,
				 const struct sc_pkcs15_object *obj,
				 u8 **buf, size_t *buflen)
{
	struct sc_asn1_entry asn1_com_ao_attr[2], asn1_pin_attr[10], asn1_type_pin_attr[2];
	struct sc_asn1_entry asn1_pin[2];
	struct sc_pkcs15_pin_info *pin =
                (struct sc_pkcs15_pin_info *) obj->data;
	struct sc_asn1_pkcs15_object pin_obj = { (struct sc_pkcs15_object *) obj,
						 asn1_com_ao_attr, NULL, asn1_type_pin_attr };
	int r;
	size_t flags_len;
	size_t padchar_len = 1;

	sc_copy_asn1_entry(c_asn1_pin, asn1_pin);
        sc_copy_asn1_entry(c_asn1_type_pin_attr, asn1_type_pin_attr);
        sc_copy_asn1_entry(c_asn1_pin_attr, asn1_pin_attr);
        sc_copy_asn1_entry(c_asn1_com_ao_attr, asn1_com_ao_attr);

	sc_format_asn1_entry(asn1_pin + 0, &pin_obj, NULL, 1);

	sc_format_asn1_entry(asn1_type_pin_attr + 0, asn1_pin_attr, NULL, 1);

	flags_len = sizeof(pin->flags);
	sc_format_asn1_entry(asn1_pin_attr + 0, &pin->flags, &flags_len, 1);
	sc_format_asn1_entry(asn1_pin_attr + 1, &pin->type, NULL, 1);
	sc_format_asn1_entry(asn1_pin_attr + 2, &pin->min_length, NULL, 1);
	sc_format_asn1_entry(asn1_pin_attr + 3, &pin->stored_length, NULL, 1);
	if (pin->max_length > 0)
		sc_format_asn1_entry(asn1_pin_attr + 4, &pin->max_length, NULL, 1);
	if (pin->reference >= 0)
		sc_format_asn1_entry(asn1_pin_attr + 5, &pin->reference, NULL, 1);
	/* FIXME: check if pad_char present */
	sc_format_asn1_entry(asn1_pin_attr + 6, &pin->pad_char, &padchar_len, 1);
	sc_format_asn1_entry(asn1_pin_attr + 8, &pin->path, NULL, 1);

	sc_format_asn1_entry(asn1_com_ao_attr + 0, &pin->auth_id, NULL, 1);

	assert(pin->magic == SC_PKCS15_PIN_MAGIC);
	r = sc_asn1_encode(ctx, asn1_pin, buf, buflen);

	return r;
}

static int _validate_pin(struct sc_pkcs15_card *p15card,
                         struct sc_pkcs15_pin_info *pin,
                         size_t pinlen)
{
	size_t max_length;
	assert(p15card != NULL);
	
	if (pin->magic != SC_PKCS15_PIN_MAGIC)
		return SC_ERROR_OBJECT_NOT_VALID;
		
	/* prevent buffer overflow from hostile card */	
	if (pin->stored_length > SC_MAX_PIN_SIZE)
		return SC_ERROR_BUFFER_TOO_SMALL;

	/* if we use pinpad, no more checks are needed */
	if (p15card->card->slot->capabilities & SC_SLOT_CAP_PIN_PAD)
		return SC_SUCCESS;
		
	/* If pin is given, make sure it is within limits */
	max_length = pin->max_length != 0 ? pin->max_length : SC_MAX_PIN_SIZE;
	if (pinlen > max_length || pinlen < pin->min_length)
		return SC_ERROR_INVALID_PIN_LENGTH;

	return SC_SUCCESS;
}

/*
 * Verify a PIN.
 *
 * If the code given to us has zero length, this means we
 * should ask the card reader to obtain the PIN from the
 * reader's PIN pad
 */
int sc_pkcs15_verify_pin(struct sc_pkcs15_card *p15card,
			 struct sc_pkcs15_pin_info *pin,
			 const u8 *pincode, size_t pinlen)
{
	int r;
	sc_card_t *card;
	struct sc_pin_cmd_data data;

	if ((r = _validate_pin(p15card, pin, pinlen)) != SC_SUCCESS)
		return r;

	card = p15card->card;

	r = sc_lock(card);
	if (r == SC_ERROR_CARD_RESET || r == SC_ERROR_READER_REATTACHED) {
		r = sc_lock(card);
	}
	SC_TEST_RET(card->ctx, r, "sc_lock() failed");
	/* the path in the pin object is optional */
	if (pin->path.len > 0) {
		r = sc_select_file(card, &pin->path, NULL);
		if (r)
			goto out;
	}

	/* Initialize arguments */
	memset(&data, 0, sizeof(data));
	data.cmd = SC_PIN_CMD_VERIFY;
	data.pin_type = SC_AC_CHV;
	data.pin_reference = pin->reference;
	data.pin1.min_length = pin->min_length;
	data.pin1.max_length = pin->max_length;
	data.pin1.pad_length = pin->stored_length;
	data.pin1.pad_char = pin->pad_char;
	data.pin1.data = pincode;
	data.pin1.len = pinlen;

	if (pin->flags & SC_PKCS15_PIN_FLAG_NEEDS_PADDING)
		data.flags |= SC_PIN_CMD_NEED_PADDING;

	switch (pin->type) {
	case SC_PKCS15_PIN_TYPE_BCD:
		data.pin1.encoding = SC_PIN_ENCODING_BCD;
		break;
	case SC_PKCS15_PIN_TYPE_ASCII_NUMERIC:
		data.pin1.encoding = SC_PIN_ENCODING_ASCII;
		break;
	default:
		/* assume/hope the card driver knows how to encode the pin */
		data.pin1.encoding = 0;
	}

	if(p15card->card->slot->capabilities & SC_SLOT_CAP_PIN_PAD) {
		data.flags |= SC_PIN_CMD_USE_PINPAD;
		if (pin->flags & SC_PKCS15_PIN_FLAG_SO_PIN)
			data.pin1.prompt = "Please enter SO PIN";
		else
			data.pin1.prompt = "Please enter PIN";
	}

	r = sc_pin_cmd(card, &data, &pin->tries_left);
out:
	sc_unlock(card);
	return r;
}

/*
 * Change a PIN.
 */
int sc_pkcs15_change_pin(struct sc_pkcs15_card *p15card,
			 struct sc_pkcs15_pin_info *pin,
			 const u8 *oldpin, size_t oldpinlen,
			 const u8 *newpin, size_t newpinlen)
{
	int r;
	sc_card_t *card;
	struct sc_pin_cmd_data data;
	
	/* make sure the pins are in valid range */
	if ((r = _validate_pin(p15card, pin, oldpinlen)) != SC_SUCCESS)
		return r;
	if ((r = _validate_pin(p15card, pin, newpinlen)) != SC_SUCCESS)
		return r;

	card = p15card->card;
	r = sc_lock(card);
	SC_TEST_RET(card->ctx, r, "sc_lock() failed");
	/* the path in the pin object is optional */
	if (pin->path.len > 0) {
		r = sc_select_file(card, &pin->path, NULL);
		if (r)
			goto out;
	}

	/* set pin_cmd data */
	memset(&data, 0, sizeof(data));
	data.cmd             = SC_PIN_CMD_CHANGE;
	data.pin_type        = SC_AC_CHV;
	data.pin_reference   = pin->reference;
	data.pin1.data       = oldpin;
	data.pin1.len        = oldpinlen;
	data.pin1.pad_char   = pin->pad_char;
	data.pin1.min_length = pin->min_length;
	data.pin1.max_length = pin->max_length;
	data.pin1.pad_length = pin->stored_length;
	data.pin2.data       = newpin;
	data.pin2.len        = newpinlen;
	data.pin2.pad_char   = pin->pad_char;
	data.pin2.min_length = pin->min_length;
	data.pin2.max_length = pin->max_length;
	data.pin2.pad_length = pin->stored_length;

	if (pin->flags & SC_PKCS15_PIN_FLAG_NEEDS_PADDING)
		data.flags |= SC_PIN_CMD_NEED_PADDING;

	switch (pin->type) {
	case SC_PKCS15_PIN_TYPE_BCD:
		data.pin1.encoding = SC_PIN_ENCODING_BCD;
		data.pin2.encoding = SC_PIN_ENCODING_BCD;
		break;
	case SC_PKCS15_PIN_TYPE_ASCII_NUMERIC:
		data.pin1.encoding = SC_PIN_ENCODING_ASCII;
		data.pin2.encoding = SC_PIN_ENCODING_ASCII;
		break;
	}
	
	if(p15card->card->slot->capabilities & SC_SLOT_CAP_PIN_PAD) {
		data.flags |= SC_PIN_CMD_USE_PINPAD;
		if (pin->flags & SC_PKCS15_PIN_FLAG_SO_PIN) {
			data.pin1.prompt = "Please enter SO PIN";
			data.pin2.prompt = "Please enter new SO PIN";
		} else {
			data.pin1.prompt = "Please enter PIN";
			data.pin2.prompt = "Please enter new PIN";
		}
	}

	r = sc_pin_cmd(card, &data, &pin->tries_left);

out:
	sc_unlock(card);
	return r;
}

/*
 * Unblock a PIN.
 */
int sc_pkcs15_unblock_pin(struct sc_pkcs15_card *p15card,
			 struct sc_pkcs15_pin_info *pin,
			 const u8 *puk, size_t puklen,
			 const u8 *newpin, size_t newpinlen)
{
	int r;
	sc_card_t *card;
	struct sc_pin_cmd_data data;
	struct sc_pkcs15_object *pin_obj, *puk_obj;
	struct sc_pkcs15_pin_info *puk_info = NULL;

	/* make sure the pins are in valid range */
	if ((r = _validate_pin(p15card, pin, newpinlen)) != SC_SUCCESS)
		return r;

	card = p15card->card;
	/* get pin_info object of the puk (this is a little bit complicated
	 * as we don't have the id of the puk (at least now))
	 * note: for compatibility reasons we give no error if no puk object
	 * is found */
	/* first step:  get the pkcs15 object of the pin */
	r = sc_pkcs15_find_pin_by_auth_id(p15card, &pin->auth_id, &pin_obj);
	if (r >= 0 && pin_obj) {
		/* second step: try to get the pkcs15 object of the puk */
		r = sc_pkcs15_find_pin_by_auth_id(p15card, &pin_obj->auth_id, &puk_obj);
		if (r >= 0 && puk_obj) {
			/* third step:  get the pkcs15 info object of the puk */
			puk_info = (struct sc_pkcs15_pin_info *)puk_obj->data;
		}
	}
	if (!puk_info) {
		sc_debug(card->ctx, "Unable to get puk object, using pin object instead!\n");
		puk_info = pin;
	}
	
	/* make sure the puk is in valid range */
	if ((r = _validate_pin(p15card, puk_info, puklen)) != SC_SUCCESS)
		return r;

	r = sc_lock(card);
	SC_TEST_RET(card->ctx, r, "sc_lock() failed");
	/* the path in the pin object is optional */
	if (pin->path.len > 0) {
		r = sc_select_file(card, &pin->path, NULL);
		if (r)
			goto out;
	}

	/* set pin_cmd data */
	memset(&data, 0, sizeof(data));
	data.cmd             = SC_PIN_CMD_UNBLOCK;
	data.pin_type        = SC_AC_CHV;
	data.pin_reference   = pin->reference;
	data.pin1.data       = puk;
	data.pin1.len        = puklen;
	data.pin1.pad_char   = pin->pad_char;
	data.pin1.min_length = pin->min_length;
	data.pin1.max_length = pin->max_length;
	data.pin1.pad_length = pin->stored_length;
	data.pin2.data       = newpin;
	data.pin2.len        = newpinlen;
	data.pin2.pad_char   = puk_info->pad_char;
	data.pin2.min_length = puk_info->min_length;
	data.pin2.max_length = puk_info->max_length;
	data.pin2.pad_length = puk_info->stored_length;

	if (pin->flags & SC_PKCS15_PIN_FLAG_NEEDS_PADDING)
		data.flags |= SC_PIN_CMD_NEED_PADDING;

	switch (pin->type) {
	case SC_PKCS15_PIN_TYPE_BCD:
		data.pin1.encoding = SC_PIN_ENCODING_BCD;
		break;
	case SC_PKCS15_PIN_TYPE_ASCII_NUMERIC:
		data.pin1.encoding = SC_PIN_ENCODING_ASCII;
		break;
	}

	switch (puk_info->type) {
	case SC_PKCS15_PIN_TYPE_BCD:
		data.pin2.encoding = SC_PIN_ENCODING_BCD;
		break;
	case SC_PKCS15_PIN_TYPE_ASCII_NUMERIC:
		data.pin2.encoding = SC_PIN_ENCODING_ASCII;
		break;
	}
	
	if(p15card->card->slot->capabilities & SC_SLOT_CAP_PIN_PAD) {
		data.flags |= SC_PIN_CMD_USE_PINPAD;
		if (pin->flags & SC_PKCS15_PIN_FLAG_SO_PIN) {
			data.pin1.prompt = "Please enter PUK";
			data.pin2.prompt = "Please enter new SO PIN";
		} else {
			data.pin1.prompt = "Please enter PUK";
			data.pin2.prompt = "Please enter new PIN";
		}
	}

	r = sc_pin_cmd(card, &data, &pin->tries_left);

out:
	sc_unlock(card);
	return r;
}

void sc_pkcs15_free_pin_info(sc_pkcs15_pin_info_t *pin)
{
	free(pin);
}
