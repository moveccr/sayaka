using Gdk;

public errordomain XXXError
{
	ProgError,
}

public struct ColorRGB24
{
	uint8 r;
	uint8 g;
	uint8 b;
}

public struct ColorRGBint
{
	int r;
	int g;
	int b;
}

extern int imagereductor_resize_reduce_fast_fixed8(
	uint8[] dst,
	int dstWidth, int dstHeight,
	uint8[] src,
	int srcWidth, int srcHeight,
	int srcNch, int srcStride);

extern int imagereductor_resize_reduce_fast_fixed16(
	uint8[] dst,
	int dstWidth, int dstHeight,
	uint8[] src,
	int srcWidth, int srcHeight,
	int srcNch, int srcStride);


public class ImageReductor
{
	private Diag diag = new Diag("ImageReductor");

	// ----- カラーパレットから色をさがす

	// カスタムパレット時に、最も近いパレット番号を返します。
	public uint8 FindCustom(uint8 r, uint8 g, uint8 b)
	{
		// RGB の各色の距離の和が最小、にしてある。
		// YCC で判断したほうが良好なのは知ってるけど、そこまで必要じゃない。
		// とおもったけどやっぱり品質わるいので色差も考えていく。

		// 色差情報を重みにしていく。
		int K1 = ((int)r*2 - (int)g - (int)b); if (K1 < 1) K1 = 1; if (K1 > 8) K1 = 4;
		int K2 = ((int)g*2 - (int)r - (int)b); if (K2 < 1) K2 = 1; if (K2 > 8) K2 = 4;
		int K3 = ((int)b*2 - (int)r - (int)g); if (K3 < 1) K3 = 1; if (K3 > 8) K3 = 4;
		uint8 rv = 0;
		int min_d = int.MAX;
		for (int i = 0; i < PaletteCount; i++) {
			int dR = (int)Palette[i, 0] - (int)r;
			int dG = (int)Palette[i, 1] - (int)g;
			int dB = (int)Palette[i, 2] - (int)b;
			int d = dR.abs() * K1 + dG.abs() * K2 + dB.abs() * K3;

			if (d < min_d) {
				rv = (uint8)i;
				min_d = d;
				if (d == 0) break;
			}
		}
		return rv;
	}

	// ----- カラー変換 (減色)

	// ----- 単純減色法
	// ----- 要は誤差拡散法ではなく、当該ピクセルのみの値で減色するアルゴリズム。

	// 単純減色法で NTSC 輝度減色でグレースケールにします。
	public void SimpleReduceGray()
	{
		SimpleReduceCustom(FindGray);
	}

	// 単純減色法で 固定8色にします。
	public void SimpleReduceFixed8()
	{
		SimpleReduceCustom(FindFixed8);
	}

	// 単純減色法で 固定16色にします。
	public void SimpleReduceFixed16()
	{
		SimpleReduceCustom(FindFixed16);
	}

	// 単純減色法で 固定256色にします。
	public void SimpleReduceFixed256()
	{
		SimpleReduceCustom(FindFixed256);
	}

	// 単純減色法を適用します。
	// この関数を実行すると、pix の内容は画像からパレット番号の列に変わります。
	public void SimpleReduceCustom(FindFunc op)
	{
		unowned uint8[] p0 = Pix.get_pixels();
		int w = Pix.get_width();
		int h = Pix.get_height();
		int nch = Pix.get_n_channels();
		int ybase = 0;
		int dst = 0;

		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				uint8* psrc = &p0[ybase + x * nch];
				uint8 r = psrc[0];
				uint8 g = psrc[1];
				uint8 b = psrc[2];
				// パレット番号の列として、画像を破壊して書き込み
				p0[dst++] = op(r, g, b);
			}
			ybase += Pix.get_rowstride();
		}
	}

	private uint8 satulate_add(uint8 a, int16 b)
	{
		int16 rv = (int16)a + b;
		if (rv > 255) rv = 255;
		if (rv < 0) rv = 0;
		return (uint8)rv;
	}

	public void DiffuseReduceGray()
	{
		DiffuseReduceCustom(FindGray);
	}

	public void DiffuseReduceFixed8()
	{
		DiffuseReduceCustom(FindFixed8);
	}

	public void DiffuseReduceFixed16()
	{
		DiffuseReduceCustom(FindFixed16);
	}

	public void DiffuseReduceFixed256()
	{
		DiffuseReduceCustom(FindFixed256);
	}

	public int16 DiffuseMultiplier = 1;
	public int16 DiffuseDivisor = 3;

	private void DiffusePixel(uint8 r, uint8 g, uint8 b, uint8* ptr, uint8 c)
	{
		ptr[0] = satulate_add(ptr[0], ((int16)r - Palette[c, 0]) * DiffuseMultiplier / DiffuseDivisor);
		ptr[1] = satulate_add(ptr[1], ((int16)g - Palette[c, 1]) * DiffuseMultiplier / DiffuseDivisor);
		ptr[2] = satulate_add(ptr[2], ((int16)b - Palette[c, 2]) * DiffuseMultiplier / DiffuseDivisor);
	}

	public void DiffuseReduceCustom(FindFunc op)
	{
		unowned uint8[] p0 = Pix.get_pixels();
		int w = Pix.get_width();
		int h = Pix.get_height();
		int nch = Pix.get_n_channels();
		int stride = Pix.get_rowstride();
		int ybase = 0;
		int dst = 0;

		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				uint8* psrc = &p0[ybase + x * nch];
				uint8 r = psrc[0];
				uint8 g = psrc[1];
				uint8 b = psrc[2];

				uint8 C = op(r, g, b);
				// パレット番号の列として、画像を破壊して書き込み
				p0[dst++] = C;

				if (x < w - 1) {
					// 右のピクセルに誤差を分散
					DiffusePixel(r, g, b, &p0[ybase + (x + 1) * nch], C);
				}
				if (y < h - 1) {
					// 下のピクセルに誤差を分散
					DiffusePixel(r, g, b, &p0[ybase + stride + x * nch], C);
				}
				if (x < w - 1 && y < h - 1) {
					// 右下のピクセルに誤差を分散
					DiffusePixel(r, g, b, &p0[ybase + stride + (x + 1) * nch], C);
				}
			}
			ybase += stride;
		}
	}

	/////////////////////////////

	public void FastFixed(int toWidth, int toHeight)
	{
		unowned uint8[] p0 = Pix.get_pixels();
		int w = Pix.get_width();
		int h = Pix.get_height();
		int nch = Pix.get_n_channels();
		int stride = Pix.get_rowstride();

		output = new uint8[toWidth * toHeight];
stderr.printf("PaletteCount=%d\n", PaletteCount);
		switch (PaletteCount) {
			case 8:
				imagereductor_resize_reduce_fast_fixed8(output, toWidth, toHeight, p0, w, h, nch, stride);
				break;
			case 16:
			default:
				imagereductor_resize_reduce_fast_fixed16(output, toWidth, toHeight, p0, w, h, nch, stride);
				break;
		}
	}
}

