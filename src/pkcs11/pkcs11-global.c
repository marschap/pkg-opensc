/*
 * pkcs11-global.c: PKCS#11 module level functions and function table
 *
 * Copyright (C) 2002  Timo Teräs <timo.teras@iki.fi>
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

#include <stdlib.h>
#include <string.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include "sc-pkcs11.h"

sc_context_t *context = NULL;
struct sc_pkcs11_pool session_pool;
struct sc_pkcs11_slot virtual_slots[SC_PKCS11_MAX_VIRTUAL_SLOTS];
struct sc_pkcs11_card card_table[SC_PKCS11_MAX_READERS];
struct sc_pkcs11_config sc_pkcs11_conf;

extern CK_FUNCTION_LIST pkcs11_function_list;

#if defined(HAVE_PTHREAD) && defined(PKCS11_THREAD_LOCKING)
#include <pthread.h>
CK_RV mutex_create(void **mutex)
{
	pthread_mutex_t *m = (pthread_mutex_t *) malloc(sizeof(*mutex));
	if (m == NULL)
		return CKR_GENERAL_ERROR;;
	pthread_mutex_init(m, NULL);
	*mutex = m;
	return CKR_OK;
}

CK_RV mutex_lock(void *p)
{
	if (pthread_mutex_lock((pthread_mutex_t *) p) == 0)
		return CKR_OK;
	else
		return CKR_GENERAL_ERROR;
}

CK_RV mutex_unlock(void *p)
{
	if (pthread_mutex_unlock((pthread_mutex_t *) p) == 0)
		return CKR_OK;
	else
		return CKR_GENERAL_ERROR;
}

CK_RV mutex_destroy(void *p)
{
	pthread_mutex_destroy((pthread_mutex_t *) p);
	free(p);
	return CKR_OK;
}

static CK_C_INITIALIZE_ARGS _def_locks = {
	mutex_create, mutex_destroy, mutex_lock, mutex_unlock, 0, NULL };
#elif defined(_WIN32) && defined (PKCS11_THREAD_LOCKING)
CK_RV mutex_create(void **mutex)
{
	CRITICAL_SECTION *m;

	m = (CRITICAL_SECTION *) malloc(sizeof(*m));
	if (m == NULL)
		return CKR_GENERAL_ERROR;
	InitializeCriticalSection(m);
	*mutex = m;
	return CKR_OK;
}

CK_RV mutex_lock(void *p)
{
	EnterCriticalSection((CRITICAL_SECTION *) p);
	return CKR_OK;
}


CK_RV mutex_unlock(void *p)
{
	LeaveCriticalSection((CRITICAL_SECTION *) p);
	return CKR_OK;
}


CK_RV mutex_destroy(void *p)
{
	DeleteCriticalSection((CRITICAL_SECTION *) p);
	free(p);
	return CKR_OK;
}
static CK_C_INITIALIZE_ARGS _def_locks = {
	mutex_create, mutex_destroy, mutex_lock, mutex_unlock, 0, NULL };
#endif

static CK_C_INITIALIZE_ARGS_PTR	_locking;
static void *			_lock = NULL;
#if (defined(HAVE_PTHREAD) || defined(_WIN32)) && defined(PKCS11_THREAD_LOCKING)
#define HAVE_OS_LOCKING
static CK_C_INITIALIZE_ARGS_PTR default_mutex_funcs = &_def_locks;
#else
static CK_C_INITIALIZE_ARGS_PTR default_mutex_funcs = NULL;
#endif

/* wrapper for the locking functions for libopensc */
static int sc_create_mutex(void **m)
{
	if (_locking == NULL)
		return SC_SUCCESS;
	if (_locking->CreateMutex(m) == CKR_OK)
		return SC_SUCCESS;
	else
		return SC_ERROR_INTERNAL;
}

static int sc_lock_mutex(void *m)
{
	if (_locking == NULL)
		return SC_SUCCESS;
	if (_locking->LockMutex(m) == CKR_OK)
		return SC_SUCCESS;
	else
		return SC_ERROR_INTERNAL;
}

static int sc_unlock_mutex(void *m)
{
	if (_locking == NULL)
		return SC_SUCCESS;
	if (_locking->UnlockMutex(m) == CKR_OK)
		return SC_SUCCESS;
	else
		return SC_ERROR_INTERNAL;
	
}

static int sc_destroy_mutex(void *m)
{
	if (_locking == NULL)
		return SC_SUCCESS;
	if (_locking->DestroyMutex(m) == CKR_OK)
		return SC_SUCCESS;
	else
		return SC_ERROR_INTERNAL;
}

static sc_thread_context_t sc_thread_ctx = {
	0, sc_create_mutex, sc_lock_mutex,
	sc_unlock_mutex, sc_destroy_mutex, NULL
};

CK_RV C_Initialize(CK_VOID_PTR pInitArgs)
{
	int i, rc, rv;
	sc_context_param_t ctx_opts;

	if (context != NULL) {
		sc_error(context, "C_Initialize(): Cryptoki already initialized\n");
		return CKR_CRYPTOKI_ALREADY_INITIALIZED;
	}

	rv = sc_pkcs11_init_lock((CK_C_INITIALIZE_ARGS_PTR) pInitArgs);
	if (rv != CKR_OK)   {
		sc_release_context(context);
		context = NULL;
	}

	/* set context options */
	memset(&ctx_opts, 0, sizeof(sc_context_param_t));
	ctx_opts.ver        = 0;
	ctx_opts.app_name   = "opensc-pkcs11";
	ctx_opts.thread_ctx = &sc_thread_ctx;
	
	rc = sc_context_create(&context, &ctx_opts);
	if (rc != SC_SUCCESS) {
		rv = CKR_DEVICE_ERROR;
		goto out;
	}

	/* Load configuration */
	load_pkcs11_parameters(&sc_pkcs11_conf, context);

	first_free_slot = 0;
	pool_initialize(&session_pool, POOL_TYPE_SESSION);
	for (i=0; i<SC_PKCS11_MAX_VIRTUAL_SLOTS; i++)
		slot_initialize(i, &virtual_slots[i]);
	for (i=0; i<SC_PKCS11_MAX_READERS; i++)
		card_initialize(i);

	/* Detect any card, but do not flag "insert" events */
	__card_detect_all(0);

out:	
	if (context != NULL)
		sc_debug(context, "C_Initialize: result = %d\n", rv);
	return rv;
}

CK_RV C_Finalize(CK_VOID_PTR pReserved)
{
	int i;
	CK_RV rv;

	rv = sc_pkcs11_lock();
	if (rv != CKR_OK)
		return rv;

	if (pReserved != NULL_PTR) {
		rv = CKR_ARGUMENTS_BAD;
		goto out;
	}

	sc_debug(context, "Shutting down Cryptoki\n");
	for (i=0; i < (int)sc_ctx_get_reader_count(context); i++)
		card_removed(i);

	sc_release_context(context);
	context = NULL;

out:	/* Release and destroy the mutex */
	sc_pkcs11_free_lock();

	return rv;
}

CK_RV C_GetInfo(CK_INFO_PTR pInfo)
{
	CK_RV rv = CKR_OK;

	rv = sc_pkcs11_lock();
	if (rv != CKR_OK)
		return rv;

	if (pInfo == NULL_PTR) {
		rv = CKR_ARGUMENTS_BAD;
		goto out;
	}

	sc_debug(context, "Cryptoki info query\n");

	memset(pInfo, 0, sizeof(CK_INFO));
	pInfo->cryptokiVersion.major = 2;
	pInfo->cryptokiVersion.minor = 11;
	strcpy_bp(pInfo->manufacturerID,
		  "OpenSC (www.opensc-project.org)",
		  sizeof(pInfo->manufacturerID));
	strcpy_bp(pInfo->libraryDescription,
		  "smart card PKCS#11 API",
		  sizeof(pInfo->libraryDescription));
	pInfo->libraryVersion.major = 1;
	pInfo->libraryVersion.minor = 0;

out:	sc_pkcs11_unlock();
	return rv;
}	

CK_RV C_GetFunctionList(CK_FUNCTION_LIST_PTR_PTR ppFunctionList)
{
	if (ppFunctionList == NULL_PTR)
		return CKR_ARGUMENTS_BAD;

	*ppFunctionList = &pkcs11_function_list;
	return CKR_OK;
}

CK_RV C_GetSlotList(CK_BBOOL       tokenPresent,  /* only slots with token present */
		    CK_SLOT_ID_PTR pSlotList,     /* receives the array of slot IDs */
		    CK_ULONG_PTR   pulCount)      /* receives the number of slots */
{
	CK_SLOT_ID found[SC_PKCS11_MAX_VIRTUAL_SLOTS];
	int i;
	CK_ULONG numMatches;
	sc_pkcs11_slot_t *slot;
	CK_RV rv;

	rv = sc_pkcs11_lock();
	if (rv != CKR_OK)
		return rv;

	if (pulCount == NULL_PTR) {
		rv = CKR_ARGUMENTS_BAD;
		goto out;
	}

	sc_debug(context, "Getting slot listing\n");
	card_detect_all();

	numMatches = 0;
	for (i=0; i<SC_PKCS11_MAX_VIRTUAL_SLOTS; i++) {
		slot = &virtual_slots[i];

		if (!tokenPresent || (slot->slot_info.flags & CKF_TOKEN_PRESENT))
			found[numMatches++] = i;
	}

	if (pSlotList == NULL_PTR) {
		sc_debug(context, "was only a size inquiry (%d)\n", numMatches);
		*pulCount = numMatches;
		rv = CKR_OK;
		goto out;
	}

	if (*pulCount < numMatches) {
		sc_debug(context, "buffer was too small (needed %d)\n", numMatches);
		*pulCount = numMatches;
		rv = CKR_BUFFER_TOO_SMALL;
		goto out;
	}

	memcpy(pSlotList, found, numMatches * sizeof(CK_SLOT_ID));
	*pulCount = numMatches;
	rv = CKR_OK;

	sc_debug(context, "returned %d slots\n", numMatches);

out:	sc_pkcs11_unlock();
	return rv;
}

static sc_timestamp_t get_current_time(void)
{
#ifndef _WIN32
	struct timeval tv;
	struct timezone tz;
	sc_timestamp_t curr;

	if (gettimeofday(&tv, &tz) != 0)
		return 0;

	curr = tv.tv_sec;
	curr *= 1000;
	curr += tv.tv_usec / 1000;
#else
	struct _timeb time_buf;
	sc_timestamp_t curr;

	_ftime(&time_buf);

	curr = time_buf.time;
	curr *= 1000;
	curr += time_buf.millitm;
#endif

	return curr;
}

CK_RV C_GetSlotInfo(CK_SLOT_ID slotID, CK_SLOT_INFO_PTR pInfo)
{
	struct sc_pkcs11_slot *slot;
	sc_timestamp_t now;
	CK_RV rv;

	rv = sc_pkcs11_lock();
	if (rv != CKR_OK)
		return rv;

	if (pInfo == NULL_PTR) {
		rv = CKR_ARGUMENTS_BAD;
		goto out;
	}

	sc_debug(context, "Getting info about slot %d\n", slotID);

	rv = slot_get_slot(slotID, &slot);
	if (rv == CKR_OK){
		now = get_current_time();
		if (now >= card_table[slot->reader].slot_state_expires || now == 0) {
			/* Update slot status */
			rv = card_detect(slot->reader);
			/* Don't ask again within the next second */
			card_table[slot->reader].slot_state_expires = now + 1000;
		}
	}
	if (rv == CKR_TOKEN_NOT_PRESENT || rv == CKR_TOKEN_NOT_RECOGNIZED)
		rv = CKR_OK;

	if (rv == CKR_OK)
		memcpy(pInfo, &slot->slot_info, sizeof(CK_SLOT_INFO));

out:	sc_pkcs11_unlock();
	return rv;
}

CK_RV C_GetTokenInfo(CK_SLOT_ID slotID, CK_TOKEN_INFO_PTR pInfo)
{
	struct sc_pkcs11_slot *slot;
	CK_RV rv;

	rv = sc_pkcs11_lock();
	if (rv != CKR_OK)
		return rv;

	if (pInfo == NULL_PTR) {
		rv = CKR_ARGUMENTS_BAD;
		goto out;
	}

	sc_debug(context, "Getting info about token in slot %d\n", slotID);

	rv = slot_get_token(slotID, &slot);
	if (rv == CKR_OK)
		memcpy(pInfo, &slot->token_info, sizeof(CK_TOKEN_INFO));

out:	sc_pkcs11_unlock();
	return rv;
}

CK_RV C_GetMechanismList(CK_SLOT_ID slotID,
			 CK_MECHANISM_TYPE_PTR pMechanismList,
                         CK_ULONG_PTR pulCount)
{
	struct sc_pkcs11_slot *slot;
	CK_RV rv;

	rv = sc_pkcs11_lock();
	if (rv != CKR_OK)
		return rv;

	rv = slot_get_token(slotID, &slot);
	if (rv == CKR_OK)
		rv = sc_pkcs11_get_mechanism_list(slot->card, pMechanismList, pulCount);

	sc_pkcs11_unlock();
	return rv;
}

CK_RV C_GetMechanismInfo(CK_SLOT_ID slotID,
			 CK_MECHANISM_TYPE type,
			 CK_MECHANISM_INFO_PTR pInfo)
{
	struct sc_pkcs11_slot *slot;
	CK_RV rv;

	rv = sc_pkcs11_lock();
	if (rv != CKR_OK)
		return rv;

	if (pInfo == NULL_PTR) {
		rv = CKR_ARGUMENTS_BAD;
		goto out;
	}
	rv = slot_get_token(slotID, &slot);
	if (rv == CKR_OK)
		rv = sc_pkcs11_get_mechanism_info(slot->card, type, pInfo);

out:	sc_pkcs11_unlock();
	return rv;
}

CK_RV C_InitToken(CK_SLOT_ID slotID,
		  CK_CHAR_PTR pPin,
		  CK_ULONG ulPinLen,
		  CK_CHAR_PTR pLabel)
{
	struct sc_pkcs11_pool_item *item;
	struct sc_pkcs11_session *session;
	struct sc_pkcs11_slot *slot;
	CK_RV rv;

	rv = sc_pkcs11_lock();
	if (rv != CKR_OK)
		return rv;

	rv = slot_get_token(slotID, &slot);
	if (rv != CKR_OK)
		goto out;

	/* Make sure there's no open session for this token */
	for (item = session_pool.head; item; item = item->next) {
		session = (struct sc_pkcs11_session*) item->item;
		if (session->slot == slot) {
			rv = CKR_SESSION_EXISTS;
			goto out;
		}
	}

	if (slot->card->framework->init_token == NULL) {
		rv = CKR_FUNCTION_NOT_SUPPORTED;
		goto out;
	}
	rv = slot->card->framework->init_token(slot->card,
				 slot->fw_data, pPin, ulPinLen, pLabel);

	if (rv == CKR_OK) {
		/* Now we should re-bind all tokens so they get the
		 * corresponding function vector and flags */
	}

out:	sc_pkcs11_unlock();
	return rv;
}

CK_RV C_WaitForSlotEvent(CK_FLAGS flags,   /* blocking/nonblocking flag */
			 CK_SLOT_ID_PTR pSlot,  /* location that receives the slot ID */
			 CK_VOID_PTR pReserved) /* reserved.  Should be NULL_PTR */
{
	sc_reader_t *reader, *readers[SC_MAX_SLOTS * SC_MAX_READERS];
	int slots[SC_MAX_SLOTS * SC_MAX_READERS];
	int i, j, k, r, found;
	unsigned int mask, events;
	CK_RV rv;

	/* Firefox 1.5 (NSS 3.10) calls this function (blocking) from a seperate thread,
	 * which gives 2 problems:
	 * - on Windows/Mac: this waiting thread will log to a NULL context
	 *   after the 'main' thread does a C_Finalize() and sets the ctx to NULL.
	 * - on Linux, things just hang (at least on Debian 'sid')
	 * So we just return CKR_FUNCTION_NOT_SUPPORTED on a blocking call,
	 * in which case FF just seems to default to polling in the main thread
	 * as earlier NSS versions.
	 */
	if (!(flags & CKF_DONT_BLOCK))
		return CKR_FUNCTION_NOT_SUPPORTED;

	rv = sc_pkcs11_lock();
	if (rv != CKR_OK)
		return rv;

	if (pReserved != NULL_PTR) {
		rv = CKR_ARGUMENTS_BAD;
		goto out;
	}

	mask = SC_EVENT_CARD_INSERTED|SC_EVENT_CARD_REMOVED;

	if ((rv = slot_find_changed(pSlot, mask)) == CKR_OK
	 || (flags & CKF_DONT_BLOCK))
		goto out;

	for (i = k = 0; i < (int)sc_ctx_get_reader_count(context); i++) {
		reader = sc_ctx_get_reader(context, i);
		if (reader == NULL) {
			rv = CKR_GENERAL_ERROR;
			goto out;
		}
		for (j = 0; j < reader->slot_count; j++, k++) {
			readers[k] = reader;
			slots[k] = j;
		}
	}

again:
	/* Check if C_Finalize() has been called in another thread */
	if (context == NULL)
		return CKR_CRYPTOKI_NOT_INITIALIZED;

	sc_pkcs11_unlock();
	r = sc_wait_for_event(readers, slots, k, mask, &found, &events, -1);

	/* There may have been a C_Finalize while we slept */
	if (context == NULL)
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	if ((rv = sc_pkcs11_lock()) != CKR_OK)
		return rv;

	if (r != SC_SUCCESS) {
		sc_error(context, "sc_wait_for_event() returned %d\n",  r);
		rv = sc_to_cryptoki_error(r, -1);
		goto out;
	}

	/* If no changed slot was found (maybe an unsupported card
	 * was inserted/removed) then go waiting again */
	if ((rv = slot_find_changed(pSlot, mask)) != CKR_OK)
		goto again;

out:	sc_pkcs11_unlock();
	return rv;
}

/*
 * Locking functions
 */

CK_RV
sc_pkcs11_init_lock(CK_C_INITIALIZE_ARGS_PTR args)
{
	int rv = CKR_OK;

	int applock = 0;
	int oslock = 0;
	if (_lock)
		return CKR_OK;

	/* No CK_C_INITIALIZE_ARGS pointer, no locking */
	if (!args)
		return CKR_OK;

	if (args->pReserved != NULL_PTR)
		return CKR_ARGUMENTS_BAD;

	/* If the app tells us OS locking is okay,
	 * use that. Otherwise use the supplied functions.
	 */
	_locking = NULL;
	if (args->CreateMutex && args->DestroyMutex &&
		   args->LockMutex   && args->UnlockMutex) {
			applock = 1;
	}
	if ((args->flags & CKF_OS_LOCKING_OK)) {
		oslock = 1;
	}

	/* Based on PKCS#11 v2.11 11.4 */
	if (applock && oslock) {
		/* Shall be used in threaded environment, prefer app provided locking */
		_locking = args;
	} else if (!applock && oslock) {
		/* Shall be used in threaded environment, must use operating system locking */
		_locking = default_mutex_funcs;
	} else if (applock && !oslock) {
		/* Shall be used in threaded envirnoment, must use app provided locking */
		_locking = args;
	} else if (!applock && !oslock) {
		/* Shall not be used in threaded environemtn, use operating system locking */
		_locking = default_mutex_funcs;
	}

	if (_locking != NULL) {
		/* create mutex */
		rv = _locking->CreateMutex(&_lock);
	}

	return rv;
}

CK_RV sc_pkcs11_lock(void)
{
	if (context == NULL)
		return CKR_CRYPTOKI_NOT_INITIALIZED;

	if (!_lock)
		return CKR_OK;
	if (_locking)  {
		while (_locking->LockMutex(_lock) != CKR_OK)
			;
	} 

	return CKR_OK;
}

static void
__sc_pkcs11_unlock(void *lock)
{
	if (!lock)
		return;
	if (_locking) {
		while (_locking->UnlockMutex(lock) != CKR_OK)
			;
	} 
}

void sc_pkcs11_unlock(void)
{
	__sc_pkcs11_unlock(_lock);
}

/*
 * Free the lock - note the lock must be held when
 * you come here
 */
void sc_pkcs11_free_lock(void)
{
	void	*tempLock;

	if (!(tempLock = _lock))
		return;

	/* Clear the global lock pointer - once we've
	 * unlocked the mutex it's as good as gone */
	_lock = NULL;

	/* Now unlock. On SMP machines the synchronization
	 * primitives should take care of flushing out
	 * all changed data to RAM */
	__sc_pkcs11_unlock(tempLock);

	if (_locking)
		_locking->DestroyMutex(tempLock);
	_locking = NULL;
}

CK_FUNCTION_LIST pkcs11_function_list = {
	{ 2, 11 },
	C_Initialize,
	C_Finalize,
	C_GetInfo,
	C_GetFunctionList,
	C_GetSlotList,
	C_GetSlotInfo,
	C_GetTokenInfo,
	C_GetMechanismList,
	C_GetMechanismInfo,
	C_InitToken,
	C_InitPIN,
	C_SetPIN,
	C_OpenSession,
	C_CloseSession,
	C_CloseAllSessions,
	C_GetSessionInfo,
	C_GetOperationState,
	C_SetOperationState,
	C_Login,
	C_Logout,
	C_CreateObject,
	C_CopyObject,
	C_DestroyObject,
	C_GetObjectSize,
	C_GetAttributeValue,
	C_SetAttributeValue,
	C_FindObjectsInit,
	C_FindObjects,
	C_FindObjectsFinal,
	C_EncryptInit,
	C_Encrypt,
	C_EncryptUpdate,
	C_EncryptFinal,
	C_DecryptInit,
	C_Decrypt,
	C_DecryptUpdate,
	C_DecryptFinal,
	C_DigestInit,
	C_Digest,
	C_DigestUpdate,
	C_DigestKey,
	C_DigestFinal,
	C_SignInit,
	C_Sign,
	C_SignUpdate,
	C_SignFinal,
	C_SignRecoverInit,
	C_SignRecover,
	C_VerifyInit,
	C_Verify,
	C_VerifyUpdate,
	C_VerifyFinal,
	C_VerifyRecoverInit,
	C_VerifyRecover,
	C_DigestEncryptUpdate,
	C_DecryptDigestUpdate,
	C_SignEncryptUpdate,
	C_DecryptVerifyUpdate,
	C_GenerateKey,
	C_GenerateKeyPair,
	C_WrapKey,
	C_UnwrapKey,
	C_DeriveKey,
	C_SeedRandom,
	C_GenerateRandom,
	C_GetFunctionStatus,
	C_CancelFunction,
	C_WaitForSlotEvent
};
