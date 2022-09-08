#include <stdio.h>
#include <kernel.h>
#include <graph.h>
#include <dma.h>
#include <draw.h>
#include <packet.h>
#include <gs_psm.h>
#include <gs_gp.h>
#include <stdlib.h>
#include <time.h>

extern unsigned int size_dvd_tex;
extern unsigned char dvd_tex[];

framebuffer_t fb;
zbuffer_t zb;
int texAddress = 0;
int palAddress = 0;
void init_gs()
{
	fb.width = 640;
	fb.height = 448;
	fb.psm = GS_PSM_24;
	fb.mask = 0;
	fb.address = graph_vram_allocate(fb.width, fb.height, fb.psm, GRAPH_ALIGN_PAGE);

	graph_initialize(fb.address, fb.width, fb.height, fb.psm, 0, 0);

	zb.enable = 1;
	zb.method = ZTEST_METHOD_ALLPASS;
	zb.mask = 0;
	zb.zsm = GS_PSMZ_24;
	zb.address = graph_vram_allocate(fb.width, fb.height, zb.zsm, GRAPH_ALIGN_PAGE);

	texAddress = fb.address;

	palAddress = graph_vram_allocate(16, 16, GS_PSM_32, GRAPH_ALIGN_BLOCK);

	packet_t* p = packet_init(20, PACKET_NORMAL);
	qword_t* q = p->data;

	q = draw_setup_environment(q, 0, &fb, &zb);
	q = draw_primitive_xyoffset(q, 0, 5, 5);
	dma_channel_send_normal(DMA_CHANNEL_GIF, p->data, q - p->data, 0, 0);
	dma_channel_fast_waits(DMA_CHANNEL_GIF);
	dma_wait_fast();

	packet_free(p);
	return;
}

void upload_texture()
{
	packet_t* p = packet_init(9000, PACKET_NORMAL);
	qword_t* q = p->data;

	q = draw_texture_transfer(q, dvd_tex, 512, 256, GS_PSM_8H, texAddress, 512);

	dma_channel_send_chain(DMA_CHANNEL_GIF, p->data, q - p->data, 0, 0);
	dma_wait_fast();

	q = p->data;

	u32 pal[256] ALIGNED(16);
	pal[0] = 0x0;
	pal[1] = ~0;

	FlushCache(0);

	q = draw_texture_transfer(q, pal, 16, 16, GS_PSM_32, palAddress, 64);

	dma_channel_send_chain(DMA_CHANNEL_GIF, p->data, q - p->data, 0, 0);
	dma_wait_fast();

	packet_free(p);
	return;
}

void change_logo_colour(u32 colour)
{
	colour |= (0xFF << 24);
	packet_t* p = packet_init(32, PACKET_NORMAL);
	qword_t* q = p->data;
	u32 pal[256] ALIGNED(16);
	pal[0] = 0;
	pal[1] = colour;

	FlushCache(0);

	q = draw_texture_transfer(q, pal, 16, 16, GS_PSM_32, palAddress, 64);

	dma_channel_send_chain(DMA_CHANNEL_GIF, p->data, q - p->data, 0, 0);
	dma_wait_fast();

	packet_free(p);
}

void draw_frame(s32 x, s32 y)
{
	packet_t* p = packet_init(100, PACKET_NORMAL);
	qword_t* q = p->data;

	// Framebuffer clear

	PACK_GIFTAG(q, GIF_SET_TAG(1, 0, GIF_PRE_ENABLE, GIF_PRIM_SPRITE, GIF_FLG_PACKED, 3),
		GIF_REG_RGBAQ | (GIF_REG_XYZ2 << 4) | (GIF_REG_XYZ2 << 8));
	q++;
	// RGBAQ
	q->dw[0] = (u64)((0x33) | ((u64)0x33 << 32));
	q->dw[1] = (u64)((0x33) | ((u64)0xFF << 32));
	q++;
	// XYZ2
	q->dw[0] = (u64)((((0 << 4)) | (((u64)(0 << 4)) << 32)));
	q->dw[1] = (u64)(0);
	q++;
	// XYZ2
	q->dw[0] = (u64)(((((fb.width + 5) << 4)) | (((u64)((fb.height + 5) << 4)) << 32)));
	q->dw[1] = (u64)(0);
	q++;

	// Texture registers

	PACK_GIFTAG(q, GIF_SET_TAG(2, 0, GIF_PRE_ENABLE, GIF_PRIM_SPRITE, GIF_FLG_PACKED, 1),
		GIF_REG_AD);
	q++;
	q->dw[0] = GIF_SET_TEX0(texAddress / 64, 512 / 64, GS_PSM_8H, 9, 8, 1, 1, palAddress / 64, GS_PSM_32, 0, 0, 1);
	q->dw[1] = GIF_REG_TEX0;
	q++;
	q->dw[0] = GS_SET_TEX1(1, 0, 1, 0, 0, 0, 0);
	q->dw[1] = GS_REG_TEX1;
	q++;


	// Sprite draw
	PACK_GIFTAG(q, GIF_SET_TAG(1, 1, GIF_PRE_ENABLE, GIF_SET_PRIM(PRIM_SPRITE, 0, 1, 0, 1, 0, 1, 0, 0), GIF_FLG_PACKED, 5),
		GIF_REG_RGBAQ | (GIF_REG_UV << 4) | (GIF_REG_XYZ2 << 8) | (GIF_REG_UV << 12) | (GIF_REG_XYZ2 << 16));
	q++;
	// RGBAQ
	q->dw[0] = (u64)((0x44) | ((u64)0x00 << 32));
	q->dw[1] = (u64)((0x44) | ((u64)0xFF << 32));
	q++;
	// UV
	q->dw[0] = GIF_SET_ST(0, 0);
	q->dw[1] = 0;
	q++;
	// XYZ2
	q->dw[0] = (u64)((((x << 4)) | (((u64)(y << 4)) << 32)));
	q->dw[1] = (u64)(0);
	q++;
	// UV
	q->dw[0] = GIF_SET_ST(512 << 4, 256 << 4);
	q->dw[1] = 0;
	q++;
	// XYZ2
	q->dw[0] = (u64)(((((128 + x) << 4)) | (((u64)((64 + y) << 4)) << 32)));
	q->dw[1] = (u64)(0);
	q++;

	dma_channel_send_normal(DMA_CHANNEL_GIF, p->data, q - p->data, 0, 0);
	dma_wait_fast();

	packet_free(p);
}

int main(void)
{
	printf("Hello, World!\n");
	init_gs();
	upload_texture();
	time_t t;
	srand(time(&t));

	// Account for the empty space between the logo and it's texture wall
	// There is an xyoffset set to 5 pixels because
	// xMin and yMin cannot be negative! So 0 is actually -5
	const s32 xMax = fb.width - 120;
	const s32 yMax = fb.height - 51;
	const s32 xMin = 1;
	const s32 yMin = 0;

	s32 x = rand() % xMax;
	s32 y = rand() % yMax;
	s32 xdir = 1;
	s32 ydir = 1;

	while (1)
	{
		for (int i = 0; i < 0x10; i++)
		{
			x += xdir;
			y += ydir;

			if (x == xMin || x == xMax)
			{
				change_logo_colour(rand() % 0xFFFFFF);
				xdir *= -1;
			}
			if (y == yMin || y == yMax)
			{
				ydir *= -1;
				change_logo_colour(rand() % 0xFFFFFF);
			}


			draw_frame(x, y);
			graph_wait_vsync();
		}
	}
	SleepThread();
}
