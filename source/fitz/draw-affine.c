#include "mupdf/fitz.h"
#include "draw-imp.h"

typedef unsigned char byte;

static inline int lerp(int a, int b, int t)
{
	return a + (((b - a) * t) >> 16);
}

static inline int bilerp(int a, int b, int c, int d, int u, int v)
{
	return lerp(lerp(a, b, u), lerp(c, d, u), v);
}

static inline const byte *sample_nearest(const byte *s, int w, int h, int str, int n, int u, int v)
{
	if (u < 0) u = 0;
	if (v < 0) v = 0;
	if (u >= (w>>16)) u = (w>>16) - 1;
	if (v >= (h>>16)) v = (h>>16) - 1;
	return s + v * str + u * n;
}

/* Blend premultiplied source image in constant alpha over destination */

static inline void
fz_paint_affine_alpha_N_lerp(byte * restrict dp, int da, const byte * restrict sp, int sw, int sh, int ss, int sa, int u, int v, int fa, int fb, int w, int n1, int alpha, byte * restrict hp)
{
	int k;

	while (w--)
	{
		if (u + 32768 >= 0 && u < sw && v + 32768 >= 0 && v < sh)
		{
			int ui = u >> 16;
			int vi = v >> 16;
			int uf = u & 0xffff;
			int vf = v & 0xffff;
			const byte *a = sample_nearest(sp, sw, sh, ss, n1+sa, ui, vi);
			const byte *b = sample_nearest(sp, sw, sh, ss, n1+sa, ui+1, vi);
			const byte *c = sample_nearest(sp, sw, sh, ss, n1+sa, ui, vi+1);
			const byte *d = sample_nearest(sp, sw, sh, ss, n1+sa, ui+1, vi+1);
			int xa = sa ? fz_mul255(bilerp(a[n1], b[n1], c[n1], d[n1], uf, vf), alpha) : alpha;
			int t;
			t = 255 - xa;
			for (k = 0; k < n1; k++)
			{
				int x = bilerp(a[k], b[k], c[k], d[k], uf, vf);
				dp[k] = fz_mul255(x, alpha) + fz_mul255(dp[k], t);
			}
			if (da)
				dp[n1] = xa + fz_mul255(dp[n1], t);
			if (hp)
				hp[0] = xa + fz_mul255(hp[0], t);
		}
		dp += n1+da;
		if (hp)
			hp++;
		u += fa;
		v += fb;
	}
}

/* Special case code for gray -> rgb */
static inline void
fz_paint_affine_alpha_g2rgb_lerp(byte * restrict dp, int da, const byte * restrict sp, int sw, int sh, int ss, int sa, int u, int v, int fa, int fb, int w, int alpha, byte * restrict hp)
{
	while (w--)
	{
		if (u + 32768 >= 0 && u < sw && v + 32768 >= 0 && v < sh)
		{
			int ui = u >> 16;
			int vi = v >> 16;
			int uf = u & 0xffff;
			int vf = v & 0xffff;
			const byte *a = sample_nearest(sp, sw, sh, ss, 1+sa, ui, vi);
			const byte *b = sample_nearest(sp, sw, sh, ss, 1+sa, ui+1, vi);
			const byte *c = sample_nearest(sp, sw, sh, ss, 1+sa, ui, vi+1);
			const byte *d = sample_nearest(sp, sw, sh, ss, 1+sa, ui+1, vi+1);
			int y = (sa ? fz_mul255(bilerp(a[1], b[1], c[1], d[1], uf, vf), alpha) : alpha);
			int x = bilerp(a[0], b[0], c[0], d[0], uf, vf);
			int t = 255 - y;
			x = fz_mul255(x, alpha);
			dp[0] = x + fz_mul255(dp[0], t);
			dp[1] = x + fz_mul255(dp[1], t);
			dp[2] = x + fz_mul255(dp[2], t);
			if (da)
				dp[3] = y + fz_mul255(dp[3], t);
			if (hp)
				hp[0] = y + fz_mul255(hp[0], t);
		}
		dp += 4;
		if (hp)
			hp++;
		u += fa;
		v += fb;
	}
}

static inline void
fz_paint_affine_alpha_N_near(byte * restrict dp, int da, const byte * restrict sp, int sw, int sh, int ss, int sa, int u, int v, int fa, int fb, int w, int n1, int alpha, byte * restrict hp)
{
	int k;

	if (fa == 0)
	{
		int ui = u >> 16;
		if (ui < 0 || ui >= sw)
			return;
		sp += ui * (n1+sa);
		while (w--)
		{
			int vi = v >> 16;
			if (vi >= 0 && vi < sh)
			{
				const byte *sample = sp + (vi * ss);
				int a = (sa ? fz_mul255(sample[n1], alpha) : alpha);
				int t = 255 - a;
				for (k = 0; k < n1; k++)
					dp[k] = fz_mul255(sample[k], alpha) + fz_mul255(dp[k], t);
				if (da)
					dp[n1] = a + fz_mul255(dp[n1], t);
				if (hp)
					hp[0] = a + fz_mul255(hp[0], t);
			}
			dp += n1+da;
			if (hp)
				hp++;
			v += fb;
		}
	}
	else if (fb == 0)
	{
		int vi = v >> 16;
		if (vi < 0 || vi >= sh)
			return;
		sp += vi * ss;
		while (w--)
		{
			int ui = u >> 16;
			if (ui >= 0 && ui < sw)
			{
				const byte *sample = sp + (ui * (n1+sa));
				int a = (sa ? fz_mul255(sample[n1], alpha) : alpha);
				int t = 255 - a;
				for (k = 0; k < n1; k++)
					dp[k] = fz_mul255(sample[k], alpha) + fz_mul255(dp[k], t);
				if (da)
					dp[n1] = a + fz_mul255(dp[n1], t);
				if (hp)
					hp[0] = a + fz_mul255(hp[0], t);
			}
			dp += n1+da;
			if (hp)
				hp++;
			u += fa;
		}
	}
	else
	{
		while (w--)
		{
			int ui = u >> 16;
			int vi = v >> 16;
			if (ui >= 0 && ui < sw && vi >= 0 && vi < sh)
			{
				const byte *sample = sp + (vi * ss) + (ui * (n1+sa));
				int a = (sa ? fz_mul255(sample[n1], alpha) : alpha);
				int t = 255 - a;
				for (k = 0; k < n1; k++)
					dp[k] = fz_mul255(sample[k], alpha) + fz_mul255(dp[k], t);
				if (da)
					dp[n1] = a + fz_mul255(dp[n1], t);
				if (hp)
					hp[0] = a + fz_mul255(hp[0], t);
			}
			dp += n1+da;
			if (hp)
				hp++;
			u += fa;
			v += fb;
		}
	}
}

static inline void
fz_paint_affine_alpha_g2rgb_near(byte * restrict dp, int da, const byte * restrict sp, int sw, int sh, int ss, int sa, int u, int v, int fa, int fb, int w, int alpha, byte * restrict hp)
{
	if (fa == 0)
	{
		int ui = u >> 16;
		if (ui < 0 || ui >= sw)
			return;
		sp += ui * (1+sa);
		while (w--)
		{
			int vi = v >> 16;
			if (vi >= 0 && vi < sh)
			{
				const byte *sample = sp + (vi * ss);
				int x = fz_mul255(sample[0], alpha);
				int a = (sa ? fz_mul255(sample[1], alpha) : alpha);
				int t = 255 - a;
				dp[0] = x + fz_mul255(dp[0], t);
				dp[1] = x + fz_mul255(dp[1], t);
				dp[2] = x + fz_mul255(dp[2], t);
				if (da)
					dp[3] = a + fz_mul255(dp[3], t);
				if (hp)
					hp[0] = a + fz_mul255(hp[0], t);
			}
			dp += 3 + da;
			if (hp)
				hp++;
			v += fb;
		}
	}
	else if (fb == 0)
	{
		int vi = v >> 16;
		if (vi < 0 || vi >= sh)
			return;
		sp += vi * ss;
		while (w--)
		{
			int ui = u >> 16;
			if (ui >= 0 && ui < sw)
			{
				const byte *sample = sp + (ui * (1+sa));
				int x = fz_mul255(sample[0], alpha);
				int a = (sa ? fz_mul255(sample[1], alpha) : alpha);
				int t = 255 - a;
				dp[0] = x + fz_mul255(dp[0], t);
				dp[1] = x + fz_mul255(dp[1], t);
				dp[2] = x + fz_mul255(dp[2], t);
				if (da)
					dp[3] = a + fz_mul255(dp[3], t);
				if (hp)
					hp[0] = a + fz_mul255(hp[0], t);
			}
			dp += 3 + da;
			if (hp)
				hp++;
			u += fa;
		}
	}
	else
	{
		while (w--)
		{
			int ui = u >> 16;
			int vi = v >> 16;
			if (ui >= 0 && ui < sw && vi >= 0 && vi < sh)
			{
				const byte *sample = sp + (vi * ss) + (ui * (1+sa));
				int x = fz_mul255(sample[0], alpha);
				int a = (sa ? fz_mul255(sample[1], alpha): alpha);
				int t = 255 - a;
				dp[0] = x + fz_mul255(dp[0], t);
				dp[1] = x + fz_mul255(dp[1], t);
				dp[2] = x + fz_mul255(dp[2], t);
				if (da)
					dp[3] = a + fz_mul255(dp[3], t);
				if (hp)
					hp[0] = a + fz_mul255(hp[0], t);
			}
			dp += 3 + da;
			if (hp)
				hp++;
			u += fa;
			v += fb;
		}
	}
}

/* Blend premultiplied source image over destination */

static inline void
fz_paint_affine_N_lerp(byte * restrict dp, int da, const byte * restrict sp, int sw, int sh, int ss, int sa, int u, int v, int fa, int fb, int w, int n1, byte * restrict hp)
{
	int k;

	while (w--)
	{
		if (u + 32768 >= 0 && u < sw && v + 32768 >= 0 && v < sh)
		{
			int ui = u >> 16;
			int vi = v >> 16;
			int uf = u & 0xffff;
			int vf = v & 0xffff;
			const byte *a = sample_nearest(sp, sw, sh, ss, n1+sa, ui, vi);
			const byte *b = sample_nearest(sp, sw, sh, ss, n1+sa, ui+1, vi);
			const byte *c = sample_nearest(sp, sw, sh, ss, n1+sa, ui, vi+1);
			const byte *d = sample_nearest(sp, sw, sh, ss, n1+sa, ui+1, vi+1);
			int y = sa ? bilerp(a[n1], b[n1], c[n1], d[n1], uf, vf) : 255;
			int t = 255 - y;
			for (k = 0; k < n1; k++)
			{
				int x = bilerp(a[k], b[k], c[k], d[k], uf, vf);
				dp[k] = x + fz_mul255(dp[k], t);
			}
			if (da)
				dp[n1] = y + fz_mul255(dp[n1], t);
			if (hp)
				hp[0] = y + fz_mul255(hp[0], t);
		}
		dp += n1 + da;
		if (hp)
			hp++;
		u += fa;
		v += fb;
	}
}

static inline void
fz_paint_affine_solid_g2rgb_lerp(byte * restrict dp, int da, const byte * restrict sp, int sw, int sh, int ss, int sa, int u, int v, int fa, int fb, int w, byte * restrict hp)
{
	while (w--)
	{
		if (u + 32768 >= 0 && u < sw && v + 32768 >= 0 && v < sh)
		{
			int ui = u >> 16;
			int vi = v >> 16;
			int uf = u & 0xffff;
			int vf = v & 0xffff;
			const byte *a = sample_nearest(sp, sw, sh, ss, 1+sa, ui, vi);
			const byte *b = sample_nearest(sp, sw, sh, ss, 1+sa, ui+1, vi);
			const byte *c = sample_nearest(sp, sw, sh, ss, 1+sa, ui, vi+1);
			const byte *d = sample_nearest(sp, sw, sh, ss, 1+sa, ui+1, vi+1);
			int y = (sa ? bilerp(a[1], b[1], c[1], d[1], uf, vf) : 255);
			int t = 255 - y;
			int x = bilerp(a[0], b[0], c[0], d[0], uf, vf);
			dp[0] = x + fz_mul255(dp[0], t);
			dp[1] = x + fz_mul255(dp[1], t);
			dp[2] = x + fz_mul255(dp[2], t);
			if (da)
				dp[3] = y + fz_mul255(dp[3], t);
			if (hp)
				hp[0] = y + fz_mul255(hp[0], t);
		}
		dp += 3 + da;
		if (hp)
			hp++;
		u += fa;
		v += fb;
	}
}

static inline void
fz_paint_affine_N_near(byte * restrict dp, int da, const byte * restrict sp, int sw, int sh, int ss, int sa, int u, int v, int fa, int fb, int w, int n1, byte * restrict hp)
{
	int k;

	if (fa == 0)
	{
		int ui = u >> 16;
		if (ui < 0 || ui >= sw)
			return;
		sp += ui*(n1+sa);
		while (w--)
		{
			int vi = v >> 16;
			if (vi >= 0 && vi < sh)
			{
				const byte *sample = sp + (vi * ss);
				int a = (sa ? sample[n1] : 255);
				/* If a is 0, then sample[k] = 0 for all k, as premultiplied */
				if (a != 0)
				{
					int t = 255 - a;
					if (t == 0)
					{
						if (n1+da == 4 && n1+sa == 4)
						{
							*(int32_t *)dp = *(int32_t *)sample;
						}
						else
						{
							for (k = 0; k < n1; k++)
								dp[k] = sample[k];
							if (da)
								dp[n1] = a;
						}
						if (hp)
							hp[0] = a;
					}
					else
					{
						for (k = 0; k < n1; k++)
							dp[k] = sample[k] + fz_mul255(dp[k], t);
						if (da)
							dp[n1] = a + fz_mul255(dp[n1], t);
						if (hp)
							hp[0] = a + fz_mul255(hp[0], t);
					}
				}
			}
			dp += n1+da;
			if (hp)
				hp++;
			v += fb;
		}
	}
	else if (fb == 0)
	{
		int vi = v >> 16;
		if (vi < 0 || vi >= sh)
			return;
		sp += vi * ss;
		while (w--)
		{
			int ui = u >> 16;
			if (ui >= 0 && ui < sw)
			{
				const byte *sample = sp + (ui * (n1+sa));
				int a = sa ? sample[n1] : 255;
				/* If a is 0, then sample[k] = 0 for all k, as premultiplied */
				if (a != 0)
				{
					int t = 255 - a;
					if (t == 0)
					{
						if (n1+da == 4 && n1+sa == 4)
						{
							*(int32_t *)dp = *(int32_t *)sample;
						}
						else
						{
							for (k = 0; k < n1; k++)
								dp[k] = sample[k];
							if (da)
								dp[n1] = a;
						}
						if (hp)
							hp[0] = a;
					}
					else
					{
						for (k = 0; k < n1; k++)
							dp[k] = sample[k] + fz_mul255(dp[k], t);
						if(da)
							dp[n1] = a + fz_mul255(dp[n1], t);
						if (hp)
							hp[0] = a + fz_mul255(hp[0], t);
					}
				}
			}
			dp += n1+da;
			if (hp)
				hp++;
			u += fa;
		}
	}
	else
	{
		while (w--)
		{
			int ui = u >> 16;
			int vi = v >> 16;
			if (ui >= 0 && ui < sw && vi >= 0 && vi < sh)
			{
				const byte *sample = sp + (vi * ss) + (ui * (n1+sa));
				int a = sa ? sample[n1] : 255;
				/* If a is 0, then sample[k] = 0 for all k, as premultiplied */
				if (a != 0)
				{
					int t = 255 - a;
					if (t == 0)
					{
						if (n1+da == 4 && n1+sa == 4)
						{
							*(int32_t *)dp = *(int32_t *)sample;
						}
						else
						{
							for (k = 0; k < n1; k++)
								dp[k] = sample[k];
							if (da)
								dp[n1] = a;
						}
						if (hp)
							hp[0] = a;
					}
					else
					{
						for (k = 0; k < n1; k++)
							dp[k] = sample[k] + fz_mul255(dp[k], t);
						if (da)
							dp[n1] = a + fz_mul255(dp[n1], t);
						if (hp)
							hp[0] = a + fz_mul255(hp[0], t);
					}
				}
			}
			dp += n1+da;
			if (hp)
				hp++;
			u += fa;
			v += fb;
		}
	}
}

static inline void
fz_paint_affine_solid_g2rgb_near(byte * restrict dp, int da, const byte * restrict sp, int sw, int sh, int ss, int sa, int u, int v, int fa, int fb, int w, byte * restrict hp)
{
	if (fa == 0)
	{
		int ui = u >> 16;
		if (ui < 0 || ui >= sw)
			return;
		sp += ui * (1+sa);
		while (w--)
		{
			int vi = v >> 16;
			if (vi >= 0 && vi < sh)
			{
				const byte *sample = sp + (vi * ss);
				int a = (sa ? sample[1] : 255);
				if (a != 0)
				{
					int x = sample[0];
					int t = 255 - a;
					if (t == 0)
					{
						dp[0] = x;
						dp[1] = x;
						dp[2] = x;
						if (da)
							dp[3] = a;
						if (hp)
							hp[0] = a;
					}
					else
					{
						dp[0] = x + fz_mul255(dp[0], t);
						dp[1] = x + fz_mul255(dp[1], t);
						dp[2] = x + fz_mul255(dp[2], t);
						if (da)
							dp[3] = a + fz_mul255(dp[3], t);
						if (hp)
							hp[0] = a + fz_mul255(hp[0], t);
					}
				}
			}
			dp += 3 + da;
			if (hp)
				hp++;
			v += fb;
		}
	}
	else if (fb == 0)
	{
		int vi = v >> 16;
		if (vi < 0 || vi >= sh)
			return;
		sp += vi * ss;
		while (w--)
		{
			int ui = u >> 16;
			if (ui >= 0 && ui < sw)
			{
				const byte *sample = sp + (ui * (1+sa));
				int a = (sa ? sample[1] : 255);
				if (a != 0)
				{
					int x = sample[0];
					int t = 255 - a;
					if (t == 0)
					{
						dp[0] = x;
						dp[1] = x;
						dp[2] = x;
						if (da)
							dp[3] = a;
						if (hp)
							hp[0] = a;
					}
					else
					{
						dp[0] = x + fz_mul255(dp[0], t);
						dp[1] = x + fz_mul255(dp[1], t);
						dp[2] = x + fz_mul255(dp[2], t);
						if (da)
							dp[3] = a + fz_mul255(dp[3], t);
						if (hp)
							hp[0] = a + fz_mul255(hp[0], t);
					}
				}
			}
			dp += 3 + da;
			if (hp)
				hp++;
			u += fa;
		}
	}
	else
	{
		while (w--)
		{
			int ui = u >> 16;
			int vi = v >> 16;
			if (ui >= 0 && ui < sw && vi >= 0 && vi < sh)
			{
				const byte *sample = sp + (vi * ss) + (ui * (1+sa));
				int a = sa ? sample[1] : 255;
				if (a != 0)
				{
					int x = sample[0];
					int t = 255 - a;
					if (t == 0)
					{
						dp[0] = x;
						dp[1] = x;
						dp[2] = x;
						if (da)
							dp[3] = a;
						if (hp)
							hp[0] = a;
					}
					else
					{
						dp[0] = x + fz_mul255(dp[0], t);
						dp[1] = x + fz_mul255(dp[1], t);
						dp[2] = x + fz_mul255(dp[2], t);
						if (da)
							dp[3] = a + fz_mul255(dp[3], t);
						if (hp)
							hp[0] = a + fz_mul255(hp[0], t);
					}
				}
			}
			dp += 3 + da;
			if (hp)
				hp++;
			u += fa;
			v += fb;
		}
	}
}

/* Blend non-premultiplied color in source image mask over destination */

static inline void
fz_paint_affine_color_N_lerp(byte * restrict dp, int da, const byte * restrict sp, int sw, int sh, int ss, int u, int v, int fa, int fb, int w, int n1, const byte * restrict color, byte * restrict hp)
{
	int sa = color[n1];
	int k;

	while (w--)
	{
		if (u + 32768 >= 0 && u < sw && v + 32768 >= 0 && v < sh)
		{
			int ui = u >> 16;
			int vi = v >> 16;
			int uf = u & 0xffff;
			int vf = v & 0xffff;
			const byte *a = sample_nearest(sp, sw, sh, ss, 1, ui, vi);
			const byte *b = sample_nearest(sp, sw, sh, ss, 1, ui+1, vi);
			const byte *c = sample_nearest(sp, sw, sh, ss, 1, ui, vi+1);
			const byte *d = sample_nearest(sp, sw, sh, ss, 1, ui+1, vi+1);
			int ma = bilerp(a[0], b[0], c[0], d[0], uf, vf);
			int masa = FZ_COMBINE(FZ_EXPAND(ma), sa);
			for (k = 0; k < n1; k++)
				dp[k] = FZ_BLEND(color[k], dp[k], masa);
			if (da)
				dp[n1] = FZ_BLEND(255, dp[n1], masa);
			if (hp)
				hp[0] = FZ_BLEND(255, hp[0], masa);
		}
		dp += n1 + da;
		if (hp)
			hp++;
		u += fa;
		v += fb;
	}
}

static inline void
fz_paint_affine_color_N_near(byte * restrict dp, int da, const byte * restrict sp, int sw, int sh, int ss, int u, int v, int fa, int fb, int w, int n1, const byte * restrict color, byte * restrict hp)
{
	int sa = color[n1];
	int k;

	while (w--)
	{
		int ui = u >> 16;
		int vi = v >> 16;
		if (ui >= 0 && ui < sw && vi >= 0 && vi < sh)
		{
			int ma = sp[vi * ss + ui];
			int masa = FZ_COMBINE(FZ_EXPAND(ma), sa);
			for (k = 0; k < n1; k++)
				dp[k] = FZ_BLEND(color[k], dp[k], masa);
			if (da)
				dp[n1] = FZ_BLEND(255, dp[n1], masa);
			if (hp)
				hp[0] = FZ_BLEND(255, hp[0], masa);
		}
		dp += n1+da;
		if (hp)
			hp++;
		u += fa;
		v += fb;
	}
}

static void
fz_paint_affine_lerp(byte * restrict dp, int da, const byte * restrict sp, int sw, int sh, int ss, int sa, int u, int v, int fa, int fb, int w, int n, int alpha, const byte * restrict color/*unused*/, byte * restrict hp)
{
	if (da)
	{
		if (sa)
		{
			if (alpha == 255)
			{
				switch (n)
				{
				case 1: fz_paint_affine_N_lerp(dp, 1, sp, sw, sh, ss, 1, u, v, fa, fb, w, 1, hp); break;
				case 3: fz_paint_affine_N_lerp(dp, 1, sp, sw, sh, ss, 1, u, v, fa, fb, w, 3, hp); break;
				case 4: fz_paint_affine_N_lerp(dp, 1, sp, sw, sh, ss, 1, u, v, fa, fb, w, 4, hp); break;
				default: fz_paint_affine_N_lerp(dp, 1, sp, sw, sh, ss, 1, u, v, fa, fb, w, n, hp); break;
				}
			}
			else if (alpha > 0)
			{
				switch (n)
				{
				case 1: fz_paint_affine_alpha_N_lerp(dp, 1, sp, sw, sh, ss, 1, u, v, fa, fb, w, 1, alpha, hp); break;
				case 3: fz_paint_affine_alpha_N_lerp(dp, 1, sp, sw, sh, ss, 1, u, v, fa, fb, w, 3, alpha, hp); break;
				case 4: fz_paint_affine_alpha_N_lerp(dp, 1, sp, sw, sh, ss, 1, u, v, fa, fb, w, 4, alpha, hp); break;
				default: fz_paint_affine_alpha_N_lerp(dp, 1, sp, sw, sh, ss, 1, u, v, fa, fb, w, n, alpha, hp); break;
				}
			}
		}
		else
		{
			if (alpha == 255)
			{
				switch (n)
				{
				case 1: fz_paint_affine_N_lerp(dp, 1, sp, sw, sh, ss, 0, u, v, fa, fb, w, 1, hp); break;
				case 3: fz_paint_affine_N_lerp(dp, 1, sp, sw, sh, ss, 0, u, v, fa, fb, w, 3, hp); break;
				case 4: fz_paint_affine_N_lerp(dp, 1, sp, sw, sh, ss, 0, u, v, fa, fb, w, 4, hp); break;
				default: fz_paint_affine_N_lerp(dp, 1, sp, sw, sh, ss, 0, u, v, fa, fb, w, n, hp); break;
				}
			}
			else if (alpha > 0)
			{
				switch (n)
				{
				case 1: fz_paint_affine_alpha_N_lerp(dp, 1, sp, sw, sh, ss, 0, u, v, fa, fb, w, 1, alpha, hp); break;
				case 3: fz_paint_affine_alpha_N_lerp(dp, 1, sp, sw, sh, ss, 0, u, v, fa, fb, w, 3, alpha, hp); break;
				case 4: fz_paint_affine_alpha_N_lerp(dp, 1, sp, sw, sh, ss, 0, u, v, fa, fb, w, 4, alpha, hp); break;
				default: fz_paint_affine_alpha_N_lerp(dp, 1, sp, sw, sh, ss, 0, u, v, fa, fb, w, n, alpha, hp); break;
				}
			}
		}
	}
	else
	{
		if (sa)
		{
			if (alpha == 255)
			{
				switch (n)
				{
				case 1: fz_paint_affine_N_lerp(dp, 0, sp, sw, sh, ss, 1, u, v, fa, fb, w, 1, hp); break;
				case 3: fz_paint_affine_N_lerp(dp, 0, sp, sw, sh, ss, 1, u, v, fa, fb, w, 3, hp); break;
				case 4: fz_paint_affine_N_lerp(dp, 0, sp, sw, sh, ss, 1, u, v, fa, fb, w, 4, hp); break;
				default: fz_paint_affine_N_lerp(dp, 0, sp, sw, sh, ss, 1, u, v, fa, fb, w, n, hp); break;
				}
			}
			else if (alpha > 0)
			{
				switch (n)
				{
				case 1: fz_paint_affine_alpha_N_lerp(dp, 0, sp, sw, sh, ss, 1, u, v, fa, fb, w, 1, alpha, hp); break;
				case 3: fz_paint_affine_alpha_N_lerp(dp, 0, sp, sw, sh, ss, 1, u, v, fa, fb, w, 3, alpha, hp); break;
				case 4: fz_paint_affine_alpha_N_lerp(dp, 0, sp, sw, sh, ss, 1, u, v, fa, fb, w, 4, alpha, hp); break;
				default: fz_paint_affine_alpha_N_lerp(dp, 0, sp, sw, sh, ss, 1, u, v, fa, fb, w, n, alpha, hp); break;
				}
			}
		}
		else
		{
			if (alpha == 255)
			{
				switch (n)
				{
				case 1: fz_paint_affine_N_lerp(dp, 0, sp, sw, sh, ss, 0, u, v, fa, fb, w, 1, hp); break;
				case 3: fz_paint_affine_N_lerp(dp, 0, sp, sw, sh, ss, 0, u, v, fa, fb, w, 3, hp); break;
				case 4: fz_paint_affine_N_lerp(dp, 0, sp, sw, sh, ss, 0, u, v, fa, fb, w, 4, hp); break;
				default: fz_paint_affine_N_lerp(dp, 0, sp, sw, sh, ss, 0, u, v, fa, fb, w, n, hp); break;
				}
			}
			else if (alpha > 0)
			{
				switch (n)
				{
				case 1: fz_paint_affine_alpha_N_lerp(dp, 0, sp, sw, sh, ss, 0, u, v, fa, fb, w, 1, alpha, hp); break;
				case 3: fz_paint_affine_alpha_N_lerp(dp, 0, sp, sw, sh, ss, 0, u, v, fa, fb, w, 3, alpha, hp); break;
				case 4: fz_paint_affine_alpha_N_lerp(dp, 0, sp, sw, sh, ss, 0, u, v, fa, fb, w, 4, alpha, hp); break;
				default: fz_paint_affine_alpha_N_lerp(dp, 0, sp, sw, sh, ss, 0, u, v, fa, fb, w, n, alpha, hp); break;
				}
			}
		}
	}
}

static void
fz_paint_affine_g2rgb_lerp(byte *dp, int da, const byte * restrict sp, int sw, int sh, int ss, int sa, int u, int v, int fa, int fb, int w, int n, int alpha, const byte * restrict color/*unused*/, byte * restrict hp)
{
	if (da)
	{
		if (sa)
		{
			if (alpha == 255)
			{
				fz_paint_affine_solid_g2rgb_lerp(dp, 1, sp, sw, sh, ss, 1, u, v, fa, fb, w, hp);
			}
			else if (alpha > 0)
			{
				fz_paint_affine_alpha_g2rgb_lerp(dp, 1, sp, sw, sh, ss, 1, u, v, fa, fb, w, alpha, hp);
			}
		}
		else
		{
			if (alpha == 255)
			{
				fz_paint_affine_solid_g2rgb_lerp(dp, 1, sp, sw, sh, ss, 0, u, v, fa, fb, w, hp);
			}
			else if (alpha > 0)
			{
				fz_paint_affine_alpha_g2rgb_lerp(dp, 1, sp, sw, sh, ss, 0, u, v, fa, fb, w, alpha, hp);
			}
		}
	}
	else
	{
		if (sa)
		{
			if (alpha == 255)
			{
				fz_paint_affine_solid_g2rgb_lerp(dp, 0, sp, sw, sh, ss, 1, u, v, fa, fb, w, hp);
			}
			else if (alpha > 0)
			{
				fz_paint_affine_alpha_g2rgb_lerp(dp, 0, sp, sw, sh, ss, 1, u, v, fa, fb, w, alpha, hp);
			}
		}
		else
		{
			if (alpha == 255)
			{
				fz_paint_affine_solid_g2rgb_lerp(dp, 0, sp, sw, sh, ss, 0, u, v, fa, fb, w, hp);
			}
			else if (alpha > 0)
			{
				fz_paint_affine_alpha_g2rgb_lerp(dp, 0, sp, sw, sh, ss, 0, u, v, fa, fb, w, alpha, hp);
			}
		}
	}
}

static void
fz_paint_affine_near(byte *dp, int da, const byte * restrict sp, int sw, int sh, int ss, int sa, int u, int v, int fa, int fb, int w, int n, int alpha, const byte * restrict color/*unused */, byte * restrict hp)
{
	if (da)
	{
		if (sa)
		{
			if (alpha == 255)
			{
				switch (n)
				{
				case 0: fz_paint_affine_N_near(dp, 1, sp, sw, sh, ss, 1, u, v, fa, fb, w, 0, hp); break;
				case 1: fz_paint_affine_N_near(dp, 1, sp, sw, sh, ss, 1, u, v, fa, fb, w, 1, hp); break;
				case 3: fz_paint_affine_N_near(dp, 1, sp, sw, sh, ss, 1, u, v, fa, fb, w, 3, hp); break;
				case 4: fz_paint_affine_N_near(dp, 1, sp, sw, sh, ss, 1, u, v, fa, fb, w, 4, hp); break;
				default: fz_paint_affine_N_near(dp, 1, sp, sw, sh, ss, 1, u, v, fa, fb, w, n, hp); break;
				}
			}
			else if (alpha > 0)
			{
				switch (n)
				{
				case 0: fz_paint_affine_alpha_N_near(dp, 1, sp, sw, sh, ss, 1, u, v, fa, fb, w, 0, alpha, hp); break;
				case 1: fz_paint_affine_alpha_N_near(dp, 1, sp, sw, sh, ss, 1, u, v, fa, fb, w, 1, alpha, hp); break;
				case 3: fz_paint_affine_alpha_N_near(dp, 1, sp, sw, sh, ss, 1, u, v, fa, fb, w, 3, alpha, hp); break;
				case 4: fz_paint_affine_alpha_N_near(dp, 1, sp, sw, sh, ss, 1, u, v, fa, fb, w, 4, alpha, hp); break;
				default: fz_paint_affine_alpha_N_near(dp, 1, sp, sw, sh, ss, 1, u, v, fa, fb, w, n, alpha, hp); break;
				}
			}
		}
		else
		{
			if (alpha == 255)
			{
				switch (n)
				{
				case 0: fz_paint_affine_N_near(dp, 1, sp, sw, sh, ss, 0, u, v, fa, fb, w, 0, hp); break;
				case 1: fz_paint_affine_N_near(dp, 1, sp, sw, sh, ss, 0, u, v, fa, fb, w, 1, hp); break;
				case 3: fz_paint_affine_N_near(dp, 1, sp, sw, sh, ss, 0, u, v, fa, fb, w, 3, hp); break;
				case 4: fz_paint_affine_N_near(dp, 1, sp, sw, sh, ss, 0, u, v, fa, fb, w, 4, hp); break;
				default: fz_paint_affine_N_near(dp, 1, sp, sw, sh, ss, 0, u, v, fa, fb, w, n, hp); break;
				}
			}
			else if (alpha > 0)
			{
				switch (n)
				{
				case 0: fz_paint_affine_alpha_N_near(dp, 1, sp, sw, sh, ss, 0, u, v, fa, fb, w, 0, alpha, hp); break;
				case 1: fz_paint_affine_alpha_N_near(dp, 1, sp, sw, sh, ss, 0, u, v, fa, fb, w, 1, alpha, hp); break;
				case 3: fz_paint_affine_alpha_N_near(dp, 1, sp, sw, sh, ss, 0, u, v, fa, fb, w, 3, alpha, hp); break;
				case 4: fz_paint_affine_alpha_N_near(dp, 1, sp, sw, sh, ss, 0, u, v, fa, fb, w, 4, alpha, hp); break;
				default: fz_paint_affine_alpha_N_near(dp, 1, sp, sw, sh, ss, 0, u, v, fa, fb, w, n, alpha, hp); break;
				}
			}
		}
	}
	else
	{
		if (sa)
		{
			if (alpha == 255)
			{
				switch (n)
				{
				case 1: fz_paint_affine_N_near(dp, 0, sp, sw, sh, ss, 1, u, v, fa, fb, w, 1, hp); break;
				case 3: fz_paint_affine_N_near(dp, 0, sp, sw, sh, ss, 1, u, v, fa, fb, w, 3, hp); break;
				case 4: fz_paint_affine_N_near(dp, 0, sp, sw, sh, ss, 1, u, v, fa, fb, w, 4, hp); break;
				default: fz_paint_affine_N_near(dp, 0, sp, sw, sh, ss, 1, u, v, fa, fb, w, n, hp); break;
				}
			}
			else if (alpha > 0)
			{
				switch (n)
				{
				case 1: fz_paint_affine_alpha_N_near(dp, 0, sp, sw, sh, ss, 1, u, v, fa, fb, w, 1, alpha, hp); break;
				case 3: fz_paint_affine_alpha_N_near(dp, 0, sp, sw, sh, ss, 1, u, v, fa, fb, w, 3, alpha, hp); break;
				case 4: fz_paint_affine_alpha_N_near(dp, 0, sp, sw, sh, ss, 1, u, v, fa, fb, w, 4, alpha, hp); break;
				default: fz_paint_affine_alpha_N_near(dp, 0, sp, sw, sh, ss, 1, u, v, fa, fb, w, n, alpha, hp); break;
				}
			}
		}
		else
		{
			if (alpha == 255)
			{
				switch (n)
				{
				case 1: fz_paint_affine_N_near(dp, 0, sp, sw, sh, ss, 0, u, v, fa, fb, w, 1, hp); break;
				case 3: fz_paint_affine_N_near(dp, 0, sp, sw, sh, ss, 0, u, v, fa, fb, w, 3, hp); break;
				case 4: fz_paint_affine_N_near(dp, 0, sp, sw, sh, ss, 0, u, v, fa, fb, w, 4, hp); break;
				default: fz_paint_affine_N_near(dp, 0, sp, sw, sh, ss, 0, u, v, fa, fb, w, n, hp); break;
				}
			}
			else if (alpha > 0)
			{
				switch (n)
				{
				case 1: fz_paint_affine_alpha_N_near(dp, 0, sp, sw, sh, ss, 0, u, v, fa, fb, w, 1, alpha, hp); break;
				case 3: fz_paint_affine_alpha_N_near(dp, 0, sp, sw, sh, ss, 0, u, v, fa, fb, w, 3, alpha, hp); break;
				case 4: fz_paint_affine_alpha_N_near(dp, 0, sp, sw, sh, ss, 0, u, v, fa, fb, w, 4, alpha, hp); break;
				default: fz_paint_affine_alpha_N_near(dp, 0, sp, sw, sh, ss, 0, u, v, fa, fb, w, n, alpha, hp); break;
				}
			}
		}
	}
}

static void
fz_paint_affine_g2rgb_near(byte *dp, int da, const byte * restrict sp, int sw, int sh, int ss, int sa, int u, int v, int fa, int fb, int w, int n, int alpha, const byte * restrict color/*unused*/, byte * restrict hp)
{
	if (da)
	{
		if (sa)
		{
			if (alpha == 255)
			{
				fz_paint_affine_solid_g2rgb_near(dp, 1, sp, sw, sh, ss, 1, u, v, fa, fb, w, hp);
			}
			else if (alpha > 0)
			{
				fz_paint_affine_alpha_g2rgb_near(dp, 1, sp, sw, sh, ss, 1, u, v, fa, fb, w, alpha, hp);
			}
		}
		else
		{
			if (alpha == 255)
			{
				fz_paint_affine_solid_g2rgb_near(dp, 1, sp, sw, sh, ss, 0, u, v, fa, fb, w, hp);
			}
			else if (alpha > 0)
			{
				fz_paint_affine_alpha_g2rgb_near(dp, 1, sp, sw, sh, ss, 0, u, v, fa, fb, w, alpha, hp);
			}
		}
	}
	else
	{
		if (sa)
		{
			if (alpha == 255)
			{
				fz_paint_affine_solid_g2rgb_near(dp, 0, sp, sw, sh, ss, 1, u, v, fa, fb, w, hp);
			}
			else if (alpha > 0)
			{
				fz_paint_affine_alpha_g2rgb_near(dp, 0, sp, sw, sh, ss, 1, u, v, fa, fb, w, alpha, hp);
			}
		}
		else
		{
			if (alpha == 255)
			{
				fz_paint_affine_solid_g2rgb_near(dp, 0, sp, sw, sh, ss, 0, u, v, fa, fb, w, hp);
			}
			else if (alpha > 0)
			{
				fz_paint_affine_alpha_g2rgb_near(dp, 0, sp, sw, sh, ss, 0, u, v, fa, fb, w, alpha, hp);
			}
		}
	}
}

static void
fz_paint_affine_color_lerp(byte *dp, int da, const byte * restrict sp, int sw, int sh, int ss, int sa, int u, int v, int fa, int fb, int w, int n, int alpha/*unused*/, const byte * restrict color, byte * restrict hp)
{
	if (da)
	{
		switch (n)
		{
		case 1: fz_paint_affine_color_N_lerp(dp, 1, sp, sw, sh, ss, u, v, fa, fb, w, 1, color, hp); break;
		case 3: fz_paint_affine_color_N_lerp(dp, 1, sp, sw, sh, ss, u, v, fa, fb, w, 3, color, hp); break;
		case 4: fz_paint_affine_color_N_lerp(dp, 1, sp, sw, sh, ss, u, v, fa, fb, w, 4, color, hp); break;
		default: fz_paint_affine_color_N_lerp(dp, 1, sp, sw, sh, ss, u, v, fa, fb, w, n, color, hp); break;
		}
	}
	else
	{
		switch (n)
		{
		case 1: fz_paint_affine_color_N_lerp(dp, 0, sp, sw, sh, ss, u, v, fa, fb, w, 1, color, hp); break;
		case 3: fz_paint_affine_color_N_lerp(dp, 0, sp, sw, sh, ss, u, v, fa, fb, w, 3, color, hp); break;
		case 4: fz_paint_affine_color_N_lerp(dp, 0, sp, sw, sh, ss, u, v, fa, fb, w, 4, color, hp); break;
		default: fz_paint_affine_color_N_lerp(dp, 0, sp, sw, sh, ss, u, v, fa, fb, w, n, color, hp); break;
		}
	}
}

static void
fz_paint_affine_color_near(byte *dp, int da, const byte * restrict sp, int sw, int sh, int ss, int sa, int u, int v, int fa, int fb, int w, int n, int alpha/*unused*/, const byte * restrict color, byte * restrict hp)
{
	if (da)
	{
		switch (n)
		{
		case 1: fz_paint_affine_color_N_near(dp, 1, sp, sw, sh, ss, u, v, fa, fb, w, 1, color, hp); break;
		case 3: fz_paint_affine_color_N_near(dp, 1, sp, sw, sh, ss, u, v, fa, fb, w, 3, color, hp); break;
		case 4: fz_paint_affine_color_N_near(dp, 1, sp, sw, sh, ss, u, v, fa, fb, w, 4, color, hp); break;
		default: fz_paint_affine_color_N_near(dp, 1, sp, sw, sh, ss, u, v, fa, fb, w, n, color, hp); break;
		}
	}
	else
	{
		switch (n)
		{
		case 1: fz_paint_affine_color_N_near(dp, 0, sp, sw, sh, ss, u, v, fa, fb, w, 1, color, hp); break;
		case 3: fz_paint_affine_color_N_near(dp, 0, sp, sw, sh, ss, u, v, fa, fb, w, 3, color, hp); break;
		case 4: fz_paint_affine_color_N_near(dp, 0, sp, sw, sh, ss, u, v, fa, fb, w, 4, color, hp); break;
		default: fz_paint_affine_color_N_near(dp, 0, sp, sw, sh, ss, u, v, fa, fb, w, n, color, hp); break;
		}
	}
}

/* RJW: The following code was originally written to be sensitive to
 * FLT_EPSILON. Given the way the 'minimum representable difference'
 * between 2 floats changes size as we scale, we now pick a larger
 * value to ensure idempotency even with rounding problems. The
 * value we pick is still far smaller than would ever show up with
 * antialiasing.
 */
#define MY_EPSILON 0.001

/* We have 2 possible ways of gridfitting images. The first way, considered
 * 'safe' in all cases, is to expand an image out to fill a box that entirely
 * covers all the pixels touched by the current image. This is our 'standard'
 * mechanism.
 * The alternative, used when we know images are tiled across a page, is to
 * round the edge of each image to the closest integer pixel boundary. This
 * would not be safe in the general case, but gives less distortion across
 * neighbouring images when tiling is used. We use this for .gproof files.
 */
void
fz_gridfit_matrix(int as_tiled, fz_matrix *m)
{
	if (fabsf(m->b) < FLT_EPSILON && fabsf(m->c) < FLT_EPSILON)
	{
		if (as_tiled)
		{
			float f;
			/* Nearest boundary for left */
			f = (float)(int)(m->e + 0.5);
			m->a += m->e - f; /* Adjust width for change */
			m->e = f;
			/* Nearest boundary for right (width really) */
			m->a = (float)(int)(m->a + 0.5);
		}
		else if (m->a > 0)
		{
			float f;
			/* Adjust left hand side onto pixel boundary */
			f = (float)(int)(m->e);
			if (f - m->e > MY_EPSILON)
				f -= 1.0; /* Ensure it moves left */
			m->a += m->e - f; /* width gets wider as f <= m.e */
			m->e = f;
			/* Adjust right hand side onto pixel boundary */
			f = (float)(int)(m->a);
			if (m->a - f > MY_EPSILON)
				f += 1.0; /* Ensure it moves right */
			m->a = f;
		}
		else if (m->a < 0)
		{
			float f;
			/* Adjust right hand side onto pixel boundary */
			f = (float)(int)(m->e);
			if (m->e - f > MY_EPSILON)
				f += 1.0; /* Ensure it moves right */
			m->a += m->e - f; /* width gets wider (more -ve) */
			m->e = f;
			/* Adjust left hand side onto pixel boundary */
			f = (float)(int)(m->a);
			if (f - m->a > MY_EPSILON)
				f -= 1.0; /* Ensure it moves left */
			m->a = f;
		}
		if (as_tiled)
		{
			float f;
			/* Nearest boundary for top */
			f = (float)(int)(m->f + 0.5);
			m->d += m->f - f; /* Adjust width for change */
			m->f = f;
			/* Nearest boundary for bottom (height really) */
			m->d = (float)(int)(m->d + 0.5);
		}
		else if (m->d > 0)
		{
			float f;
			/* Adjust top onto pixel boundary */
			f = (float)(int)(m->f);
			if (f - m->f > MY_EPSILON)
				f -= 1.0; /* Ensure it moves upwards */
			m->d += m->f - f; /* width gets wider as f <= m.f */
			m->f = f;
			/* Adjust bottom onto pixel boundary */
			f = (float)(int)(m->d);
			if (m->d - f > MY_EPSILON)
				f += 1.0; /* Ensure it moves down */
			m->d = f;
		}
		else if (m->d < 0)
		{
			float f;
			/* Adjust bottom onto pixel boundary */
			f = (float)(int)(m->f);
			if (m->f - f > MY_EPSILON)
				f += 1.0; /* Ensure it moves down */
			m->d += m->f - f; /* width gets wider (more -ve) */
			m->f = f;
			/* Adjust top onto pixel boundary */
			f = (float)(int)(m->d);
			if (f - m->d > MY_EPSILON)
				f -= 1.0; /* Ensure it moves up */
			m->d = f;
		}
	}
	else if (fabsf(m->a) < FLT_EPSILON && fabsf(m->d) < FLT_EPSILON)
	{
		if (as_tiled)
		{
			float f;
			/* Nearest boundary for left */
			f = (float)(int)(m->e + 0.5);
			m->b += m->e - f; /* Adjust width for change */
			m->e = f;
			/* Nearest boundary for right (width really) */
			m->b = (float)(int)(m->b + 0.5);
		}
		else if (m->b > 0)
		{
			float f;
			/* Adjust left hand side onto pixel boundary */
			f = (float)(int)(m->f);
			if (f - m->f > MY_EPSILON)
				f -= 1.0; /* Ensure it moves left */
			m->b += m->f - f; /* width gets wider as f <= m.f */
			m->f = f;
			/* Adjust right hand side onto pixel boundary */
			f = (float)(int)(m->b);
			if (m->b - f > MY_EPSILON)
				f += 1.0; /* Ensure it moves right */
			m->b = f;
		}
		else if (m->b < 0)
		{
			float f;
			/* Adjust right hand side onto pixel boundary */
			f = (float)(int)(m->f);
			if (m->f - f > MY_EPSILON)
				f += 1.0; /* Ensure it moves right */
			m->b += m->f - f; /* width gets wider (more -ve) */
			m->f = f;
			/* Adjust left hand side onto pixel boundary */
			f = (float)(int)(m->b);
			if (f - m->b > MY_EPSILON)
				f -= 1.0; /* Ensure it moves left */
			m->b = f;
		}
		if (as_tiled)
		{
			float f;
			/* Nearest boundary for left */
			f = (float)(int)(m->f + 0.5);
			m->c += m->f - f; /* Adjust width for change */
			m->f = f;
			/* Nearest boundary for right (width really) */
			m->c = (float)(int)(m->c + 0.5);
		}
		else if (m->c > 0)
		{
			float f;
			/* Adjust top onto pixel boundary */
			f = (float)(int)(m->e);
			if (f - m->e > MY_EPSILON)
				f -= 1.0; /* Ensure it moves upwards */
			m->c += m->e - f; /* width gets wider as f <= m.e */
			m->e = f;
			/* Adjust bottom onto pixel boundary */
			f = (float)(int)(m->c);
			if (m->c - f > MY_EPSILON)
				f += 1.0; /* Ensure it moves down */
			m->c = f;
		}
		else if (m->c < 0)
		{
			float f;
			/* Adjust bottom onto pixel boundary */
			f = (float)(int)(m->e);
			if (m->e - f > MY_EPSILON)
				f += 1.0; /* Ensure it moves down */
			m->c += m->e - f; /* width gets wider (more -ve) */
			m->e = f;
			/* Adjust top onto pixel boundary */
			f = (float)(int)(m->c);
			if (f - m->c > MY_EPSILON)
				f -= 1.0; /* Ensure it moves up */
			m->c = f;
		}
	}
}

/* Draw an image with an affine transform on destination */

static void
fz_paint_image_imp(fz_pixmap * restrict dst, const fz_irect *scissor, const fz_pixmap * restrict shape, const fz_pixmap * restrict img, const fz_matrix * restrict ctm, const byte * restrict color, int alpha, int lerp_allowed, int as_tiled)
{
	byte *dp, *sp, *hp;
	int u, v, fa, fb, fc, fd;
	int x, y, w, h;
	int sw, sh, ss, sa, n, hs, da;
	fz_irect bbox;
	int dolerp;
	void (*paintfn)(byte * restrict dp, int da, const byte * restrict sp, int sw, int sh, int ss, int sa, int u, int v, int fa, int fb, int w, int n, int alpha, const byte * restrict color, byte * restrict hp);
	fz_matrix local_ctm = *ctm;
	fz_rect rect;
	int is_rectilinear;

	/* grid fit the image */
	fz_gridfit_matrix(as_tiled, &local_ctm);

	/* turn on interpolation for upscaled and non-rectilinear transforms */
	dolerp = 0;
	is_rectilinear = fz_is_rectilinear(&local_ctm);
	if (!is_rectilinear)
		dolerp = lerp_allowed;
	if (sqrtf(local_ctm.a * local_ctm.a + local_ctm.b * local_ctm.b) > img->w)
		dolerp = lerp_allowed;
	if (sqrtf(local_ctm.c * local_ctm.c + local_ctm.d * local_ctm.d) > img->h)
		dolerp = lerp_allowed;

	/* except when we shouldn't, at large magnifications */
	if (!img->interpolate)
	{
		if (sqrtf(local_ctm.a * local_ctm.a + local_ctm.b * local_ctm.b) > img->w * 2)
			dolerp = 0;
		if (sqrtf(local_ctm.c * local_ctm.c + local_ctm.d * local_ctm.d) > img->h * 2)
			dolerp = 0;
	}

	rect = fz_unit_rect;
	fz_irect_from_rect(&bbox, fz_transform_rect(&rect, &local_ctm));
	fz_intersect_irect(&bbox, scissor);

	x = bbox.x0;
	if (shape && shape->x > x)
		x = shape->x;
	y = bbox.y0;
	if (shape && shape->y > y)
		y = shape->y;
	w = bbox.x1;
	if (shape && shape->x + shape->w < w)
		w = shape->x + shape->w;
	w -= x;
	h = bbox.y1;
	if (shape && shape->y + shape->h < h)
		h = shape->y + shape->h;
	h -= y;
	if (w < 0 || h < 0)
		return;

	/* map from screen space (x,y) to image space (u,v) */
	fz_pre_scale(&local_ctm, 1.0f / img->w, 1.0f / img->h);
	fz_invert_matrix(&local_ctm, &local_ctm);

	fa = (int)(local_ctm.a *= 65536.0f);
	fb = (int)(local_ctm.b *= 65536.0f);
	fc = (int)(local_ctm.c *= 65536.0f);
	fd = (int)(local_ctm.d *= 65536.0f);
	local_ctm.e *= 65536.0f;
	local_ctm.f *= 65536.0f;

	/* Calculate initial texture positions. Do a half step to start. */
	/* Bug 693021: Keep calculation in float for as long as possible to
	 * avoid overflow. */
	u = (int)((local_ctm.a * x) + (local_ctm.c * y) + local_ctm.e + ((local_ctm.a + local_ctm.c) * .5f));
	v = (int)((local_ctm.b * x) + (local_ctm.d * y) + local_ctm.f + ((local_ctm.b + local_ctm.d) * .5f));

	dp = dst->samples + (unsigned int)((y - dst->y) * dst->stride + (x - dst->x) * dst->n);
	da = dst->alpha;
	n = dst->n - da;
	sp = img->samples;
	sw = img->w;
	sh = img->h;
	ss = img->stride;
	sa = img->alpha;
	if (shape)
	{
		hs = shape->stride;
		hp = shape->samples + (unsigned int)((y - shape->y) * shape->stride + x - shape->x);
	}
	else
	{
		hs = 0;
		hp = NULL;
	}

	/* TODO: if (fb == 0 && fa == 1) call fz_paint_span */

	/* Sometimes we can get an alpha only input to be
	 * ploted. In this case treat it as a greyscale
	 * input. */
	if (img->n == sa && n > 0)
		sa = 0;

	if (n == 3 && img->n == 1 + sa && !color)
	{
		if (dolerp)
			paintfn = fz_paint_affine_g2rgb_lerp;
		else
			paintfn = fz_paint_affine_g2rgb_near;
	}
	else
	{
		assert((!color && img->n - sa == n) || (color && img->n - sa == 1));
		if (dolerp)
		{
			if (color)
				paintfn = fz_paint_affine_color_lerp;
			else
				paintfn = fz_paint_affine_lerp;
		}
		else
		{
			if (color)
				paintfn = fz_paint_affine_color_near;
			else
				paintfn = fz_paint_affine_near;
		}
	}

	if (dolerp)
	{
		u -= 32768;
		v -= 32768;
		sw = (sw<<16) + 32768;
		sh = (sh<<16) + 32768;
	}

	while (h--)
	{
		paintfn(dp, da, sp, sw, sh, ss, sa, u, v, fa, fb, w, n, alpha, color, hp);
		dp += dst->stride;
		hp += hs;
		u += fc;
		v += fd;
	}
}

void
fz_paint_image_with_color(fz_pixmap * restrict dst, const fz_irect * restrict scissor, fz_pixmap * restrict shape, const fz_pixmap * restrict img, const fz_matrix * restrict ctm, const byte * restrict color, int lerp_allowed, int as_tiled)
{
	assert(img->n == 1);
	fz_paint_image_imp(dst, scissor, shape, img, ctm, color, 255, lerp_allowed, as_tiled);
}

void
fz_paint_image(fz_pixmap * restrict dst, const fz_irect * restrict scissor, fz_pixmap * restrict shape, const fz_pixmap * restrict img, const fz_matrix * restrict ctm, int alpha, int lerp_allowed, int as_tiled)
{
	assert(dst->n - dst->alpha == img->n - img->alpha|| (dst->n == 3 + dst->alpha && img->n == 1 + img->alpha));
	fz_paint_image_imp(dst, scissor, shape, img, ctm, NULL, alpha, lerp_allowed, as_tiled);
}
