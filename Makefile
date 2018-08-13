LUA_VER     = 5.2
LUA         = lua$(LUA_VER)

LUA_CFLAGS  = `pkg-config $(LUA) --cflags`
LUA_LIBS    = `pkg-config $(LUA) --libs`
LUA_DESTDIR = `pkg-config $(LUA) --variable=INSTALL_CMOD`

CFLAGS      = -Wall -Wextra -Werror=implicit-function-declaration
CFLAGS     += -O2 -g
CFLAGS     += $(LUA_CFLAGS)

#LDFLAGS    += -static

LIBS        = -lgcrypt -lgpg-error
LIBS       += $(LUA_LIBS)

OS          = $(shell uname)
ifeq ($(OS), Darwin)
LIBFLAG     = -bundle -undefined dynamic_lookup -all_load
else
LIBFLAG     = -shared
endif

luagcrypt.so: luagcrypt.c
	$(CC) $(CFLAGS) $(LIBFLAG) -o $@ $< -fPIC $(LDFLAGS) $(LIBS)

check: luagcrypt.so
	$(LUA) luagcrypt_test.lua

.PHONY: clean nstall -Dm755 luagcrypt.so $(LUA_DESTDIR)/luagcrypt.soinstall

clean:
	$(RM) luagcrypt.so luagcrypt.gcda luagcrypt.gcno luagcrypt.o luagcrypt.c.gcov

install: luagcrypt.so
	install -Dm755 luagcrypt.so $(LUA_DESTDIR)/luagcrypt.so

checkcoverage:
	$(MAKE) -s clean
	$(MAKE) luagcrypt.so CFLAGS="$(CFLAGS) --coverage"
	$(LUA) luagcrypt_test.lua
	-gcov -n luagcrypt.c
