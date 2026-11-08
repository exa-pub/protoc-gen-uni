/* Package sort, ported from go1.25.0 src/sort/zsortfunc.go, sort.go and slice.go: sort.Slice
 * (pattern-defeating quicksort, not stable) over a slice of pointers. Copyright 2009 The Go
 * Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go). The same in go1.27.1. */

typedef struct {
	void **v;
	bool (*less)(const void *a, const void *b, void *ctx);
	void *ctx;
} sort_lessSwap;

static bool sort_Less(sort_lessSwap d, int i, int j) { return d.less(d.v[i], d.v[j], d.ctx); }

static void sort_Swap(sort_lessSwap d, int i, int j)
{
	void *t = d.v[i];
	d.v[i] = d.v[j];
	d.v[j] = t;
}

enum { sort_unknownHint, sort_increasingHint, sort_decreasingHint };

static int sort_bitsLen(unsigned long long x)
{
	int n = 0;
	while (x) {
		n++;
		x >>= 1;
	}
	return n;
}

/* insertionSort_func sorts data[a:b] using insertion sort. */
static void sort_insertionSort(sort_lessSwap data, int a, int b)
{
	for (int i = a + 1; i < b; i++)
		for (int j = i; j > a && sort_Less(data, j, j - 1); j--)
			sort_Swap(data, j, j - 1);
}

/* siftDown_func implements the heap property on data[lo:hi]. */
static void sort_siftDown(sort_lessSwap data, int lo, int hi, int first)
{
	int root = lo;
	for (;;) {
		int child = 2 * root + 1;
		if (child >= hi)
			break;
		if (child + 1 < hi && sort_Less(data, first + child, first + child + 1))
			child++;
		if (!sort_Less(data, first + root, first + child))
			return;
		sort_Swap(data, first + root, first + child);
		root = child;
	}
}

static void sort_heapSort(sort_lessSwap data, int a, int b)
{
	int first = a, lo = 0, hi = b - a;
	/* Build heap with greatest element at top. */
	for (int i = (hi - 1) / 2; i >= 0; i--)
		sort_siftDown(data, i, hi, first);
	/* Pop elements, largest first, into end of data. */
	for (int i = hi - 1; i >= 0; i--) {
		sort_Swap(data, first, first + i);
		sort_siftDown(data, lo, i, first);
	}
}

static int sort_partition(sort_lessSwap data, int a, int b, int pivot, bool *alreadyPartitioned)
{
	sort_Swap(data, a, pivot);
	int i = a + 1, j = b - 1; /* i and j are inclusive of the elements remaining to be partitioned */
	while (i <= j && sort_Less(data, i, a))
		i++;
	while (i <= j && !sort_Less(data, j, a))
		j--;
	if (i > j) {
		sort_Swap(data, j, a);
		*alreadyPartitioned = true;
		return j;
	}
	sort_Swap(data, i, j);
	i++;
	j--;
	for (;;) {
		while (i <= j && sort_Less(data, i, a))
			i++;
		while (i <= j && !sort_Less(data, j, a))
			j--;
		if (i > j)
			break;
		sort_Swap(data, i, j);
		i++;
		j--;
	}
	sort_Swap(data, j, a);
	*alreadyPartitioned = false;
	return j;
}

static int sort_partitionEqual(sort_lessSwap data, int a, int b, int pivot)
{
	sort_Swap(data, a, pivot);
	int i = a + 1, j = b - 1;
	for (;;) {
		while (i <= j && !sort_Less(data, a, i))
			i++;
		while (i <= j && sort_Less(data, a, j))
			j--;
		if (i > j)
			break;
		sort_Swap(data, i, j);
		i++;
		j--;
	}
	return i;
}

static bool sort_partialInsertionSort(sort_lessSwap data, int a, int b)
{
	enum { maxSteps = 5, shortestShifting = 50 };
	int i = a + 1;
	for (int j = 0; j < maxSteps; j++) {
		while (i < b && !sort_Less(data, i, i - 1))
			i++;
		if (i == b)
			return true;
		if (b - a < shortestShifting)
			return false;
		sort_Swap(data, i, i - 1);
		/* Shift the smaller one to the left. */
		if (i - a >= 2) {
			for (int k = i - 1; k >= 1; k--) {
				if (!sort_Less(data, k, k - 1))
					break;
				sort_Swap(data, k, k - 1);
			}
		}
		/* Shift the greater one to the right. */
		if (b - i >= 2) {
			for (int k = i + 1; k < b; k++) {
				if (!sort_Less(data, k, k - 1))
					break;
				sort_Swap(data, k, k - 1);
			}
		}
	}
	return false;
}

static void sort_breakPatterns(sort_lessSwap data, int a, int b)
{
	int length = b - a;
	if (length >= 8) {
		uint64_t random = (uint64_t)length; /* xorshift(length) */
		unsigned long long modulus = 1ULL << sort_bitsLen((unsigned long long)length);
		for (int idx = a + (length / 4) * 2 - 1; idx <= a + (length / 4) * 2 + 1; idx++) {
			random ^= random << 13;
			random ^= random >> 7;
			random ^= random << 17;
			int other = (int)((unsigned long long)random & (modulus - 1));
			if (other >= length)
				other -= length;
			sort_Swap(data, idx, a + other);
		}
	}
}

static void sort_order2(sort_lessSwap data, int *a, int *b, int *swaps)
{
	if (sort_Less(data, *b, *a)) {
		(*swaps)++;
		int t = *a;
		*a = *b;
		*b = t;
	}
}

static int sort_median(sort_lessSwap data, int a, int b, int c, int *swaps)
{
	sort_order2(data, &a, &b, swaps);
	sort_order2(data, &b, &c, swaps);
	sort_order2(data, &a, &b, swaps);
	return b;
}

static int sort_medianAdjacent(sort_lessSwap data, int a, int *swaps) { return sort_median(data, a - 1, a, a + 1, swaps); }

static int sort_choosePivot(sort_lessSwap data, int a, int b, int *hint)
{
	enum { shortestNinther = 50, maxSwaps = 4 * 3 };
	int l = b - a;
	int swaps = 0, i = a + l / 4 * 1, j = a + l / 4 * 2, k = a + l / 4 * 3;
	if (l >= 8) {
		if (l >= shortestNinther) {
			/* Tukey ninther method, the idea came from Rust's implementation. */
			i = sort_medianAdjacent(data, i, &swaps);
			j = sort_medianAdjacent(data, j, &swaps);
			k = sort_medianAdjacent(data, k, &swaps);
		}
		/* Find the median among i, j, k and stores it into j. */
		j = sort_median(data, i, j, k, &swaps);
	}
	switch (swaps) {
	case 0:
		*hint = sort_increasingHint;
		break;
	case maxSwaps:
		*hint = sort_decreasingHint;
		break;
	default:
		*hint = sort_unknownHint;
	}
	return j;
}

static void sort_reverseRange(sort_lessSwap data, int a, int b)
{
	for (int i = a, j = b - 1; i < j; i++, j--)
		sort_Swap(data, i, j);
}

/* pdqsort_func sorts data[a:b]. */
static void sort_pdqsort(sort_lessSwap data, int a, int b, int limit)
{
	enum { maxInsertion = 12 };
	bool wasBalanced = true, wasPartitioned = true;
	for (;;) {
		int length = b - a;
		if (length <= maxInsertion) {
			sort_insertionSort(data, a, b);
			return;
		}
		/* Fall back to heapsort if too many bad choices were made. */
		if (limit == 0) {
			sort_heapSort(data, a, b);
			return;
		}
		/* If the last partitioning was imbalanced, we need to breakPatterns. */
		if (!wasBalanced) {
			sort_breakPatterns(data, a, b);
			limit--;
		}
		int hint;
		int pivot = sort_choosePivot(data, a, b, &hint);
		if (hint == sort_decreasingHint) {
			sort_reverseRange(data, a, b);
			/* The chosen pivot was pivot-a elements after the start of the array. After
			 * reversing it is pivot-a elements before the end of the array. The idea came
			 * from Rust's implementation. */
			pivot = (b - 1) - (pivot - a);
			hint = sort_increasingHint;
		}
		/* The slice is likely already sorted. */
		if (wasBalanced && wasPartitioned && hint == sort_increasingHint) {
			if (sort_partialInsertionSort(data, a, b))
				return;
		}
		/* Probably the slice contains many duplicate elements, partition the slice into
		 * elements equal to and elements greater than the pivot. */
		if (a > 0 && !sort_Less(data, a - 1, pivot)) {
			a = sort_partitionEqual(data, a, b, pivot);
			continue;
		}
		bool alreadyPartitioned;
		int mid = sort_partition(data, a, b, pivot, &alreadyPartitioned);
		wasPartitioned = alreadyPartitioned;
		int leftLen = mid - a, rightLen = b - mid;
		int balanceThreshold = length / 8;
		if (leftLen < rightLen) {
			wasBalanced = leftLen >= balanceThreshold;
			sort_pdqsort(data, a, mid, limit);
			a = mid + 1;
		} else {
			wasBalanced = rightLen >= balanceThreshold;
			sort_pdqsort(data, mid + 1, b, limit);
			b = mid;
		}
	}
}
