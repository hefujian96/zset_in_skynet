SKYNET_PATH ?= ./skynet

LUA_CLIB_PATH ?= ./luaclib
CSERVICE_PATH ?= ./cservice

LUA_LIB_SRC ?= lualib-src
LUA_SVC_SRC ?= service-src

LUA_INC ?= $(SKYNET_PATH)/3rd/lua

CFLAGS = -g -O2 -Wall -I$(LUA_INC)
SHARED := -fPIC --shared

LUA_CLIB = lfs zset
CSERVICE = channel

.PHONY : skynet all clean cleanall $(CSERVICE_PATH) $(LUA_CLIB_PATH)

all : \
	skynet \
	$(CSERVICE_PATH) \
	$(LUA_CLIB_PATH) \
	$(foreach v, $(LUA_CLIB), $(LUA_CLIB_PATH)/$(v).so) \
	$(foreach v, $(CSERVICE), $(CSERVICE_PATH)/$(v).so)

define CSERVICE_TEMP
  $$(CSERVICE_PATH)/$(1).so : $(LUA_SVC_SRC)/service_$(1).c | $$(CSERVICE_PATH)
	$$(CC) $$(CFLAGS) $$(SHARED) $$< -o $$@ -I$(SKYNET_PATH)/skynet-src
endef

$(foreach v, $(CSERVICE), $(eval $(call CSERVICE_TEMP,$(v))))

skynet :
	cd $(SKYNET_PATH) && $(MAKE) PLAT='linux'

$(CSERVICE_PATH) :
	mkdir -p $(CSERVICE_PATH)

$(LUA_CLIB_PATH) :
	mkdir -p $(LUA_CLIB_PATH)

$(LUA_CLIB_PATH)/lfs.so : $(LUA_LIB_SRC)/luafilesystem/src/lfs.c
	$(CC) $(CFLAGS) $(SHARED) -I$(LUA_LIB_SRC)/luafilesystem/src $^ -o $@

$(LUA_CLIB_PATH)/zset.so : $(LUA_LIB_SRC)/lua-zset/zset.c $(LUA_LIB_SRC)/lua-zset/lua_zset.c
	$(CC) $(CFLAGS) $(SHARED) -I$(LUA_LIB_SRC)/lua-zset -I$(SKYNET_PATH)/skynet-src $^ -o $@

clean :
	rm -f $(LUA_CLIB_PATH)/*.so $(CSERVICE_PATH)/*.so

cleanall :
	rm -f $(LUA_CLIB_PATH)/*.so $(CSERVICE_PATH)/*.so
	cd $(SKYNET_PATH) && $(MAKE) cleanall
