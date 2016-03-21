/**
 * Lua interface to libgcrypt.
 *
 * Copyright (C) 2016 Peter Wu <peter@lekensteyn.nl>
 * Licensed under the MIT license. See the LICENSE file for details.
 */
#include <gcrypt.h>
#include <lua.h>
#include <lauxlib.h>

int luaopen_luagcrypt(lua_State *L);

/* {{{ Compatibility with older Lua */
#if LUA_VERSION_NUM == 501
static void
luaL_setfuncs(lua_State *L, const luaL_Reg *l, int nup)
{
    if (nup) {
        luaL_error(L, "nup == 0 is not supported by this compat function");
    }
    for (; l->name; l++) {
        lua_pushcclosure(L, l->func, 0);
        lua_setfield(L, -2, l->name);
    }
}

#define luaL_newlibtable(L,l)   lua_createtable(L, 0, sizeof(l)/sizeof*(l) - 1)
#define luaL_newlib(L,l)        (luaL_newlibtable(L,l), luaL_setfuncs(L,l,0))
#endif
#if LUA_VERSION_NUM >= 503
#define luaL_checkint(L,n)      ((int)luaL_checkinteger(L,n))
#endif
/* }}} */

/* {{{ Symmetric encryption */
typedef struct {
    gcry_cipher_hd_t h;
    unsigned char *out; /* temporary to avoid resource leaks on error paths */
} LgcryptCipher;

/* Initializes a new gcrypt.Cipher userdata and pushes it on the stack. */
static LgcryptCipher *
lgcrypt_cipher_new(lua_State *L)
{
    LgcryptCipher *state;

    state = (LgcryptCipher *) lua_newuserdata(L, sizeof(LgcryptCipher));
    state->h = NULL;
    state->out = NULL;
    luaL_getmetatable(L, "gcrypt.Cipher");
    lua_setmetatable(L, -2);
    return state;
}

static int
lgcrypt_cipher_open(lua_State *L)
{
    int algo, mode;
    LgcryptCipher *state;
    gcry_error_t err;

    algo = luaL_checkint(L, 1);
    mode = luaL_checkint(L, 2);

    state = lgcrypt_cipher_new(L);

    err = gcry_cipher_open(&state->h, algo, mode, 0);
    if (err != GPG_ERR_NO_ERROR) {
        lua_pop(L, 1);
        luaL_error(L, "gcry_cipher_open() failed with %s", gcry_strerror(err));
    }
    return 1;
}

static LgcryptCipher *
checkCipher(lua_State *L, int arg)
{
    return (LgcryptCipher *)luaL_checkudata(L, arg, "gcrypt.Cipher");
}

static int
lgcrypt_cipher___gc(lua_State *L)
{
    LgcryptCipher *state = checkCipher(L, 1);

    if (state->h) {
        gcry_cipher_close(state->h);
    }
    if (state->out) {
        gcry_free(state->out);
    }
    return 0;
}


static int
lgcrypt_cipher_setkey(lua_State *L)
{
    LgcryptCipher *state = checkCipher(L, 1);
    size_t key_len;
    const char *key = luaL_checklstring(L, 2, &key_len);
    gcry_error_t err;

    err = gcry_cipher_setkey(state->h, key, key_len);
    if (err != GPG_ERR_NO_ERROR) {
        luaL_error(L, "gcry_cipher_setkey() failed with %s", gcry_strerror(err));
    }
    return 0;
}

static int
lgcrypt_cipher_setiv(lua_State *L)
{
    LgcryptCipher *state = checkCipher(L, 1);
    size_t iv_len;
    const char *iv = luaL_checklstring(L, 2, &iv_len);
    gcry_error_t err;

    err = gcry_cipher_setiv(state->h, iv, iv_len);
    if (err != GPG_ERR_NO_ERROR) {
        luaL_error(L, "gcry_cipher_setiv() failed with %s", gcry_strerror(err));
    }
    return 0;
}

static int
lgcrypt_cipher_reset(lua_State *L)
{
    LgcryptCipher *state = checkCipher(L, 1);
    gcry_error_t err;

    err = gcry_cipher_reset(state->h);
    if (err != GPG_ERR_NO_ERROR) {
        luaL_error(L, "gcry_cipher_reset() failed with %s", gcry_strerror(err));
    }
    return 0;
}

static int
lgcrypt_cipher_encrypt(lua_State *L)
{
    LgcryptCipher *state = checkCipher(L, 1);
    size_t in_len, out_len;
    const char *in;
    gcry_error_t err;

    in = luaL_checklstring(L, 2, &in_len);

    out_len = in_len;
    state->out = gcry_malloc(out_len);
    if (!state->out) {
        luaL_error(L, "Failed to allocate memory for ciphertext");
    }
    err = gcry_cipher_encrypt(state->h, state->out, out_len, in, in_len);
    if (err != GPG_ERR_NO_ERROR) {
        gcry_free(state->out);
        state->out = NULL;
        luaL_error(L, "gcry_cipher_encrypt() failed with %s", gcry_strerror(err));
    }
    lua_pushlstring(L, (const char *)state->out, out_len);
    gcry_free(state->out);
    state->out = NULL;
    return 1;
}

static int
lgcrypt_cipher_decrypt(lua_State *L)
{
    LgcryptCipher *state = checkCipher(L, 1);
    size_t in_len, out_len;
    const char *in;
    gcry_error_t err;

    in = luaL_checklstring(L, 2, &in_len);

    out_len = in_len;
    state->out = gcry_malloc(out_len);
    if (!state->out) {
        luaL_error(L, "Failed to allocate memory for plaintext");
    }
    err = gcry_cipher_decrypt(state->h, state->out, out_len, in, in_len);
    if (err != GPG_ERR_NO_ERROR) {
        gcry_free(state->out);
        state->out = NULL;
        luaL_error(L, "gcry_cipher_decrypt() failed with %s", gcry_strerror(err));
    }
    lua_pushlstring(L, (const char *)state->out, out_len);
    gcry_free(state->out);
    state->out = NULL;
    return 1;
}


/* https://gnupg.org/documentation/manuals/gcrypt/Working-with-cipher-handles.html */
static const struct luaL_Reg lgcrypt_cipher_meta[] = {
    {"__gc",    lgcrypt_cipher___gc},
    {"setkey",  lgcrypt_cipher_setkey},
    {"setiv",   lgcrypt_cipher_setiv},
    {"reset",   lgcrypt_cipher_reset},
    {"encrypt", lgcrypt_cipher_encrypt},
    {"decrypt", lgcrypt_cipher_decrypt},
    {NULL,      NULL}
};
/* }}} */
/* {{{ Message digests */
typedef struct {
    gcry_md_hd_t h;
} LgcryptHash;

/* Initializes a new gcrypt.Hash userdata and pushes it on the stack. */
static LgcryptHash *
lgcrypt_hash_new(lua_State *L)
{
    LgcryptHash *state;

    state = (LgcryptHash *) lua_newuserdata(L, sizeof(LgcryptHash));
    state->h = NULL;
    luaL_getmetatable(L, "gcrypt.Hash");
    lua_setmetatable(L, -2);
    return state;
}

static int
lgcrypt_hash_open(lua_State *L)
{
    int algo;
    unsigned int flags;
    LgcryptHash *state;
    gcry_error_t err;

    algo = luaL_checkint(L, 1);
    flags = (unsigned int)luaL_optinteger(L, 2, 0);

    state = lgcrypt_hash_new(L);

    err = gcry_md_open(&state->h, algo, flags);
    if (err != GPG_ERR_NO_ERROR) {
        lua_pop(L, 1);
        luaL_error(L, "gcry_md_open() failed with %s", gcry_strerror(err));
    }
    return 1;
}

static LgcryptHash *
checkHash(lua_State *L, int arg)
{
    return (LgcryptHash *)luaL_checkudata(L, arg, "gcrypt.Hash");
}

static int
lgcrypt_hash___gc(lua_State *L)
{
    LgcryptHash *state = checkHash(L, 1);

    if (state->h) {
        gcry_md_close(state->h);
    }
    return 0;
}


static int
lgcrypt_hash_setkey(lua_State *L)
{
    LgcryptHash *state = checkHash(L, 1);
    size_t key_len;
    const char *key = luaL_checklstring(L, 2, &key_len);
    gcry_error_t err;

    err = gcry_md_setkey(state->h, key, key_len);
    if (err != GPG_ERR_NO_ERROR) {
        luaL_error(L, "gcry_md_setkey() failed with %s", gcry_strerror(err));
    }
    return 0;
}

static int
lgcrypt_hash_reset(lua_State *L)
{
    LgcryptHash *state = checkHash(L, 1);
    gcry_md_reset(state->h);
    return 0;
}

static int
lgcrypt_hash_write(lua_State *L)
{
    LgcryptHash *state = checkHash(L, 1);
    size_t buffer_len;
    const char *buffer = luaL_checklstring(L, 2, &buffer_len);

    gcry_md_write(state->h, buffer, buffer_len);
    return 0;
}

static int
lgcrypt_hash_read(lua_State *L)
{
    LgcryptHash *state = checkHash(L, 1);
    unsigned char *digest;
    size_t digest_len;
    int algo;

    algo = (int)luaL_optinteger(L, 2, gcry_md_get_algo(state->h));
    if (!gcry_md_is_enabled(state->h, algo)) {
        luaL_error(L, "Unable to obtain digest for a disabled algorithm");
    }

    digest_len = gcry_md_get_algo_dlen(algo);
    if (!digest_len) {
        luaL_error(L, "Invalid digest length detected");
    }
    digest = gcry_md_read(state->h, algo);
    if (!digest) {
        luaL_error(L, "Failed to obtain digest");
    }
    lua_pushlstring(L, (const char *) digest, digest_len);
    return 1;
}



/* https://gnupg.org/documentation/manuals/gcrypt/Working-with-hash-algorithms.html */
static const struct luaL_Reg lgcrypt_hash_meta[] = {
    {"__gc",    lgcrypt_hash___gc},
    {"setkey",  lgcrypt_hash_setkey},
    {"reset",   lgcrypt_hash_reset},
    {"write",   lgcrypt_hash_write},
    {"read",    lgcrypt_hash_read},
    {NULL,      NULL}
};
/* }}} */

static int
lgcrypt_init(lua_State *L)
{
    if (gcry_control(GCRYCTL_INITIALIZATION_FINISHED_P)) {
        luaL_error(L, "libgcrypt was already initialized");
    }
    gcry_check_version(NULL);
    gcry_control(GCRYCTL_DISABLE_SECMEM, 0);
    gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
    return 0;
}

static const struct luaL_Reg lgcrypt[] = {
    {"init",    lgcrypt_init},
    {"Cipher",  lgcrypt_cipher_open},
    {"Hash",    lgcrypt_hash_open},
    {NULL, NULL}
};

static void
register_metatable(lua_State *L, const char *name, const struct luaL_Reg *funcs)
{
    luaL_newmetatable(L, name);
    luaL_setfuncs(L, funcs, 0);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
}

int
luaopen_luagcrypt(lua_State *L)
{
    register_metatable(L, "gcrypt.Cipher", lgcrypt_cipher_meta);
    register_metatable(L, "gcrypt.Hash",   lgcrypt_hash_meta);

    luaL_newlib(L, lgcrypt);

#define INT_GCRY(name) do { \
    lua_pushinteger(L, GCRY_ ## name); \
    lua_setfield(L, -2, #name); \
    } while (0)

    /* Add constants for gcrypt.Cipher */
    /* https://gnupg.org/documentation/manuals/gcrypt/Available-ciphers.html */
    INT_GCRY(CIPHER_AES128);
    INT_GCRY(CIPHER_AES192);
    INT_GCRY(CIPHER_AES256);

    /* https://gnupg.org/documentation/manuals/gcrypt/Available-cipher-modes.html */
    INT_GCRY(CIPHER_MODE_CBC);

    INT_GCRY(MD_FLAG_HMAC);

    /* https://gnupg.org/documentation/manuals/gcrypt/Available-hash-algorithms.html */
    INT_GCRY(MD_SHA256);
#undef INT_GCRY

    return 1;
}