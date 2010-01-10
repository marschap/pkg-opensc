/*
 * opensc.h: OpenSC library header file
 *
 * Copyright (C) 2001, 2002  Juha Yrjölä <juha.yrjola@iki.fi>
 *               2005        The OpenSC project
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

/**
 * @file src/libopensc/opensc.h
 * OpenSC library core header file
 */

#ifndef _OPENSC_H
#define _OPENSC_H

#include <stdio.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <opensc/scconf.h>
#include <opensc/errors.h>
#include <opensc/types.h>

/* Different APDU cases */
#define SC_APDU_CASE_NONE		0x00
#define SC_APDU_CASE_1			0x01
#define SC_APDU_CASE_2_SHORT		0x02
#define SC_APDU_CASE_3_SHORT		0x03
#define SC_APDU_CASE_4_SHORT		0x04
#define SC_APDU_SHORT_MASK		0x0f
#define SC_APDU_EXT			0x10
#define SC_APDU_CASE_2_EXT		SC_APDU_CASE_2_SHORT | SC_APDU_EXT
#define SC_APDU_CASE_3_EXT		SC_APDU_CASE_3_SHORT | SC_APDU_EXT
#define SC_APDU_CASE_4_EXT		SC_APDU_CASE_4_SHORT | SC_APDU_EXT
/* the following types let OpenSC decides whether to use 
 * short or extended APDUs */
#define SC_APDU_CASE_2			0x22
#define SC_APDU_CASE_3			0x23
#define SC_APDU_CASE_4			0x24

/* File types */
#define SC_FILE_TYPE_DF			0x04
#define SC_FILE_TYPE_INTERNAL_EF	0x03
#define SC_FILE_TYPE_WORKING_EF		0x01

/* EF structures */
#define SC_FILE_EF_UNKNOWN		0x00
#define SC_FILE_EF_TRANSPARENT		0x01
#define SC_FILE_EF_LINEAR_FIXED		0x02
#define SC_FILE_EF_LINEAR_FIXED_TLV	0x03
#define SC_FILE_EF_LINEAR_VARIABLE	0x04
#define SC_FILE_EF_LINEAR_VARIABLE_TLV	0x05
#define SC_FILE_EF_CYCLIC		0x06
#define SC_FILE_EF_CYCLIC_TLV		0x07

/* File status flags */
#define SC_FILE_STATUS_ACTIVATED	0x00
#define SC_FILE_STATUS_INVALIDATED	0x01
#define SC_FILE_STATUS_CREATION		0x02 /* Full access in this state,
						(at least for SetCOS 4.4 */

/* Access Control flags */
#define SC_AC_NONE			0x00000000
#define SC_AC_CHV			0x00000001 /* Card Holder Verif. */
#define SC_AC_TERM			0x00000002 /* Terminal auth. */
#define SC_AC_PRO			0x00000004 /* Secure Messaging */
#define SC_AC_AUT			0x00000008 /* Key auth. */

#define SC_AC_SYMBOLIC			0x00000010 /* internal use only */
#define SC_AC_UNKNOWN			0xFFFFFFFE
#define SC_AC_NEVER			0xFFFFFFFF

/* Operations relating to access control (in case of DF) */
#define SC_AC_OP_SELECT			0
#define SC_AC_OP_LOCK			1
#define SC_AC_OP_DELETE			2
#define SC_AC_OP_CREATE			3
#define SC_AC_OP_REHABILITATE		4
#define SC_AC_OP_INVALIDATE		5
#define SC_AC_OP_LIST_FILES		6
#define SC_AC_OP_CRYPTO			7
#define SC_AC_OP_DELETE_SELF		8
/* If you add more OPs here, make sure you increase
 * SC_MAX_AC_OPS in types.h */

/* Operations relating to access control (in case of EF) */
#define SC_AC_OP_READ			0
#define SC_AC_OP_UPDATE			1
/* the use of SC_AC_OP_ERASE is deprecated, SC_AC_OP_DELETE should be used
 * instead  */
#define SC_AC_OP_ERASE			SC_AC_OP_DELETE
#define SC_AC_OP_WRITE			3
/* rehab and invalidate are the same as in DF case */

/* various maximum values */
#define SC_MAX_READER_DRIVERS		6
#define SC_MAX_READERS			16
#define SC_MAX_CARD_DRIVERS		32
#define SC_MAX_CARD_DRIVER_SNAME_SIZE	16
#define SC_MAX_SLOTS			4
#define SC_MAX_CARD_APPS		8
#define SC_MAX_APDU_BUFFER_SIZE		258
#define SC_MAX_EXT_APDU_BUFFER_SIZE	65538
#define SC_MAX_PIN_SIZE			256 /* OpenPGP card has 254 max */
#define SC_MAX_ATR_SIZE			33
#define SC_MAX_AID_SIZE			16

/* default max_send_size/max_recv_size */
/* GPK rounds down to a multiple of 4, other driver have their own limits */
#define SC_DEFAULT_MAX_SEND_SIZE	255
#define SC_DEFAULT_MAX_RECV_SIZE	256

#define SC_AC_KEY_REF_NONE	0xFFFFFFFF

#define SC_SEC_OPERATION_DECIPHER	0x0001
#define SC_SEC_OPERATION_SIGN		0x0002
#define SC_SEC_OPERATION_AUTHENTICATE	0x0003

/* sc_security_env flags */
#define SC_SEC_ENV_ALG_REF_PRESENT	0x0001
#define SC_SEC_ENV_FILE_REF_PRESENT	0x0002
#define SC_SEC_ENV_KEY_REF_PRESENT	0x0004
/* FIXME: the flag below is misleading */
#define SC_SEC_ENV_KEY_REF_ASYMMETRIC	0x0008
#define SC_SEC_ENV_ALG_PRESENT		0x0010

/* PK algorithms */
#define SC_ALGORITHM_RSA		0
#define SC_ALGORITHM_DSA		1
#define SC_ALGORITHM_EC			2
#define SC_ALGORITHM_GOSTR3410		3

/* Symmetric algorithms */
#define SC_ALGORITHM_DES		64
#define SC_ALGORITHM_3DES		65
#define SC_ALGORITHM_GOST		66

/* Hash algorithms */
#define SC_ALGORITHM_MD5		128
#define SC_ALGORITHM_SHA1		129
#define SC_ALGORITHM_GOSTR3411		130

/* Key derivation algorithms */
#define SC_ALGORITHM_PBKDF2		192

/* Key encryption algoprithms */
#define SC_ALGORITHM_PBES2		256

#define SC_ALGORITHM_ONBOARD_KEY_GEN	0x80000000
#define SC_ALGORITHM_NEED_USAGE		0x40000000
#define SC_ALGORITHM_SPECIFIC_FLAGS	0x0000FFFF

#define SC_ALGORITHM_RSA_RAW		0x00000001
/* If the card is willing to produce a cryptogram padded with the following
 * methods, set these flags accordingly. */
#define SC_ALGORITHM_RSA_PADS		0x0000000E
#define SC_ALGORITHM_RSA_PAD_NONE	0x00000000
#define SC_ALGORITHM_RSA_PAD_PKCS1	0x00000002
#define SC_ALGORITHM_RSA_PAD_ANSI	0x00000004
#define SC_ALGORITHM_RSA_PAD_ISO9796	0x00000008

/* If the card is willing to produce a cryptogram with the following
 * hash values, set these flags accordingly. */
#define SC_ALGORITHM_RSA_HASH_NONE	0x00000010
#define SC_ALGORITHM_RSA_HASH_SHA1	0x00000020
#define SC_ALGORITHM_RSA_HASH_MD5	0x00000040
#define SC_ALGORITHM_RSA_HASH_MD5_SHA1	0x00000080
#define SC_ALGORITHM_RSA_HASH_RIPEMD160	0x00000100
#define SC_ALGORITHM_RSA_HASH_SHA256	0x00000200
#define SC_ALGORITHM_RSA_HASH_SHA384	0x00000400
#define SC_ALGORITHM_RSA_HASH_SHA512	0x00000800
#define SC_ALGORITHM_RSA_HASH_SHA224	0x00001000
#define SC_ALGORITHM_RSA_HASHES		0x00001FE0

#define SC_ALGORITHM_GOSTR3410_RAW		0x00002000
#define SC_ALGORITHM_GOSTR3410_HASH_NONE	0x00004000
#define SC_ALGORITHM_GOSTR3410_HASH_GOSTR3411	0x00008000
#define SC_ALGORITHM_GOSTR3410_HASHES		0x00008000

/* Event masks for sc_wait_for_event() */
#define SC_EVENT_CARD_INSERTED		0x0001
#define SC_EVENT_CARD_REMOVED		0x0002

typedef struct sc_security_env {
	unsigned long flags;
	int operation;
	unsigned int algorithm, algorithm_flags;

	unsigned int algorithm_ref;
	struct sc_path file_ref;
	u8 key_ref[8];
	size_t key_ref_len;
} sc_security_env_t;

struct sc_algorithm_id {
	unsigned int algorithm;
	struct sc_object_id obj_id;
	void *params;
};

struct sc_pbkdf2_params {
	u8 salt[16];
	size_t salt_len;
	int iterations;
	size_t key_length;
	struct sc_algorithm_id hash_alg;
};

struct sc_pbes2_params {
	struct sc_algorithm_id derivation_alg;
	struct sc_algorithm_id key_encr_alg;
};

typedef struct sc_algorithm_info {
	unsigned int algorithm;
	unsigned int key_length;
	unsigned int flags;

	union {
		struct sc_rsa_info {
			unsigned long exponent;
		} _rsa;
	} u;
} sc_algorithm_info_t;

typedef struct sc_app_info {
	u8 aid[SC_MAX_AID_SIZE];
	size_t aid_len;
	char *label;
	struct sc_path path;
	u8 *ddo;
	size_t ddo_len;

	const char *desc;	/* App description, if known */
	int rec_nr;		/* -1, if EF(DIR) is transparent */
} sc_app_info_t;

struct sc_card_cache {
	struct sc_path current_path;
};

#define SC_PROTO_T0		0x00000001
#define SC_PROTO_T1		0x00000002
#define SC_PROTO_RAW		0x00001000
#define SC_PROTO_ANY		0xFFFFFFFF

struct sc_reader_driver {
	const char *name;
	const char *short_name;
	struct sc_reader_operations *ops;

	size_t max_send_size, max_recv_size;
	void *dll;
};

/* slot flags */
#define SC_SLOT_CARD_PRESENT	0x00000001
#define SC_SLOT_CARD_CHANGED	0x00000002
/* slot capabilities */
#define SC_SLOT_CAP_DISPLAY	0x00000001
#define SC_SLOT_CAP_PIN_PAD	0x00000002

typedef struct sc_slot_info {
	int id;
	unsigned long flags, capabilities;
	unsigned int supported_protocols, active_protocol;
	u8 atr[SC_MAX_ATR_SIZE];
	size_t atr_len;

	struct _atr_info {
		u8 *hist_bytes;
		size_t hist_bytes_len;
		int Fi, f, Di, N;
		u8 FI, DI;
	} atr_info;

	void *drv_data;
} sc_slot_info_t;

struct sc_event_listener {
	unsigned int event_mask;
	void (*func)(void *, const struct sc_slot_info *, unsigned int event);
};

typedef struct sc_reader {
	struct sc_context *ctx;
	const struct sc_reader_driver *driver;
	const struct sc_reader_operations *ops;
	void *drv_data;
	char *name;

	struct sc_slot_info slot[SC_MAX_SLOTS];
	int slot_count;
} sc_reader_t;

/* This will be the new interface for handling PIN commands.
 * It is supposed to support pin pads (with or without display)
 * attached to the reader.
 */
#define SC_PIN_CMD_VERIFY	0
#define SC_PIN_CMD_CHANGE	1
#define SC_PIN_CMD_UNBLOCK	2

#define SC_PIN_CMD_USE_PINPAD	0x0001
#define SC_PIN_CMD_NEED_PADDING	0x0002

#define SC_PIN_ENCODING_ASCII	0
#define SC_PIN_ENCODING_BCD	1
#define SC_PIN_ENCODING_GLP	2 /* Global Platform - Card Specification v2.0.1 */

struct sc_pin_cmd_pin {
	const char *prompt;	/* Prompt to display */

	const u8 *data;		/* PIN, if given by the appliction */
	int len;		/* set to -1 to get pin from pin pad */

	size_t min_length;	/* min/max length of PIN */
	size_t max_length;
	unsigned int encoding;	/* ASCII-numeric, BCD, etc */
	size_t pad_length;	/* filled in by the card driver */
	u8 pad_char;
	size_t offset;          /* PIN offset in the APDU */
	size_t length_offset;	/* Effective PIN length offset in the APDU */
};

struct sc_pin_cmd_data {
	unsigned int cmd;
	unsigned int flags;

	unsigned int pin_type;		/* usually SC_AC_CHV */
	int pin_reference;

	struct sc_pin_cmd_pin pin1, pin2;

	struct sc_apdu *apdu;		/* APDU of the PIN command */
};

/* structure for the card serial number (normally the ICCSN) */
#define SC_MAX_SERIALNR		32

typedef struct sc_serial_number {
	u8 value[SC_MAX_SERIALNR];
	size_t len;
} sc_serial_number_t;

/* these flags are deprecated and shouldn't be used anymore */
#define SC_DISCONNECT			0
#define SC_DISCONNECT_AND_RESET		1
#define SC_DISCONNECT_AND_UNPOWER	2
#define SC_DISCONNECT_AND_EJECT		3

struct sc_reader_operations {
	/* Called during sc_establish_context(), when the driver
	 * is loaded */
	int (*init)(struct sc_context *ctx, void **priv_data);
	/* Called when the driver is being unloaded.  finish() has to
	 * deallocate the private data and any resources. */
	int (*finish)(struct sc_context *ctx, void *priv_data);
	/* Called when library wish to detect new readers
	 * should add only new readers. */
	int (*detect_readers)(struct sc_context *ctx, void *priv_data);
	/* Called when releasing a reader.  release() has to
	 * deallocate the private data.  Other fields will be
	 * freed by OpenSC. */
	int (*release)(struct sc_reader *reader);

	int (*detect_card_presence)(struct sc_reader *reader,
				    struct sc_slot_info *slot);
	int (*connect)(struct sc_reader *reader, struct sc_slot_info *slot);
	int (*disconnect)(struct sc_reader *reader, struct sc_slot_info *slot);
	int (*transmit)(struct sc_reader *reader, struct sc_slot_info *slot,
			sc_apdu_t *apdu);
	int (*lock)(struct sc_reader *reader, struct sc_slot_info *slot);
	int (*unlock)(struct sc_reader *reader, struct sc_slot_info *slot);
	int (*set_protocol)(struct sc_reader *reader, struct sc_slot_info *slot,
			    unsigned int proto);
	/* Pin pad functions */
	int (*display_message)(struct sc_reader *, struct sc_slot_info *,
			       const char *);
	int (*perform_verify)(struct sc_reader *, struct sc_slot_info *,
			 struct sc_pin_cmd_data *);

	/* Wait for an event */
	int (*wait_for_event)(struct sc_reader **readers,
			      struct sc_slot_info **slots,
			      size_t nslots,
			      unsigned int event_mask,
			      int *reader_index,
			      unsigned int *event,
			      int timeout);
	int (*reset)(struct sc_reader *, struct sc_slot_info *);
};

/*
 * Card flags
 *
 * Used to hint about card specific capabilities and algorithms
 * supported to the card driver. Used in sc_atr_table and
 * card_atr block structures in the configuration file.
 *
 * Unknown, card vendor specific values may exists, but must
 * not conflict with values defined here. All actions defined
 * by the flags must be handled by the card driver themselves.
 */

/* Mask for card vendor specific values */
#define SC_CARD_FLAG_VENDOR_MASK	0xFFFF0000

/* Hint SC_ALGORITHM_ONBOARD_KEY_GEN */
#define SC_CARD_FLAG_ONBOARD_KEY_GEN	0x00000001
/* Hint SC_CARD_CAP_RNG */
#define SC_CARD_FLAG_RNG		0x00000002

/*
 * Card capabilities
 */

/* Card can handle large (> 256 bytes) buffers in calls to
 * read_binary, write_binary and update_binary; if not,
 * several successive calls to the corresponding function
 * is made. */
#define SC_CARD_CAP_APDU_EXT		0x00000001

/* Card can handle operations specified in the
 * EMV 4.0 standard. */
#define SC_CARD_CAP_EMV			0x00000002

/* Card has on-board random number source. */
#define SC_CARD_CAP_RNG			0x00000004

/* Card doesn't return any File Control Info. */
#define SC_CARD_CAP_NO_FCI		0x00000008

/* Use the card's ACs in sc_pkcs15init_authenticate(),
 * instead of relying on the ACL info in the profile files. */
#define SC_CARD_CAP_USE_FCI_AC		0x00000010

/* The card supports 2048 bit RSA keys */
#define SC_CARD_CAP_RSA_2048		0x00000020

/* D-TRUST CardOS cards special flags */
#define SC_CARD_CAP_ONLY_RAW_HASH        0x00000040
#define SC_CARD_CAP_ONLY_RAW_HASH_STRIPPED      0x00000080

typedef struct sc_card {
	struct sc_context *ctx;
	struct sc_reader *reader;
	struct sc_slot_info *slot;

	int type;			/* Card type, for card driver internal use */
	unsigned long caps, flags;
	unsigned int wait_resend_apdu;	/* Delay (msec) before responding to an SW12 = 6CXX */
	int cla;
	u8 atr[SC_MAX_ATR_SIZE];
	size_t atr_len;
	size_t max_send_size;
	size_t max_recv_size;

	struct sc_app_info *app[SC_MAX_CARD_APPS];
	int app_count;
	struct sc_file *ef_dir;

	struct sc_algorithm_info *algorithms;
	int algorithm_count;

	int lock_count;

	struct sc_card_driver *driver;
	struct sc_card_operations *ops;
	const char *name;
	void *drv_data;
	int max_pin_len;

	struct sc_card_cache cache;
	int cache_valid;

	sc_serial_number_t serialnr;

	void *mutex;

	unsigned int magic;
} sc_card_t;

struct sc_card_operations {
	/* Called in sc_connect_card().  Must return 1, if the current
	 * card can be handled with this driver, or 0 otherwise.  ATR
	 * field of the sc_card struct is filled in before calling
	 * this function. */
	int (*match_card)(struct sc_card *card);

	/* Called when ATR of the inserted card matches an entry in ATR
	 * table.  May return SC_ERROR_INVALID_CARD to indicate that
	 * the card cannot be handled with this driver. */
	int (*init)(struct sc_card *card);
	/* Called when the card object is being freed.  finish() has to
	 * deallocate all possible private data. */
	int (*finish)(struct sc_card *card);

	/* ISO 7816-4 functions */

	int (*read_binary)(struct sc_card *card, unsigned int idx,
			   u8 * buf, size_t count, unsigned long flags);
	int (*write_binary)(struct sc_card *card, unsigned int idx,
			    const u8 * buf, size_t count, unsigned long flags);
	int (*update_binary)(struct sc_card *card, unsigned int idx,
			     const u8 * buf, size_t count, unsigned long flags);
	int (*erase_binary)(struct sc_card *card, unsigned int idx,
			    size_t count, unsigned long flags);

	int (*read_record)(struct sc_card *card, unsigned int rec_nr,
			   u8 * buf, size_t count, unsigned long flags);
	int (*write_record)(struct sc_card *card, unsigned int rec_nr,
			    const u8 * buf, size_t count, unsigned long flags);
	int (*append_record)(struct sc_card *card, const u8 * buf,
			     size_t count, unsigned long flags);
	int (*update_record)(struct sc_card *card, unsigned int rec_nr,
			     const u8 * buf, size_t count, unsigned long flags);

	/* select_file: Does the equivalent of SELECT FILE command specified
	 *   in ISO7816-4. Stores information about the selected file to
	 *   <file>, if not NULL. */
	int (*select_file)(struct sc_card *card, const struct sc_path *path,
			   struct sc_file **file_out);
	int (*get_response)(struct sc_card *card, size_t *count, u8 *buf);
	int (*get_challenge)(struct sc_card *card, u8 * buf, size_t count);

	/*
	 * ISO 7816-8 functions
	 */

	/* verify:  Verifies reference data of type <acl>, identified by
	 *   <ref_qualifier>. If <tries_left> is not NULL, number of verifying
	 *   tries left is saved in case of verification failure, if the
	 *   information is available. */
	int (*verify)(struct sc_card *card, unsigned int type,
		      int ref_qualifier, const u8 *data, size_t data_len,
		      int *tries_left);

	/* logout: Resets all access rights that were gained. */
	int (*logout)(struct sc_card *card);

	/* restore_security_env:  Restores a previously saved security
	 *   environment, and stores information about the environment to
	 *   <env_out>, if not NULL. */
	int (*restore_security_env)(struct sc_card *card, int se_num);

	/* set_security_env:  Initializes the security environment on card
	 *   according to <env>, and stores the environment as <se_num> on the
	 *   card. If se_num <= 0, the environment will not be stored. */
	int (*set_security_env)(struct sc_card *card,
			        const struct sc_security_env *env, int se_num);
	/* decipher:  Engages the deciphering operation.  Card will use the
	 *   security environment set in a call to set_security_env or
	 *   restore_security_env. */
	int (*decipher)(struct sc_card *card, const u8 * crgram,
		        size_t crgram_len, u8 * out, size_t outlen);

	/* compute_signature:  Generates a digital signature on the card.  Similiar
	 *   to the function decipher. */
	int (*compute_signature)(struct sc_card *card, const u8 * data,
				 size_t data_len, u8 * out, size_t outlen);
	int (*change_reference_data)(struct sc_card *card, unsigned int type,
				     int ref_qualifier,
				     const u8 *old, size_t oldlen,
				     const u8 *newref, size_t newlen,
				     int *tries_left);
	int (*reset_retry_counter)(struct sc_card *card, unsigned int type,
				   int ref_qualifier,
				   const u8 *puk, size_t puklen,
				   const u8 *newref, size_t newlen);
	/*
	 * ISO 7816-9 functions
	 */
	int (*create_file)(struct sc_card *card, struct sc_file *file);
	int (*delete_file)(struct sc_card *card, const struct sc_path *path);
	/* list_files:  Enumerates all the files in the current DF, and
	 *   writes the corresponding file identifiers to <buf>.  Returns
	 *   the number of bytes stored. */
	int (*list_files)(struct sc_card *card, u8 *buf, size_t buflen);

	int (*check_sw)(struct sc_card *card,unsigned int sw1,unsigned int sw2);
	int (*card_ctl)(struct sc_card *card, unsigned long request,
				void *data);
	int (*process_fci)(struct sc_card *card, struct sc_file *file,
			const u8 *buf, size_t buflen);
	int (*construct_fci)(struct sc_card *card, const struct sc_file *file,
			u8 *out, size_t *outlen);

	/* pin_cmd: verify/change/unblock command; optionally using the
	 * card's pin pad if supported.
	 */
	int (*pin_cmd)(struct sc_card *, struct sc_pin_cmd_data *,
				int *tries_left);

	int (*get_data)(sc_card_t *, unsigned int, u8 *, size_t);
	int (*put_data)(sc_card_t *, unsigned int, const u8 *, size_t);

	int (*delete_record)(sc_card_t *card, unsigned int rec_nr);
};

typedef struct sc_card_driver {
	const char *name;
	const char *short_name;
	struct sc_card_operations *ops;
	struct sc_atr_table *atr_map;
	unsigned int natrs;
	void *dll;
} sc_card_driver_t;

/**
 * @struct sc_thread_context_t
 * Structure for the locking function to use when using libopensc
 * in a multi-threaded application.
 */
typedef struct {
	/** the version number of this structure (0 for this version) */
	unsigned int ver;
	/** creates a mutex object */
	int (*create_mutex)(void **);
	/** locks a mutex object (blocks until the lock has been acquired) */
	int (*lock_mutex)(void *);
	/** unlocks a mutex object  */
	int (*unlock_mutex)(void *);
	/** destroys a mutex object */
	int (*destroy_mutex)(void *);
	/** returns unique identifier for the thread (can be NULL) */
	unsigned long (*thread_id)(void);
} sc_thread_context_t;

typedef struct sc_context {
	scconf_context *conf;
	scconf_block *conf_blocks[3];
	char *app_name;
	int debug;

	int suppress_errors;
	FILE *debug_file, *error_file;
	char *preferred_language;

	const struct sc_reader_driver *reader_drivers[SC_MAX_READER_DRIVERS];
	void *reader_drv_data[SC_MAX_READER_DRIVERS];

	struct sc_reader *reader[SC_MAX_READERS];
	int reader_count;

	struct sc_card_driver *card_drivers[SC_MAX_CARD_DRIVERS];
	struct sc_card_driver *forced_driver;

	sc_thread_context_t	*thread_ctx;
	void *mutex;

	unsigned int magic;
} sc_context_t;

/* APDU handling functions */

/** Sends a APDU to the card
 *  @param  card  sc_card_t object to which the APDU should be send
 *  @param  apdu  sc_apdu_t object of the APDU to be send
 *  @return SC_SUCCESS on succcess and an error code otherwise
 */
int sc_transmit_apdu(sc_card_t *card, sc_apdu_t *apdu);

void sc_format_apdu(sc_card_t *card, sc_apdu_t *apdu, int cse, int ins,
		    int p1, int p2);

int sc_check_sw(struct sc_card *card, unsigned int sw1, unsigned int sw2);

/********************************************************************/
/*                  opensc context functions                        */
/********************************************************************/

/**
 * Establishes an OpenSC context. Note: this function is deprecated,
 * please use sc_context_create() instead.
 * @param ctx A pointer to a pointer that will receive the allocated context
 * @param app_name A string that identifies the application, used primarily
 *	in finding application-specific configuration data. Can be NULL.
 */
int sc_establish_context(sc_context_t **ctx, const char *app_name);

/**
 * @struct sc_context_t initialization parameters
 * Structure to supply additional parameters, for example
 * mutex information, to the sc_context_t creation.
 */
typedef struct {
	/** version number of this structure (0 for this version) */
	unsigned int  ver;
	/** name of the application (used for finding application
	 *  dependend configuration data). If NULL the name "default"
	 *  will be used. */
	const char    *app_name;
	/** flags, currently unused */
	unsigned long flags;
	/** mutex functions to use (optional) */
	sc_thread_context_t *thread_ctx;
} sc_context_param_t;
/**
 * Creates a new sc_context_t object.
 * @param  ctx   pointer to a sc_context_t pointer for the newly
 *               created sc_context_t object.
 * @param  parm  parameters for the sc_context_t creation (see 
 *               sc_context_param_t for a description of the supported
 *               options). This parameter is optional and can be NULL.
 * @return SC_SUCCESS on success and an error code otherwise.
 */
int sc_context_create(sc_context_t **ctx, const sc_context_param_t *parm);

/**
 * Releases an established OpenSC context
 * @param ctx A pointer to the context structure to be released
 */
int sc_release_context(sc_context_t *ctx);

/**
 * Detect new readers available on system.
 * @param  ctx  OpenSC context
 * @return SC_SUCCESS on success and an error code otherwise.
 */
int sc_ctx_detect_readers(sc_context_t *ctx);

/**
 * Returns a pointer to the specified sc_reader_t object
 * @param  ctx  OpenSC context
 * @param  i    number of the reader structure to return (starting with 0)
 * @return the requested sc_reader object or NULL if the index is
 *         not available
 */
sc_reader_t *sc_ctx_get_reader(sc_context_t *ctx, unsigned int i);

/**
 * Returns the number a available sc_reader objects
 * @param  ctx  OpenSC context
 * @return the number of available reader objects
 */
unsigned int sc_ctx_get_reader_count(sc_context_t *ctx);

/**  
 * Turns on error suppression 
 * @param  ctx  OpenSC context
 */
void sc_ctx_suppress_errors_on(sc_context_t *ctx);

/**
 * Turns off error suppression
 * @param  ctx  OpenSC context
 */
void sc_ctx_suppress_errors_off(sc_context_t *ctx);

/**
 * Forces the use of a specified card driver
 * @param ctx OpenSC context
 * @param short_name The short name of the driver to use (e.g. 'emv')
 */
int sc_set_card_driver(sc_context_t *ctx, const char *short_name);
/**
 * Connects to a card in a reader and auto-detects the card driver.
 * The ATR (Answer to Reset) string of the card is also retrieved.
 * @param reader Reader structure
 * @param slot_id Slot ID to connect to
 * @param card The allocated card object will go here */
int sc_connect_card(sc_reader_t *reader, int slot_id, sc_card_t **card);
/**
 * Disconnects from a card, and frees the card structure. Any locks
 * made by the application must be released before calling this function.
 * NOTE: The card is not reset nor powered down after the operation.
 * @param  card  The card to disconnect
 * @param  flag  currently not used (should be set to 0)
 * @return SC_SUCCESS on success and an error code otherwise
 */
int sc_disconnect_card(sc_card_t *card, int flag);
/**
 * Returns 1 if the magic value of the card object is correct. Mostly
 * used internally by the library.
 * @param card The card object to check
 */
int sc_card_valid(const sc_card_t *card);

/**
 * Checks if a card is present in a reader
 * @param reader Reader structure
 * @param slot_id Slot ID
 * @retval If an error occured, the return value is a (negative)
 *	OpenSC error code. If no card is present, 0 is returned.
 *	Otherwise, a positive value is returned, which is a
 *	combination of flags. The flag SC_SLOT_CARD_PRESENT is
 *	always set. In addition, if the card was exchanged,
 *	the SC_SLOT_CARD_CHANGED flag is set.
 */
int sc_detect_card_presence(sc_reader_t *reader, int slot_id);

/**
 * Waits for an event on readers. Note: only the event is detected,
 * there is no update of any card or other info.
 * @param readers array of pointer to a Reader structure
 * @param reader_count amount of readers in the array
 * @param slot_id Slot ID
 * @param event_mask The types of events to wait for; this should
 *   be ORed from one of the following
 *   	SC_EVENT_CARD_REMOVED
 *   	SC_EVENT_CARD_INSERTED
 * @param reader (OUT) the reader on which the event was detected
 * @param event (OUT) the events that occurred. This is also ORed
 *   from the SC_EVENT_CARD_* constants listed above.
 * @param timeout Amount of millisecs to wait; -1 means forever
 * @retval < 0 if an error occured
 * @retval = 0 if a an event happened
 * @retval = 1 if the timeout occured
 */
int sc_wait_for_event(sc_reader_t **readers, int *slots, size_t nslots,
                      unsigned int event_mask,
                      int *reader, unsigned int *event, int timeout);

/**
 * Resets the card.
 * NOTE: only PC/SC backend implements this function at this moment.
 * @param card The card to reset.
 * @retval SC_SUCCESS on success
 */
int sc_reset(sc_card_t *card);

/**
 * Tries acquire the reader lock.
 * @param  card  The card to lock
 * @retval SC_SUCCESS on success
 */
int sc_lock(sc_card_t *card);
/**
 * Unlocks a previously acquired reader lock.
 * @param  card  The card to unlock
 * @retval SC_SUCCESS on success
 */
int sc_unlock(sc_card_t *card);


/********************************************************************/
/*                ISO 7816-4 related functions                      */
/********************************************************************/

/**
 * Does the equivalent of ISO 7816-4 command SELECT FILE.
 * @param  card  sc_card_t object on which to issue the command
 * @param  path  The path, file id or name of the desired file
 * @param  file  If not NULL, will receive a pointer to a new structure
 * @return SC_SUCCESS on success and an error code otherwise
 */
int sc_select_file(sc_card_t *card, const sc_path_t *path,
		   sc_file_t **file);
/**
 * List file ids within a DF
 * @param  card    sc_card_t object on which to issue the command
 * @param  buf     buffer for the read file ids (the filed ids are
 *                 stored in the buffer as a sequence of 2 byte values)
 * @param  buflen  length of the supplied buffer
 * @return number of files ids read or an error code
 */
int sc_list_files(sc_card_t *card, u8 *buf, size_t buflen);
/**
 * Read data from a binary EF
 * @param  card   sc_card_t object on which to issue the command
 * @param  idx    index within the file with the data to read
 * @param  buf    buffer to the read data
 * @param  count  number of bytes to read
 * @param  flags  flags for the READ BINARY command (currently not used)
 * @return number of bytes read or an error code
 */
int sc_read_binary(sc_card_t *card, unsigned int idx, u8 * buf,
		   size_t count, unsigned long flags);
/**
 * Write data to a binary EF
 * @param  card   sc_card_t object on which to issue the command 
 * @param  idx    index within the file for the data to be written 
 * @param  buf    buffer with the data
 * @param  count  number of bytes to write
 * @param  flags  flags for the WRITE BINARY command (currently not used)
 * @return number of bytes writen or an error code
 */
int sc_write_binary(sc_card_t *card, unsigned int idx, const u8 * buf,
		    size_t count, unsigned long flags);
/**
 * Updates the content of a binary EF
 * @param  card   sc_card_t object on which to issue the command
 * @param  idx    index within the file for the data to be updated
 * @param  buf    buffer with the new data
 * @param  count  number of bytes to update
 * @param  flags  flags for the UPDATE BINARY command (currently not used)
 * @return number of bytes writen or an error code
 */
int sc_update_binary(sc_card_t *card, unsigned int idx, const u8 * buf,
		     size_t count, unsigned long flags);

#define SC_RECORD_EF_ID_MASK		0x0001FUL
/** flags for record operations */
/** use first record */
#define SC_RECORD_BY_REC_ID		0x00000UL
/** use the specified record number */
#define SC_RECORD_BY_REC_NR		0x00100UL
/** use currently selected record */
#define SC_RECORD_CURRENT		0UL

/**
 * Reads a record from the current (i.e. selected) file.
 * @param  card    sc_card_t object on which to issue the command
 * @param  rec_nr  SC_READ_RECORD_CURRENT or a record number starting from 1
 * @param  buf     Pointer to a buffer for storing the data
 * @param  count   Number of bytes to read
 * @param  flags   flags (may contain a short file id of a file to select)
 * @retval number of bytes read or an error value
 */
int sc_read_record(sc_card_t *card, unsigned int rec_nr, u8 * buf,
		   size_t count, unsigned long flags);
/**
 * Writes data to a record from the current (i.e. selected) file.
 * @param  card    sc_card_t object on which to issue the command
 * @param  rec_nr  SC_READ_RECORD_CURRENT or a record number starting from 1
 * @param  buf     buffer with to the data to be writen
 * @param  count   number of bytes to write
 * @param  flags   flags (may contain a short file id of a file to select)
 * @retval number of bytes writen or an error value
 */
int sc_write_record(sc_card_t *card, unsigned int rec_nr, const u8 * buf,
		    size_t count, unsigned long flags);
/**
 * Appends a record to the current (i.e. selected) file.
 * @param  card    sc_card_t object on which to issue the command
 * @param  buf     buffer with to the data for the new record
 * @param  count   length of the data
 * @param  flags   flags (may contain a short file id of a file to select)
 * @retval number of bytes writen or an error value
 */
int sc_append_record(sc_card_t *card, const u8 * buf, size_t count,
		     unsigned long flags);
/**
 * Updates the data of a record from the current (i.e. selected) file.
 * @param  card    sc_card_t object on which to issue the command
 * @param  rec_nr  SC_READ_RECORD_CURRENT or a record number starting from 1
 * @param  buf     buffer with to the new data to be writen
 * @param  count   number of bytes to update
 * @param  flags   flags (may contain a short file id of a file to select)
 * @retval number of bytes writen or an error value
 */
int sc_update_record(sc_card_t *card, unsigned int rec_nr, const u8 * buf,
		     size_t count, unsigned long flags);
int sc_delete_record(sc_card_t *card, unsigned int rec_nr);

/* get/put data functions */
int sc_get_data(sc_card_t *, unsigned int, u8 *, size_t);
int sc_put_data(sc_card_t *, unsigned int, const u8 *, size_t);

/**
 * Gets challenge from the card (normally random data).
 * @param  card    sc_card_t object on which to issue the command
 * @param  rndout  buffer for the returned random challenge
 * @param  len     length of the challenge
 * @return SC_SUCCESS on success and an error code otherwise
 */
int sc_get_challenge(sc_card_t *card, u8 * rndout, size_t len);

/********************************************************************/
/*              ISO 7816-8 related functions                        */
/********************************************************************/

int sc_restore_security_env(sc_card_t *card, int se_num);
int sc_set_security_env(sc_card_t *card,
			const struct sc_security_env *env, int se_num);
int sc_decipher(sc_card_t *card, const u8 * crgram, size_t crgram_len,
		u8 * out, size_t outlen);
int sc_compute_signature(sc_card_t *card, const u8 * data,
			 size_t data_len, u8 * out, size_t outlen);
int sc_verify(sc_card_t *card, unsigned int type, int ref, const u8 *buf,
	      size_t buflen, int *tries_left);
/**
 * Resets the security status of the card (i.e. withdraw all granted
 * access rights). Note: not all card operating systems support a logout
 * command and in this case SC_ERROR_NOT_SUPPORTED is returned.
 * @param  card  sc_card_t object
 * @return SC_SUCCESS on success, SC_ERROR_NOT_SUPPORTED if the card
 *         doesn't support a logout command and an error code otherwise
 */
int sc_logout(sc_card_t *card);
int sc_pin_cmd(sc_card_t *card, struct sc_pin_cmd_data *, int *tries_left);
int sc_change_reference_data(sc_card_t *card, unsigned int type,
			     int ref, const u8 *old, size_t oldlen,
			     const u8 *newref, size_t newlen,
			     int *tries_left);
int sc_reset_retry_counter(sc_card_t *card, unsigned int type,
			   int ref, const u8 *puk, size_t puklen,
			   const u8 *newref, size_t newlen);
int sc_build_pin(u8 *buf, size_t buflen, struct sc_pin_cmd_pin *pin, int pad);


/********************************************************************/
/*               ISO 7816-9 related functions                       */
/********************************************************************/

int sc_create_file(sc_card_t *card, sc_file_t *file);
int sc_delete_file(sc_card_t *card, const sc_path_t *path);

/* Card controls */
int sc_card_ctl(sc_card_t *card, unsigned long command, void *arg);

int sc_file_valid(const sc_file_t *file);
sc_file_t * sc_file_new(void);
void sc_file_free(sc_file_t *file);
void sc_file_dup(sc_file_t **dest, const sc_file_t *src);

int sc_file_add_acl_entry(sc_file_t *file, unsigned int operation,
			  unsigned int method, unsigned long key_ref);
const struct sc_acl_entry * sc_file_get_acl_entry(const sc_file_t *file,
						  unsigned int operation);
void sc_file_clear_acl_entries(sc_file_t *file, unsigned int operation);

int sc_file_set_sec_attr(sc_file_t *file, const u8 *sec_attr,
			 size_t sec_attr_len);
int sc_file_set_prop_attr(sc_file_t *file, const u8 *prop_attr,
			  size_t prop_attr_len);
int sc_file_set_type_attr(sc_file_t *file, const u8 *type_attr,
			  size_t type_attr_len);


/********************************************************************/
/*             sc_path_t handling functions                         */
/********************************************************************/

/**
 * Sets the content of a sc_path_t object.
 * @param  path    sc_path_t object to set
 * @param  type    type of path
 * @param  id      value of the path 
 * @param  id_len  length of the path value 
 * @param  index   index within the file
 * @param  count   number of bytes
 * @return SC_SUCCESS on success and an error code otherwise
 */
int sc_path_set(sc_path_t *path, int type, const u8 *id, size_t id_len,
	int index, int count);

void sc_format_path(const char *path_in, sc_path_t *path_out);
/**
 * Return string representation of the given sc_path_t object
 * Warning: as static memory is used for the return value 
 *          this function is not thread-safe !!!
 * @param  path  sc_path_t object of the path to be printed
 * @return pointer to a const buffer with the string representation
 *         of the path
 */
const char *sc_print_path(const sc_path_t *path);
/**
 * Prints the sc_path_t object to a character buffer
 * @param  buf     pointer to the buffer
 * @param  buflen  size of the buffer
 * @param  path    sc_path_t object to be printed
 * @return SC_SUCCESS on success and an error code otherwise
 */
int sc_path_print(char *buf, size_t buflen, const sc_path_t *path);
/**
 * Compares two sc_path_t objects 
 * @param  patha  sc_path_t object of the first path
 * @param  pathb  sc_path_t object of the second path
 * @return 1 if both paths are equal and 0 otherwise
 */
int sc_compare_path(const sc_path_t *patha, const sc_path_t *pathb);
/**
 * Concatenate two sc_path_t values and store the result in
 * d (note: d can be the same as p1 or p2).
 * @param  d   destination sc_path_t object
 * @param  p1  first sc_path_t object
 * @param  p2  second sc_path_t object
 * @return SC_SUCCESS on success and an error code otherwise
 */
int sc_concatenate_path(sc_path_t *d, const sc_path_t *p1, const sc_path_t *p2);
/**
 * Appends a sc_path_t object to another sc_path_t object (note:
 * this function is a wrapper for sc_concatenate_path)
 * @param  dest  destination sc_path_t object
 * @param  src   sc_path_t object to append
 * @return SC_SUCCESS on success and an error code otherwise
 */
int sc_append_path(sc_path_t *dest, const sc_path_t *src);
/**
 * Checks whether one path is a prefix of another path 
 * @param  prefix  sc_path_t object with the prefix
 * @param  path    sc_path_t object with the path which should start
 *                 with the given prefix
 * @return 1 if the parameter prefix is a prefix of path and 0 otherwise
 */
int sc_compare_path_prefix(const sc_path_t *prefix, const sc_path_t *path);
int sc_append_path_id(sc_path_t *dest, const u8 *id, size_t idlen);
int sc_append_file_id(sc_path_t *dest, unsigned int fid);
/**
 * Returns a const sc_path_t object for the MF
 * @return sc_path_t object of the MF
 */
const sc_path_t *sc_get_mf_path(void);

/********************************************************************/
/*             miscellaneous functions                              */
/********************************************************************/

int sc_hex_to_bin(const char *in, u8 *out, size_t *outlen);
int sc_bin_to_hex(const u8 *, size_t, char *, size_t, int separator);
scconf_block *sc_get_conf_block(sc_context_t *ctx, const char *name1, const char *name2, int priority);
/**
 * Converts a given OID in ascii form to a internal sc_object_id object
 * @param  oid  OUT sc_object_id object for the result
 * @param  in   ascii string with the oid ("1.2.3.4.5...")
 * @return SC_SUCCESS or an error value if an error occurred.
 */
int sc_format_oid(struct sc_object_id *oid, const char *in);
/**
 * Compares two sc_object_id objects
 * @param  oid1  the first sc_object_id object
 * @param  oid2  the second sc_object_id object
 * @return 1 if the oids are equal and a non-zero value otherwise
 */
int sc_compare_oid(const struct sc_object_id *oid1, const struct sc_object_id *oid2);

/* Base64 encoding/decoding functions */
int sc_base64_encode(const u8 *in, size_t inlen, u8 *out, size_t outlen,
		     size_t linelength);
int sc_base64_decode(const char *in, u8 *out, size_t outlen);

/**
 * Clears a memory buffer (note: when OpenSSL is used this is
 * currently a wrapper for OPENSSL_cleanse() ).
 * @param  ptr  pointer to the memory buffer
 * @param  len  length of the memory buffer
 */
void sc_mem_clear(void *ptr, size_t len);

int sc_get_cache_dir(sc_context_t *ctx, char *buf, size_t bufsize);
int sc_make_cache_dir(sc_context_t *ctx);

int sc_enum_apps(sc_card_t *card);
void sc_free_apps(sc_card_t *card);
const sc_app_info_t * sc_find_pkcs15_app(sc_card_t *card);
const sc_app_info_t * sc_find_app_by_aid(sc_card_t *card,
					 const u8 *aid, size_t aid_len);
int sc_update_dir(sc_card_t *card, sc_app_info_t *app);

struct sc_card_error {
	unsigned int SWs;
	int errorno;
	const char *errorstr;
};

extern const char *sc_get_version(void);

#define SC_IMPLEMENT_DRIVER_VERSION(a) \
	static const char *drv_version = (a); \
	const char *sc_driver_version()\
	{ \
		return drv_version; \
	}

extern sc_card_driver_t *sc_get_iso7816_driver(void);

#ifdef __cplusplus
}
#endif

#endif
