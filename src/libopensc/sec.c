/*
 * sec.c: Cryptography and security (ISO7816-8) functions
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
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdio.h>
#include <string.h>
#include <assert.h>

int sc_decipher(struct sc_card *card,
		const u8 * crgram, size_t crgram_len, u8 * out, size_t outlen)
{
	int r;

	assert(card != NULL && crgram != NULL && out != NULL);
	SC_FUNC_CALLED(card->ctx, 2);
	if (card->ops->decipher == NULL)
		SC_FUNC_RETURN(card->ctx, 2, SC_ERROR_NOT_SUPPORTED);
	r = card->ops->decipher(card, crgram, crgram_len, out, outlen);
        SC_FUNC_RETURN(card->ctx, 2, r);
}

int sc_compute_signature(struct sc_card *card,
			 const u8 * data, size_t datalen,
			 u8 * out, size_t outlen)
{
	int r;

	assert(card != NULL);
	SC_FUNC_CALLED(card->ctx, 2);
	if (card->ops->compute_signature == NULL)
		SC_FUNC_RETURN(card->ctx, 2, SC_ERROR_NOT_SUPPORTED);
	r = card->ops->compute_signature(card, data, datalen, out, outlen);
        SC_FUNC_RETURN(card->ctx, 2, r);
}

int sc_set_security_env(struct sc_card *card,
			const struct sc_security_env *env,
			int se_num)
{
	int r;

	assert(card != NULL);
	SC_FUNC_CALLED(card->ctx, 2);
	if (card->ops->set_security_env == NULL)
		SC_FUNC_RETURN(card->ctx, 2, SC_ERROR_NOT_SUPPORTED);
	r = card->ops->set_security_env(card, env, se_num);
        SC_FUNC_RETURN(card->ctx, 2, r);
}

int sc_restore_security_env(struct sc_card *card, int se_num)
{
	int r;

	assert(card != NULL);
	SC_FUNC_CALLED(card->ctx, 2);
	if (card->ops->restore_security_env == NULL)
		SC_FUNC_RETURN(card->ctx, 2, SC_ERROR_NOT_SUPPORTED);
	r = card->ops->restore_security_env(card, se_num);
	SC_FUNC_RETURN(card->ctx, 2, r);
}

int sc_verify(struct sc_card *card, unsigned int type, int ref, 
	      const u8 *pin, size_t pinlen, int *tries_left)
{
	struct sc_pin_cmd_data data;

	memset(&data, 0, sizeof(data));
	data.cmd = SC_PIN_CMD_VERIFY;
	data.pin_type = type;
	data.pin_reference = ref;
	data.pin1.data = pin;
	data.pin1.len = pinlen;

	return sc_pin_cmd(card, &data, tries_left);
}

int sc_logout(struct sc_card *card)
{
	int r;
	if (card->ops->logout == NULL)
		/* Or should we return SC_ERROR_NOT_SUPPORTED? */
		SC_FUNC_RETURN(card->ctx, 2, SC_NO_ERROR);
	r = card->ops->logout(card);
        SC_FUNC_RETURN(card->ctx, 2, r);
}

int sc_change_reference_data(struct sc_card *card, unsigned int type,
			     int ref, const u8 *old, size_t oldlen,
			     const u8 *newref, size_t newlen,
			     int *tries_left)
{
	struct sc_pin_cmd_data data;

	memset(&data, 0, sizeof(data));
	data.cmd = SC_PIN_CMD_CHANGE;
	data.pin_type = type;
	data.pin_reference = ref;
	data.pin1.data = old;
	data.pin1.len = oldlen;
	data.pin2.data = newref;
	data.pin2.len = newlen;

	return sc_pin_cmd(card, &data, tries_left);
}

int sc_reset_retry_counter(struct sc_card *card, unsigned int type, int ref,
			   const u8 *puk, size_t puklen, const u8 *newref,
			   size_t newlen)
{
	struct sc_pin_cmd_data data;

	memset(&data, 0, sizeof(data));
	data.cmd = SC_PIN_CMD_UNBLOCK;
	data.pin_type = type;
	data.pin_reference = ref;
	data.pin1.data = puk;
	data.pin1.len = puklen;
	data.pin2.data = newref;
	data.pin2.len = newlen;

	return sc_pin_cmd(card, &data, NULL);
}

/*
 * This is the new style pin command, which takes care of all PIN
 * operations.
 * If a PIN was given by the application, the card driver should
 * send this PIN to the card. If no PIN was given, the driver should
 * ask the reader to obtain the pin(s) via the pin pad
 */
int sc_pin_cmd(struct sc_card *card, struct sc_pin_cmd_data *data,
		int *tries_left)
{
	int r;

	assert(card != NULL);
	SC_FUNC_CALLED(card->ctx, 2);
	if (card->ops->pin_cmd) {
		r = card->ops->pin_cmd(card, data, tries_left);
	} else if (!(data->flags & SC_PIN_CMD_USE_PINPAD)) {
		/* Card driver doesn't support new style pin_cmd, fall
		 * back to old interface */

		r = SC_ERROR_NOT_SUPPORTED;
		switch (data->cmd) {
		case SC_PIN_CMD_VERIFY:
			if (card->ops->verify != NULL)
				r = card->ops->verify(card,
					data->pin_type,
					data->pin_reference,
					data->pin1.data,
					data->pin1.len,
					tries_left);
			break;
		case SC_PIN_CMD_CHANGE:
			if (card->ops->change_reference_data != NULL)
				r = card->ops->change_reference_data(card,
					data->pin_type,
					data->pin_reference,
					data->pin1.data,
					data->pin1.len,
					data->pin2.data,
					data->pin2.len,
					tries_left);
			break;
		case SC_PIN_CMD_UNBLOCK:
			if (card->ops->reset_retry_counter != NULL)
				r = card->ops->reset_retry_counter(card,
					data->pin_type,
					data->pin_reference,
					data->pin1.data,
					data->pin1.len,
					data->pin2.data,
					data->pin2.len);
			break;
		}
		if (r == SC_ERROR_NOT_SUPPORTED)
			sc_error(card->ctx, "unsupported PIN operation (%d)",
					data->cmd);
	} else {
		sc_error(card->ctx, "Use of pin pad not supported by card driver");
		r = SC_ERROR_NOT_SUPPORTED;
	}
        SC_FUNC_RETURN(card->ctx, 2, r);
}

/*
 * This function will copy a PIN, convert and pad it as required
 *
 * Note about the SC_PIN_ENCODING_GLP encoding:
 * PIN buffers are allways 16 nibbles (8 bytes) and look like this:
 *   0x2 + len + pin_in_BCD + paddingnibbles
 * in which the paddingnibble = 0xF
 * E.g. if PIN = 12345, then sbuf = {0x24, 0x12, 0x34, 0x5F, 0xFF, 0xFF, 0xFF, 0xFF}
 * E.g. if PIN = 123456789012, then sbuf = {0x2C, 0x12, 0x34, 0x56, 0x78, 0x90, 0x12, 0xFF}
 * Reference: Global Platform - Card Specification - version 2.0.1' - April 7, 2000
 */
int sc_build_pin(u8 *buf, size_t buflen, struct sc_pin_cmd_pin *pin, int pad)
{
	size_t i = 0, j, pad_length = 0;
	size_t pin_len = pin->len;

	/* XXX: Should we silently truncate PINs that are too long? */
	if (pin->max_length && pin_len > pin->max_length)
		return SC_ERROR_INVALID_ARGUMENTS;

	if (pin->encoding == SC_PIN_ENCODING_GLP) {
		int i;
		while (pin_len > 0 && pin->data[pin_len - 1] == 0xFF)
			pin_len--;
		if (pin_len > 12)
			return SC_ERROR_INVALID_ARGUMENTS;
		for (i = 0; i < pin_len; i++) {
			if (pin->data[i] < '0' || pin->data[i] > '9')
				return SC_ERROR_INVALID_ARGUMENTS;
		}
		buf[0] = 0x20 | pin_len;
		buf++;
		buflen--;
	}

	/* PIN given by application, encode if required */
	if (pin->encoding == SC_PIN_ENCODING_ASCII) {
		if (pin_len > buflen)
			return SC_ERROR_BUFFER_TOO_SMALL;
		memcpy(buf, pin->data, pin_len);
		i = pin_len;
	} else if (pin->encoding == SC_PIN_ENCODING_BCD || pin->encoding == SC_PIN_ENCODING_GLP) {
		if (pin_len > 2 * buflen)
			return SC_ERROR_BUFFER_TOO_SMALL;
		for (i = j = 0; j < pin_len; j++) {
			buf[i] <<= 4;
			buf[i] |= pin->data[j] & 0xf;
			if (j & 1)
				i++;
		}
		if (j & 1) {
			buf[i] <<= 4;
			buf[i] |= pin->pad_char & 0xf;
			i++;
		}
	}

	/* Pad to maximum PIN length if requested */
	if (pad || pin->encoding == SC_PIN_ENCODING_GLP) {
		pad_length = pin->max_length;
		if (pin->encoding == SC_PIN_ENCODING_BCD)
			pad_length >>= 1;
		if (pin->encoding == SC_PIN_ENCODING_GLP)
			pad_length = 8;
	}

	if (pad_length > buflen)
		return SC_ERROR_BUFFER_TOO_SMALL;

	if (pad_length && i < pad_length) {
		memset(buf + i, 
			pin->encoding == SC_PIN_ENCODING_GLP ? 0xFF : pin->pad_char,
			pad_length - i);
		i = pad_length;
	}
	return i;
}
