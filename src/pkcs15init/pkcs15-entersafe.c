/*
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
/* Initially written by Weitao Sun (weitao@ftsafe.com) 2008*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include <opensc/log.h>
#include <opensc/opensc.h>
#include <opensc/cardctl.h>
#include "pkcs15-init.h"
#include "profile.h"

static u8 process_acl_entry(sc_file_t *in, unsigned int method, unsigned int in_def)
{
	u8 def = (u8)in_def;
	const sc_acl_entry_t *entry = sc_file_get_acl_entry(in, method);
	if (!entry)
	{
		return def;
	}
	else if (entry->method == SC_AC_CHV)
	{
		unsigned int key_ref = entry->key_ref;
		if (key_ref == SC_AC_KEY_REF_NONE)
			return def;
		else
			return ENTERSAFE_AC_ALWAYS&0x04;
	}
	else if (entry->method == SC_AC_SYMBOLIC)
	{
		 return ENTERSAFE_AC_ALWAYS&0x04;
	}
	else if (entry->method == SC_AC_NEVER)
	{
		return ENTERSAFE_AC_NEVER;
	}
	else
	{
		return def;
	}
}

static int entersafe_erase_card(struct sc_profile *profile, sc_card_t *card)
{
	SC_FUNC_CALLED(card->ctx, 1);
	 return sc_card_ctl(card,SC_CARDCTL_ERASE_CARD,0);
}

static int entersafe_init_card(sc_profile_t *profile, sc_card_t *card)
{
	int ret;

	{/* MF */
		 sc_file_t *mf_file;
		 sc_entersafe_create_data mf_data;

		 SC_FUNC_CALLED(card->ctx, 1);

		 ret = sc_profile_get_file(profile, "MF", &mf_file);
		 SC_TEST_RET(card->ctx,ret,"Get MF info failed");

		 mf_data.type = SC_ENTERSAFE_MF_DATA;
		 mf_data.data.df.file_id[0]=0x3F;
		 mf_data.data.df.file_id[1]=0x00;
		 mf_data.data.df.file_count=0x04;
		 mf_data.data.df.flag=0x11;
		 mf_data.data.df.ikf_size[0]=(mf_file->size>>8)&0xFF;
		 mf_data.data.df.ikf_size[1]=mf_file->size&0xFF;
		 mf_data.data.df.create_ac=0x10;
		 mf_data.data.df.append_ac=0xC0;
		 mf_data.data.df.lock_ac=0x10;
		 memcpy(mf_data.data.df.aid,mf_file->name,mf_file->namelen);
		 sc_file_free(mf_file);
		 
		 ret = sc_card_ctl(card, SC_CARDCTL_ENTERSAFE_CREATE_FILE, &mf_data);
		 SC_TEST_RET(card->ctx,ret,"Create MF failed");
	}

	{/* EF(DIR) */
		 sc_file_t *dir_file;
		 size_t fid,size;
		 sc_entersafe_create_data ef_data;
		 u8 *buff=0;

		 /* get dir profile */
		 ret = sc_profile_get_file(profile, "dir", &dir_file);
		 SC_TEST_RET(card->ctx,ret,"Get EF(DIR) info failed");
		 fid=dir_file->id;
		 size=dir_file->size;
		 sc_file_free(dir_file);

		 ef_data.type=SC_ENTERSAFE_EF_DATA;
		 ef_data.data.ef.file_id[0]=(fid>>8)&0xFF;
		 ef_data.data.ef.file_id[1]=fid&0xFF;
		 ef_data.data.ef.size[0]=(size>>8)&0xFF;
		 ef_data.data.ef.size[1]=size&0xFF;
		 ef_data.data.ef.attr[0]=0x00;
		 ef_data.data.ef.attr[1]=0x00;
		 ef_data.data.ef.name=0x00;
		 memset(ef_data.data.ef.ac,0x10,sizeof(ef_data.data.ef.ac));
		 memset(ef_data.data.ef.sm,0x00,sizeof(ef_data.data.ef.sm));
		 
		 ret = sc_card_ctl(card, SC_CARDCTL_ENTERSAFE_CREATE_FILE, &ef_data);
		 SC_TEST_RET(card->ctx,ret,"Create EF(DIR) failed");
		 

		 /* fill file by 0 */
		 buff = calloc(1,size);
		 if(!buff)
			  SC_FUNC_RETURN(card->ctx,4,SC_SUCCESS);
		 memset(buff,0,size);
		 
		 ret = sc_update_binary(card,0,buff,size,0);
		 free(buff);
		 SC_TEST_RET(card->ctx,ret,"Initialize EF(DIR) failed");
	}

	SC_FUNC_RETURN(card->ctx,4,SC_SUCCESS);

}

static int entersafe_create_dir(sc_profile_t *profile, sc_card_t *card,
								sc_file_t *df)
{
	int             ret;

	SC_FUNC_CALLED(card->ctx, 1);

	{/* df */
		 sc_entersafe_create_data df_data;

		 df_data.type = SC_ENTERSAFE_DF_DATA;
		 df_data.data.df.file_id[0]=(df->id >> 8) & 0xFF;
		 df_data.data.df.file_id[1]=df->id & 0xFF;
		 df_data.data.df.file_count=0x0F;
		 df_data.data.df.flag=0x01;
		 df_data.data.df.ikf_size[0]=(df->size>>8)&0xFF;
		 df_data.data.df.ikf_size[1]=df->size&0xFF;
		 df_data.data.df.create_ac=0x10;
		 df_data.data.df.append_ac=0xC0;
		 df_data.data.df.lock_ac=0x10;
		 memcpy(df_data.data.df.aid,df->name,df->namelen);

		 ret = sc_card_ctl(card, SC_CARDCTL_ENTERSAFE_CREATE_FILE, &df_data);
		 SC_TEST_RET(card->ctx,ret,"Crate DF failed");
	}

	{/* GPKF */
		 sc_file_t *gpkf_file;
		 sc_entersafe_create_data ef_data;

		 /* get p15_gpkf profile */
		 ret = sc_profile_get_file(profile, "p15_gpkf", &gpkf_file);
		 SC_TEST_RET(card->ctx,ret,"Get GPKF info failed");

		 ef_data.type=SC_ENTERSAFE_EF_DATA;
		 ef_data.data.ef.file_id[0]=(gpkf_file->id>>8)&0xFF;
		 ef_data.data.ef.file_id[1]=gpkf_file->id&0xFF;
		 ef_data.data.ef.size[0]=(gpkf_file->size>>8)&0xFF;
		 ef_data.data.ef.size[1]=gpkf_file->size&0xFF;
		 ef_data.data.ef.attr[0]=0x15;
		 ef_data.data.ef.attr[1]=0x80;
		 ef_data.data.ef.name=0x00;
		 memset(ef_data.data.ef.ac,0x10,sizeof(ef_data.data.ef.ac));
		 memset(ef_data.data.ef.sm,0x00,sizeof(ef_data.data.ef.sm));

		 sc_file_free(gpkf_file);
		 
		 ret = sc_card_ctl(card, SC_CARDCTL_ENTERSAFE_CREATE_FILE, &ef_data);
		 SC_TEST_RET(card->ctx,ret,"Create GPKF failed");
	}

	{/* p15 efs */
		 char* create_efs[]={
			  "PKCS15-ODF",
			  "PKCS15-TokenInfo",
			  "PKCS15-UnusedSpace",
			  "PKCS15-AODF",
			  "PKCS15-PrKDF",
			  "PKCS15-PuKDF",
			  "PKCS15-CDF",
			  "PKCS15-DODF",
			  NULL,
		 };
		 int i;
		 sc_file_t *file=0;
		 sc_entersafe_create_data tmp;
		 
		 for(i = 0; create_efs[i]; ++i)   {
			  if (sc_profile_get_file(profile, create_efs[i], &file))   {
				   sc_error(card->ctx, "Inconsistent profile: cannot find %s", create_efs[i]);
				   SC_FUNC_RETURN(card->ctx,4,SC_ERROR_INCONSISTENT_PROFILE);
			  }

			  tmp.type=SC_ENTERSAFE_EF_DATA;
			  tmp.data.ef.file_id[0]=(file->id>>8)&0xFF;
			  tmp.data.ef.file_id[1]=file->id&0xFF;
			  tmp.data.ef.size[0]=(file->size>>8)&0xFF;
			  tmp.data.ef.size[1]=file->size&0xFF;
			  tmp.data.ef.attr[0]=0x00;
			  tmp.data.ef.attr[1]=0x00;
			  tmp.data.ef.name=0x00;
			  memset(tmp.data.ef.ac,ENTERSAFE_AC_ALWAYS,sizeof(tmp.data.ef.ac));
			  tmp.data.ef.ac[0]=process_acl_entry(file,SC_AC_OP_READ,ENTERSAFE_AC_ALWAYS); /* read */
			  tmp.data.ef.ac[1]=process_acl_entry(file,SC_AC_OP_UPDATE,ENTERSAFE_AC_ALWAYS); /* update */
			  memset(tmp.data.ef.sm,0x00,sizeof(tmp.data.ef.sm));
			  
			  sc_file_free(file);

			  ret = sc_card_ctl(card, SC_CARDCTL_ENTERSAFE_CREATE_FILE, &tmp);
			  SC_TEST_RET(card->ctx,ret,"Create pkcs15 file failed");
		 }
	}

	{/* Preinstall keys */
		 ret = sc_card_ctl(card, SC_CARDCTL_ENTERSAFE_PREINSTALL_KEYS, 0);
		 SC_TEST_RET(card->ctx,ret,"Preinstall keys failed");
	}

	SC_FUNC_RETURN(card->ctx,4,ret);
}

static int entersafe_pin_reference(sc_profile_t *profile, sc_card_t *card,
								   sc_pkcs15_pin_info_t *pin_info)
{
	SC_FUNC_CALLED(card->ctx, 1);

	if (pin_info->reference < ENTERSAFE_USER_PIN_ID)
		 pin_info->reference = ENTERSAFE_USER_PIN_ID;
	if(pin_info->reference>ENTERSAFE_USER_PIN_ID)
		 return SC_ERROR_TOO_MANY_OBJECTS;
	SC_FUNC_RETURN(card->ctx,4,SC_SUCCESS);
}

static int entersafe_create_pin(sc_profile_t *profile, sc_card_t *card,
								sc_file_t *df, sc_pkcs15_object_t *pin_obj,
								const unsigned char *pin, size_t pin_len,
								const unsigned char *puk, size_t puk_len)
{
	int	r;
	sc_pkcs15_pin_info_t *pin_info = (sc_pkcs15_pin_info_t *) pin_obj->data;
	sc_entersafe_wkey_data  data;

	SC_FUNC_CALLED(card->ctx, 1);

	if (!pin || !pin_len || pin_len > 16)
		return SC_ERROR_INVALID_ARGUMENTS;

	data.key_id=pin_info->reference;
	data.usage=0x0B;
	data.key_data.symmetric.EC=0x33;
	data.key_data.symmetric.ver=0x00;
	/* pad pin with 0 */
	memset(data.key_data.symmetric.key_val, 0, sizeof(data.key_data.symmetric.key_val));
	memcpy(data.key_data.symmetric.key_val, pin, pin_len);
	data.key_data.symmetric.key_len=16;

	r = sc_card_ctl(card, SC_CARDCTL_ENTERSAFE_WRITE_KEY, &data);

	SC_FUNC_RETURN(card->ctx,4,r);
}

static int entersafe_key_reference(sc_profile_t *profile, sc_card_t *card,
								   sc_pkcs15_prkey_info_t *prkey)
{
	struct sc_file	*df = profile->df_info->file;

	SC_FUNC_CALLED(card->ctx, 1);

	if (prkey->key_reference < ENTERSAFE_MIN_KEY_ID)
		prkey->key_reference = ENTERSAFE_MIN_KEY_ID;
	if (prkey->key_reference > ENTERSAFE_MAX_KEY_ID)
		return SC_ERROR_TOO_MANY_OBJECTS;

	prkey->path = df->path;
	SC_FUNC_RETURN(card->ctx,4,SC_SUCCESS);
}

static int entersafe_create_key(sc_profile_t *profile, sc_card_t *card,
								sc_pkcs15_object_t *obj)
{
	SC_FUNC_CALLED(card->ctx, 1);
	SC_FUNC_RETURN(card->ctx,4,SC_SUCCESS);
}

static int entersafe_store_key(sc_profile_t *profile, sc_card_t *card,
							   sc_pkcs15_object_t *obj, sc_pkcs15_prkey_t *key)
{
	sc_pkcs15_prkey_info_t *kinfo = (sc_pkcs15_prkey_info_t *) obj->data;
	sc_entersafe_wkey_data data;
	sc_file_t              *tfile;
	const sc_acl_entry_t   *acl_entry;
	int r;

	SC_FUNC_CALLED(card->ctx, 1);

	if (key->algorithm != SC_ALGORITHM_RSA)
		 /* ignore DSA keys */
		 SC_FUNC_RETURN(card->ctx,4,SC_ERROR_INVALID_ARGUMENTS);

	r = sc_profile_get_file(profile, "PKCS15-AODF", &tfile);
	if (r < 0)
		 return r;
	acl_entry = sc_file_get_acl_entry(tfile, SC_AC_OP_UPDATE);
	if (acl_entry->method  != SC_AC_NONE) {
		 r = sc_pkcs15init_authenticate(profile, card, tfile, SC_AC_OP_UPDATE);
		 if(r<0)
			  r = SC_ERROR_SECURITY_STATUS_NOT_SATISFIED;
	}
	sc_file_free(tfile);
	SC_TEST_RET(card->ctx, r, "cant verify pin");

	data.key_id = (u8) kinfo->key_reference;
	data.usage=0x22;
	data.key_data.rsa=&key->u.rsa;
	return sc_card_ctl(card, SC_CARDCTL_ENTERSAFE_WRITE_KEY, &data);
}

static int entersafe_generate_key(sc_profile_t *profile, sc_card_t *card,
								  sc_pkcs15_object_t *obj, sc_pkcs15_pubkey_t *pubkey)
{
	int r;
	sc_entersafe_gen_key_data	gendat;
	sc_pkcs15_prkey_info_t *kinfo = (sc_pkcs15_prkey_info_t *) obj->data;
	sc_file_t              *tfile;
	const sc_acl_entry_t   *acl_entry;

	SC_FUNC_CALLED(card->ctx, 1);

	if (obj->type != SC_PKCS15_TYPE_PRKEY_RSA)
		return SC_ERROR_NOT_SUPPORTED;

	r = sc_profile_get_file(profile, "PKCS15-AODF", &tfile);
	if (r < 0)
		 return r;
	acl_entry = sc_file_get_acl_entry(tfile, SC_AC_OP_UPDATE);
	if (acl_entry->method  != SC_AC_NONE) {
		 r = sc_pkcs15init_authenticate(profile, card, tfile, SC_AC_OP_UPDATE);
		 if(r<0)
			  r = SC_ERROR_SECURITY_STATUS_NOT_SATISFIED;
	}
	sc_file_free(tfile);
	SC_TEST_RET(card->ctx, r, "cant verify pin");

	/* generate key pair */
	gendat.key_id     = (u8) kinfo->key_reference;
	gendat.key_length = (size_t) kinfo->modulus_length;
	gendat.modulus    = NULL;
	r = sc_card_ctl(card, SC_CARDCTL_ENTERSAFE_GENERATE_KEY, &gendat);
	SC_TEST_RET(card->ctx, r, "EnterSafe generate RSA key pair failed");

	/* get the modulus via READ PUBLIC KEY */
	if (pubkey) {
		u8 *buf;
		struct sc_pkcs15_pubkey_rsa *rsa = &pubkey->u.rsa;
		/* set the modulus */
		rsa->modulus.data = gendat.modulus;
		rsa->modulus.len  = kinfo->modulus_length >> 3;
		/* set the exponent (always 0x10001) */
		buf = (u8 *) malloc(3);
		if (!buf)
			return SC_ERROR_OUT_OF_MEMORY;
		buf[0] = 0x01;
		buf[1] = 0x00;
		buf[2] = 0x01;
		rsa->exponent.data = buf;
		rsa->exponent.len  = 3;

		pubkey->algorithm = SC_ALGORITHM_RSA;
	} else
		/* free public key */
		free(gendat.modulus);

	SC_FUNC_RETURN(card->ctx,4,SC_SUCCESS);
}

static struct sc_pkcs15init_operations sc_pkcs15init_entersafe_operations = {
	entersafe_erase_card,
	entersafe_init_card,
	entersafe_create_dir,
	NULL,				/* create_domain */
	entersafe_pin_reference,
	entersafe_create_pin,
	entersafe_key_reference,
	entersafe_create_key,
	entersafe_store_key,
	entersafe_generate_key,
	NULL, NULL,			/* encode private/public key */
	NULL,	  			/* finalize */
	NULL, NULL, NULL, NULL, NULL,	/* old style api */
	NULL 				/* delete_object */
};

struct sc_pkcs15init_operations *sc_pkcs15init_get_entersafe_ops(void)
{
	return &sc_pkcs15init_entersafe_operations;
}
