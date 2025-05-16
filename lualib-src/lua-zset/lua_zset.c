
#include <stdio.h>
#include <stdlib.h>
#include <string.h> /* memset */
#include <unistd.h> /* close */

#include "lua.h"
#include "lauxlib.h"

#include "skynet.h"
#include "zset.h"
#include "spinlock.h"
#define skynet_free free

struct zset {
	zskiplist * zsl;
	int ref;
	struct spinlock lock;
};

struct ud_zset {
	struct zset * zs;
};

static struct skynet_context *
get_skynet_context(lua_State *L) {
	lua_getfield(L, LUA_REGISTRYINDEX, "skynet_context");
	struct skynet_context *ctx = lua_touserdata(L, -1);
	lua_pop(L, 1);
	return ctx;
}

static struct zset *
zset_new(lua_State *L) {
	struct zset *zs = skynet_malloc(sizeof(*zs));
	if (zs == NULL) {
		luaL_error(L, "zset malloc failed");
		return NULL;
	}
	memset(zs, 0, sizeof(*zs));
	zskiplist *zsl = zslCreate();
	if (zsl == NULL) {
		luaL_error(L, "zsl malloc failed");
		skynet_free(zs);
		return NULL;
	}
	zs->zsl = zsl;
	zs->ref = 1;
	SPIN_INIT(zs)
	return zs;
}

static struct zset *
zset_clone(struct zset *zs) {
	struct zset *clone = NULL;
	SPIN_LOCK(zs)
	++zs->ref;
	clone = zs;
	SPIN_UNLOCK(zs)
	return clone;
}

static struct zset *
zset_release(lua_State *L, struct zset *zs) {
	struct skynet_context *ctx = get_skynet_context(L);
	SPIN_LOCK(zs)
	--zs->ref;
	if (zs->ref > 0) {
		skynet_error(ctx, "collect zs:%p, ref:%d, uncompletely", zs, zs->ref);
		SPIN_UNLOCK(zs)
		return zs;
	}
	skynet_error(ctx, "collect zs:%p, ref:%d, completely", zs, zs->ref);
	zslFree(zs->zsl);
	SPIN_UNLOCK(zs)
	SPIN_DESTROY(zs)
	skynet_free(zs);
	return NULL;
}

static inline struct ud_zset *
get_userdata(lua_State *L) {
	return (struct ud_zset *)luaL_checkudata(L, 1, "mk.zset");
}

static int 
lupdate_score(lua_State *L) {
	struct ud_zset *uzs = get_userdata(L);
	struct zset *zs = uzs->zs;
	luaL_checktype(L, 2, LUA_TSTRING);

	size_t strlen = 0;
	const char* ptr = lua_tolstring(L, 2, &strlen);
	sds *obj = zslCreateSds(ptr, strlen);

	double oldScore = luaL_checknumber(L, 3);
	double score = luaL_checknumber(L, 4);

	struct skynet_context *ctx = get_skynet_context(L);
	zslUpdateScore(zs->zsl, oldScore, obj, score, ctx);

	lua_pushboolean(L, 1);
	return 1;
}

static int
lupdate(lua_State *L) {
	struct ud_zset *uzs = get_userdata(L);
	struct zset *zs = uzs->zs;
	size_t len, i;
	// delete
	luaL_checktype(L, 2, LUA_TTABLE);
	len = lua_rawlen(L, 2);
	i = 1;
	for (; i <= len; i+=2) {
		lua_rawgeti(L, 2, i);
		double score = luaL_checknumber(L, -1);
		lua_pop(L, 1);
		lua_rawgeti(L, 2, i+1);
		sds obj;
		obj.ptr = (char *)lua_tolstring(L, -1, &obj.length);
		lua_pop(L, 1);
		zslDelete(zs->zsl, score, &obj);
	}

	// add
	struct skynet_context *ctx = get_skynet_context(L);
	luaL_checktype(L, 3, LUA_TTABLE);
	len = lua_rawlen(L, 3);
	i = 1;
	size_t strlen = 0;
	for (; i <= len; i+=2) {
		lua_rawgeti(L, 3, i);
		double score = luaL_checknumber(L, -1);
		lua_pop(L, 1);

		lua_rawgeti(L, 3, i+1);
		const char* ptr = lua_tolstring(L, -1, &strlen);
		sds *obj = zslCreateSds(ptr, strlen);
		if (!obj) {
			lua_pop(L, 1);
			skynet_error(ctx, "update add sds malloc failed");
			lua_pushboolean(L, 0);
			return 1;
		}
		lua_pop(L, 1);
		if (zslInsert(zs->zsl, score, obj) == 0) {
			skynet_error(ctx, "update add node malloc failed");
			lua_pushboolean(L, 0);
			return 1;
		}
	}
	lua_pushboolean(L, 1);
	return 1;
}

static void
_delete_rank_cb(void* ud, sds *obj) {
	lua_State *L = (lua_State*)ud;
	lua_pushvalue(L, 4);
	lua_pushlstring(L, obj->ptr, obj->length);
	lua_call(L, 1, 0);
}

static int
ldelete_by_rank(lua_State *L) {
	struct ud_zset *uzs = get_userdata(L);
	struct zset *zs = uzs->zs;
	unsigned int start = luaL_checkinteger(L, 2);
	unsigned int end = luaL_checkinteger(L, 3);
	luaL_checktype(L, 4, LUA_TFUNCTION);
	if (start > end) {
		unsigned int tmp = start;
		start = end;
		end = tmp;
	}

	lua_pushinteger(L, zslDeleteByRank(zs->zsl, start, end, _delete_rank_cb, L));
	return 1;
}

static int
lget_count(lua_State *L) {
	struct ud_zset *uzs = get_userdata(L);
	struct zset *zs = uzs->zs;
	lua_pushinteger(L, zs->zsl->length);
	return 1;
}

static int
lget_rank(lua_State *L) {
	struct ud_zset *uzs = get_userdata(L);
	struct zset *zs = uzs->zs;
	double score = luaL_checknumber(L, 2);
	luaL_checktype(L, 3, LUA_TSTRING);
	sds obj;
	obj.ptr = (char *)lua_tolstring(L, 3, &obj.length);

	unsigned long rank = zslGetRank(zs->zsl, score, &obj);
	if(rank == 0) {
		return 0;
	}

	lua_pushinteger(L, rank);

	return 1;
}

static int
lget_rank_range(lua_State *L) {
	struct ud_zset *uzs = get_userdata(L);
	struct zset *zs = uzs->zs;
	unsigned long r1 = luaL_checkinteger(L, 2);
	unsigned long r2 = luaL_checkinteger(L, 3);
	int reverse, rangelen;
	if(r1 <= r2) {
		reverse = 0;
		rangelen = r2 - r1 + 1;
	} else {
		reverse = 1;
		rangelen = r1 - r2 + 1;
	}

	zskiplistNode* node = zslGetNodeByRank(zs->zsl, r1);
	lua_createtable(L, rangelen, 0);
	int n = 0;
	while(node && n < rangelen) {
		n++;

		lua_pushlstring(L, node->obj->ptr, node->obj->length);
		lua_rawseti(L, -2, n);
		node = reverse ? node->backward : node->level[0].forward;
	} 
	return 1;
}

static int
lget_score_range(lua_State *L) {
	struct ud_zset *uzs = get_userdata(L);
	struct zset *zs = uzs->zs;
	double s1 = luaL_checknumber(L, 2);
	double s2 = luaL_checknumber(L, 3);
	int reverse; 
	zskiplistNode *node;

	if(s1 <= s2) {
		reverse = 0;
		node = zslFirstInRange(zs->zsl, s1, s2);
	} else {
		reverse = 1;
		node = zslLastInRange(zs->zsl, s2, s1);
	}

	lua_newtable(L);
	int n = 0;
	while(node) {
		if(reverse) {
			if(node->score < s2) break;
		} else {
			if(node->score > s2) break;
		}
		n++;

		lua_pushlstring(L, node->obj->ptr, node->obj->length);
		lua_rawseti(L, -2, n);

		node = reverse? node->backward:node->level[0].forward;
	}
	return 1;
}

static int
ldump(lua_State *L) {
	struct ud_zset *uzs = get_userdata(L);
	struct zset *zs = uzs->zs;
	zskiplistNode *x;
	int i;
	x = zs->zsl->header;
	i = 0;
	struct skynet_context *ctx = get_skynet_context(L);
	while(x->level[0].forward) {
		x = x->level[0].forward;
		i++;
		skynet_error(ctx, "node %d: score:%f, member:%s", i, x->score, x->obj->ptr);
	}
	return 0;
}

static int
lget_zset_data(lua_State *L) {
	struct ud_zset *uzs = get_userdata(L);
	struct zset *zs = uzs->zs;
	zskiplistNode *x;
	x = zs->zsl->header;
	lua_createtable(L, 0, zs->zsl->length);   //创建空表压栈
	int i = 1;
	while(x->level[0].forward) {
		x = x->level[0].forward;
		lua_pushlstring(L, x->obj->ptr, x->obj->length);
		lua_rawseti(L, -2, i);
		lua_pushnumber(L, x->score);
		lua_rawseti(L, -2, i+1);
		i+=2;
	}
	return 1;
}

// static int
// lget_zset_data_1(lua_State *L, struct zset *zs) {
// 	zskiplistNode *x;
// 	x = zs->zsl->header;
// 	lua_createtable(L, 0, zs->zsl->length);   //创建空表压栈
// 	while(x->level[0].forward) {
// 		x = x->level[0].forward;
// 		lua_pushlstring(L, x->obj->ptr, x->obj->length);
// 		lua_pushnumber(L, x->score);
// 		lua_rawset(L, -3);
// 	}
// 	return 1;
// }

static int
lrelease(lua_State *L) {
	struct ud_zset *uzs = get_userdata(L);
	if (uzs && zset_release(L, uzs->zs) == NULL) {
		uzs->zs = NULL;
	}
	return 0;
}

static void
zset_metatable(lua_State *L) {
	if (luaL_newmetatable(L, "mk.zset")) { //从注册表中查询键"mk.zset",为用户数据的元表创建一张新表。 向这张表加入 __name = tname 键值对， 并将 [tname] = new table 添加到注册表中， 返回 1
		luaL_Reg m[] = {
			{"update", lupdate},
			{"update_score", lupdate_score},
			{"delete_by_rank", ldelete_by_rank},

			{"get_count", lget_count},
			{"get_rank", lget_rank},
			{"get_rank_range", lget_rank_range},
			{"get_score_range", lget_score_range},
			{"get_zset_data", lget_zset_data},
			// {"get_zset_data_1", lget_zset_data_1},

			{"dump", ldump},
			{NULL, NULL},
		};
		luaL_newlib(L, m);         //创建一张表，并把列表 m 中的函数注册进去
		lua_setfield(L, -2, "__index");  //给索引-2的userdata q设置元方法__index，当索引表的key不是表或者不存在时，触发__index方法
		lua_pushcfunction(L, lrelease);   //压入一个c function 
		lua_setfield(L, -2, "__gc");    //当元表的gc方法触发时，会调用lrelease
	}
}

static int
lnew(lua_State *L) {
	struct skynet_context *ctx = get_skynet_context(L);
	if (!lua_isnoneornil(L, 1)) { 			//如果是多个lua服务共享zset，新创建的lua服务需要向已创建zset的lua服务获取lud传递过来
		skynet_error(ctx, "new1");
		struct zset * zs = lua_touserdata(L, 1);  //获取lua传过来的lud
		if (zs == NULL) {
			return luaL_error(L, "need struct zset * lightuserdata");
		}
		struct ud_zset *q = (struct ud_zset*)lua_newuserdata(L, sizeof(struct ud_zset));  //创建一个userdata q并压栈
		q->zs = zset_clone(zs);  
		lua_pushlightuserdata(L, zs);   //压入lightuserdata
		zset_metatable(L);            //生成一张表并压栈
		lua_setmetatable(L, -3);      //把zset_metatable()生成的表弹栈，并将其设为userdata q的元表
		// lget_zset_data_1(L, zs);
		return 3;
	}
	skynet_error(ctx, "new2");
	struct ud_zset *q = (struct ud_zset*)lua_newuserdata(L, sizeof(struct ud_zset)); //创建一个userdata q并压栈
	struct zset * zs = zset_new(L);
	q->zs = zs;   //单服务使用zset时不需要加锁
	lua_pushlightuserdata(L, zs);
	zset_metatable(L);
	lua_setmetatable(L, -3);
	return 2;
}

int 
luaopen_zset_core(lua_State *L) {
	luaL_Reg l[] = {
		{"new", lnew},
		{NULL, NULL},
	};
	luaL_newlib(L, l);
	return 1;
}
