
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "skynet.h"

#include "zset.h"

zskiplistNode *zslCreateNode(int level, double score, sds *obj) {
	zskiplistNode *n = skynet_malloc(sizeof(*n) + level * sizeof(struct skiplistLevel));
	if (!n) return NULL;
	n->score = score;
	n->obj = obj;
	return n;
}

zskiplist *zslCreate(void) {
	int j;
	zskiplist *sl;

	sl = skynet_malloc(sizeof(*sl));
	if (!sl) return NULL;
	sl->level = 1;
	sl->length = 0;
	sl->header = zslCreateNode(ZSKIPLIST_MAXLEVEL, 0, NULL);
	if (!sl->header) {
		skynet_free(sl);
		return NULL;
	}
	for (j=0; j < ZSKIPLIST_MAXLEVEL; j++) {
		sl->header->level[j].forward = NULL;
		sl->header->level[j].span = 0;
	}
	sl->header->backward = NULL;
	sl->tail = NULL;
	return sl;
}

sds* zslCreateSds(const char* ptr, size_t length) {
	sds *obj = skynet_malloc(sizeof(*obj));
	obj->ptr = skynet_malloc(length + 1);

	if(ptr) {
		memcpy(obj->ptr, ptr, length);
	}
	obj->ptr[length] = '\0';

	obj->length = length;
	return obj;
}

void zslFreeSds(sds *obj) {
	skynet_free(obj->ptr);
	skynet_free(obj);
}

void zslFreeNode(zskiplistNode *node) {
	zslFreeSds(node->obj);
	skynet_free(node);
}

void zslFree(zskiplist *sl) {
	zskiplistNode *node = sl->header->level[0].forward, *next;

	skynet_free(sl->header);
	while(node) {
		next = node->level[0].forward;
		zslFreeNode(node);
		node = next;
	}
	skynet_free(sl);
}

int zslRandomLevel(void) {
	int level = 1;
	while((random() & 0xffff) < (ZSKIPLIST_P * 0xffff))
		level += 1;
	return (level < ZSKIPLIST_MAXLEVEL) ? level : ZSKIPLIST_MAXLEVEL;
}

int comparesds(sds *a, sds *b) {
	int cmp = memcmp(a->ptr, b->ptr, a->length <= b->length ? a->length : b->length);
	if(cmp == 0) return a->length - b->length;
	return cmp;
}

int equalsds(sds *a, sds *b) {
	return comparesds(a, b) == 0;
}

int zslInsert(zskiplist *sl, double score, sds *obj) {
	zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
	unsigned int rank[ZSKIPLIST_MAXLEVEL];
	int i, level;

	x = sl->header;
	for (i = sl->level-1; i >= 0; i--) {
		/* store rank that is crossed to reach the insert position */
		rank[i] = i == (sl->level-1) ? 0 : rank[i+1];
		while (x->level[i].forward &&
			(x->level[i].forward->score < score ||
				(x->level[i].forward->score == score &&
				comparesds(x->level[i].forward->obj,obj) < 0))) {
			rank[i] += x->level[i].span;
			x = x->level[i].forward;
		}
		update[i] = x;
	}
	/* we assume the key is not already inside, since we allow duplicated
	 * scores, and the re-insertion of score and redis object should never
	 * happen since the caller of zslInsert() should test in the hash table
	 * if the element is already inside or not. */
	level = zslRandomLevel();
	if (level > sl->level) {
		for (i = sl->level; i < level; i++) {
			rank[i] = 0;
			update[i] = sl->header;
			update[i]->level[i].span = sl->length;
		}
		sl->level = level;
	}
	x = zslCreateNode(level,score,obj);
	if (!x) return 0;
	for (i = 0; i < level; i++) {
		x->level[i].forward = update[i]->level[i].forward;
		update[i]->level[i].forward = x;

		/* update span covered by update[i] as x is inserted here */
		x->level[i].span = update[i]->level[i].span - (rank[0] - rank[i]);
		update[i]->level[i].span = (rank[0] - rank[i]) + 1;
	}

	/* increment span for untouched levels */
	for (i = level; i < sl->level; i++) {
		update[i]->level[i].span++;
	}

	x->backward = (update[0] == sl->header) ? NULL : update[0];
	if (x->level[0].forward)
		x->level[0].forward->backward = x;
	else
		sl->tail = x;
	sl->length++;
	return 1;
}

/* Internal function used by zslDelete, slDeleteByScore */
void zslDeleteNode(zskiplist *sl, zskiplistNode *x, zskiplistNode **update) {
	int i;
	for (i = 0; i < sl->level; i++) {
		if (update[i]->level[i].forward == x) {
			update[i]->level[i].span += x->level[i].span - 1;
			update[i]->level[i].forward = x->level[i].forward;
		} else {
			update[i]->level[i].span -= 1;
		}
	}
	if (x->level[0].forward) {
		x->level[0].forward->backward = x->backward;
	} else {
		sl->tail = x->backward;
	}
	while(sl->level > 1 && sl->header->level[sl->level-1].forward == NULL)
		sl->level--;
	sl->length--;
}

/* Delete an element with matching score/object from the zskiplist. */
int zslDelete(zskiplist *sl, double score, sds *obj) {
	zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
	int i;

	x = sl->header;
	for (i = sl->level-1; i >= 0; i--) {
		while (x->level[i].forward &&
			(x->level[i].forward->score < score ||
				(x->level[i].forward->score == score &&
				comparesds(x->level[i].forward->obj,obj) < 0)))
			x = x->level[i].forward;
		update[i] = x;
	}
	/* We may have multiple elements with the same score, what we need
	 * is to find the element with both the right score and object. */
	x = x->level[0].forward;
	if (x && score == x->score && equalsds(x->obj,obj)) {
		zslDeleteNode(sl, x, update);
		zslFreeNode(x);
		return 1;
	} else {
		return 0; /* not found */
	}
	return 0; /* not found */
}

/* Delete all the elements with rank between start and end from the zskiplist.
 * Start and end are inclusive. Note that start and end need to be 1-based */
unsigned long zslDeleteByRank(zskiplist *sl, unsigned int start, unsigned int end, zslDeleteCallback cb, void* ud) {
	zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
	unsigned long traversed = 0, removed = 0;
	int i;

	x = sl->header;
	for (i = sl->level-1; i >= 0; i--) {
		while (x->level[i].forward && (traversed + x->level[i].span) < start) {
			traversed += x->level[i].span;
			x = x->level[i].forward;
		}
		update[i] = x;
	}

	traversed++;
	x = x->level[0].forward;
	while (x && traversed <= end) {
		zskiplistNode *next = x->level[0].forward;
		zslDeleteNode(sl,x,update);
		cb(ud, x->obj);
		zslFreeNode(x);
		removed++;
		traversed++;
		x = next;
	}
	return removed;
}

/* Find the rank for an element by both score and key.
 * Returns 0 when the element cannot be found, rank otherwise.
 * Note that the rank is 1-based due to the span of sl->header to the
 * first element. */
unsigned long zslGetRank(zskiplist *sl, double score, sds *o) {
	zskiplistNode *x;
	unsigned long rank = 0;
	int i;

	x = sl->header;
	for (i = sl->level-1; i >= 0; i--) {
		while (x->level[i].forward &&
			(x->level[i].forward->score < score ||
				(x->level[i].forward->score == score &&
				comparesds(x->level[i].forward->obj,o) <= 0))) {
			rank += x->level[i].span;
			x = x->level[i].forward;
		}

		/* x might be equal to sl->header, so test if obj is non-NULL */
		if (x->obj && equalsds(x->obj, o)) {
			return rank;
		}
	}
	return 0;
}

/* Finds an element by its rank. The rank argument needs to be 1-based. */
zskiplistNode* zslGetNodeByRank(zskiplist *sl, unsigned long rank) {
	if(rank == 0 || rank > sl->length) {
		return NULL;
	}

	zskiplistNode *x;
	unsigned long traversed = 0;
	int i;

	x = sl->header;
	for (i = sl->level-1; i >= 0; i--) {
		while (x->level[i].forward && (traversed + x->level[i].span) <= rank)
		{
			traversed += x->level[i].span;
			x = x->level[i].forward;
		}
		if (traversed == rank) {
			return x;
		}
	}

	return NULL;
}

/* range [min, max], left & right both include */
/* Returns if there is a part of the zset is in range. */
int zslIsInRange(zskiplist *sl, double min, double max) {
	zskiplistNode *x;

	/* Test for ranges that will always be empty. */
	if(min > max) {
		return 0;
	}
	x = sl->tail;
	if (x == NULL || x->score < min)
		return 0;

	x = sl->header->level[0].forward;
	if (x == NULL || x->score > max)
		return 0;
	return 1;
}

/* Find the first node that is contained in the specified range.
 * Returns NULL when no element is contained in the range. */
zskiplistNode *zslFirstInRange(zskiplist *sl, double min, double max) {
	zskiplistNode *x;
	int i;

	/* If everything is out of range, return early. */
	if (!zslIsInRange(sl,min, max)) return NULL;

	x = sl->header;
	for (i = sl->level-1; i >= 0; i--) {
		/* Go forward while *OUT* of range. */
		while (x->level[i].forward && x->level[i].forward->score < min)
				x = x->level[i].forward;
	}

	/* This is an inner range, so the next node cannot be NULL. */
	x = x->level[0].forward;
	return x;
}

/* Find the last node that is contained in the specified range.
 * Returns NULL when no element is contained in the range. */
zskiplistNode *zslLastInRange(zskiplist *sl, double min, double max) {
	zskiplistNode *x;
	int i;

	/* If everything is out of range, return early. */
	if (!zslIsInRange(sl, min, max)) return NULL;

	x = sl->header;
	for (i = sl->level-1; i >= 0; i--) {
		/* Go forward while *IN* range. */
		while (x->level[i].forward &&
			x->level[i].forward->score <= max)
				x = x->level[i].forward;
	}

	/* This is an inner range, so this node cannot be NULL. */
	return x;
}
