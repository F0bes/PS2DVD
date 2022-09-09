#include <stdio.h>
#include <kernel.h>
#include <graph.h>
#include <dma.h>
#include <draw.h>
#include <packet.h>
#include <gs_psm.h>
#include <gs_gp.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sio.h>

extern u64 vu_init_start __attribute__((section(".vudata")));
extern u64 vu_init_end __attribute__((section(".vudata")));
extern u64 vu_frame_start __attribute__((section(".vudata")));
extern u64 vu_frame_end __attribute__((section(".vudata")));


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

// VIF stuff
#define VIFMPG(size, pos) ((0x4A000000) | (size << 16) | pos)
#define VIFFLUSHE 0x10000000
#define VIFFLUSH 0x11000000
#define VIFFLUSHA 0x13000000
#define VIFMSCAL(execaddr) (0x14000000 | execaddr)
#define VIFMSCNT 0x17000000
#define VIFFMSCALF(execaddr) (0x15000000 | execaddr)
#define VIF1CHCR (*(volatile u32*)0x10009000)
#define VIF1MADR (*(volatile u32*)0x10009010)
#define VIF1QWC (*(volatile u32*)0x10009020)
#define VIF1STAT (*(volatile u32*)0x10003C00)
void upload_microprogram(int pos, u64* start, u64* end)
{
	u64 microInstructioncnt = (end - start);
	if (microInstructioncnt > 255)
	{
		printf("WARNING, THE MICRO PROGRAM IS > THAN 255 INSTRUCTIONS!\n");
	}

	u32 vif_packet[2000] __attribute__((aligned(64)));
	u32 vif_index = 0;

	// Wait for any possible micro programs
	vif_packet[vif_index++] = VIFFLUSHE;

	// Upload the micro program to address 0
	vif_packet[vif_index++] = VIFMPG(microInstructioncnt, pos);

	// Embed the micro program into the VIF packet
	for (u32 instr = 0; instr < microInstructioncnt; instr++)
	{
		vif_packet[vif_index++] = (start)[instr];
		vif_packet[vif_index++] = (start)[instr] >> 32;
	}

	vif_packet[vif_index++] = VIFMSCAL(pos);

	while (vif_index % 4) // Align the packet
		vif_packet[vif_index++] = 0x00; // VIFNOP

	VIF1MADR = (u32)vif_packet;
	VIF1QWC = vif_index / 4;

	FlushCache(0);

	VIF1CHCR = 0x101;

	while (VIF1CHCR & 0x100)
	{
	};
	while (VIF1STAT & 2)
	{
	};
}

void call_microprogram(int pos)
{
	u32 vif_packet[100] __attribute__((aligned(64)));
	u32 vif_index = 0;

	// Wait for any possible micro programs
	vif_packet[vif_index++] = VIFFLUSHE;

	// Upload the micro program to address 0
	vif_packet[vif_index++] = VIFMSCAL(pos);

	while (vif_index % 4) // Align the packet
		vif_packet[vif_index++] = 0x00; // VIFNOP

	VIF1MADR = (u32)vif_packet;
	VIF1QWC = vif_index / 4;

	FlushCache(0);

	VIF1CHCR = 0x101;

	while (VIF1CHCR & 0x100)
	{
	};
	while (VIF1STAT & 2)
	{
	};
}

void upload_gif_packet()
{
	qword_t* q = (qword_t*)0x1100C000; // Directly to VU1 memory

	// Clear frame
	PACK_GIFTAG(q, GIF_SET_TAG(1, 0, GIF_PRE_ENABLE, GIF_PRIM_SPRITE, GIF_FLG_PACKED, 3),
		GIF_REG_RGBAQ | (GIF_REG_XYZ2 << 4) | (GIF_REG_XYZ2 << 8));
	q++; // 1
	// RGBAQ
	q->dw[0] = (u64)((0x22) | ((u64)0x22 << 32));
	q->dw[1] = (u64)((0x22) | ((u64)0xFF << 32));
	q++; // 2
	// XYZ2
	q->dw[0] = (u64)((((0 << 4)) | (((u64)(0 << 4)) << 32)));
	q->dw[1] = (u64)(0);
	q++; // 3
	// XYZ2
	q->dw[0] = (u64)(((((fb.width + 5) << 4)) | (((u64)((fb.height + 5) << 4)) << 32)));
	q->dw[1] = (u64)(0);
	q++; // 4

	// Sprite draw
	PACK_GIFTAG(q, GIF_SET_TAG(1, 1, GIF_PRE_ENABLE, GIF_SET_PRIM(PRIM_SPRITE, 0, 1, 0, 1, 0, 1, 0, 0), GIF_FLG_PACKED, 5),
		GIF_REG_RGBAQ | (GIF_REG_UV << 4) | (GIF_REG_XYZ2 << 8) | (GIF_REG_UV << 12) | (GIF_REG_XYZ2 << 16));
	q++; // 5
	// RGBAQ
	q->dw[0] = (u64)((0x55) | ((u64)0x00 << 32));
	q->dw[1] = (u64)((0x44) | ((u64)0xFF << 32));
	q++; // 6
	// UV
	q->dw[0] = GIF_SET_ST(0, 0);
	q->dw[1] = 0;
	q++; // 7
	// XYZ2 (Set by VU)
	q++; // 8
	// UV
	q->dw[0] = GIF_SET_ST(512 << 4, 256 << 4);
	q->dw[1] = 0;
	q++; // 9
	// XYZ2
	q++; // 10
	FlushCache(0);
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
	q = draw_texture_flush(q);
	dma_channel_send_chain(DMA_CHANNEL_GIF, p->data, q - p->data, 0, 0);
	dma_wait_fast();

	packet_free(p);
	return;
}

void set_texregs()
{
	packet_t* p = packet_init(100, PACKET_NORMAL);
	qword_t* q = p->data;

	// Texture registers

	PACK_GIFTAG(q, GIF_SET_TAG(3, 1, GIF_PRE_ENABLE, GIF_PRIM_SPRITE, GIF_FLG_PACKED, 1),
		GIF_REG_AD);
	q++;
	q->dw[0] = GIF_SET_TEX0(texAddress / 64, 512 / 64, GS_PSM_8H, 9, 8, 1, 0, palAddress / 64, GS_PSM_32, 0, 0, 1);
	q->dw[1] = GIF_REG_TEX0;
	q++;
	q->dw[0] = GS_SET_TEX1(1, 0, 0, 0, 0, 0, 0);
	q->dw[1] = GS_REG_TEX1;
	q++;
	q->dw[0] = GS_SET_COLCLAMP(0);
	q->dw[1] = GS_REG_COLCLAMP;
	q++;

	dma_channel_send_normal(DMA_CHANNEL_GIF, p->data, q - p->data, 0, 0);
	dma_wait_fast();
	packet_free(p);
}

int vsync_sema = 0;

int vsync_handler(s32 cause)
{
	iSignalSema(vsync_sema);
	EIntr();
	return 0;
}

int main(void)
{
	init_gs();

	// Set random seed
	time_t t;
	srand(time(&t));
	float ftime = rand() % 0xFFFFF;
	memcpy(&vu_init_start, &ftime, sizeof(float));
	FlushCache(0);
	upload_microprogram(0, &vu_init_start, &vu_init_end);
	upload_gif_packet();
	upload_microprogram(100, &vu_frame_start, &vu_frame_end);
	upload_texture();
	set_texregs();

	ee_sema_t semvsync;
	semvsync.attr = 0;
	semvsync.init_count =0;
	semvsync.max_count =1;
	semvsync.option = 0;
	vsync_sema = CreateSema(&semvsync);

	AddIntcHandler(INTC_VBLANK_S, vsync_handler, 0);
	EnableIntc(INTC_VBLANK_S);
	while (1)
	{
		call_microprogram(100);
		WaitSema(vsync_sema);
	}
	SleepThread();
}
