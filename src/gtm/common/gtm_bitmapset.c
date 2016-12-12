/*-------------------------------------------------------------------------
 *
 * gtm_bitmapset.c
 * bitmapset of GTM
 *
 * Portions Copyright (c) 1996-2010, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 * Portions Copyright (c) 2010-2012 Postgres-XC Development Group
 *
 *
 * IDENTIFICATION
 *      src/gtm/common/gtm_bitmapset.c
 *
 *-------------------------------------------------------------------------
 */

#include "gtm/gtm.h"
#include "gtm/elog.h"
#include "gtm/gtm_bitmapset.h"
#include "gtm/memutils.h"

#define GTM_WORDNUM(x)  ((x) / GTM_BITS_PER_BITMAPWORD)
#define GTM_BITNUM(x)   ((x) % GTM_BITS_PER_BITMAPWORD)

#define GTM_BITMAPSET_SIZE(nwords)	\
	(offsetof(gtm_Bitmapset, words) + (nwords) * sizeof(gtm_bitmapword))

#define GTM_RIGHTMOST_ONE(x) ((gtm_signedbitmapword) (x) & -((gtm_signedbitmapword) (x)))

static const uint8 gtm_rightmost_one_pos[256] = {
	0, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
	4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
	5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
	4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
	6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
	4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
	5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
	4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
	7, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
	4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
	5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
	4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
	6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
	4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
	5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
	4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0
};

static const uint8 gtm_number_of_ones[256] = {
	0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
	1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
	1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
	1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
	4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8
};

/*
 *  * bms_copy - make a palloc'd copy of a bitmapset
 *   */
gtm_Bitmapset *
gtm_bms_copy(const gtm_Bitmapset *a)
{
	gtm_Bitmapset  *result;
	size_t		size;

	if (a == NULL)
		return NULL;
	size = GTM_BITMAPSET_SIZE(a->nwords);
	result = (gtm_Bitmapset *) palloc(size);
	memcpy(result, a, size);
	return result;
}


/*
 * gtm_bms_make_singleton - build a bitmapset containing a single member
 */
gtm_Bitmapset *
gtm_bms_make_singleton(int x)
{
	gtm_Bitmapset  *result;
	int			wordnum,
				bitnum;

	if (x < 0)
		elog(ERROR, "negative bitmapset member not allowed");
	wordnum = GTM_WORDNUM(x);
	bitnum = GTM_BITNUM(x);
	result = (gtm_Bitmapset *) palloc0(GTM_BITMAPSET_SIZE(wordnum + 1));
	result->nwords = wordnum + 1;
	result->words[wordnum] = ((gtm_bitmapword) 1 << bitnum);
	return result;
}

/*
 * gtm_bms_free - free a bitmapset
 *
 * Same as pfree except for allowing NULL input
 */
void
gtm_bms_free(gtm_Bitmapset *a)
{
	if (a)
		pfree(a);
}

/*
 * gtm_bms_num_members - count members of set
 */
int
gtm_bms_num_members(const gtm_Bitmapset *a)
{
	int			result = 0;
	int			nwords;
	int			wordnum;

	if (a == NULL)
		return 0;
	nwords = a->nwords;
	for (wordnum = 0; wordnum < nwords; wordnum++)
	{
		gtm_bitmapword	w = a->words[wordnum];

		/* we assume here that bitmapword is an unsigned type */
		while (w != 0)
		{
			result += gtm_number_of_ones[w & 255];
			w >>= 8;
		}
	}
	return result;
}


/*
 * gtm_bms_add_member - add a specified member to set
 *
 * Input set is modified or recycled!
 */
gtm_Bitmapset *
gtm_bms_add_member(gtm_Bitmapset *a, int x)
{
	int			wordnum,
				bitnum;

	if (x < 0)
		elog(ERROR, "negative bitmapset member not allowed");
	if (a == NULL)
		return gtm_bms_make_singleton(x);
	wordnum = GTM_WORDNUM(x);
	bitnum = GTM_BITNUM(x);
	if (wordnum >= a->nwords)
	{
		/* Slow path: make a larger set and union the input set into it */
		gtm_Bitmapset  *result;
		int			nwords;
		int			i;

		result = gtm_bms_make_singleton(x);
		nwords = a->nwords;
		for (i = 0; i < nwords; i++)
			result->words[i] |= a->words[i];
		pfree(a);
		return result;
	}
	/* Fast path: x fits in existing set */
	a->words[wordnum] |= ((gtm_bitmapword) 1 << bitnum);
	return a;
}

/*
 * gtm_bms_del_member - remove a specified member from set
 *
 * No error if x is not currently a member of set
 *
 * Input set is modified in-place!
 */
gtm_Bitmapset *
gtm_bms_del_member(gtm_Bitmapset *a, int x)
{
	int			wordnum,
				bitnum;

	if (x < 0)
		elog(ERROR, "negative bitmapset member not allowed");
	if (a == NULL)
		return NULL;
	wordnum = GTM_WORDNUM(x);
	bitnum = GTM_BITNUM(x);
	if (wordnum < a->nwords)
		a->words[wordnum] &= ~((gtm_bitmapword) 1 << bitnum);
	return a;
}

/*
 * gtm_bms_is_empty - is a set empty?
 *
 */
bool
gtm_bms_is_empty(const gtm_Bitmapset *a)
{
	int         nwords;
	int         wordnum;

	if (a == NULL)
		return true;
	nwords = a->nwords;
	for (wordnum = 0; wordnum < nwords; wordnum++)
	{
		gtm_bitmapword  w = a->words[wordnum];

		if (w != 0)
			return false;
	}
	return true;
}

/*
 * gtm_bms_del_members - like bms_difference, but left input is recycled
 */
gtm_Bitmapset *
gtm_bms_del_members(gtm_Bitmapset *a, const gtm_Bitmapset *b)
{
	int			shortlen;
	int			i;

	/* Handle cases where either input is NULL */
	if (a == NULL)
		return NULL;
	if (b == NULL)
		return a;
	/* Remove b's bits from a; we need never copy */
	shortlen = Min(a->nwords, b->nwords);
	for (i = 0; i < shortlen; i++)
		a->words[i] &= ~b->words[i];
	return a;
}

/*----------
 * gtm_bms_first_member - find and remove first member of a set
 *
 * Returns -1 if set is empty.	NB: set is destructively modified!
 */
int
gtm_bms_first_member(gtm_Bitmapset *a)
{
	int			nwords;
	int			wordnum;

	if (a == NULL)
		return -1;
	nwords = a->nwords;
	for (wordnum = 0; wordnum < nwords; wordnum++)
	{
		gtm_bitmapword	w = a->words[wordnum];

		if (w != 0)
		{
			int			result;

			w = GTM_RIGHTMOST_ONE(w);
			a->words[wordnum] &= ~w;

			result = wordnum * GTM_BITS_PER_BITMAPWORD;
			while ((w & 255) == 0)
			{
				w >>= 8;
				result += 8;
			}
			result += gtm_rightmost_one_pos[w & 255];
			return result;
		}
	}
	return -1;
}

/*
 * gtm_bms_reset - reset all bits to be 0
 */
void
gtm_bms_reset(gtm_Bitmapset *a)
{
	int			i;

	if (a == NULL)
		return ;
	for (i = 0; i < a->nwords; i++)
		a->words[i] &= 0;
	return ;
}

