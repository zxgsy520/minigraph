#include "mgpriv.h"
#include "ksort.h"

typedef struct {
	uint32_t srt;
	int32_t i;
} gc_frag_t;

#define gc_frag_key(p) ((p).srt)
KRADIX_SORT_INIT(gc, gc_frag_t, gc_frag_key, 4)

static int32_t find_max(int32_t n, const gc_frag_t *gf, int32_t x)
{
	int32_t s = 0, e = n;
	if (n == 0) return -1;
	if (gf[n-1].srt < x) return n - 1;
	if (gf[0].srt >= x) return -1;
	while (e > s) { // TODO: finish this block
		int32_t m = s + (e - s) / 2;
		if (gf[m].srt >= x) e = m;
		else s = m + 1;
	}
	assert(s == e);
	return s;
}

int32_t mg_gchain1(void *km, const gfa_t *g, int32_t n_frag, mg_gfrag_t *frag, int32_t qlen, int32_t max_dist_g, int32_t max_dist_q, int32_t bw, uint64_t **gc_)
{
	int32_t i, j, m_dst, n_dst, n_ext, j_st, n_u, n_v;
	int32_t *f, *p, *v, *t;
	uint64_t *u;
	gfa_path_dst_t *dst;
	gc_frag_t *a;

	a = KMALLOC(km, gc_frag_t, n_frag);
	for (i = n_ext = 0; i < n_frag; ++i) {
		mg_gfrag_t *r = &frag[i];
		gc_frag_t *gi = &a[i];
		int32_t is_isolated = 0;
		if (r->rs > max_dist_g && g->seg[r->v>>1].len - r->re > max_dist_g)
			is_isolated = 1;
		gi->srt = (uint32_t)is_isolated<<31 | r->qe;
		gi->i = i;
		if (!is_isolated) ++n_ext;
	}
	if (n_ext < 2) { // no graph chaining needed
		kfree(km, a);
		return n_frag;
	}
	radix_sort_gc(a, a + n_frag);

	f = KMALLOC(km, int32_t, n_ext);
	p = KMALLOC(km, int32_t, n_ext);
	t = KCALLOC(km, int32_t, n_ext);
	v = KMALLOC(km, int32_t, n_ext);

	m_dst = n_dst = 0, dst = 0;
	for (i = 0, j_st = 0; i < n_ext; ++i) {
		gc_frag_t *gi = &a[i];
		mg_gfrag_t *fi = &frag[gi->i];
		int32_t max_f = fi->sc_chain;
		int32_t max_j = -1;
		int32_t x = fi->qs + bw;
		while (j_st < i && a[j_st].srt + max_dist_q < fi->qs) ++j_st;
		if (x > qlen) x = qlen;
		x = j_st + find_max(i - j_st, a + j_st, x);
		n_dst = 0;
		for (j = x; j >= j_st; --j) {
			gc_frag_t *gj = &a[j];
			mg_gfrag_t *fj = &frag[gj->i];
			gfa_path_dst_t *q;
			int32_t min_dist = fi->rs + (g->seg[fj->v>>1].len - fj->re);
			if (min_dist > max_dist_g) continue;
			if (min_dist - bw > fi->qs - fj->qe) continue; // TODO: double check this line
			if (n_dst == m_dst) KEXPAND(km, dst, m_dst); // TODO: watch out the quadratic behavior!
			q = &dst[n_dst++];
			q->v = fj->v;
			q->meta = j;
			q->target_dist = (fi->qs - fj->qe) - min_dist + g->seg[fi->v>>1].len;
			if (q->target_dist < 0) q->target_dist = 0;
		}
		gfa_shortest_k(km, g, fi->v^1, n_dst, dst, max_dist_g + (g->seg[fi->v>>1].len - fi->rs), GFA_MAX_SHORT_K, 0);
		for (j = 0; j < n_dst; ++j) {
			gfa_path_dst_t *dj = &dst[j];
			int32_t gap, log_gap, sc;
			if (dj->n_path == 0) continue;
			gap = dj->dist - dj->target_dist;
			if (gap < 0) gap = -gap;
			if (gap > bw) continue;
			log_gap = gap? mg_ilog2_32(gap) : 0;
			sc = fi->sc_chain;
			sc -= (int32_t)(gap * 0.2) + (log_gap >> 1);
			sc += f[dj->meta];
			if (sc > max_f) max_f = sc, max_j = dj->meta;
		}
		f[i] = max_f, p[i] = max_j;
		v[i] = max_j >= 0 && v[max_j] > max_f? v[max_j] : max_f;
	}

	u = mg_chain_backtrack(km, n_ext, f, p, v, t, 0, 0, n_frag - n_ext, &n_u, &n_v);
	kfree(km, f); kfree(km, p); kfree(km, t);
	assert(n_v == n_ext);
	if (n_u > 0) {
		int32_t k;
		gc_frag_t *b;
		b = KMALLOC(km, gc_frag_t, n_ext);
		for (i = 0, k = 0; i < n_u; ++i) {
			int32_t k0 = k, ni = (int32_t)u[i];
			for (j = 0; j < ni; ++j)
				b[k] = a[v[k0 + (ni - j - 1)]], ++k;
		}
		assert(k == n_v);
		memcpy(a, b, n_ext * sizeof(gc_frag_t));
	}
	kfree(km, v);
	return n_u;
}
