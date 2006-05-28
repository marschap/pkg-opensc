/*
 * reader-openct.c: backend for OpenCT
 *
 * Copyright (C) 2003  Olaf Kirch <okir@suse.de>
 */

#include "internal.h"
#ifdef HAVE_OPENCT
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <openct/openct.h>
#include <openct/logging.h>
#include <openct/error.h>

/* If you set PREALLOCATE to a non-zero value, this backend
 * will allocate that many reader slots. This will allow hot-
 * plugging devices (such as USB tokens) while OpenSC is running.
 *
 * To disable this, set PREALLOCATE to 0.
 *
 * This will most likely become a config file option soon.
 */
#define PREALLOCATE	5

/* function declarations */
static int openct_reader_init(sc_context_t *ctx, void **priv_data);
static int openct_add_reader(sc_context_t *ctx, unsigned int num, ct_info_t *info);
static int openct_reader_finish(sc_context_t *ctx, void *priv_data);
static int openct_reader_release(sc_reader_t *reader);
static int openct_reader_detect_card_presence(sc_reader_t *reader,
			sc_slot_info_t *slot);
static int openct_reader_connect(sc_reader_t *reader,
			sc_slot_info_t *slot);
static int openct_reader_disconnect(sc_reader_t *reader,
			sc_slot_info_t *slot);
static int openct_reader_transmit(sc_reader_t *reader,
			sc_slot_info_t *slot, sc_apdu_t *apdu);
static int openct_reader_perform_verify(sc_reader_t *reader,
			sc_slot_info_t *slot,
			struct sc_pin_cmd_data *info);
static int openct_reader_lock(sc_reader_t *reader,
			sc_slot_info_t *slot);
static int openct_reader_unlock(sc_reader_t *reader,
			sc_slot_info_t *slot);
static int		openct_error(sc_reader_t *, int);

static struct sc_reader_operations openct_ops;

static struct sc_reader_driver openct_reader_driver = {
	"OpenCT reader",
	"openct",
	&openct_ops,
	0, 0, NULL
};

/* private data structures */
struct driver_data {
	ct_handle *	h;
	unsigned int	num;
	ct_info_t	info;
};

struct slot_data {
	ct_lock_handle	excl_lock;
	ct_lock_handle	shared_lock;
};

/*
 * Initialize readers
 *
 * Called during sc_establish_context(), when the driver
 * is loaded
 */
static int
openct_reader_init(sc_context_t *ctx, void **priv_data)
{
	unsigned int	i,max;
	scconf_block *conf_block;

	SC_FUNC_CALLED(ctx, 1);

	max=OPENCT_MAX_READERS; 

        conf_block = sc_get_conf_block(ctx, "reader_driver", "openct", 1);
	if (conf_block) {
		max = scconf_get_int(conf_block, "readers", OPENCT_MAX_READERS);
	}

	for (i = 0; i < max; i++) {
		ct_info_t	info;

		if (ct_reader_info(i, &info) >= 0) {
			openct_add_reader(ctx, i, &info);
		} else if (i < PREALLOCATE) {
			openct_add_reader(ctx, i, NULL);
		}
	}

	return SC_NO_ERROR;
}

static int
openct_add_reader(sc_context_t *ctx, unsigned int num, ct_info_t *info)
{
	sc_reader_t	*reader;
	struct driver_data *data;
	int		rc, i;

	if (!(reader = calloc(1, sizeof(*reader)))
	 || !(data = (calloc(1, sizeof(*data))))) {
		if (reader)
			free(reader);
		return SC_ERROR_OUT_OF_MEMORY;
	}

	if (info) {
		data->info = *info;
	} else {
		strcpy(data->info.ct_name, "OpenCT reader (detached)");
		data->info.ct_slots = 1;
	}
	data->num = num;

	reader->driver = &openct_reader_driver;
	reader->ops = &openct_ops;
	reader->drv_data = data;
	reader->name = strdup(data->info.ct_name);
	reader->slot_count = data->info.ct_slots;

	if ((rc = _sc_add_reader(ctx, reader)) < 0) { 
		free(data);
		free(reader->name);
		free(reader);
		return rc;
	}

	for (i = 0; i < SC_MAX_SLOTS; i++) {
		reader->slot[i].drv_data = calloc(1, sizeof(struct slot_data));
		reader->slot[i].id = i;
		if (data->info.ct_display)
			reader->slot[i].capabilities |= SC_SLOT_CAP_DISPLAY;
		if (data->info.ct_keypad)
			reader->slot[i].capabilities |= SC_SLOT_CAP_PIN_PAD;
	}

	return 0;
}

/*
 * Called when the driver is being unloaded.  finish() has to
 * deallocate the private data and any resources.
 */
int
openct_reader_finish(sc_context_t *ctx, void *priv_data)
{
	SC_FUNC_CALLED(ctx, 1);
	return SC_NO_ERROR;
}

/*
 * Called when releasing a reader.  release() has to
 * deallocate the private data.  Other fields will be
 * freed by OpenSC.
 */
int
openct_reader_release(sc_reader_t *reader)
{
	struct driver_data *data = (struct driver_data *) reader->drv_data;
	int i;

	SC_FUNC_CALLED(reader->ctx, 1);
	if (data) {
		if (data->h)
			ct_reader_disconnect(data->h);
		sc_mem_clear(data, sizeof(*data));
		reader->drv_data = NULL;
		free(data);
	}

	for (i = 0; i < SC_MAX_SLOTS; i++) {
		if(reader->slot[i].drv_data)
			free(reader->slot[i].drv_data);
	}
	
	return SC_NO_ERROR;
}

/*
 * Check whether a card was added/removed
 */
int
openct_reader_detect_card_presence(sc_reader_t *reader,
			sc_slot_info_t *slot)
{
	struct driver_data *data = (struct driver_data *) reader->drv_data;
	int rc, status;

	SC_FUNC_CALLED(reader->ctx, 1);

	slot->flags = 0;
	if (!data->h && !(data->h = ct_reader_connect(data->num)))
		return 0;

	if ((rc = ct_card_status(data->h, slot->id, &status)) < 0)
		return SC_ERROR_TRANSMIT_FAILED;

	if (status & IFD_CARD_PRESENT) {
		slot->flags = SC_SLOT_CARD_PRESENT;
		if (status & IFD_CARD_STATUS_CHANGED)
			slot->flags = SC_SLOT_CARD_PRESENT;
	}
	return slot->flags;
}

static int
openct_reader_connect(sc_reader_t *reader,
			sc_slot_info_t *slot)
{
	struct driver_data *data = (struct driver_data *) reader->drv_data;
	int rc;

	SC_FUNC_CALLED(reader->ctx, 1);

	if (data->h)
		ct_reader_disconnect(data->h);

	if (!(data->h = ct_reader_connect(data->num))) {
		sc_error(reader->ctx, "ct_reader_connect socket failed\n");
		return SC_ERROR_CARD_NOT_PRESENT;
	}

	rc = ct_card_request(data->h, slot->id, 0, NULL,
				slot->atr, sizeof(slot->atr));
	if (rc < 0) {
		sc_error(reader->ctx,
				"openct_reader_connect read failed: %s\n",
				ct_strerror(rc));
		return SC_ERROR_CARD_NOT_PRESENT;
	}

	if (rc == 0) {
		sc_error(reader->ctx, "openct_reader_connect recved no data\n");
		return SC_ERROR_READER;
	}

	slot->atr_len = rc;
	return SC_NO_ERROR;
}

static int
openct_reader_reconnect(sc_reader_t *reader,
			sc_slot_info_t *slot)
{
	struct driver_data *data = (struct driver_data *) reader->drv_data;
	int	rc;

	if (data->h != NULL)
		return 0;

	if ((rc = openct_reader_connect(reader, slot)) < 0)
		return SC_ERROR_READER_DETACHED;
	return SC_ERROR_READER_REATTACHED;
}

int
openct_reader_disconnect(sc_reader_t *reader,
			sc_slot_info_t *slot)
{
	struct driver_data *data = (struct driver_data *) reader->drv_data;

	SC_FUNC_CALLED(reader->ctx, 1);
	if (data->h)
		ct_reader_disconnect(data->h);
	data->h = NULL;
	return SC_NO_ERROR;
}

static int
openct_reader_internal_transmit(sc_reader_t *reader,
		sc_slot_info_t *slot,
		const u8 *sendbuf, size_t sendsize,
		u8 *recvbuf, size_t *recvsize, unsigned long control)
{
	struct driver_data *data = (struct driver_data *) reader->drv_data;
	int rc;

	/* Hotplug check */
	if ((rc = openct_reader_reconnect(reader, slot)) < 0)
		return rc;

	rc = ct_card_transact(data->h, slot->id,
			sendbuf, sendsize,
			recvbuf, *recvsize);

	if (rc == IFD_ERROR_NOT_CONNECTED) {
		ct_reader_disconnect(data->h);
		data->h = NULL;
		return SC_ERROR_READER_DETACHED;
	}

	if (rc >= 0)
		*recvsize = rc;

	return openct_error(reader, rc);
}

static int openct_reader_transmit(sc_reader_t *reader, sc_slot_info_t *slot,
	sc_apdu_t *apdu)
{
	size_t       ssize, rsize, rbuflen = 0;
	u8           *sbuf = NULL, *rbuf = NULL;
	int          r;

	rsize = rbuflen = apdu->resplen + 2;
	rbuf     = malloc(rbuflen);
	if (rbuf == NULL) {
		r = SC_ERROR_MEMORY_FAILURE;
		goto out;
	}
	/* encode and log the APDU */
	r = sc_apdu_get_octets(reader->ctx, apdu, &sbuf, &ssize, SC_PROTO_RAW);
	if (r != SC_SUCCESS)
		goto out;
	/* log data if DEBUG is defined */
#ifdef DEBUG
	sc_apdu_log(reader->ctx, sbuf, ssize, 1);
#endif
	r = openct_reader_internal_transmit(reader, slot, sbuf, ssize,
				rbuf, &rsize, apdu->control);
	if (r < 0) {
		/* unable to transmit ... most likely a reader problem */
		sc_error(reader->ctx, "unable to transmit");
		goto out;
	}
	/* log data if DEBUG is defined */
#ifdef DEBUG
	sc_apdu_log(reader->ctx, rbuf, rsize, 0);
#endif
	/* set response */
	r = sc_apdu_set_resp(reader->ctx, apdu, rbuf, rsize);
out:
	if (sbuf != NULL) {
		sc_mem_clear(sbuf, ssize);
		free(sbuf);
	}
	if (rbuf != NULL) {
		sc_mem_clear(rbuf, rbuflen);
		free(rbuf);
	}
	
	return r;
}


int
openct_reader_perform_verify(sc_reader_t *reader,
		sc_slot_info_t *slot,
		struct sc_pin_cmd_data *info)
{
	struct driver_data *data = (struct driver_data *) reader->drv_data;
	unsigned int pin_length = 0, pin_encoding;
	size_t j = 0;
	u8 buf[254];
	int rc;

	/* Hotplug check */
	if ((rc = openct_reader_reconnect(reader, slot)) < 0)
		return rc;

	if (info->apdu == NULL) {
		/* complain */
		return SC_ERROR_INVALID_ARGUMENTS;
	}

	buf[j++] = info->apdu->cla;
	buf[j++] = info->apdu->ins;
	buf[j++] = info->apdu->p1;
	buf[j++] = info->apdu->p2;

	if (info->apdu->lc) {
		size_t len = info->apdu->lc;

		if (j + 1 + len > sizeof(buf))
			return SC_ERROR_BUFFER_TOO_SMALL;
		buf[j++] = len;
		memcpy(buf+j, info->apdu->data, len);
		j += len;
	}

	if (info->pin1.min_length == info->pin1.max_length)
		pin_length = info->pin1.min_length;

	if (info->pin1.encoding == SC_PIN_ENCODING_ASCII)
		pin_encoding = IFD_PIN_ENCODING_ASCII;
	else if (info->pin1.encoding == SC_PIN_ENCODING_BCD)
		pin_encoding = IFD_PIN_ENCODING_BCD;
	else
		return SC_ERROR_INVALID_ARGUMENTS;

	rc = ct_card_verify(data->h, slot->id,
			0, /* no timeout?! */
			info->pin1.prompt,
			pin_encoding,
			pin_length,
			info->pin1.offset,
			buf, j,
			buf, sizeof(buf));
	if (rc < 0)
		return openct_error(reader, rc);
	if (rc != 2)
		return SC_ERROR_UNKNOWN_DATA_RECEIVED;
	info->apdu->sw1 = buf[0];
	info->apdu->sw2 = buf[1];
	return 0;
}


int
openct_reader_lock(sc_reader_t *reader,
			sc_slot_info_t *slot)
{
	struct driver_data *data = (struct driver_data *) reader->drv_data;
	struct slot_data *slot_data = (struct slot_data *) slot->drv_data;
	int rc;

	SC_FUNC_CALLED(reader->ctx, 1);

	/* Hotplug check */
	if ((rc = openct_reader_reconnect(reader, slot)) < 0)
		return rc;

	rc = ct_card_lock(data->h, slot->id,
				IFD_LOCK_EXCLUSIVE,
				&slot_data->excl_lock);

	if (rc == IFD_ERROR_NOT_CONNECTED) {
		ct_reader_disconnect(data->h);
		data->h = NULL;
		return SC_ERROR_READER_DETACHED;
	}

	return openct_error(reader, rc);
}

int
openct_reader_unlock(sc_reader_t *reader,
			sc_slot_info_t *slot)
{
	struct driver_data *data = (struct driver_data *) reader->drv_data;
	struct slot_data *slot_data = (struct slot_data *) slot->drv_data;
	int rc;

	SC_FUNC_CALLED(reader->ctx, 1);

	/* Not connected */
	if (data->h == NULL)
		return 0;

	rc = ct_card_unlock(data->h, slot->id, slot_data->excl_lock);

	/* We couldn't care less */
	if (rc == IFD_ERROR_NOT_CONNECTED)
		return 0;

	return openct_error(reader, rc);
}

/*
 * Handle an error code returned by OpenCT
 */
int
openct_error(sc_reader_t *reader, int code)
{
	if (code >= 0)
		return code;

	/* Fixme: translate error code */
	switch (code) {
	case IFD_ERROR_USER_TIMEOUT:
		return SC_ERROR_KEYPAD_TIMEOUT;
	case IFD_ERROR_USER_ABORT:
		return SC_ERROR_KEYPAD_CANCELLED;
	}
	return SC_ERROR_READER;
}

struct sc_reader_driver *sc_get_openct_driver(void)
{
	openct_ops.init = openct_reader_init;
	openct_ops.finish = openct_reader_finish;
	openct_ops.release = openct_reader_release;
	openct_ops.detect_card_presence = openct_reader_detect_card_presence;
	openct_ops.connect = openct_reader_connect;
	openct_ops.disconnect = openct_reader_disconnect;
	openct_ops.transmit = openct_reader_transmit;
	openct_ops.perform_verify = openct_reader_perform_verify;
	openct_ops.lock = openct_reader_lock;
	openct_ops.unlock = openct_reader_unlock;

	return &openct_reader_driver;
}

#endif	/* HAVE_OPENCT */
