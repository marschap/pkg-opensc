/*
 * Oberthur AWP extention for PKCS #15 initialization
 *
 * Copyright (C) 2010  Viktor Tarasov <viktor.tarasov@opentrust.com>
 * Copyright (C) 2002  Juha Yrjola <juha.yrjola@iki.fi>
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
 *
 *  best view with tabstop=4
 *  
 */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "config.h"
#include "libopensc/opensc.h"
#include "libopensc/cardctl.h"
#include "libopensc/log.h"
#include "profile.h"
#include "pkcs15-init.h"
#include "pkcs15-oberthur.h"
#include "libopensc/asn1.h"

#ifdef ENABLE_OPENSSL

struct awp_lv zero_lv = { 0, NULL };
struct awp_lv x30_lv = { 0x10, (unsigned char *)"0000000000000000" };

static unsigned char *
awp_get_commonName(X509 *x)
{
	unsigned char *ret = NULL;
	int r;

   	r = X509_NAME_get_index_by_NID(X509_get_subject_name(x),
		   NID_commonName, -1);
	if (r >= 0)   {
		X509_NAME_ENTRY *ne;
		ASN1_STRING *a_str;
		
		if (!(ne = X509_NAME_get_entry(X509_get_subject_name(x), r))) 
			;
		else if (!(a_str = X509_NAME_ENTRY_get_data(ne))) 
			;
		else if (a_str->type == 0x0C)   {
			ret = malloc(a_str->length + 1);
			if (ret)   {
				memcpy(ret, a_str->data, a_str->length);
				*(ret + a_str->length) = '\0';
			}
		}
		else    {
			unsigned char *tmp = NULL;

			r = ASN1_STRING_to_UTF8(&tmp, a_str);
			if (r > 0)   {
				ret = malloc(r + 1);
				if (ret)   {
					memcpy(ret, tmp, r);
					*(ret + r) = '\0';
				}

				OPENSSL_free(tmp);
			}
		}
	}
		
	return ret;
}


static int
awp_new_file(struct sc_pkcs15_card *p15card, struct sc_profile *profile,
		unsigned int type, unsigned int num,
		struct sc_file **info_out, struct sc_file **obj_out)
{
	struct sc_context *ctx = p15card->card->ctx;
	struct sc_file	*ifile=NULL, *ofile=NULL;
	char	name[NAME_MAX_LEN];
	const char *itag=NULL, *otag=NULL;

	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "type 0x%X; num %i; info %p; obj %p", type, num, info_out, obj_out);
	switch (type) {
	case SC_PKCS15_TYPE_CERT_X509:
		itag = "certificate-info";
		otag = "template-certificate";
		break;
	case SC_PKCS15_TYPE_PRKEY_RSA:
	case COSM_TYPE_PRKEY_RSA:
		itag = "private-key-info";
		otag = "template-private-key";
		break;
	case SC_PKCS15_TYPE_PUBKEY_RSA:
	case COSM_TYPE_PUBKEY_RSA:
		itag = "public-key-info";
		otag = "template-public-key";
		break;
	case SC_PKCS15_TYPE_DATA_OBJECT:
		itag = "data-info";
		otag = "template-data";
		break;
	case COSM_TYPE_PRIVDATA_OBJECT:
		itag = "privdata-info";
		otag = "template-privdata";
		break;
	case SC_PKCS15_TYPE_AUTH_PIN:
	case COSM_TOKENINFO : 
		itag = "token-info";
		num = 0;
		break;
	case COSM_PUBLIC_LIST:
		itag = "public-list";
		num = 0;
		break;
	case COSM_PRIVATE_LIST:
		itag = "private-list";
		num = 0;
		break;
	case COSM_CONTAINER_LIST:
        itag = "container-list";
        num = 0;
        break;
	default:
		return SC_ERROR_INVALID_ARGUMENTS;
	}
	
	if (itag)  {
		snprintf(name, sizeof(name),"%s-%s", COSM_TITLE, itag);
		sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "info template %s",name);
		if (sc_profile_get_file(profile, name, &ifile) < 0) {
			sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "profile does not defines template '%s'", name);
			return SC_ERROR_INCONSISTENT_PROFILE;
		}
	}

	if (otag)   {
		sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "obj template %s",otag);
		if (sc_profile_get_file(profile, otag, &ofile) < 0) {
			sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "profile does not defines template '%s'", name);
			return SC_ERROR_INCONSISTENT_PROFILE;
		}
		
		ofile->id |= (num & 0xFF);
		ofile->path.value[ofile->path.len-1] |= (num & 0xFF);
	}

	if (ifile)    {
		if(info_out)    {
			if (ofile)   {
				ifile->id = ofile->id | 0x100;
				
				ifile->path = ofile->path;
				ifile->path.value[ifile->path.len-2] |= 0x01;
			}
			
			sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "info_file(id:%04X,size:%i,rlen:%i)", 
					ifile->id, ifile->size, ifile->record_length);
			*info_out = ifile;
		}
		else   {
			sc_file_free(ifile);
		}
	}

	if (ofile)   {
		sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "obj file %04X; size %i; ", ofile->id, ofile->size);
		if (obj_out)   
			*obj_out = ofile;
		else   
			sc_file_free(ofile);	
	}

	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, SC_SUCCESS);
}


static int
awp_update_blob(struct sc_context *ctx,
		unsigned char **blob, int *blob_size,
		struct awp_lv *lv, int type)
{
	unsigned char *pp;

	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);
	switch (type)  {
	case TLV_TYPE_LLV :
		if (!(pp = realloc(*blob, *blob_size + 2 + lv->len)))
			return SC_ERROR_OUT_OF_MEMORY;
		*(pp + *blob_size) = (lv->len >> 8) & 0xFF;
		*(pp + *blob_size + 1) = lv->len & 0xFF;
		memcpy(pp + *blob_size + 2, lv->value, (lv->len & 0xFF));
		*blob_size += 2 + lv->len;
		break;
	case TLV_TYPE_LV :
		if (!(pp = realloc(*blob, *blob_size + 1 + lv->len)))
			return SC_ERROR_OUT_OF_MEMORY;
		*(pp + *blob_size) = lv->len & 0xFF;
		memcpy(pp + *blob_size + 1, lv->value, (lv->len & 0xFF));
		*blob_size += 1 + lv->len;
		break;
	case TLV_TYPE_V :
		if (!(pp = realloc(*blob, *blob_size + lv->len)))
			return SC_ERROR_OUT_OF_MEMORY;
		memcpy(pp + *blob_size, lv->value, lv->len);
		*blob_size += lv->len;
		break;
	default:
		sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "Invalid tlv type %i",type);
		return SC_ERROR_INCORRECT_PARAMETERS;
	}
	
	*blob = pp;
	
	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, SC_SUCCESS);
}


static int
awp_new_container_entry(struct sc_pkcs15_card *p15card, unsigned char *buff, int len)
{
	struct sc_context *ctx = p15card->card->ctx;
	int ii, mm, rv = 0;
	int marks[5] = {4,6,8,10,0};
	unsigned char rand_buf[0x10];

	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);
	if (len<0x34) 
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_INCORRECT_PARAMETERS, "Invalid container update size");

	rv = sc_get_challenge(p15card->card, rand_buf, sizeof(rand_buf));
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "Cannot get challenge");

	*(buff + 12) = 0x26;
	*(buff + 13) = '{';
	for (ii=0, mm = 0; ii<sizeof(rand_buf); ii++)   {
		if (ii==marks[mm])   {
			*(buff + 14 + ii*2 + mm) = '-';
			mm++;
		}
		sprintf((char *)(buff + 14 + ii*2 + mm),"%02X", rand_buf[ii]);
	}
	*(buff + 14 + ii*2 + mm) = (unsigned char)'}';

	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, rv);
}


static int 
awp_create_container_record (struct sc_pkcs15_card *p15card, struct sc_profile *profile, 
		struct sc_file *list_file,  struct awp_crypto_container *acc)
{
	struct sc_context *ctx = p15card->card->ctx;
	int rv;
	unsigned char *buff = NULL;
	
	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "container file(file-id:%X,rlen:%i,rcount:%i)", 
			list_file->id, list_file->record_length, list_file->record_count);

	buff = malloc(list_file->record_length);
	if (!buff)
		SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_OUT_OF_MEMORY);
	
	memset(buff, 0, list_file->record_length);
	
	rv = awp_new_container_entry(p15card, buff, list_file->record_length);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "Cannot create container");
	
	*(buff + 0) = (acc->pubkey_id >> 8) & 0xFF;
	*(buff + 1) = acc->pubkey_id & 0xFF;
	*(buff + 2) = (acc->prkey_id >> 8) & 0xFF;
	*(buff + 3) = acc->prkey_id & 0xFF;
	*(buff + 4) = (acc->cert_id >> 8) & 0xFF;
	*(buff + 5) = acc->cert_id & 0xFF;

	rv = sc_select_file(p15card->card, &list_file->path, NULL);
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "rv:%i", rv);
	if (rv == SC_ERROR_FILE_NOT_FOUND) 
		rv = sc_pkcs15init_create_file(profile, p15card, list_file);
		
	if (!rv) 
		rv = sc_append_record(p15card->card, buff, list_file->record_length, SC_RECORD_BY_REC_NR);
	
	free(buff);
	
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "return after failure");

	rv = 0;
	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, rv);
}


static int 
awp_create_container(struct sc_pkcs15_card *p15card, struct sc_profile *profile, int type,	
		struct awp_lv *key_id, struct awp_crypto_container *acc)
{
	struct sc_context *ctx = p15card->card->ctx;
	struct sc_file *clist = NULL, *file = NULL;
	int rv = 0;
	unsigned char *list = NULL;
	  
	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "create container(%X:%X:%X)", acc->prkey_id, acc->cert_id, acc->pubkey_id);

	rv = awp_new_file(p15card, profile, COSM_CONTAINER_LIST, 0, &clist, NULL);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "Create container failed");
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "contaner cfile(rcount:%i,rlength:%i)", clist->record_count, clist->record_length);

	rv = sc_select_file(p15card->card, &clist->path, &file);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "Create container failed: cannot select container's list");
	file->record_length = clist->record_length;
	
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "contaner file(rcount:%i,rlength:%i)", file->record_count, file->record_length);
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "Append new record %i for private key", file->record_count + 1);

	rv = awp_create_container_record(p15card, profile, file, acc);

	if (clist)
		sc_file_free(clist);
	if (file)
		sc_file_free(file);
	if (list)
		free(list);
	
	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, rv);
}


static int 
awp_update_container_entry (struct sc_pkcs15_card *p15card, struct sc_profile *profile, 
		struct sc_file *list_file,  int type, int file_id, 
		int rec, int offs)
{
	struct sc_context *ctx = p15card->card->ctx;
	int rv;
	unsigned char *buff = NULL;
	
	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "update container entry(type:%X,len:%i,count %i,rec %i,offs %i", type, file_id, rec, offs);
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "container file(file-id:%X,rlen:%i,rcount:%i)", 
			list_file->id, list_file->record_length, list_file->record_count);

	buff = malloc(list_file->record_length);
	if (!buff)
		SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_OUT_OF_MEMORY);
	
	memset(buff, 0, list_file->record_length);
	
	if (rec > list_file->record_count)   {
		rv = awp_new_container_entry(p15card, buff, list_file->record_length);
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "Cannot create container");
	}
	else   {
		rv = sc_select_file(p15card->card, &list_file->path, NULL);
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "Cannot select list_file");
		
		rv = sc_read_record(p15card->card, rec, buff, list_file->record_length, SC_RECORD_BY_REC_NR);
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "Cannot read record");
	}
	
	switch (type)  {
	case SC_PKCS15_TYPE_PUBKEY_RSA:
	case COSM_TYPE_PUBKEY_RSA:
		if (*(buff + offs + 4))  
			sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "Insert public key to container that contains certificate %02X%02X",
					*(buff + offs + 4), *(buff + offs + 5));
		*(buff + offs + 0) = (file_id >> 8) & 0xFF;
		*(buff + offs + 1) = file_id & 0xFF;
		break;
	case SC_PKCS15_TYPE_PRKEY_RSA:
	case COSM_TYPE_PRKEY_RSA:
		if (*(buff + offs + 2))
			SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_INVALID_CARD, "private key exists already");
		
		*(buff + offs + 2) = (file_id >> 8) & 0xFF;
		*(buff + offs + 3) = file_id & 0xFF;
		break;
	case SC_PKCS15_TYPE_CERT_X509 :
		*(buff + offs + 4) = (file_id >> 8) & 0xFF;
		*(buff + offs + 5) = file_id & 0xFF;
		break;
	default:
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_INCORRECT_PARAMETERS, "invalid object type");
	}

	if (rec > list_file->record_count)   {
		rv = sc_select_file(p15card->card, &list_file->path, NULL);
		if (rv == SC_ERROR_FILE_NOT_FOUND) 
			rv = sc_pkcs15init_create_file(profile, p15card, list_file);
		
		if (!rv)
			rv = sc_append_record(p15card->card, buff, list_file->record_length, SC_RECORD_BY_REC_NR);
	}
	else   {
		rv = sc_update_record(p15card->card, rec, buff, list_file->record_length, SC_RECORD_BY_REC_NR);
		sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "rv:%i", rv);
	}
	
	free(buff);
	
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "return after failure");

	rv = 0;
	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, rv);
}


static int 
awp_remove_container_entry (struct sc_pkcs15_card *p15card, struct sc_profile *profile,
		 int type, int file_id)
{
	struct sc_context *ctx = p15card->card->ctx;
	struct sc_file *clist=NULL, *file=NULL;
	int rv = 0, ii;
	unsigned rec, rec_len;
	unsigned char *buff=NULL, id[2];

	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "file_id %X", file_id);

	rv = awp_new_file(p15card, profile, COSM_CONTAINER_LIST, 0, &clist, NULL);
	if (rv)
		goto done;

	rv = sc_select_file(p15card->card, &clist->path, &file);
	if (rv)
		goto done;
	
	if (!(buff = malloc(file->record_length)))   {
		rv = SC_ERROR_OUT_OF_MEMORY;
		goto done;
	}
	
	id[0] = (file_id >> 8) & 0xFF;
	id[1] = file_id & 0xFF;

	for (rec = 1; rec <= file->record_count; rec++)   {
		rv = sc_read_record(p15card->card, rec, buff, file->record_length, SC_RECORD_BY_REC_NR);
		if (rv < 0)
			break;
		rec_len = rv;
		
		for (ii=0; ii<12; ii+=2)  
			if (!memcmp(id, buff+ii, 2))
				break;
		if (ii==12)
			continue;

		*(buff + ii + 0) = 0;
		*(buff + ii + 1) = 0;

		if (type == SC_PKCS15_TYPE_PRKEY_RSA || type == COSM_TYPE_PRKEY_RSA) 
			memset(buff + ii/6*6, 0, 6);
	
		if (!memcmp(buff,"\0\0\0\0\0\0\0\0\0\0\0\0",12))   {
			rv = sc_pkcs15init_authenticate(profile, p15card, file, SC_AC_OP_ERASE);
			if (rv)
				break;
			rv = sc_delete_record(p15card->card, rec);

			if (rv)
				break;

			rv =  awp_remove_container_entry(p15card, profile, type, file_id);	
			break;
		}
		else   {
			rv = sc_pkcs15init_authenticate(profile, p15card, file, SC_AC_OP_UPDATE);
			if (rv)
				break;
			rv = sc_update_record(p15card->card, rec, buff, rec_len, SC_RECORD_BY_REC_NR);
		}

		if (rv<0)
			break;
	}

	if (rv>0)
		rv = 0;
	
done:
	if (buff)		free(buff);
	if (file)		sc_file_free(file);
	if (clist)		sc_file_free(clist);

	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, rv);
}


static int 
awp_update_container(struct sc_pkcs15_card *p15card, struct sc_profile *profile, int type,	
		struct awp_lv *key_id, unsigned obj_id, unsigned int *prkey_id)
{
	struct sc_context *ctx = p15card->card->ctx;
	struct sc_file *clist = NULL, *file = NULL;
	struct sc_path  private_path;
	int rv = 0, rec, rec_offs;
	unsigned char *list = NULL;
	  
	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "update container(type:%X,obj_id:%X)", type, obj_id);

	if (prkey_id)
		*prkey_id = 0;

	/*
	 * Get path of the DF that contains private objects.
	 */
	rv = awp_new_file(p15card, profile, SC_PKCS15_TYPE_PRKEY_RSA, 1, NULL, &file);
	if (rv)
		goto done;
	private_path = file->path;
	sc_file_free(file), file=NULL;

	rv = awp_new_file(p15card, profile, COSM_CONTAINER_LIST, 0, &clist, NULL);
	if (rv)
		goto done;
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "contaner cfile(rcount:%i,rlength:%i)", clist->record_count, clist->record_length);

	rv = sc_select_file(p15card->card, &clist->path, &file);
	if (rv)
		goto done;
	file->record_length = clist->record_length;
	
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "contaner file(rcount:%i,rlength:%i)", file->record_count, file->record_length);
	if (type == SC_PKCS15_TYPE_PRKEY_RSA || type == COSM_TYPE_PRKEY_RSA)   {
		rec_offs = 0;
		sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "Append new record %i for private key", file->record_count + 1);
		rv = awp_update_container_entry(p15card, profile, file, type, obj_id, file->record_count + 1, rec_offs);
		goto done;
	}

	list = malloc(AWP_CONTAINER_RECORD_LEN * file->record_count);
	if (!list)  {
		rv = SC_ERROR_OUT_OF_MEMORY;
		goto done;
	}
	
	rv = sc_pkcs15init_authenticate(profile, p15card, file, SC_AC_OP_READ);
	if (rv)
		goto done;

	for (rec=0; rec < file->record_count; rec++)   {
		unsigned char tmp[256];

		rv = sc_read_record(p15card->card, rec + 1, tmp, sizeof(tmp), SC_RECORD_BY_REC_NR);
		if (rv >= AWP_CONTAINER_RECORD_LEN)
			memcpy(list + rec*AWP_CONTAINER_RECORD_LEN, tmp, AWP_CONTAINER_RECORD_LEN);
		else
			goto done;
	}

	for (rec=0, rv=0; !rv && rec < file->record_count; rec++)   {
		for (rec_offs=0; !rv && rec_offs<12; rec_offs+=6)   {
			int offs;
			
			sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "rec %i; rec_offs %i", rec, rec_offs);
			offs = rec*AWP_CONTAINER_RECORD_LEN + rec_offs;
			if (*(list + offs + 2))   {
				unsigned char *buff = NULL;
				int id_offs;
				struct sc_path path = private_path;
				struct sc_file *ff = NULL;
				
				sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "container contains PrKey %02X%02X", *(list + offs + 2), *(list + offs + 3));
				path.value[path.len - 2] = *(list + offs + 2) | 0x01;
				path.value[path.len - 1] = *(list + offs + 3);
				rv = sc_select_file(p15card->card, &path, &ff);
				if (rv)
					continue;
				
        			sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "file id %X; size %i", ff->id, ff->size);
				buff = malloc(ff->size);
				if (!buff)   {
					rv = SC_ERROR_OUT_OF_MEMORY;
					break;
				}

				rv = sc_pkcs15init_authenticate(profile, p15card, ff, SC_AC_OP_READ);
				if (rv)   {
					sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "sc_pkcs15init_authenticate(READ) failed");
					break;
				}

				rv = sc_read_binary(p15card->card, 0, buff, ff->size, 0);
				if (rv == ff->size)  {
					rv = 0;
					id_offs = 5 + *(buff+3);
					sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "rec %i; id offset %i",rec, id_offs);
					if (key_id->len == *(buff + id_offs) &&
						!memcmp(key_id->value, buff + id_offs + 1, key_id->len))  {
						sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "found key file friend");
						if (!rv)
							rv = awp_update_container_entry(p15card, profile, file, type, obj_id, rec + 1, rec_offs);
						
						if (rv >= 0 && prkey_id)    { 
							*prkey_id = *(list + offs + 2) * 0x100 + *(list + offs + 3);
							sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "*prkey_id 0x%X", *prkey_id);
						}
					}
				}
				
				free(buff);
				sc_file_free(ff);
			}
		}
	}
	
done:	
	if (clist)	sc_file_free(clist);
	if (file)	sc_file_free(file);
	if (list)  free(list);
	
	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, rv);
}


static int
awp_update_df_create_pin(struct sc_pkcs15_card *p15card, struct sc_profile *profile, 
		struct sc_pkcs15_object *pinobj)
{
	SC_FUNC_CALLED(p15card->card->ctx, 1);
	/* No update DF when creating PIN objects */
	SC_FUNC_RETURN(p15card->card->ctx, 1, SC_SUCCESS);
}


static int 
awp_set_certificate_info (struct sc_pkcs15_card *p15card, 
		struct sc_profile *profile,
		struct sc_file *file, 
		struct awp_cert_info *ci)
{
	struct sc_context *ctx = p15card->card->ctx;
	int r = 0, blob_size;
	unsigned char *blob;
	const char *default_cert_label = "Certificate";
	
	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);
	blob_size = 2;
	if (!(blob = malloc(blob_size)))   {
		r = SC_ERROR_OUT_OF_MEMORY;
        	goto done;
	}

	/* TODO: cert flags */	
	*blob       = (COSM_TAG_CERT >> 8) & 0xFF;
	*(blob + 1) = COSM_TAG_CERT & 0xFF;

	if (ci->label.len 
			&& ci->label.len != strlen(default_cert_label)
		       	&& memcmp(ci->label.value, default_cert_label, strlen(default_cert_label)))
		r = awp_update_blob(ctx, &blob, &blob_size, &ci->label, TLV_TYPE_LLV); 
	else
		r = awp_update_blob(ctx, &blob, &blob_size, &ci->cn, TLV_TYPE_LLV);   
	if (r)   
		goto done;

	r = awp_update_blob(ctx, &blob, &blob_size, &ci->id, TLV_TYPE_LLV); 
	if (r) 
        	goto done;
	
	r = awp_update_blob(ctx, &blob, &blob_size, &ci->subject, TLV_TYPE_LLV);
	if (r) 
        	goto done;

	if (ci->issuer.len != ci->subject.len ||
			memcmp(ci->issuer.value, ci->subject.value, ci->subject.len))   {
		r = awp_update_blob(ctx, &blob, &blob_size, &ci->issuer, TLV_TYPE_LLV); 
		if (r)
			goto done;
		r = awp_update_blob(ctx, &blob, &blob_size, &ci->serial, TLV_TYPE_LLV);
		if (r)
			goto done;
	}
	else   {
		r = awp_update_blob(ctx, &blob, &blob_size, &zero_lv, TLV_TYPE_LLV);
		if (r) 
			goto done;
		r = awp_update_blob(ctx, &blob, &blob_size, &zero_lv, TLV_TYPE_LLV);
		if (r)
			goto done;
	}
	
	file->size = blob_size;	
	r = sc_pkcs15init_create_file(profile, p15card, file);
	if (r)
		goto done;

	r = sc_pkcs15init_update_file(profile, p15card, file, blob, blob_size);
	if (r < 0)
		goto done;
	
	r = 0;
done:	
	if (blob) 	
		free(blob);
	
	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, r);
}


static int 
awp_update_object_list(struct sc_pkcs15_card *p15card, struct sc_profile *profile, 
		unsigned int type, int num)
{
	struct sc_context *ctx = p15card->card->ctx;
	struct sc_file *obj_file = NULL, *lst_file = NULL;
	struct sc_file *file = NULL;
	char obj_name[NAME_MAX_LEN], lst_name[NAME_MAX_LEN];
	unsigned char *buff = NULL;
	int rv, ii;

	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "type %i, num %i", type, num);
	switch (type)   {
	case SC_PKCS15_TYPE_CERT_X509:
		snprintf(obj_name, NAME_MAX_LEN, "template-certificate");
		snprintf(lst_name, NAME_MAX_LEN,"%s-public-list", COSM_TITLE);
		break;
	case SC_PKCS15_TYPE_PUBKEY_RSA:
	case COSM_TYPE_PUBKEY_RSA:
		snprintf(obj_name, NAME_MAX_LEN, "template-public-key");
		snprintf(lst_name, NAME_MAX_LEN,"%s-public-list", COSM_TITLE);
		break;
	case SC_PKCS15_TYPE_DATA_OBJECT:
		snprintf(obj_name, NAME_MAX_LEN, "template-data");
		snprintf(lst_name, NAME_MAX_LEN,"%s-public-list", COSM_TITLE);
		break;
	case COSM_TYPE_PRIVDATA_OBJECT:
		snprintf(obj_name, NAME_MAX_LEN, "template-privdata");
		snprintf(lst_name, NAME_MAX_LEN,"%s-private-list", COSM_TITLE);
		break;
	case SC_PKCS15_TYPE_PRKEY_RSA:
	case COSM_TYPE_PRKEY_RSA:
		snprintf(obj_name, NAME_MAX_LEN,"template-private-key");
		snprintf(lst_name, NAME_MAX_LEN,"%s-private-list", COSM_TITLE);
		break;
	default:
        sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "Not supported file type %X", type);
        return SC_ERROR_INVALID_ARGUMENTS;
	}

	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "obj_name %s; num 0x%X",obj_name, num);
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "lst_name %s",lst_name);
	if (sc_profile_get_file(profile, obj_name, &obj_file) < 0) {
		sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "No profile template '%s'", obj_name);
		rv = SC_ERROR_NOT_SUPPORTED;
		goto done;
	}
	else if (sc_profile_get_file(profile, lst_name, &lst_file) < 0)  {
		sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "No profile template '%s'", lst_name);
		rv = SC_ERROR_NOT_SUPPORTED;
		goto done;
	}

	obj_file->id |= (num & 0xFF);
	obj_file->path.value[obj_file->path.len-1] |= (num & 0xFF);

	rv = sc_select_file(p15card->card, &obj_file->path, &file);
	if (rv)
		goto done;

	if (type == SC_PKCS15_TYPE_PUBKEY_RSA || type == COSM_TYPE_PUBKEY_RSA)   {
		if (file->size==PUBKEY_512_ASN1_SIZE)
			file->size = 512;
		else if (file->size==PUBKEY_1024_ASN1_SIZE)
			file->size = 1024;
		else if (file->size==PUBKEY_2048_ASN1_SIZE)
			file->size = 2048;
	}

	buff = malloc(lst_file->size);
	if (!buff)   {
		rv = SC_ERROR_OUT_OF_MEMORY;
		goto done;
	}

	rv = sc_pkcs15init_authenticate(profile, p15card, lst_file, SC_AC_OP_READ);
	if (rv)
		goto done;
	rv = sc_pkcs15init_authenticate(profile, p15card, lst_file, SC_AC_OP_UPDATE);
	if (rv)
		goto done;
	
	rv = sc_select_file(p15card->card, &lst_file->path, NULL);
	if (rv == SC_ERROR_FILE_NOT_FOUND)
		rv = sc_pkcs15init_create_file(profile, p15card, lst_file);
	if (rv < 0)
		goto done;
	
	rv = sc_read_binary(p15card->card, 0, buff, lst_file->size, lst_file->ef_structure);
	if (rv < 0)
		goto done;
	
	for (ii=0; ii < lst_file->size; ii+=5)
		if (*(buff + ii) != COSM_LIST_TAG)
			break;
	if (ii>=lst_file->size)   {
		rv = SC_ERROR_UNKNOWN_DATA_RECEIVED;
		goto done;
	}
	
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "ii %i, rv %i; %X; %i", ii, rv, file->id, file->size);
	*(buff + ii) = COSM_LIST_TAG;
	*(buff + ii + 1) = (file->id >> 8) & 0xFF;
	*(buff + ii + 2) = file->id & 0xFF;
	*(buff + ii + 3) = (file->size >> 8) & 0xFF;
	*(buff + ii + 4) = file->size & 0xFF;
	
	rv = sc_update_binary(p15card->card, ii, buff + ii, 5, 0);
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "rv %i",rv);
	if (rv < 0)
		goto done;
		
	rv = 0;
done:
	if (buff)   
		free(buff);
	sc_file_free(lst_file);
	sc_file_free(obj_file);
	sc_file_free(file);

	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, rv);
}


static int 
awp_encode_key_info(struct sc_pkcs15_card *p15card, struct sc_pkcs15_object *obj,
		struct sc_pkcs15_pubkey_rsa *pubkey, struct awp_key_info *ki)
{
	struct sc_context *ctx = p15card->card->ctx;
	struct sc_pkcs15_prkey_info *key_info;
	int r = 0;

	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);
	ERR_load_ERR_strings();
	ERR_load_crypto_strings();

	key_info = (struct sc_pkcs15_prkey_info *)obj->data;

	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "object(%s,type:%X)", obj->label, obj->type);
	if (obj->type == SC_PKCS15_TYPE_PUBKEY_RSA || obj->type == COSM_TYPE_PUBKEY_RSA )
		ki->flags = COSM_TAG_PUBKEY_RSA;
	else if (obj->type == SC_PKCS15_TYPE_PRKEY_RSA || obj->type == COSM_TYPE_PRKEY_RSA)
		ki->flags = COSM_TAG_PRVKEY_RSA;
	else 
		return SC_ERROR_INCORRECT_PARAMETERS;
	
	if (obj->type == COSM_TYPE_PUBKEY_RSA || obj->type == COSM_TYPE_PRKEY_RSA)
		ki->flags |= COSM_GENERATED;

	if (obj->label)   {
		ki->label.value = (unsigned char *)strdup(obj->label);
		ki->label.len = strlen(obj->label);
	}
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "cosm_encode_key_info() label(%i):%s",ki->label.len, ki->label.value);

	/*
	 * Oberthur saves modulus value without tag and length.
	 */
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "pubkey->modulus.len %i",pubkey->modulus.len);
	ki->modulus.value = malloc(pubkey->modulus.len);
	if (!ki->modulus.value)   {
		r = SC_ERROR_OUT_OF_MEMORY;
		goto done;
	}
	memcpy(ki->modulus.value, pubkey->modulus.data, pubkey->modulus.len);
	ki->modulus.len = pubkey->modulus.len;

	/*
	 * Oberthur saves exponents as length and value, without tag.
	 */
	ki->exponent.value = malloc(pubkey->exponent.len);
	if (!ki->exponent.value)   {
		r = SC_ERROR_OUT_OF_MEMORY;
		goto done;
	}
	memcpy(ki->exponent.value, pubkey->exponent.data, pubkey->exponent.len);
	ki->exponent.len = pubkey->exponent.len;
	
	/*
	 * ID 
	 */
	ki->id.value = calloc(1, key_info->id.len);
	if (!ki->id.value)
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_OUT_OF_MEMORY, "AWP encode cert failed: ID allocation error");
	memcpy(ki->id.value, key_info->id.value, key_info->id.len);
	ki->id.len = key_info->id.len;

	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "cosm_encode_key_info() label:%s",ki->label.value);
done:
	ERR_load_ERR_strings();
	ERR_load_crypto_strings();
	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, r);
}


static void 
awp_free_key_info(struct awp_key_info *ki)
{
	if (ki->modulus.value)
		free(ki->modulus.value);
	if (ki->exponent.value)
		free(ki->exponent.value);
	if (ki->id.value)
		free(ki->id.value);
}


static int 
awp_set_key_info (struct sc_pkcs15_card *p15card, struct sc_profile *profile, struct sc_file *file, 
		struct awp_key_info *ki, struct awp_cert_info *ci)
{
	struct sc_context *ctx = p15card->card->ctx;
	int r = 0, blob_size;
	unsigned char *blob;

	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "file:%p, kinfo:%p, cinfo:%p", file, ki, ci);
	blob_size = 2;
	blob = malloc(blob_size);
	if (!blob)
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_OUT_OF_MEMORY, "AWP set key info failed: blob allocation error");

	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "label:%s",ki->label.value);

	*blob = (ki->flags >> 8) & 0xFF;
	*(blob + 1) = ki->flags & 0xFF;
	if (ci && ci->label.len)
		r = awp_update_blob(ctx, &blob, &blob_size, &ci->label, TLV_TYPE_LLV);
	else if (ci && !ci->label.len)
		r = awp_update_blob(ctx, &blob, &blob_size, &ci->cn, TLV_TYPE_LLV);
	else 
		r = awp_update_blob(ctx, &blob, &blob_size, &ki->label, TLV_TYPE_LLV);
	if (r)
		goto done;
	
	r = awp_update_blob(ctx, &blob, &blob_size, &ki->id, TLV_TYPE_LLV);
	if (r)
		goto done;
	
	r = awp_update_blob(ctx, &blob, &blob_size, &x30_lv, TLV_TYPE_V);
	if (r)
		goto done;
	
	if (ci)
		r = awp_update_blob(ctx, &blob, &blob_size, &(ci->subject), TLV_TYPE_LLV);
	else
		r = awp_update_blob(ctx, &blob, &blob_size, &zero_lv, TLV_TYPE_LLV);
	if (r)
		goto done;

	if ((ki->flags & ~COSM_GENERATED) != COSM_TAG_PUBKEY_RSA)   {
		r = awp_update_blob(ctx, &blob, &blob_size, &ki->modulus, TLV_TYPE_V);
		if (r)
			goto done;
	
		r = awp_update_blob(ctx, &blob, &blob_size, &ki->exponent, TLV_TYPE_LV);
		if (r)
			goto done;
	}
	
	file->size = blob_size;
	r = sc_pkcs15init_create_file(profile, p15card, file);
	if (r == SC_ERROR_FILE_ALREADY_EXISTS)   {
		r =  cosm_delete_file(p15card, profile, file);
		if (!r)
			r = sc_pkcs15init_create_file(profile, p15card, file);
	}
	
	if (r<0)
		goto done;

	r = sc_pkcs15init_update_file(profile, p15card, file, blob, blob_size);
	if (r < 0)
		goto done;
	
	r = 0;
done:	
	if (blob) 	
		free(blob);
	
	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, r);
}


static int 
awp_encode_cert_info(struct sc_pkcs15_card *p15card, struct sc_pkcs15_object *obj,
		struct awp_cert_info *ci)
{
	struct sc_context *ctx = p15card->card->ctx;
	struct sc_pkcs15_cert_info *cert_info;
	struct sc_pkcs15_pubkey_rsa pubkey;
	int r = 0;
	unsigned char *buff = NULL, *ptr;
	BIO *mem = NULL;
	X509 *x = NULL;

	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);

	ERR_load_ERR_strings();
	ERR_load_crypto_strings();
	
	if (!obj || !ci)
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_INVALID_ARGUMENTS, "AWP encode cert failed: invalid parameters");

	cert_info = (struct sc_pkcs15_cert_info *)obj->data;

	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "Encode cert(%s,id:%s,der(%p,%i))", obj->label, 
			sc_pkcs15_print_id(&cert_info->id), obj->content.value, obj->content.len);
	memset(&pubkey, 0, sizeof(pubkey));

	if (obj->label)   {
		ci->label.value = (unsigned char *)strdup(obj->label);
		ci->label.len = strlen(obj->label);
	}

	mem = BIO_new_mem_buf(obj->content.value, obj->content.len);
	if (!mem)
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_INVALID_DATA, "AWP encode cert failed: invalid data");

	x = d2i_X509_bio(mem, NULL);
	if (!x)
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_INVALID_DATA, "AWP encode cert failed: x509 parse error");

	buff = OPENSSL_malloc(i2d_X509(x,NULL) + EVP_MAX_MD_SIZE);
	if (!buff)
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_OUT_OF_MEMORY, "AWP encode cert failed: memory allocation error");
	
	/*
	 * subject commonName.
	 */
	ptr = awp_get_commonName(x);
	if (!ptr)
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_INTERNAL, "AWP encode cert failed: cannot get CommonName");
	ci->cn.value = ptr;
	ci->cn.len = strlen((char *)ptr);
	
	/*
	 * subject DN
	 */
	ptr = buff;
	r = i2d_X509_NAME(X509_get_subject_name(x),&ptr);
	if (r<=0) 
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_INTERNAL, "AWP encode cert failed: cannot get SubjectName");
	
	ci->subject.value = malloc(r);
	if (!ci->subject.value)
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_OUT_OF_MEMORY, "AWP encode cert failed: subject allocation error");
	memcpy(ci->subject.value, buff, r);
	ci->subject.len = r;
	
	/*
	 * issuer DN
	 */
	ptr = buff;
	r = i2d_X509_NAME(X509_get_issuer_name(x),&ptr);
	if (r <= 0)
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_INTERNAL, "AWP encode cert failed: cannot get IssuerName");
		
	ci->issuer.value = malloc(r);
	if (!ci->issuer.value)
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_OUT_OF_MEMORY, "AWP encode cert failed: issuer allocation error");
	memcpy(ci->issuer.value, buff, r);
	ci->issuer.len = r;

	/*
	 * ID 
	 */
	ci->id.value = calloc(1, cert_info->id.len);
	if (!ci->id.value)
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_OUT_OF_MEMORY, "AWP encode cert failed: ID allocation error");
	memcpy(ci->id.value, cert_info->id.value, cert_info->id.len);
	ci->id.len = cert_info->id.len;

	/*
	 * serial number
	 */
	do   {
		int encoded_len;
		unsigned char encoded[0x40], *encoded_ptr;

		encoded_ptr = encoded;
		encoded_len = i2c_ASN1_INTEGER(X509_get_serialNumber(x), &encoded_ptr);

		if (!(ci->serial.value = malloc(encoded_len + 3)))   {
			r = SC_ERROR_OUT_OF_MEMORY;
			goto done;
		}

		memcpy(ci->serial.value + 2, encoded, encoded_len);
		*(ci->serial.value + 0) = V_ASN1_INTEGER;
		*(ci->serial.value + 1) = encoded_len;
		ci->serial.len = encoded_len + 2;

		sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "cert. serial encoded length %i", encoded_len);
	} while (0);
	
	ci->x509 = X509_dup(x);
done:
	ERR_print_errors_fp(stderr);
	ERR_clear_error();
	ERR_free_strings();
	if (pubkey.exponent.data) free(pubkey.exponent.data);
	if (pubkey.modulus.data) free(pubkey.modulus.data);
	if (x) 		X509_free(x);
	if (mem)	BIO_free(mem);
	if (buff)	OPENSSL_free(buff);

	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, r);
}


static void 
awp_free_cert_info(struct awp_cert_info *ci)
{
	if (ci->cn.len && ci->cn.value)
		free(ci->cn.value);
	
	if (ci->id.len && ci->id.value)
		free(ci->id.value);
	
	if (ci->subject.len && ci->subject.value)
		free(ci->subject.value);
	
	if (ci->issuer.len && ci->issuer.value)
		free(ci->issuer.value);
	
	if (ci->x509)
		X509_free(ci->x509);

	memset(ci,0,sizeof(struct awp_cert_info));
}


static int 
awp_encode_data_info(struct sc_pkcs15_card *p15card, struct sc_pkcs15_object *obj,
		struct awp_data_info *di)
{
	struct sc_context *ctx = p15card->card->ctx;
	struct sc_pkcs15_data_info *data_info;
	int r = 0;
	unsigned char *buf = NULL;
	size_t buflen;

	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);

	if (!obj || !di)
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_INVALID_ARGUMENTS, "AWP encode data failed: invalid parameters");

	data_info = (struct sc_pkcs15_data_info *)obj->data;

	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "Encode data(%s,id:%s,der(%p,%i))", obj->label,
			sc_pkcs15_print_id(&data_info->id), obj->content.value, obj->content.len);

	di->flags = 0x0000; 

	if (obj->label)   {
		di->label.value = (unsigned char *)strdup(obj->label);
		di->label.len = strlen(obj->label);
	}

	di->app.len = strlen(data_info->app_label);
	if (di->app.len)   {
		di->app.value = strdup(data_info->app_label);
		if (!di->app.value) 
			SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_OUT_OF_MEMORY, 
					"AWP encode data failed: cannot allocate App.Label");
	}

	r = sc_asn1_encode_object_id(&buf, &buflen, &data_info->app_oid);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, r, "AWP encode data failed: cannot encode OID");
	
	di->oid.len = buflen + 2;
	di->oid.value = malloc(di->oid.len);
	if (!di->oid.value)   
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_OUT_OF_MEMORY, "AWP encode data failed: cannot allocate OID");
	
	*(di->oid.value + 0) = 0x06;
	*(di->oid.value + 1) = buflen;	
	memcpy(di->oid.value + 2, buf, buflen);

	free(buf);
	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, r);
}


static void 
awp_free_data_info(struct awp_data_info *di)
{
	if (di->label.len && di->label.value)
		free(di->label.value);
	
	if (di->app.len && di->app.value)
		free(di->app.value);
	
	if (di->oid.len && di->oid.value)
		free(di->oid.value);
	
	memset(di, 0, sizeof(struct awp_data_info));
}


static int 
awp_set_data_info (struct sc_pkcs15_card *p15card, struct sc_profile *profile,
		struct sc_file *file, struct awp_data_info *di)
{
	struct sc_context *ctx = p15card->card->ctx;
	int r = 0, blob_size;
	unsigned char *blob;
	
	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);
	sc_debug (ctx, SC_LOG_DEBUG_NORMAL, "Set 'DATA' info %p", di);
	blob_size = 2;
	if (!(blob = malloc(blob_size)))   {
		r = SC_ERROR_OUT_OF_MEMORY;
        	goto done;
	}
	*blob       = (di->flags >> 8) & 0xFF;
	*(blob + 1) = di->flags & 0xFF;
	
	r = awp_update_blob(ctx, &blob, &blob_size, &di->label, TLV_TYPE_LLV);
	if (r)
		goto done;
	
	r = awp_update_blob(ctx, &blob, &blob_size, &di->app, TLV_TYPE_LLV);
	if (r)
		goto done;
	
	r = awp_update_blob(ctx, &blob, &blob_size, &di->oid, TLV_TYPE_LLV);
	if (r)
		goto done;
	
	file->size = blob_size;	
	r = sc_pkcs15init_create_file(profile, p15card, file);
	if (r)
		goto done;

	r = sc_pkcs15init_update_file(profile, p15card, file, blob, blob_size);
	if (r < 0)
		goto done;
	
	r = 0;
done:	
	if (blob) 	
		free(blob);
	
	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, r);
}


static int
awp_get_lv(struct sc_context *ctx, unsigned char *buf, size_t buf_len, 
		size_t offs, int len_len, 
		struct awp_lv *out)
{
	int len = 0, ii;

	if (buf_len - offs < 2)
		return 0;

	if (len_len > 2)   {
		len = len_len;
		len_len = 0;
	}
	else   {
		for (len=0, ii=0; ii<len_len; ii++)
			len = len * 0x100 + *(buf + offs + ii);
	}

	if (len && out)   {
		if (out->value)
			free(out->value);

		out->value = malloc(len);
		if (!out->value)
			return SC_ERROR_OUT_OF_MEMORY;
		memcpy(out->value, buf + offs + len_len, len);
		out->len = len;
	}

	return len_len + len;
}


static int
awp_parse_key_info(struct sc_context *ctx, unsigned char *buf, size_t buf_len, 
		struct awp_key_info *ikey)
{
	size_t offs;
	int len;

	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);
	offs = 0;

	/* Flags */
	if (buf_len - offs < 2)
		SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, SC_SUCCESS);
	ikey->flags = *(buf + offs) * 0x100 + *(buf + offs + 1);
	offs += 2;

	/* Label */
	len = awp_get_lv(ctx, buf, buf_len, offs, 2, &ikey->label);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, len, "AWP parse key info failed: label");
	if (!len)
		SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, SC_SUCCESS);
	offs += len;

	/* Ignore Key ID */
	len = awp_get_lv(ctx, buf, buf_len, offs, 2, &ikey->id);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, len, "AWP parse key info failed: ID");
	if (!len)
		SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, SC_SUCCESS);
	offs += len;

	while (*(buf + offs) == '0')
		offs++;

	/* Subject */
	len = awp_get_lv(ctx, buf, buf_len, offs, 2, &ikey->subject);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, len, "AWP parse key info failed: subject");
	if (!len)
		SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, SC_SUCCESS);
	offs += len;

	/* Modulus */
	if (buf_len - offs > 64 && buf_len - offs < 128)
		len = awp_get_lv(ctx, buf, buf_len, offs, 64, &ikey->modulus);
	else if (buf_len - offs > 128 && buf_len - offs < 256)
		len = awp_get_lv(ctx, buf, buf_len, offs, 128, &ikey->modulus);
	else
		len = awp_get_lv(ctx, buf, buf_len, offs, 256, &ikey->modulus);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, len, "AWP parse key info failed: modulus");
	if (!len)
		SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, SC_SUCCESS);
	offs += len;

	/* Exponent */
	len = awp_get_lv(ctx, buf, buf_len, offs, 1, &ikey->exponent);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, len, "AWP parse key info failed: exponent");
	if (!len)
		SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, SC_SUCCESS);

	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, SC_SUCCESS);
}


static int 
awp_update_key_info(struct sc_pkcs15_card *p15card, struct sc_profile *profile,
		unsigned prvkey_id,  struct awp_cert_info *ci)
{
	struct sc_context *ctx = p15card->card->ctx;
	struct sc_file *key_file=NULL, *info_file=NULL, *file=NULL;
	struct awp_key_info ikey;
	int rv = 0;
	unsigned char *buf;
	size_t buf_len;
	
	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);

    	rv = awp_new_file(p15card, profile, SC_PKCS15_TYPE_PRKEY_RSA, prvkey_id & 0xFF, &info_file, &key_file);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP update key info failed: instantiation error");
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "key id %X; info id%X", key_file->id, info_file->id);

	rv = sc_pkcs15init_authenticate(profile, p15card, info_file, SC_AC_OP_READ);
	if (rv)   {
		sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "AWP update key info failed: 'READ' authentication error");
		goto done;
	}

	rv = sc_select_file(p15card->card, &info_file->path, &file);
	if (rv)   {
		sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "AWP update key info failed: cannot select info file");
		goto done;
	}

	buf = calloc(1,file->size);
	if (!buf)
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_OUT_OF_MEMORY, "AWP update key info failed: allocation error");

	rv = sc_read_binary(p15card->card, 0, buf, file->size, 0);
	if (rv < 0)    {
		sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "AWP update key info failed: read info file error");
		goto done;
	}
	buf_len = rv;

	memset(&ikey, 0, sizeof(ikey));
	rv = awp_parse_key_info(ctx, buf, buf_len, &ikey);
	if (rv < 0)   {
		sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "AWP update key info failed: parse key info error");
		goto done;
	}
	free(buf);

	rv = awp_set_key_info(p15card, profile, info_file, &ikey, ci);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP update key info failed: set key info error");
done:
	sc_file_free(file);
	sc_file_free(key_file);
	sc_file_free(info_file);
	
	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, rv);
}


static int
awp_update_df_create_cert(struct sc_pkcs15_card *p15card, struct sc_profile *profile, 
		struct sc_pkcs15_object *obj)
{
	struct sc_context *ctx = p15card->card->ctx;
	struct sc_file *info_file=NULL, *obj_file=NULL;
	struct awp_cert_info icert;
	struct sc_pkcs15_der der;
	struct sc_path path;
	unsigned prvkey_id, obj_id;
	int rv;

	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);

	der = obj->content;
	path = ((struct sc_pkcs15_cert_info *)obj->data)->path;
	obj_id = (path.value[path.len-1] & 0xFF) + (path.value[path.len-2] & 0xFF) * 0x100;

	rv = awp_new_file(p15card, profile, SC_PKCS15_TYPE_CERT_X509, obj_id & 0xFF, &info_file, &obj_file);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "COSM new file error");
		
	memset(&icert, 0, sizeof(icert));
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "Cert Der(%p,%i)", der.value, der.len);
	rv = awp_encode_cert_info(p15card, obj, &icert);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "'Create Cert' update DF failed: cannot encode info");

	rv = awp_set_certificate_info(p15card, profile, info_file, &icert);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "'Create Cert' update DF failed: cannot set info");
		
	rv = awp_update_object_list(p15card, profile, SC_PKCS15_TYPE_CERT_X509, obj_id & 0xFF);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "'Create Cert' update DF failed: cannot update list");
		
	rv = awp_update_container(p15card, profile, SC_PKCS15_TYPE_CERT_X509, &icert.id, obj_id, &prvkey_id);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "'Create Cert' update DF failed: cannot update container");

	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "PrvKeyID:%04X", prvkey_id);
	
	if (prvkey_id)  
		rv = awp_update_key_info(p15card, profile, prvkey_id, &icert);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "'Create Cert' update DF failed: cannot update key info");

	awp_free_cert_info(&icert);
	
	if (info_file)
		sc_file_free(info_file);
	if (obj_file)
		sc_file_free(obj_file);
		
	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, rv);
}


static int
awp_update_df_create_prvkey(struct sc_pkcs15_card *p15card, struct sc_profile *profile, 
		struct sc_pkcs15_object *key_obj)
{
	struct sc_context *ctx = p15card->card->ctx;
	struct sc_pkcs15_pubkey pubkey;
	struct sc_pkcs15_der der;
	struct awp_key_info ikey;
	struct awp_cert_info icert;
	struct sc_file *info_file=NULL, *obj_file=NULL;
	struct sc_pkcs15_prkey_info *key_info;
	struct sc_pkcs15_object *cert_obj = NULL, *pubkey_obj = NULL;
	struct sc_path path;
	struct awp_crypto_container cc;
	int rv;

	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);
                
	key_info = (struct sc_pkcs15_prkey_info *)key_obj->data;
	der = key_obj->content;

	memset(&cc, 0, sizeof(cc));
	path = key_info->path;
	cc.prkey_id = (path.value[path.len-1] & 0xFF) + (path.value[path.len-2] & 0xFF) * 0x100;

	rv = sc_pkcs15_find_cert_by_id(p15card, &key_info->id, &cert_obj);
	if (!rv)   {
		struct sc_pkcs15_cert_info *cert_info = (struct sc_pkcs15_cert_info *) cert_obj->data;
		struct sc_pkcs15_cert *p15cert;

		path = cert_info->path;
		cc.cert_id = (path.value[path.len-1] & 0xFF) + (path.value[path.len-2] & 0xFF) * 0x100;

		rv = sc_pkcs15_read_certificate(p15card, cert_info, &p15cert);
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'update private key' DF failed:  cannot get certificate");

		rv = sc_pkcs15_allocate_object_content(cert_obj, p15cert->data, p15cert->data_len);
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'update private key' DF failed:  cannot allocate content");

		rv = awp_encode_cert_info(p15card, cert_obj, &icert);
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'update private key' DF failed:  cannot encode cert info");

		sc_pkcs15_free_certificate(p15cert);
	}

	rv = sc_pkcs15_find_pubkey_by_id(p15card, &key_info->id, &pubkey_obj);
	if (!rv)   {
		path = ((struct sc_pkcs15_cert_info *)pubkey_obj->data)->path;
		cc.pubkey_id = (path.value[path.len-1] & 0xFF) + (path.value[path.len-2] & 0xFF) * 0x100;
	}

	rv = awp_new_file(p15card, profile, key_obj->type, cc.prkey_id & 0xFF, &info_file, &obj_file);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "New private key info file error");

	pubkey.algorithm = SC_ALGORITHM_RSA;
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "PrKey Der(%p,%i)", der.value, der.len);
	rv = sc_pkcs15_decode_pubkey(ctx, &pubkey, der.value, der.len);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'update private key' DF failed: decode public key error");

	memset(&ikey, 0, sizeof(ikey));
	rv = awp_encode_key_info(p15card, key_obj, &pubkey.u.rsa, &ikey);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'update private key' DF failed: encode info error");

	rv = awp_set_key_info(p15card, profile, info_file, &ikey, cert_obj ? &icert : NULL);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'update private key' DF failed: set info error");

	rv = awp_update_object_list(p15card, profile, key_obj->type, cc.prkey_id & 0xFF);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'update private key' DF failed: update object list error");

	rv = awp_create_container(p15card, profile, key_obj->type, &ikey.id, &cc);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'update private key' DF failed: update container error");

	if (cert_obj)
		awp_free_cert_info(&icert);

	awp_free_key_info(&ikey);
	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, rv);
}


static int
awp_update_df_create_pubkey(struct sc_pkcs15_card *p15card, struct sc_profile *profile, 
		struct sc_pkcs15_object *obj)
{
	struct sc_context *ctx = p15card->card->ctx;
	struct sc_pkcs15_pubkey pubkey;
	struct sc_pkcs15_der der;
	struct awp_key_info ikey;
	struct sc_file *info_file=NULL, *obj_file=NULL;
	struct sc_path path;
	unsigned obj_id;
	int index, rv;

	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);
                
	path = ((struct sc_pkcs15_pubkey_info *)obj->data)->path;
	der = obj->content;
	index = path.value[path.len-1] & 0xFF;
	obj_id = (path.value[path.len-1] & 0xFF) + (path.value[path.len-2] & 0xFF) * 0x100;

	rv = awp_new_file(p15card, profile, obj->type, index, &info_file, &obj_file);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "New public key info file error");
		
	pubkey.algorithm = SC_ALGORITHM_RSA;
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "PrKey Der(%p,%i)", der.value, der.len);
	rv = sc_pkcs15_decode_pubkey(ctx, &pubkey, der.value, der.len);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'update public key' DF failed: decode public key error");
	
	memset(&ikey, 0, sizeof(ikey));
	rv = awp_encode_key_info(p15card, obj, &pubkey.u.rsa, &ikey);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'update public key' DF failed: encode info error");
		
	rv = awp_set_key_info(p15card, profile, info_file, &ikey, NULL);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'update public key' DF failed: set info error");
		
	rv = awp_update_object_list(p15card, profile, obj->type, index);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'update public key' DF failed: update object list error");
		
	rv = awp_update_container(p15card, profile, obj->type, &ikey.id, obj_id, NULL);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'update public key' DF failed: update container error");

	awp_free_key_info(&ikey);
	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, rv);
}


static int
awp_update_df_create_data(struct sc_pkcs15_card *p15card, struct sc_profile *profile, 
		struct sc_pkcs15_object *obj)
{
	struct sc_context *ctx = p15card->card->ctx;
	struct sc_file *info_file=NULL, *obj_file=NULL;
	struct awp_data_info idata;
	struct sc_pkcs15_der der;
	struct sc_path path;
	unsigned obj_id, obj_type = obj->auth_id.len ? COSM_TYPE_PRIVDATA_OBJECT : SC_PKCS15_TYPE_DATA_OBJECT;
	int rv;

	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);

	der = obj->content;
	path = ((struct sc_pkcs15_data_info *)obj->data)->path;
	obj_id = (path.value[path.len-1] & 0xFF) + (path.value[path.len-2] & 0xFF) * 0x100;

	rv = awp_new_file(p15card, profile, obj_type, obj_id & 0xFF, &info_file, &obj_file);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "COSM new file error");
		
	memset(&idata, 0, sizeof(idata));
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "Data Der(%p,%i)", der.value, der.len);
	rv = awp_encode_data_info(p15card, obj, &idata);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "'Create Data' update DF failed: cannot encode info");

	rv = awp_set_data_info(p15card, profile, info_file, &idata);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "'Create Data' update DF failed: cannot set info");
		
	rv = awp_update_object_list(p15card, profile, obj_type, obj_id & 0xFF);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "'Create Data' update DF failed: cannot update list");
		
	awp_free_data_info(&idata);
	
	if (info_file)
		sc_file_free(info_file);
	if (obj_file)
		sc_file_free(obj_file);
		
	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, rv);
}


int
awp_update_df_create(struct sc_pkcs15_card *p15card, struct sc_profile *profile, 
		struct sc_pkcs15_object *object)
{
	struct sc_context *ctx = p15card->card->ctx;
	int rv;

	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);
	if (!object) 
		SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, SC_SUCCESS);

	switch (object->type)   {
	case SC_PKCS15_TYPE_AUTH_PIN:
		rv = awp_update_df_create_pin(p15card, profile, object);
		break;
	case SC_PKCS15_TYPE_CERT_X509:
		rv = awp_update_df_create_cert(p15card, profile, object);
		break;
	case SC_PKCS15_TYPE_PRKEY_RSA:
		rv = awp_update_df_create_prvkey(p15card, profile, object);
		break;
	case SC_PKCS15_TYPE_PUBKEY_RSA:
		rv = awp_update_df_create_pubkey(p15card, profile, object);
		break;
	case SC_PKCS15_TYPE_DATA_OBJECT:
		rv = awp_update_df_create_data(p15card, profile, object);
		break;
	default:
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_INVALID_ARGUMENTS, "'Create' update DF failed: unsupported object type");
	}

	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, rv);
}


static int 
awp_delete_from_container(struct sc_pkcs15_card *p15card, 
		struct sc_profile *profile, int type, int file_id)
{
	struct sc_context *ctx = p15card->card->ctx;
	struct sc_file *clist=NULL, *file=NULL;
	unsigned rec, rec_len;
	int rv = 0, ii;
	unsigned char *buff=NULL;

	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "update container entry (type:%X,file-id:%X)", type, file_id);

	rv = awp_new_file(p15card, profile, COSM_CONTAINER_LIST, 0, &clist, NULL);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP update contaner entry: cannot get allocate AWP file");

	rv = sc_select_file(p15card->card, &clist->path, &file);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP update contaner entry: cannot select container list file");

	buff = malloc(file->record_length);
	if (!buff)
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_OUT_OF_MEMORY, "AWP update container entry: allocation error");
	
	for (rec = 1; rec <= file->record_count; rec++)   {
		rv = sc_read_record(p15card->card, rec, buff, file->record_length, SC_RECORD_BY_REC_NR);
		if (rv < 0)   {
			sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "AWP update contaner entry: read record error %i", rv);
			break;
		}
		rec_len = rv;
		
		for (ii=0; ii<12; ii+=2)  
			if (file_id == (*(buff+ii) * 0x100 + *(buff+ii+1)))
				break;
		if (ii==12)
			continue;

		if (type == SC_PKCS15_TYPE_PRKEY_RSA || type == COSM_TYPE_PRKEY_RSA) 
			memset(buff + ii/6*6, 0, 6);
		else
			memset(buff + ii, 0, 2);
	
		if (!memcmp(buff,"\0\0\0\0\0\0\0\0\0\0\0\0",12))   {
			rv = sc_pkcs15init_authenticate(profile, p15card, file, SC_AC_OP_ERASE);
			if (rv < 0)   {
				sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "AWP update contaner entry: 'erase' authentication error %i", rv);
				break;
			}

			rv = sc_delete_record(p15card->card, rec);
			if (rv < 0)   {
				sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "AWP update contaner entry: delete record error %i", rv);
				break;
			}
		}
		else   {
			rv = sc_pkcs15init_authenticate(profile, p15card, file, SC_AC_OP_UPDATE);
			if (rv < 0)   {
				sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "AWP update contaner entry: 'update' authentication error %i", rv);
				break;
			}

			rv = sc_update_record(p15card->card, rec, buff, rec_len, SC_RECORD_BY_REC_NR);
			if (rv < 0)   {
				sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "AWP update contaner entry: update record error %i", rv);
				break;
			}
		}
	}

	if (rv > 0)
		rv = 0;

	if (buff)		free(buff);
	if (file)		sc_file_free(file);
	if (clist)		sc_file_free(clist);

	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, rv);
}


static int 
awp_remove_from_object_list( struct sc_pkcs15_card *p15card, struct sc_profile *profile,
		int type, unsigned int obj_id)
{
	struct sc_context *ctx = p15card->card->ctx;
	struct sc_file *lst_file=NULL, *lst=NULL;
	int rv = 0, ii;
	char lst_name[NAME_MAX_LEN];
	unsigned char *buff=NULL;
	unsigned char id[2];
	
	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "type %X; obj_id %X",type, obj_id);

	switch (type)   {
	case SC_PKCS15_TYPE_PRKEY_RSA:
	case COSM_TYPE_PRKEY_RSA:
		snprintf(lst_name, NAME_MAX_LEN,"%s-private-list", COSM_TITLE);
		break;
	case SC_PKCS15_TYPE_PUBKEY_RSA:
	case SC_PKCS15_TYPE_CERT_X509:
	case SC_PKCS15_TYPE_DATA_OBJECT:
	case COSM_TYPE_PUBKEY_RSA:
		snprintf(lst_name, NAME_MAX_LEN,"%s-public-list", COSM_TITLE);
		break;
	default:
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_INCORRECT_PARAMETERS, "AWP update object list: invalid type");
	} 

	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "AWP update object list: select '%s' file", lst_name);
	rv = sc_profile_get_file(profile, lst_name, &lst_file);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP update object list: cannot instantiate list file");

	rv = sc_select_file(p15card->card, &lst_file->path, &lst);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP update object list: cannot select list file");

	rv = sc_pkcs15init_authenticate(profile, p15card, lst, SC_AC_OP_READ);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP update object list: 'read' authentication failed");

	buff = malloc(lst->size);
	if (!buff)
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_OUT_OF_MEMORY, "AWP update object list: allocation error");

	rv = sc_read_binary(p15card->card, 0, buff, lst->size, 0);
	if (rv != lst->size)
		goto done;

	id[0] = (obj_id >> 8) & 0xFF;
	id[1] = obj_id & 0xFF;
	for (ii=0; ii<lst->size; ii+=5)   {
		if (*(buff+ii)==0xFF && *(buff+ii+1)==id[0] && *(buff+ii+2)==id[1])   {
			rv = sc_pkcs15init_authenticate(profile, p15card, lst, SC_AC_OP_UPDATE);
			if (rv)
				goto done;

			rv = sc_update_binary(p15card->card, ii, (unsigned char *)"\0", 1, 0);
			if (rv && rv!=1)
				rv = SC_ERROR_INVALID_CARD;
			break;
		}
	}
	
	if (rv > 0)
		rv = 0;
done:
	if (buff)
		free(buff);
	if (lst)
		sc_file_free(lst);
	if (lst_file)
		sc_file_free(lst_file);

	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, rv);
}


static int
awp_update_df_delete_cert(struct sc_pkcs15_card *p15card, struct sc_profile *profile, 
		struct sc_pkcs15_object *obj)
{
	struct sc_context *ctx = p15card->card->ctx;
	struct sc_file *info_file = NULL;
	struct sc_path path;
	int rv = SC_ERROR_NOT_SUPPORTED; 
	unsigned file_id;

	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);
	
	path = ((struct sc_pkcs15_cert_info *) obj->data)->path;
	file_id = path.value[path.len-2] * 0x100 + path.value[path.len-1];
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "file-id:%X", file_id);

	rv = awp_new_file(p15card, profile, obj->type, file_id & 0xFF, &info_file, NULL);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'delete cert' update DF failed: cannt get allocate new AWP file");
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "info file-id:%X", info_file->id);
	
	rv = cosm_delete_file(p15card, profile, info_file);
	if (rv != SC_ERROR_FILE_NOT_FOUND)
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'delete cert' update DF failed: delete info file error");

	rv = awp_delete_from_container(p15card, profile, obj->type, file_id);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'delete cert' update DF failed: cannot update container");
		
	rv = awp_remove_from_object_list(p15card, profile, obj->type, file_id);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'delete cert' update DF failed: cannot remove object");
		
	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, rv);
}


static int
awp_update_df_delete_prvkey(struct sc_pkcs15_card *p15card, struct sc_profile *profile, 
		struct sc_pkcs15_object *obj)
{
	struct sc_context *ctx = p15card->card->ctx;
	struct sc_file *info_file = NULL;
	struct sc_path path;
	int rv = SC_ERROR_NOT_SUPPORTED; 
	unsigned file_id;

	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);
	
	path = ((struct sc_pkcs15_prkey_info *) obj->data)->path;
	file_id = path.value[path.len-2] * 0x100 + path.value[path.len-1];
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "file-id:%X", file_id);

	rv = awp_new_file(p15card, profile, obj->type, file_id & 0xFF, &info_file, NULL);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'delete prkey' update DF failed: cannt get allocate new AWP file");
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "info file-id:%X", info_file->id);
	
	rv = cosm_delete_file(p15card, profile, info_file);
	if (rv != SC_ERROR_FILE_NOT_FOUND)
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'delete prkey' update DF failed: delete info file error");

	rv = awp_delete_from_container(p15card, profile, obj->type, file_id);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'delete prkey' update DF failed: cannot update container");
		
	rv = awp_remove_from_object_list(p15card, profile, obj->type, file_id);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'delete prkey' update DF failed: cannot remove object");

	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, rv);
}


static int
awp_update_df_delete_pubkey(struct sc_pkcs15_card *p15card, struct sc_profile *profile, 
		struct sc_pkcs15_object *obj)
{
	struct sc_context *ctx = p15card->card->ctx;
	struct sc_file *info_file = NULL;
	struct sc_path path;
	int rv = SC_ERROR_NOT_SUPPORTED; 
	unsigned file_id;

	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);
	
	path = ((struct sc_pkcs15_pubkey_info *) obj->data)->path;
	file_id = path.value[path.len-2] * 0x100 + path.value[path.len-1];
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "file-id:%X", file_id);

	rv = awp_new_file(p15card, profile, obj->type, file_id & 0xFF, &info_file, NULL);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'delete pubkey' update DF failed: cannt get allocate new AWP file");
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "info file-id:%X", info_file->id);
	
	rv = cosm_delete_file(p15card, profile, info_file);
	if (rv != SC_ERROR_FILE_NOT_FOUND)
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'delete pubkey' update DF failed: delete info file error");

	rv = awp_delete_from_container(p15card, profile, obj->type, file_id);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'delete pubkey' update DF failed: cannot update container");
		
	rv = awp_remove_from_object_list(p15card, profile, obj->type, file_id);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'delete pubkey' update DF failed: cannot remove object");

	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, rv);
}


static int
awp_update_df_delete_data(struct sc_pkcs15_card *p15card, struct sc_profile *profile, 
		struct sc_pkcs15_object *obj)
{
	struct sc_context *ctx = p15card->card->ctx;
	struct sc_file *info_file = NULL;
	struct sc_path path;
	int rv = SC_ERROR_NOT_SUPPORTED; 
	unsigned file_id;

	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);
	
	path = ((struct sc_pkcs15_data_info *) obj->data)->path;
	file_id = path.value[path.len-2] * 0x100 + path.value[path.len-1];
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "file-id:%X", file_id);

	rv = awp_new_file(p15card, profile, obj->type, file_id & 0xFF, &info_file, NULL);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'delete DATA' update DF failed: cannt get allocate new AWP file");
	sc_debug(ctx, SC_LOG_DEBUG_NORMAL, "info file-id:%X", info_file->id);
	
	rv = cosm_delete_file(p15card, profile, info_file);
	if (rv != SC_ERROR_FILE_NOT_FOUND)
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'delete DATA' update DF failed: delete info file error");

	rv = awp_remove_from_object_list(p15card, profile, obj->type, file_id);
	SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, rv, "AWP 'delete DATA' update DF failed: cannot remove object");

	SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, rv);
}


int
awp_update_df_delete(struct sc_pkcs15_card *p15card, struct sc_profile *profile, 
		struct sc_pkcs15_object *object)
{
	struct sc_context *ctx = p15card->card->ctx;
	int rv;

	SC_FUNC_CALLED(ctx, SC_LOG_DEBUG_NORMAL);
	if (!object) 
		SC_FUNC_RETURN(ctx, SC_LOG_DEBUG_NORMAL, SC_SUCCESS);

	switch (object->type)   {
	case SC_PKCS15_TYPE_CERT_X509:
		rv = awp_update_df_delete_cert(p15card, profile, object);
		break;
	case SC_PKCS15_TYPE_PRKEY_RSA:
		rv = awp_update_df_delete_prvkey(p15card, profile, object);
		break;
	case SC_PKCS15_TYPE_PUBKEY_RSA:
		rv = awp_update_df_delete_pubkey(p15card, profile, object);
		break;
	case SC_PKCS15_TYPE_DATA_OBJECT:
		rv = awp_update_df_delete_data(p15card, profile, object);
		break;
	default:
		SC_TEST_RET(ctx, SC_LOG_DEBUG_NORMAL, SC_ERROR_INVALID_ARGUMENTS, "'Create' update DF failed: unsupported object type");
	}

	SC_FUNC_RETURN(ctx, 1, rv);
}

#endif /* #ifdef ENABLE_OPENSSL */
