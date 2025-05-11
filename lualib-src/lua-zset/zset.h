#ifndef _SORTEDSET_H
#define _SORTEDSET_H

#include <stdlib.h>

#define ZSKIPLIST_MAXLEVEL 64 /* Should be enough for 2^64 elements */
#define ZSKIPLIST_P 0.25      /* Skiplist P = 1/4 */
typedef struct sds {
	char *ptr;
	size_t length;
} sds;

typedef struct zskiplistNode {
	sds* obj;
	double score;
	struct zskiplistNode *backward;
	struct skiplistLevel {
		struct zskiplistNode *forward;
		unsigned int span;
	}level[];
} zskiplistNode;

typedef struct zskiplist {
	struct zskiplistNode *header, *tail;
	unsigned long length;
	int level;
} zskiplist;

typedef void (*zslDeleteCallback) (void *ud, sds *obj);
sds* zslCreateSds(const char* ptr, size_t length);
void zslFreeSds(sds *obj);

zskiplist *zslCreate(void);
void zslFree(zskiplist *sl);

int zslInsert(zskiplist *sl, double score, sds *obj);
int zslDelete(zskiplist *sl, double score, sds *obj);
unsigned long zslDeleteByRank(zskiplist *sl, unsigned int start, unsigned int end, zslDeleteCallback cb, void* ud);

unsigned long zslGetRank(zskiplist *sl, double score, sds *o);
zskiplistNode* zslGetNodeByRank(zskiplist *sl, unsigned long rank);

zskiplistNode *zslFirstInRange(zskiplist *sl, double min, double max);
zskiplistNode *zslLastInRange(zskiplist *sl, double min, double max);

#endif	/*end ifndef*/
