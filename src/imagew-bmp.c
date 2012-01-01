// imagew-bmp.c
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

#include "imagew-config.h"

#include <stdlib.h>
#include <string.h>

#define IW_INCLUDE_UTIL_FUNCTIONS
#include "imagew.h"

struct iwbmpwritecontext {
	int include_file_header;
	int bitcount;
	int palentries;
	size_t palsize;
	size_t bitssize;
	struct iw_iodescr *iodescr;
	struct iw_context *ctx;
	struct iw_image *img;
	const struct iw_palette *pal;
};

static size_t iwbmp_calc_bpr(int bpp, size_t width)
{
	return ((bpp*width+31)/32)*4;
}

static void iwbmp_write(struct iwbmpwritecontext *bmpctx, const void *buf, size_t n)
{
	(*bmpctx->iodescr->write_fn)(bmpctx->ctx,bmpctx->iodescr,buf,n);
}

static void iwbmp_convert_row1(const iw_byte *srcrow, iw_byte *dstrow, int width)
{
	int i;
	int m;

	for(i=0;i<width;i++) {
		m = i%8;
		if(m==0)
			dstrow[i/8] = srcrow[i]<<7;
		else
			dstrow[i/8] |= srcrow[i]<<(7-m);
	}
}

static void iwbmp_convert_row4(const iw_byte *srcrow, iw_byte *dstrow, int width)
{
	int i;

	for(i=0;i<width;i++) {
		if(i%2==0)
			dstrow[i/2] = srcrow[i]<<4;
		else
			dstrow[i/2] |= srcrow[i];
	}
}

static void iwbmp_convert_row8(const iw_byte *srcrow, iw_byte *dstrow, int width)
{
	memcpy(dstrow,srcrow,width);
}

static void iwbmp_convert_row24(const iw_byte *srcrow, iw_byte *dstrow, int width)
{
	int i;

	for(i=0;i<width;i++) {
		dstrow[i*3+0] = srcrow[i*3+2];
		dstrow[i*3+1] = srcrow[i*3+1];
		dstrow[i*3+2] = srcrow[i*3+0];
	}
}

static void iwbmp_write_file_header(struct iwbmpwritecontext *bmpctx)
{
	iw_byte fileheader[14];

	if(!bmpctx->include_file_header) return;

	memset(&fileheader,0,sizeof(fileheader));
	fileheader[0] = 66; // 'B'
	fileheader[1] = 77; // 'M'
	iw_set_ui32le(&fileheader[ 2],14+40+(unsigned int)bmpctx->palsize+
		(unsigned int)bmpctx->bitssize); // bfSize
	iw_set_ui32le(&fileheader[10],14+40+(unsigned int)bmpctx->palsize); // bfOffBits
	iwbmp_write(bmpctx,fileheader,14);
}

static void iwbmp_write_bmp_header(struct iwbmpwritecontext *bmpctx)
{
	unsigned int dens_x, dens_y;
	iw_byte header[40];

	memset(&header,0,sizeof(header));

	iw_set_ui32le(&header[ 0],40);      // biSize
	iw_set_ui32le(&header[ 4],bmpctx->img->width);  // biWidth
	iw_set_ui32le(&header[ 8],bmpctx->img->height); // biHeight
	iw_set_ui16le(&header[12],1);    // biPlanes
	iw_set_ui16le(&header[14],bmpctx->bitcount);   // biBitCount
	//iw_set_ui32le(&header[16],0);    // biCompression = BI_RGB
	iw_set_ui32le(&header[20],(unsigned int)bmpctx->bitssize); // biSizeImage

	if(bmpctx->img->density_code==IW_DENSITY_UNITS_PER_METER) {
		dens_x = (unsigned int)(0.5+bmpctx->img->density_x);
		dens_y = (unsigned int)(0.5+bmpctx->img->density_y);
	}
	else {
		dens_x = dens_y = 2835;
	}
	iw_set_ui32le(&header[24],dens_x); // biXPelsPerMeter
	iw_set_ui32le(&header[28],dens_y); // biYPelsPerMeter

	iw_set_ui32le(&header[32],bmpctx->palentries);    // biClrUsed
	//iw_set_ui32le(&header[36],0);    // biClrImportant
	iwbmp_write(bmpctx,header,40);
}

static void iwbmp_write_palette(struct iwbmpwritecontext *bmpctx)
{
	int i;
	iw_byte buf[4];

	if(bmpctx->palentries<1) return;
	for(i=0;i<bmpctx->palentries;i++) {
		if(i<bmpctx->pal->num_entries) {
			buf[0] = bmpctx->pal->entry[i].b;
			buf[1] = bmpctx->pal->entry[i].g;
			buf[2] = bmpctx->pal->entry[i].r;
			buf[3] = 0;
		}
		else {
			memset(buf,0,4);
		}
		iwbmp_write(bmpctx,buf,4);
	}
}

static int iwbmp_write_main(struct iwbmpwritecontext *bmpctx)
{
	struct iw_image *img;
	iw_byte *dstrow = NULL;
	size_t dstbpr;
	int j;
	const iw_byte *srcrow;

	img = bmpctx->img;

	if(img->imgtype==IW_IMGTYPE_RGB) {
		bmpctx->bitcount=24;
	}
	else if(img->imgtype==IW_IMGTYPE_PALETTE) {
		if(!bmpctx->pal) goto done;
		if(bmpctx->pal->num_entries<=2)
			bmpctx->bitcount=1;
		else if(bmpctx->pal->num_entries<=16)
			bmpctx->bitcount=4;
		else
			bmpctx->bitcount=8;
	}
	else {
		iw_set_error(bmpctx->ctx,"Internal: Bad image type for BMP");
		goto done;
	}

	dstbpr = iwbmp_calc_bpr(bmpctx->bitcount,img->width);
	bmpctx->bitssize = dstbpr * img->height;
	bmpctx->palentries = 0;
	if(bmpctx->pal) {
		bmpctx->palentries = bmpctx->pal->num_entries;
		if(bmpctx->bitcount==1) {
			// The documentation says that if the bitdepth is 1, the palette
			// must contain exactly two entries.
			bmpctx->palentries=2;
		}
	}
	bmpctx->palsize = bmpctx->palentries*4;

	// File header
	iwbmp_write_file_header(bmpctx);

	// Bitmap header ("BITMAPINFOHEADER")
	iwbmp_write_bmp_header(bmpctx);

	// Palette
	iwbmp_write_palette(bmpctx);

	// Pixels
	dstrow = iw_mallocz(bmpctx->ctx,dstbpr);
	if(!dstrow) goto done;

	for(j=img->height-1;j>=0;j--) {
		srcrow = &img->pixels[j*img->bpr];
		switch(bmpctx->bitcount) {
		case 24: iwbmp_convert_row24(srcrow,dstrow,img->width); break;
		case 8: iwbmp_convert_row8(srcrow,dstrow,img->width); break;
		case 4: iwbmp_convert_row4(srcrow,dstrow,img->width); break;
		case 1: iwbmp_convert_row1(srcrow,dstrow,img->width); break;
		}
		iwbmp_write(bmpctx,dstrow,dstbpr);
	}

done:
	if(dstrow) iw_free(bmpctx->ctx,dstrow);
	return 1;
}

IW_IMPL(int) iw_write_bmp_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	struct iwbmpwritecontext bmpctx;
	int retval=0;
	struct iw_image img1;

	memset(&img1,0,sizeof(struct iw_image));

	memset(&bmpctx,0,sizeof(struct iwbmpwritecontext));

	bmpctx.ctx = ctx;
	bmpctx.include_file_header = 1;

	bmpctx.iodescr=iodescr;

	iw_get_output_image(ctx,&img1);
	bmpctx.img = &img1;

	if(bmpctx.img->imgtype==IW_IMGTYPE_PALETTE) {
		bmpctx.pal = iw_get_output_palette(ctx);
		if(!bmpctx.pal) goto done;
	}

	iwbmp_write_main(&bmpctx);

	retval=1;

done:
	if(bmpctx.iodescr->close_fn)
		(*bmpctx.iodescr->close_fn)(ctx,bmpctx.iodescr);
	return retval;
}
