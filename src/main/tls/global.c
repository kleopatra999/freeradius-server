/*
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/**
 * $Id$
 *
 * @file tls/global.c
 * @brief Initialise OpenSSL
 *
 * @copyright 2001 hereUare Communications, Inc. <raghud@hereuare.com>
 * @copyright 2003  Alan DeKok <aland@freeradius.org>
 * @copyright 2006-2016 The FreeRADIUS server project
 */
RCSID("$Id$")
USES_APPLE_DEPRECATED_API	/* OpenSSL API has been deprecated by Apple */

#ifdef WITH_TLS
#define LOG_PREFIX "tls - "

#include <openssl/conf.h>

#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/rad_assert.h>

/*
 *	Updated by threads.c in the server, and left alone for everyone else.
 */
int fr_tls_max_threads = 1;

#ifdef ENABLE_OPENSSL_VERSION_CHECK
typedef struct libssl_defect {
	uint64_t	high;		//!< The last version number this defect affected.
	uint64_t	low;		//!< The first version this defect affected.

	char const	*id;		//!< CVE (or other ID)
	char const	*name;		//!< As known in the media...
	char const	*comment;	//!< Where to get more information.
} libssl_defect_t;

/* Record critical defects in libssl here (newest first)*/
static libssl_defect_t libssl_defects[] =
{
	{
		.low		= 0x010001000,		/* 1.0.1  */
		.high		= 0x01000106f,		/* 1.0.1f */
		.id		= "CVE-2014-0160",
		.name		= "Heartbleed",
		.comment	= "For more information see http://heartbleed.com"
	}
};
#endif /* ENABLE_OPENSSL_VERSION_CHECK */

static bool tls_done_init = false;

#if OPENSSL_VERSION_NUMBER < 0x10100000L
/*
 *	If we're linking against OpenSSL, then it is the
 *	duty of the application, if it is multithreaded,
 *	to provide OpenSSL with appropriate thread id
 *	and mutex locking functions
 *
 *	Note: this only implements static callbacks.
 *	OpenSSL does not use dynamic locking callbacks
 *	right now, but may in the future, so we will have
 *	to add them at some point.
 */
static pthread_mutex_t *global_mutexes = NULL;

static unsigned long _thread_id(void)
{
	unsigned long ret;
	pthread_t thread = pthread_self();

	if (sizeof(ret) >= sizeof(thread)) {
		memcpy(&ret, &thread, sizeof(thread));
	} else {
		memcpy(&ret, &thread, sizeof(ret));
	}

	return ret;
}

static void _global_mutex(int mode, int n, UNUSED char const *file, UNUSED int line)
{
	if (mode & CRYPTO_LOCK) {
		pthread_mutex_lock(&(global_mutexes[n]));
	} else {
		pthread_mutex_unlock(&(global_mutexes[n]));
	}
}

/** Free the static mutexes we allocated for OpenSSL
 *
 */
static int _global_mutexes_free(pthread_mutex_t *mutexes)
{
	size_t i;

	/*
	 *	Ensure OpenSSL doesn't use the locks
	 */
	CRYPTO_set_id_callback(NULL);
	CRYPTO_set_locking_callback(NULL);

	/*
	 *	Destroy all the mutexes
	 */
	for (i = 0; i < talloc_array_length(mutexes); i++) pthread_mutex_destroy(&(mutexes[i]));

	return 0;
}

/** OpenSSL uses static mutexes which we need to initialise
 *
 * @note Yes, these really are global.
 *
 * @param ctx to alloc mutexes/array in.
 * @return array of mutexes.
 */
static pthread_mutex_t *global_mutexes_init(TALLOC_CTX *ctx)
{
	int i = 0;
	pthread_mutex_t *mutexes;

#define SETUP_CRYPTO_LOCK if (i < CRYPTO_num_locks()) pthread_mutex_init(&(mutexes[i++]), NULL)

	mutexes = talloc_array(ctx, pthread_mutex_t, CRYPTO_num_locks());
	if (!mutexes) {
		ERROR("Error allocating memory for OpenSSL mutexes!");
		return NULL;
	}

	talloc_set_destructor(mutexes, _global_mutexes_free);

	/*
	 *	Some profiling tools only give us the line the mutex
	 *	was initialised on.  In that case this allows us to
	 *	see which of the mutexes in the profiling tool relates
	 *	to which OpenSSL mutex.
	 *
	 *	OpenSSL locks are usually indexed from 1, but just to
	 *	be sure we initialise index 0 too.
	 */
	SETUP_CRYPTO_LOCK; /* UNUSED */
	SETUP_CRYPTO_LOCK; /* 1  - CRYPTO_LOCK_ERR */
	SETUP_CRYPTO_LOCK; /* 2  - CRYPTO_LOCK_EX_DATA */
	SETUP_CRYPTO_LOCK; /* 3  - CRYPTO_LOCK_X509 */
	SETUP_CRYPTO_LOCK; /* 4  - CRYPTO_LOCK_X509_INFO */
	SETUP_CRYPTO_LOCK; /* 5  - CRYPTO_LOCK_X509_PKEY */
	SETUP_CRYPTO_LOCK; /* 6  - CRYPTO_LOCK_X509_CRL */
	SETUP_CRYPTO_LOCK; /* 7  - CRYPTO_LOCK_X509_REQ */
	SETUP_CRYPTO_LOCK; /* 8  - CRYPTO_LOCK_DSA */
	SETUP_CRYPTO_LOCK; /* 9  - CRYPTO_LOCK_RSA */
	SETUP_CRYPTO_LOCK; /* 10 - CRYPTO_LOCK_EVP_PKEY */
	SETUP_CRYPTO_LOCK; /* 11 - CRYPTO_LOCK_X509_STORE */
	SETUP_CRYPTO_LOCK; /* 12 - CRYPTO_LOCK_SSL_CTX */
	SETUP_CRYPTO_LOCK; /* 13 - CRYPTO_LOCK_SSL_CERT */
	SETUP_CRYPTO_LOCK; /* 14 - CRYPTO_LOCK_SSL_SESSION */
	SETUP_CRYPTO_LOCK; /* 15 - CRYPTO_LOCK_SSL_SESS_CERT */
	SETUP_CRYPTO_LOCK; /* 16 - CRYPTO_LOCK_SSL */
	SETUP_CRYPTO_LOCK; /* 17 - CRYPTO_LOCK_SSL_METHOD */
	SETUP_CRYPTO_LOCK; /* 18 - CRYPTO_LOCK_RAND */
	SETUP_CRYPTO_LOCK; /* 19 - CRYPTO_LOCK_RAND2 */
	SETUP_CRYPTO_LOCK; /* 20 - CRYPTO_LOCK_MALLOC */
	SETUP_CRYPTO_LOCK; /* 21 - CRYPTO_LOCK_BIO  */
	SETUP_CRYPTO_LOCK; /* 22 - CRYPTO_LOCK_GETHOSTBYNAME */
	SETUP_CRYPTO_LOCK; /* 23 - CRYPTO_LOCK_GETSERVBYNAME */
	SETUP_CRYPTO_LOCK; /* 24 - CRYPTO_LOCK_READDIR */
	SETUP_CRYPTO_LOCK; /* 25 - CRYPTO_LOCRYPTO_LOCK_RSA_BLINDING */
	SETUP_CRYPTO_LOCK; /* 26 - CRYPTO_LOCK_DH */
	SETUP_CRYPTO_LOCK; /* 27 - CRYPTO_LOCK_MALLOC2  */
	SETUP_CRYPTO_LOCK; /* 28 - CRYPTO_LOCK_DSO */
	SETUP_CRYPTO_LOCK; /* 29 - CRYPTO_LOCK_DYNLOCK */
	SETUP_CRYPTO_LOCK; /* 30 - CRYPTO_LOCK_ENGINE */
	SETUP_CRYPTO_LOCK; /* 31 - CRYPTO_LOCK_UI */
	SETUP_CRYPTO_LOCK; /* 32 - CRYPTO_LOCK_ECDSA */
	SETUP_CRYPTO_LOCK; /* 33 - CRYPTO_LOCK_EC */
	SETUP_CRYPTO_LOCK; /* 34 - CRYPTO_LOCK_ECDH */
	SETUP_CRYPTO_LOCK; /* 35 - CRYPTO_LOCK_BN */
	SETUP_CRYPTO_LOCK; /* 36 - CRYPTO_LOCK_EC_PRE_COMP */
	SETUP_CRYPTO_LOCK; /* 37 - CRYPTO_LOCK_STORE */
	SETUP_CRYPTO_LOCK; /* 38 - CRYPTO_LOCK_COMP */
	SETUP_CRYPTO_LOCK; /* 39 - CRYPTO_LOCK_FIPS  */
	SETUP_CRYPTO_LOCK; /* 40 - CRYPTO_LOCK_FIPS2 */

	/*
	 *	Incase more are added *sigh*
	 */
	while (i < CRYPTO_num_locks()) SETUP_CRYPTO_LOCK;

	CRYPTO_set_id_callback(_thread_id);
	CRYPTO_set_locking_callback(_global_mutex);

	return mutexes;
}
#endif

#ifdef ENABLE_OPENSSL_VERSION_CHECK
/** Check for vulnerable versions of libssl
 *
 * @param acknowledged The highest CVE number a user has confirmed is not present in the system's
 *	libssl.
 * @return 0 if the CVE specified by the user matches the most recent CVE we have, else -1.
 */
int tls_global_version_check(char const *acknowledged)
{
	uint64_t v;

	if ((strcmp(acknowledged, libssl_defects[0].id) != 0) && (strcmp(acknowledged, "yes") != 0)) {
		bool bad = false;
		size_t i;

		/* Check for bad versions */
		v = (uint64_t) SSLeay();

		for (i = 0; i < (sizeof(libssl_defects) / sizeof(*libssl_defects)); i++) {
			libssl_defect_t *defect = &libssl_defects[i];

			if ((v >= defect->low) && (v <= defect->high)) {
				ERROR("Refusing to start with libssl version %s (in range %s)",
				      ssl_version(), ssl_version_range(defect->low, defect->high));
				ERROR("Security advisory %s (%s)", defect->id, defect->name);
				ERROR("%s", defect->comment);

				bad = true;
			}
		}

		if (bad) {
			INFO("Once you have verified libssl has been correctly patched, "
			     "set security.allow_vulnerable_openssl = '%s'", libssl_defects[0].id);
			return -1;
		}
	}

	return 0;
}
#endif

/** Add all the default ciphers and message digests to our context.
 *
 * This should be called exactly once from main, before reading the main config
 * or initialising any modules.
 */
int tls_global_init(void)
{
	ENGINE *rand_engine;

	if (tls_done_init) return 0;

#if OPENSSL_VERSION_NUMBER < 0x10100000L
	SSL_load_error_strings();	/* Readable error messages (examples show call before library_init) */
	SSL_library_init();		/* Initialize library */
	OpenSSL_add_all_algorithms();	/* Required for SHA2 in OpenSSL < 0.9.8o and 1.0.0.a */
	ENGINE_load_builtin_engines();	/* Needed to load AES-NI engine (also loads rdrand, boo) */

#  ifdef HAVE_OPENSSL_EVP_SHA256
	/*
	 *	SHA256 is in all versions of OpenSSL, but isn't
	 *	initialized by default.  It's needed for WiMAX
	 *	certificates.
	 */
	EVP_add_digest(EVP_sha256());
#  endif
	/*
	 *	If we're linking with OpenSSL too, then we need
	 *	to set up the mutexes and enable the thread callbacks.
	 */
	global_mutexes = global_mutexes_init(NULL);
	if (!global_mutexes) {
		ERROR("FATAL: Failed to set up SSL mutexes");
		return -1;
	}
#else
	OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CONFIG | OPENSSL_INIT_ENGINE_ALL_BUILTIN, NULL);
#endif

	/*
	 *	Mirror the paranoia found elsewhere on the net,
	 *	and disable rdrand as the default random number
	 *	generator.
	 */
	rand_engine = ENGINE_get_default_RAND();
	if (rand_engine && (strcmp(ENGINE_get_id(rand_engine), "rdrand") == 0)) ENGINE_unregister_RAND(rand_engine);
	ENGINE_register_all_complete();

	tls_done_init = true;
	OPENSSL_config(NULL);

	return 0;
}

#if OPENSSL_VERSION_NUMBER < 0x10100000L
/** Free any memory alloced by libssl
 *
 * OpenSSL >= 1.1.0 uses an atexit handler to automatically free memory
 */
void tls_global_cleanup(void)
{

	FR_TLS_REMOVE_THREAD_STATE();
	ENGINE_cleanup();
	CONF_modules_unload(1);
	ERR_free_strings();
	EVP_cleanup();
	CRYPTO_cleanup_all_ex_data();

	TALLOC_FREE(global_mutexes);

	tls_done_init = false;
}
#endif
#endif /* WITH_TLS */
