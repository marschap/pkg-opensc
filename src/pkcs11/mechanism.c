/*
 * Generic handling of PKCS11 mechanisms
 *
 * Copyright (C) 2002 Olaf Kirch <okir@lst.de>
 */

#include <stdlib.h>
#include <string.h>
#include "sc-pkcs11.h"

/* Also used for verification data */
struct hash_signature_info {
	CK_MECHANISM_TYPE	mech;
	CK_MECHANISM_TYPE	hash_mech;
	CK_MECHANISM_TYPE	sign_mech;
	sc_pkcs11_mechanism_type_t *hash_type;
	sc_pkcs11_mechanism_type_t *sign_type;
};

/* Also used for verification and decryption data */
struct signature_data {
	struct sc_pkcs11_object *key;
	struct hash_signature_info *info;
	sc_pkcs11_operation_t *	md;
	CK_BYTE			buffer[4096/8];
	unsigned int		buffer_len;
};

/*
 * Register a mechanism
 */
CK_RV
sc_pkcs11_register_mechanism(struct sc_pkcs11_card *p11card,
				sc_pkcs11_mechanism_type_t *mt)
{
	sc_pkcs11_mechanism_type_t **p;

	if (mt == NULL)
		return CKR_HOST_MEMORY;

	p = (sc_pkcs11_mechanism_type_t **) realloc(p11card->mechanisms,
			(p11card->nmechanisms + 2) * sizeof(*p));
	if (p == NULL)
		return CKR_HOST_MEMORY;
	p11card->mechanisms = p;
	p[p11card->nmechanisms++] = mt;
	p[p11card->nmechanisms] = NULL;
	return CKR_OK;
}

/*
 * Look up a mechanism
 */
sc_pkcs11_mechanism_type_t *
sc_pkcs11_find_mechanism(struct sc_pkcs11_card *p11card, CK_MECHANISM_TYPE mech, int flags)
{
	sc_pkcs11_mechanism_type_t *mt;
	unsigned int n;

	for (n = 0; n < p11card->nmechanisms; n++) {
		mt = p11card->mechanisms[n];
		if (mt && mt->mech == mech && ((mt->mech_info.flags & flags) == flags))
			return mt;
	}
	return NULL;
}

/*
 * Query mechanisms.
 * All of this is greatly simplified by having the framework
 * register all supported mechanisms at initialization
 * time.
 */
CK_RV
sc_pkcs11_get_mechanism_list(struct sc_pkcs11_card *p11card,
				CK_MECHANISM_TYPE_PTR pList,
				CK_ULONG_PTR pulCount)
{
	sc_pkcs11_mechanism_type_t *mt;
	unsigned int n, count = 0;
	int rv;

	for (n = 0; n < p11card->nmechanisms; n++) {
		if (!(mt = p11card->mechanisms[n]))
			continue;
		if (count < *pulCount && pList)
			pList[count] = mt->mech;
		count++;
	}

	rv = CKR_OK;
	if (pList && count > *pulCount)
		rv = CKR_BUFFER_TOO_SMALL;
	*pulCount = count;
	return rv;
}

CK_RV
sc_pkcs11_get_mechanism_info(struct sc_pkcs11_card *p11card,
			CK_MECHANISM_TYPE mechanism,
			CK_MECHANISM_INFO_PTR pInfo)
{
	sc_pkcs11_mechanism_type_t *mt;

	if (!(mt = sc_pkcs11_find_mechanism(p11card, mechanism, 0)))
		return CKR_MECHANISM_INVALID;
	memcpy(pInfo, &mt->mech_info, sizeof(*pInfo));
	return CKR_OK;
}

/*
 * Create/destroy operation handle
 */
sc_pkcs11_operation_t *
sc_pkcs11_new_operation(sc_pkcs11_session_t *session,
			sc_pkcs11_mechanism_type_t *type)
{
	sc_pkcs11_operation_t *res;

	res = (sc_pkcs11_operation_t *) calloc(1, type->obj_size);
	if (res) {
		res->session = session;
		res->type = type;
	}
	return res;
}

void
sc_pkcs11_release_operation(sc_pkcs11_operation_t **ptr)
{
	sc_pkcs11_operation_t *operation = *ptr;

	if (!operation)
		return;
	if (operation->type && operation->type->release)
		operation->type->release(operation);
	memset(operation, 0, sizeof(*operation));
	free(operation);
	*ptr = NULL;
}

CK_RV
sc_pkcs11_md_init(struct sc_pkcs11_session *session,
			CK_MECHANISM_PTR pMechanism)
{
	struct sc_pkcs11_card *p11card;
	sc_pkcs11_operation_t *operation;
	sc_pkcs11_mechanism_type_t *mt;
	int rv;

	if (!session || !session->slot
	 || !(p11card = session->slot->card))
		return CKR_ARGUMENTS_BAD;

	/* See if we support this mechanism type */
	mt = sc_pkcs11_find_mechanism(p11card, pMechanism->mechanism, CKF_DIGEST);
	if (mt == NULL)
		return CKR_MECHANISM_INVALID;

	rv = session_start_operation(session, SC_PKCS11_OPERATION_DIGEST, mt, &operation);
	if (rv != CKR_OK)
		return rv;

	memcpy(&operation->mechanism, pMechanism, sizeof(CK_MECHANISM));

	rv = mt->md_init(operation);

	if (rv != CKR_OK)
		session_stop_operation(session, SC_PKCS11_OPERATION_DIGEST);

	return rv;
}

CK_RV
sc_pkcs11_md_update(struct sc_pkcs11_session *session,
			CK_BYTE_PTR pData, CK_ULONG ulDataLen)
{
	sc_pkcs11_operation_t *op;
	int rv;

	rv = session_get_operation(session, SC_PKCS11_OPERATION_DIGEST, &op);
	if (rv != CKR_OK)
		goto done;

	rv = op->type->md_update(op, pData, ulDataLen);

done:
	if (rv != CKR_OK)
		session_stop_operation(session, SC_PKCS11_OPERATION_DIGEST);

	return rv;
}

CK_RV
sc_pkcs11_md_final(struct sc_pkcs11_session *session,
			CK_BYTE_PTR pData, CK_ULONG_PTR pulDataLen)
{
	sc_pkcs11_operation_t *op;
	int rv;

	rv = session_get_operation(session, SC_PKCS11_OPERATION_DIGEST, &op);
	if (rv != CKR_OK)
		return rv;

	/* This is a request for the digest length */
	if (pData == NULL)
		*pulDataLen = 0;

	rv = op->type->md_final(op, pData, pulDataLen);

	if (rv == CKR_BUFFER_TOO_SMALL)
		return pData == NULL ? CKR_OK : rv;

	session_stop_operation(session, SC_PKCS11_OPERATION_DIGEST);
	return rv;
}

/*
 * Initialize a signing context. When we get here, we know
 * the key object is capable of signing _something_
 */
CK_RV
sc_pkcs11_sign_init(struct sc_pkcs11_session *session,
		    CK_MECHANISM_PTR pMechanism,
		    struct sc_pkcs11_object *key,
		    CK_MECHANISM_TYPE key_type)
{
	struct sc_pkcs11_card *p11card;
	sc_pkcs11_operation_t *operation;
	sc_pkcs11_mechanism_type_t *mt;
	int rv;

	if (!session || !session->slot
	 || !(p11card = session->slot->card))
		return CKR_ARGUMENTS_BAD;

	/* See if we support this mechanism type */
	mt = sc_pkcs11_find_mechanism(p11card, pMechanism->mechanism, CKF_SIGN);
	if (mt == NULL)
		return CKR_MECHANISM_INVALID;

	/* See if compatible with key type */
	if (mt->key_type != key_type)
		return CKR_KEY_TYPE_INCONSISTENT;

	rv = session_start_operation(session, SC_PKCS11_OPERATION_SIGN, mt, &operation);
	if (rv != CKR_OK)
		return rv;

	memcpy(&operation->mechanism, pMechanism, sizeof(CK_MECHANISM));
	rv = mt->sign_init(operation, key);

	if (rv != CKR_OK)
		session_stop_operation(session, SC_PKCS11_OPERATION_SIGN);

	return rv;
}

CK_RV
sc_pkcs11_sign_update(struct sc_pkcs11_session *session,
		      CK_BYTE_PTR pData, CK_ULONG ulDataLen)
{
	sc_pkcs11_operation_t *op;
	int rv;

	rv = session_get_operation(session, SC_PKCS11_OPERATION_SIGN, &op);
	if (rv != CKR_OK)
		return rv;

	if (op->type->sign_update == NULL) {
		rv = CKR_KEY_TYPE_INCONSISTENT;
		goto done;
	}

	rv = op->type->sign_update(op, pData, ulDataLen);

done:
	if (rv != CKR_OK)
		session_stop_operation(session, SC_PKCS11_OPERATION_SIGN);

	return rv;
}

CK_RV
sc_pkcs11_sign_final(struct sc_pkcs11_session *session,
		     CK_BYTE_PTR pSignature, CK_ULONG_PTR pulSignatureLen)
{
	sc_pkcs11_operation_t *op;
	int rv;

	rv = session_get_operation(session, SC_PKCS11_OPERATION_SIGN, &op);
	if (rv != CKR_OK)
		return rv;

	/* Bail out for signature mechanisms that don't do hashing */
	if (op->type->sign_final == NULL) {
		rv = CKR_KEY_TYPE_INCONSISTENT;
		goto done;
	}

	rv = op->type->sign_final(op, pSignature, pulSignatureLen);

done:
	if (rv != CKR_BUFFER_TOO_SMALL && pSignature != NULL)
		session_stop_operation(session, SC_PKCS11_OPERATION_SIGN);

	return rv;
}

CK_RV
sc_pkcs11_sign_size(struct sc_pkcs11_session *session, CK_ULONG_PTR pLength)
{
	sc_pkcs11_operation_t *op;
	int rv;

	rv = session_get_operation(session, SC_PKCS11_OPERATION_SIGN, &op);
	if (rv != CKR_OK)
		return rv;

	/* Bail out for signature mechanisms that don't do hashing */
	if (op->type->sign_size == NULL) {
		rv = CKR_KEY_TYPE_INCONSISTENT;
		goto done;
	}

	rv = op->type->sign_size(op, pLength);

done:
	if (rv != CKR_OK)
		session_stop_operation(session, SC_PKCS11_OPERATION_SIGN);

	return rv;
}

/*
 * Initialize a signature operation
 */
static CK_RV
sc_pkcs11_signature_init(sc_pkcs11_operation_t *operation,
		    struct sc_pkcs11_object *key)
{
	struct hash_signature_info *info;
	struct signature_data *data;
	int rv;

	if (!(data = (struct signature_data *) calloc(1, sizeof(*data))))
		return CKR_HOST_MEMORY;

	data->info = NULL;
	data->key = key;

	/* If this is a signature with hash operation, set up the
	 * hash operation */
	info = (struct hash_signature_info *) operation->type->mech_data;
	if (info != NULL) {
		/* Initialize hash operation */
		data->md = sc_pkcs11_new_operation(operation->session,
						   info->hash_type);
		if (data->md == NULL)
			rv = CKR_HOST_MEMORY;
		else
			rv = info->hash_type->md_init(data->md);
		if (rv != CKR_OK) {
			sc_pkcs11_release_operation(&data->md);
			free(data);
			return rv;
		}
		data->info = info;
	}

	operation->priv_data = data;
	return CKR_OK;
}

static CK_RV
sc_pkcs11_signature_update(sc_pkcs11_operation_t *operation,
		    CK_BYTE_PTR pPart, CK_ULONG ulPartLen)
{
	struct signature_data *data;

	data = (struct signature_data *) operation->priv_data;
	if (data->md) {
		sc_pkcs11_operation_t	*md = data->md;

		return md->type->md_update(md, pPart, ulPartLen);
	}

	/* This signature mechanism operates on the raw data */
	if (data->buffer_len + ulPartLen > sizeof(data->buffer))
		return CKR_DATA_LEN_RANGE;
	memcpy(data->buffer + data->buffer_len, pPart, ulPartLen);
	data->buffer_len += ulPartLen;
	return CKR_OK;
}

static CK_RV
sc_pkcs11_signature_final(sc_pkcs11_operation_t *operation,
			CK_BYTE_PTR pSignature, CK_ULONG_PTR pulSignatureLen)
{
	struct signature_data *data;
	struct sc_pkcs11_object *key;
	int rv;

	data = (struct signature_data *) operation->priv_data;

	if (data->md) {
		sc_pkcs11_operation_t	*md = data->md;
		CK_ULONG len = sizeof(data->buffer);

		rv = md->type->md_final(md, data->buffer, &len);
		if (rv == CKR_BUFFER_TOO_SMALL)
			rv = CKR_FUNCTION_FAILED;
		if (rv != CKR_OK)
			return rv;
		data->buffer_len = len;
	}

	key = data->key;
	return key->ops->sign(operation->session,
				key, &operation->mechanism,
				data->buffer, data->buffer_len,
				pSignature, pulSignatureLen);
}

static CK_RV
sc_pkcs11_signature_size(sc_pkcs11_operation_t *operation, CK_ULONG_PTR pLength)
{
	struct sc_pkcs11_object *key;
	CK_ATTRIBUTE attr = { CKA_MODULUS_BITS, pLength, sizeof(*pLength) };
	CK_RV rv;

	key = ((struct signature_data *) operation->priv_data)->key;
	rv = key->ops->get_attribute(operation->session, key, &attr);

	/* convert bits to bytes */
	if (rv == CKR_OK)
		*pLength = (*pLength + 7) / 8;

	return rv;
}

static void
sc_pkcs11_signature_release(sc_pkcs11_operation_t *operation)
{
	struct signature_data *data;

	data = (struct signature_data *) operation->priv_data;
	sc_pkcs11_release_operation(&data->md);
	memset(data, 0, sizeof(*data));
	free(data);
}

#ifdef HAVE_OPENSSL
/*
 * Initialize a verify context. When we get here, we know
 * the key object is capable of verifying _something_
 */
CK_RV
sc_pkcs11_verif_init(struct sc_pkcs11_session *session,
		    CK_MECHANISM_PTR pMechanism,
		    struct sc_pkcs11_object *key,
		    CK_MECHANISM_TYPE key_type)
{
	struct sc_pkcs11_card *p11card;
	sc_pkcs11_operation_t *operation;
	sc_pkcs11_mechanism_type_t *mt;
	int rv;

	if (!session || !session->slot
	 || !(p11card = session->slot->card))
		return CKR_ARGUMENTS_BAD;

	/* See if we support this mechanism type */
	mt = sc_pkcs11_find_mechanism(p11card, pMechanism->mechanism, CKF_VERIFY);
	if (mt == NULL)
		return CKR_MECHANISM_INVALID;

	/* See if compatible with key type */
	if (mt->key_type != key_type)
		return CKR_KEY_TYPE_INCONSISTENT;

	rv = session_start_operation(session, SC_PKCS11_OPERATION_VERIFY, mt, &operation);
	if (rv != CKR_OK)
		return rv;

	memcpy(&operation->mechanism, pMechanism, sizeof(CK_MECHANISM));
	rv = mt->verif_init(operation, key);

	if (rv != CKR_OK)
		session_stop_operation(session, SC_PKCS11_OPERATION_VERIFY);

	return rv;

}

CK_RV
sc_pkcs11_verif_update(struct sc_pkcs11_session *session,
		      CK_BYTE_PTR pData, CK_ULONG ulDataLen)
{
	sc_pkcs11_operation_t *op;
	int rv;

	rv = session_get_operation(session, SC_PKCS11_OPERATION_VERIFY, &op);
	if (rv != CKR_OK)
		return rv;

	if (op->type->verif_update == NULL) {
		rv = CKR_KEY_TYPE_INCONSISTENT;
		goto done;
	}

	rv = op->type->verif_update(op, pData, ulDataLen);

done:
	if (rv != CKR_OK)
		session_stop_operation(session, SC_PKCS11_OPERATION_VERIFY);

	return rv;
}

CK_RV
sc_pkcs11_verif_final(struct sc_pkcs11_session *session,
		     CK_BYTE_PTR pSignature, CK_ULONG ulSignatureLen)
{
	sc_pkcs11_operation_t *op;
	int rv;

	rv = session_get_operation(session, SC_PKCS11_OPERATION_VERIFY, &op);
	if (rv != CKR_OK)
		return rv;

	if (op->type->verif_final == NULL) {
		rv = CKR_KEY_TYPE_INCONSISTENT;
		goto done;
	}

	rv = op->type->verif_final(op, pSignature, ulSignatureLen);

done:
	session_stop_operation(session, SC_PKCS11_OPERATION_VERIFY);
	return rv;
}

/*
 * Initialize a signature operation
 */
static CK_RV
sc_pkcs11_verify_init(sc_pkcs11_operation_t *operation,
		    struct sc_pkcs11_object *key)
{
	struct hash_signature_info *info;
	struct signature_data *data;
	int rv;

	if (!(data = (struct signature_data *) calloc(1, sizeof(*data))))
		return CKR_HOST_MEMORY;

	data->info = NULL;
	data->key = key;

	/* If this is a verify with hash operation, set up the
	 * hash operation */
	info = (struct hash_signature_info *) operation->type->mech_data;
	if (info != NULL) {
		/* Initialize hash operation */
		data->md = sc_pkcs11_new_operation(operation->session,
						   info->hash_type);
		if (data->md == NULL)
			rv = CKR_HOST_MEMORY;
		else
			rv = info->hash_type->md_init(data->md);
		if (rv != CKR_OK) {
			sc_pkcs11_release_operation(&data->md);
			free(data);
			return rv;
		}
		data->info = info;
	}

	operation->priv_data = data;
	return CKR_OK;
}

static CK_RV
sc_pkcs11_verify_update(sc_pkcs11_operation_t *operation,
		    CK_BYTE_PTR pPart, CK_ULONG ulPartLen)
{
	struct signature_data *data;

	data = (struct signature_data *) operation->priv_data;
	if (data->md) {
		sc_pkcs11_operation_t	*md = data->md;

		return md->type->md_update(md, pPart, ulPartLen);
	}

	/* This verification mechanism operates on the raw data */
	if (data->buffer_len + ulPartLen > sizeof(data->buffer))
		return CKR_DATA_LEN_RANGE;
	memcpy(data->buffer + data->buffer_len, pPart, ulPartLen);
	data->buffer_len += ulPartLen;
	return CKR_OK;
}

static CK_RV
sc_pkcs11_verify_final(sc_pkcs11_operation_t *operation,
			CK_BYTE_PTR pSignature, CK_ULONG ulSignatureLen)
{
	struct signature_data *data;
	struct sc_pkcs11_object *key;
	unsigned char *pubkey_value;
	CK_ATTRIBUTE attr = {CKA_VALUE, NULL, 0};
	int rv;

	data = (struct signature_data *) operation->priv_data;

	if (pSignature == NULL)
		return CKR_ARGUMENTS_BAD;

	key = data->key;
	rv = key->ops->get_attribute(operation->session, key, &attr);
	if (rv != CKR_OK)
		return rv;
	pubkey_value = (unsigned char *) malloc(attr.ulValueLen);
	attr.pValue = pubkey_value;
	rv = key->ops->get_attribute(operation->session, key, &attr);
	if (rv != CKR_OK)
		goto done;

	rv = sc_pkcs11_verify_data(pubkey_value, attr.ulValueLen,
		operation->mechanism.mechanism, data->md,
		data->buffer, data->buffer_len, pSignature, ulSignatureLen);

done:
	free(pubkey_value);

	return rv;
}
#endif

/*
 * Initialize a decryption context. When we get here, we know
 * the key object is capable of decrypting _something_
 */
CK_RV
sc_pkcs11_decr_init(struct sc_pkcs11_session *session,
			CK_MECHANISM_PTR pMechanism,
			struct sc_pkcs11_object *key,
			CK_MECHANISM_TYPE key_type)
{
	struct sc_pkcs11_card *p11card;
	sc_pkcs11_operation_t *operation;
	sc_pkcs11_mechanism_type_t *mt;
	CK_RV rv;

	if (!session || !session->slot
	 || !(p11card = session->slot->card))
		return CKR_ARGUMENTS_BAD;

	/* See if we support this mechanism type */
	mt = sc_pkcs11_find_mechanism(p11card, pMechanism->mechanism, CKF_DECRYPT);
	if (mt == NULL)
		return CKR_MECHANISM_INVALID;

	/* See if compatible with key type */
	if (mt->key_type != key_type)
		return CKR_KEY_TYPE_INCONSISTENT;

	rv = session_start_operation(session, SC_PKCS11_OPERATION_DECRYPT, mt, &operation);
	if (rv != CKR_OK)
		return rv;

	memcpy(&operation->mechanism, pMechanism, sizeof(CK_MECHANISM));
	rv = mt->decrypt_init(operation, key);

	if (rv != CKR_OK)
		session_stop_operation(session, SC_PKCS11_OPERATION_DECRYPT);

	return rv;
}

CK_RV
sc_pkcs11_decr(struct sc_pkcs11_session *session,
		CK_BYTE_PTR pEncryptedData, CK_ULONG ulEncryptedDataLen,
		CK_BYTE_PTR pData, CK_ULONG_PTR pulDataLen)
{
	sc_pkcs11_operation_t *op;
	int rv;

	rv = session_get_operation(session, SC_PKCS11_OPERATION_DECRYPT, &op);
	if (rv != CKR_OK)
		return rv;

	rv = op->type->decrypt(op, pEncryptedData, ulEncryptedDataLen,
	                       pData, pulDataLen);

	if (rv != CKR_BUFFER_TOO_SMALL && pData != NULL)
		session_stop_operation(session, SC_PKCS11_OPERATION_DECRYPT);

	return rv;
}

/*
 * Initialize a signature operation
 */
static CK_RV
sc_pkcs11_decrypt_init(sc_pkcs11_operation_t *operation,
			struct sc_pkcs11_object *key)
{
	struct signature_data *data;

	if (!(data = (struct signature_data *) calloc(1, sizeof(*data))))
		return CKR_HOST_MEMORY;

	data->key = key;

	operation->priv_data = data;
	return CKR_OK;
}

static CK_RV
sc_pkcs11_decrypt(sc_pkcs11_operation_t *operation,
		CK_BYTE_PTR pEncryptedData, CK_ULONG ulEncryptedDataLen,
		CK_BYTE_PTR pData, CK_ULONG_PTR pulDataLen)
{
	struct signature_data *data;
	struct sc_pkcs11_object *key;

	data = (struct signature_data*) operation->priv_data;

	key = data->key;
	return key->ops->decrypt(operation->session,
				key, &operation->mechanism,
				pEncryptedData, ulEncryptedDataLen,
				pData, pulDataLen);
}

/*
 * Create new mechanism type for a mechanism supported by
 * the card
 */
sc_pkcs11_mechanism_type_t *
sc_pkcs11_new_fw_mechanism(CK_MECHANISM_TYPE mech,
				CK_MECHANISM_INFO_PTR pInfo,
				CK_KEY_TYPE key_type,
				void *priv_data)
{
	sc_pkcs11_mechanism_type_t *mt;

	mt = (sc_pkcs11_mechanism_type_t *) calloc(1, sizeof(*mt));
	if (mt == NULL)
		return mt;
	mt->mech = mech;
	mt->mech_info = *pInfo;
	mt->key_type = key_type;
	mt->mech_data = priv_data;
	mt->obj_size = sizeof(sc_pkcs11_operation_t);

	mt->release = sc_pkcs11_signature_release;

	if (pInfo->flags & CKF_SIGN) {
		mt->sign_init = sc_pkcs11_signature_init;
		mt->sign_update = sc_pkcs11_signature_update;
		mt->sign_final = sc_pkcs11_signature_final;
		mt->sign_size = sc_pkcs11_signature_size;
#ifdef HAVE_OPENSSL
		mt->verif_init = sc_pkcs11_verify_init;
		mt->verif_update = sc_pkcs11_verify_update;
		mt->verif_final = sc_pkcs11_verify_final;
#endif
	}
	if (pInfo->flags & CKF_UNWRAP) {
		/* ... */
	}
	if (pInfo->flags & CKF_DECRYPT) {
		mt->decrypt_init = sc_pkcs11_decrypt_init;
		mt->decrypt = sc_pkcs11_decrypt;
	}

	return mt;
}

/*
 * Register generic mechanisms
 */
CK_RV
sc_pkcs11_register_generic_mechanisms(struct sc_pkcs11_card *p11card)
{
#ifdef HAVE_OPENSSL
	sc_pkcs11_register_openssl_mechanisms(p11card);
#endif

	return CKR_OK;
}

/*
 * Register a sign+hash algorithm derived from an algorithm supported
 * by the token + a software hash mechanism
 */
CK_RV
sc_pkcs11_register_sign_and_hash_mechanism(struct sc_pkcs11_card *p11card,
		CK_MECHANISM_TYPE mech,
		CK_MECHANISM_TYPE hash_mech,
		sc_pkcs11_mechanism_type_t *sign_type)
{
	sc_pkcs11_mechanism_type_t *hash_type, *new_type;
	struct hash_signature_info *info;
	CK_MECHANISM_INFO mech_info = sign_type->mech_info;

	if (!(hash_type = sc_pkcs11_find_mechanism(p11card, hash_mech, CKF_DIGEST)))
		return CKR_MECHANISM_INVALID;

	/* These hash-based mechs can only be used for sign/verify */
	mech_info.flags &= (CKF_SIGN | CKF_SIGN_RECOVER | CKF_VERIFY | CKF_VERIFY_RECOVER);

	info = (struct hash_signature_info *) calloc(1, sizeof(*info));
	info->mech = mech;
	info->sign_type = sign_type;
	info->hash_type = hash_type;
	info->sign_mech = sign_type->mech;
	info->hash_mech = hash_mech;

	new_type = sc_pkcs11_new_fw_mechanism(mech, &mech_info,
				sign_type->key_type, info);
	if (new_type)
		sc_pkcs11_register_mechanism(p11card, new_type);
	return CKR_OK;
}
