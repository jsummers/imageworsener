// imagew-tiff.c
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

#include "imagew-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IW_INCLUDE_UTIL_FUNCTIONS
#include "imagew.h"

#define IWTIFF_MAX_TAGS 20

struct iwtiffwritecontext {
	int bitsperpixel;
	int bitspersample;
	int samplesperpixel;

	int palette_is_gray;
	int palentries;
	unsigned int transferfunc_numentries;
	int has_alpha_channel;
	struct iw_iodescr *iodescr;
	struct iw_context *ctx;
	struct iw_image *img;
	const struct iw_palette *pal;

#define IWTIFF_PHOTO_MINISBLACK  1
#define IWTIFF_PHOTO_RGB         2
#define IWTIFF_PHOTO_PALETTE     3
	int photometric;

	int write_density_in_cm;

	unsigned int curr_filepos;

	// All tags whose size can be larger than 4 bytes are
	// tracked here.
	unsigned int bitspersample_offset;
	unsigned int bitspersample_size;
	unsigned int pixdens_offset;
	unsigned int pixdens_size;
	unsigned int palette_offset;
	unsigned int palette_size; // size in bytes
	unsigned int transferfunc_offset;
	unsigned int transferfunc_size;
	unsigned int bitmap_offset;
	size_t bitmap_size;

	unsigned short taglist[IWTIFF_MAX_TAGS];
	int num_tags; // Number of tags in taglist that are used.

	struct iw_csdescr csdescr;
};

static size_t iwtiff_calc_bpr(int bpp, size_t width)
{
	return (bpp*width+7)/8;
}

static void iwtiff_write(struct iwtiffwritecontext *tiffctx, const void *buf, size_t n)
{
	(*tiffctx->iodescr->write_fn)(tiffctx->ctx,tiffctx->iodescr,buf,n);
}

static void iwtiff_set_ui16(iw_byte *b, unsigned int n)
{
	b[0] = n&0xff;
	b[1] = (n>>8)&0xff;
}

static void iwtiff_set_ui32(iw_byte *b, unsigned int n)
{
	b[0] = n&0xff;
	b[1] = (n>>8)&0xff;
	b[2] = (n>>16)&0xff;
	b[3] = (n>>24)&0xff;
}

static void iwtiff_write_ui16(struct iwtiffwritecontext *tiffctx, unsigned int n)
{
	iw_byte buf[2];
	iwtiff_set_ui16(buf,n);
	iwtiff_write(tiffctx,buf,2);
}

static void iwtiff_convert_row1(const iw_byte *srcrow, iw_byte *dstrow, int width)
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

static void iwtiff_convert_row4(const iw_byte *srcrow, iw_byte *dstrow, int width)
{
	int i;

	for(i=0;i<width;i++) {
		if(i%2==0)
			dstrow[i/2] = srcrow[i]<<4;
		else
			dstrow[i/2] |= srcrow[i];
	}
}

static void iwtiff_convert_row8bps(const iw_byte *srcrow, iw_byte *dstrow, int width, int samplesperpixel)
{
	memcpy(dstrow,srcrow,width*samplesperpixel);
}

static void iwtiff_convert_row16bps(const iw_byte *srcrow, iw_byte *dstrow, int width, int samplesperpixel)
{
	int i;
	int nsamples;

	// Internally, 16-bit samples are stored in big-endian order.
	// We write little-endian TIFF files, so the byte order needs to be swapped.
	nsamples = width * samplesperpixel;
	for(i=0;i<nsamples;i++) {
		dstrow[i*2] = srcrow[i*2+1];
		dstrow[i*2+1] = srcrow[i*2];
	}
}

static void iwtiff_write_file_header(struct iwtiffwritecontext *tiffctx)
{
	iw_byte buf[8];
	buf[0] = 73;
	buf[1] = 73;
	iwtiff_set_ui16(&buf[2],42);
	iwtiff_set_ui32(&buf[4],8); // Pointer to IFD
	iwtiff_write(tiffctx,buf,8);
	tiffctx->curr_filepos = 8;
}

static void iwtiff_write_density(struct iwtiffwritecontext *tiffctx)
{
	iw_byte buf[16];
	unsigned int denom;
	unsigned int x,y;

	if(tiffctx->img->density_code==IW_DENSITY_UNITS_PER_METER) {
		if(tiffctx->write_density_in_cm) {
			x = (unsigned int)(0.5+10.0*tiffctx->img->density_x);
			y = (unsigned int)(0.5+10.0*tiffctx->img->density_y);
		}
		else {
			x = (unsigned int)(0.5+25.4*tiffctx->img->density_x);
			y = (unsigned int)(0.5+25.4*tiffctx->img->density_y);
		}
		denom=1000;
	}
	else if(tiffctx->img->density_code==IW_DENSITY_UNITS_UNKNOWN) {
		x = (unsigned int)(0.5+1000.0*tiffctx->img->density_x);
		y = (unsigned int)(0.5+1000.0*tiffctx->img->density_y);
		denom=1000;
	}
	else {
		x=1;
		y=1;
		denom=1;
	}

	iwtiff_set_ui32(&buf[0],x);
	iwtiff_set_ui32(&buf[4],denom);
	iwtiff_set_ui32(&buf[8],y);
	iwtiff_set_ui32(&buf[12],denom);
	iwtiff_write(tiffctx,buf,16);
}

// The TransferFunction tag is one of several ways to store colorspace
// information in a TIFF file. It's a very inefficient way to do it, but
// the other methods aren't really any better.
static void iwtiff_write_transferfunction(struct iwtiffwritecontext *tiffctx)
{
	iw_byte *buf = NULL;
	unsigned int i;
	double targetsample;
	double linear;
	double maxvalue;

	buf = iw_mallocz(tiffctx->ctx,tiffctx->transferfunc_size);
	if(!buf) return;

	maxvalue = (double)(tiffctx->transferfunc_numentries-1);
	for(i=0;i<tiffctx->transferfunc_numentries;i++) {
		targetsample = ((double)i)/maxvalue;

		linear = iw_convert_sample_to_linear(targetsample,&tiffctx->csdescr);

		iwtiff_set_ui16(&buf[i*2],(unsigned int)(0.5+65535.0*linear));
	}
	iwtiff_write(tiffctx,buf,tiffctx->transferfunc_size);
	iw_free(buf);
}

static void iwtiff_write_palette(struct iwtiffwritecontext *tiffctx)
{
	int c;
	int i;
	iw_byte *buf = NULL;
	unsigned int v;

	buf = iw_malloc(tiffctx->ctx,tiffctx->palette_size);
	if(!buf) return;

	if(tiffctx->palentries<1) return;

	// Palette samples are always 16-bit in TIFF files.
	// IW does not support generating palettes which contain colors that can't
	// be represented at 8 bits, so not every image that could be written
	// as a paletted TIFF will be.

	// Palette is organized so that all the red values go first,
	// then green, blue.

	for(c=0;c<3;c++) {
		for(i=0;i<tiffctx->palentries;i++) {
			if(i<tiffctx->pal->num_entries) {
				if(c==2) v=tiffctx->pal->entry[i].b;
				else if(c==1) v=tiffctx->pal->entry[i].g;
				else v=tiffctx->pal->entry[i].r;
				v |= (v<<8);
				iwtiff_set_ui16(&buf[c*tiffctx->palentries*2+i*2],v);
			}
		}
	}
	iwtiff_write(tiffctx,buf,tiffctx->palette_size);

	iw_free(buf);
}

#define IWTIFF_TAG256_IMAGEWIDTH 256
#define IWTIFF_TAG257_IMAGELENGTH 257
#define IWTIFF_TAG258_BITSPERSAMPLE 258
#define IWTIFF_TAG259_COMPRESSION 259
#define IWTIFF_TAG262_PHOTOMETRIC 262
#define IWTIFF_TAG273_STRIPOFFSETS 273
#define IWTIFF_TAG277_SAMPLESPERPIXEL 277
#define IWTIFF_TAG278_ROWSPERSTRIP 278
#define IWTIFF_TAG279_STRIPBYTECOUNTS 279
#define IWTIFF_TAG282_XRESOLUTION 282
#define IWTIFF_TAG283_YRESOLUTION 283
#define IWTIFF_TAG296_RESOLUTIONUNIT 296
#define IWTIFF_TAG301_TRANSFERFUNCTION 301
#define IWTIFF_TAG320_COLORMAP 320
#define IWTIFF_TAG338_EXTRASAMPLES 338

#define IWTIFF_UINT16   3 // "SHORT"
#define IWTIFF_UINT32   4 // "LONG"
#define IWTIFF_RATIONAL 5

static void write_tag_to_ifd(struct iwtiffwritecontext *tiffctx,int tagnum,iw_byte *buf)
{
	iwtiff_set_ui16(&buf[0],tagnum);
	iwtiff_set_ui16(&buf[2],IWTIFF_UINT16); // tag type (default=short)
	iwtiff_set_ui32(&buf[4],1); // value count (default=1)
	iwtiff_set_ui32(&buf[8],0); // value or offset (default=0)

	switch(tagnum) {
	case IWTIFF_TAG256_IMAGEWIDTH:
		iwtiff_set_ui16(&buf[2],IWTIFF_UINT32);
		iwtiff_set_ui32(&buf[8],tiffctx->img->width);
		break;
	case IWTIFF_TAG257_IMAGELENGTH:
		iwtiff_set_ui16(&buf[2],IWTIFF_UINT32);
		iwtiff_set_ui32(&buf[8],tiffctx->img->height);
		break;
	case IWTIFF_TAG258_BITSPERSAMPLE:
		iwtiff_set_ui32(&buf[4],tiffctx->samplesperpixel); // value count
		if(tiffctx->bitspersample_size<=4) {
			iwtiff_set_ui16(&buf[8],tiffctx->bitspersample);
			if(tiffctx->samplesperpixel==2) {
				iwtiff_set_ui16(&buf[10],tiffctx->bitspersample);
			}
		}
		else {
			iwtiff_set_ui32(&buf[8],tiffctx->bitspersample_offset);
		}
		break;
	case IWTIFF_TAG259_COMPRESSION:
		iwtiff_set_ui16(&buf[8],1);
		break;
	case IWTIFF_TAG262_PHOTOMETRIC:
		iwtiff_set_ui16(&buf[8],tiffctx->photometric);
		break;
	case IWTIFF_TAG282_XRESOLUTION:
		iwtiff_set_ui16(&buf[2],IWTIFF_RATIONAL);
		iwtiff_set_ui32(&buf[8],tiffctx->pixdens_offset);
		break;
	case IWTIFF_TAG283_YRESOLUTION:
		iwtiff_set_ui16(&buf[2],IWTIFF_RATIONAL);
		iwtiff_set_ui32(&buf[8],tiffctx->pixdens_offset+8);
		break;
	case IWTIFF_TAG296_RESOLUTIONUNIT:
		// 1==no units, 2=pixels/inch, 3=pixels/cm
		if(tiffctx->img->density_code==IW_DENSITY_UNITS_PER_METER) {
			iwtiff_set_ui16(&buf[8],(tiffctx->write_density_in_cm)?3:2);
		}
		else {
			iwtiff_set_ui16(&buf[8],1);
		}
		break;
	case IWTIFF_TAG273_STRIPOFFSETS:
		iwtiff_set_ui16(&buf[2],IWTIFF_UINT32);
		iwtiff_set_ui32(&buf[8],tiffctx->bitmap_offset);
		break;
	case IWTIFF_TAG277_SAMPLESPERPIXEL:
		iwtiff_set_ui16(&buf[8],tiffctx->samplesperpixel);
		break;
	case IWTIFF_TAG278_ROWSPERSTRIP:
		iwtiff_set_ui16(&buf[2],IWTIFF_UINT32);
		iwtiff_set_ui32(&buf[8],tiffctx->img->height);
		break;
	case IWTIFF_TAG279_STRIPBYTECOUNTS:
		iwtiff_set_ui16(&buf[2],IWTIFF_UINT32);
		iwtiff_set_ui32(&buf[8],(unsigned int)tiffctx->bitmap_size);
		break;
	case IWTIFF_TAG320_COLORMAP:
		iwtiff_set_ui32(&buf[4],3*tiffctx->palentries);
		iwtiff_set_ui32(&buf[8],tiffctx->palette_offset);
		break;
	case IWTIFF_TAG301_TRANSFERFUNCTION:
		iwtiff_set_ui32(&buf[4],tiffctx->transferfunc_numentries);
		iwtiff_set_ui32(&buf[8],tiffctx->transferfunc_offset);
		break;
	case IWTIFF_TAG338_EXTRASAMPLES:
		iwtiff_set_ui16(&buf[8],2); // 2 = Unassociated alpha
		break;
	}
}

// Remember that we are going to include the given tag.
static void append_tag(struct iwtiffwritecontext *tiffctx, unsigned short tagnum)
{
	if(tiffctx->num_tags >= IWTIFF_MAX_TAGS) return;
	tiffctx->taglist[tiffctx->num_tags] = tagnum;
	tiffctx->num_tags++;
}

// Writes the IFD, and meta data
static void iwtiff_write_ifd(struct iwtiffwritecontext *tiffctx)
{
	unsigned int tmppos;
	unsigned int ifd_size;
	iw_byte *buf = NULL;
	int i;

	// Note that tags must be appended in increasing numeric order.

	append_tag(tiffctx,IWTIFF_TAG256_IMAGEWIDTH);
	append_tag(tiffctx,IWTIFF_TAG257_IMAGELENGTH);
	append_tag(tiffctx,IWTIFF_TAG258_BITSPERSAMPLE);
	append_tag(tiffctx,IWTIFF_TAG259_COMPRESSION);
	append_tag(tiffctx,IWTIFF_TAG262_PHOTOMETRIC);
	append_tag(tiffctx,IWTIFF_TAG273_STRIPOFFSETS);
	append_tag(tiffctx,IWTIFF_TAG277_SAMPLESPERPIXEL);
	append_tag(tiffctx,IWTIFF_TAG278_ROWSPERSTRIP);
	append_tag(tiffctx,IWTIFF_TAG279_STRIPBYTECOUNTS);

	if(tiffctx->pixdens_size>0) {

		// Decide whether we'll write the density in dots/cm.
		if(tiffctx->img->density_code==IW_DENSITY_UNITS_PER_METER) {
			int pref_units;
			pref_units = iw_get_value(tiffctx->ctx,IW_VAL_PREF_UNITS);
			if(pref_units==IW_PREF_UNITS_METRIC)
			{
				tiffctx->write_density_in_cm = 1;
			}
		}

		append_tag(tiffctx,IWTIFF_TAG282_XRESOLUTION);
		append_tag(tiffctx,IWTIFF_TAG283_YRESOLUTION);
		append_tag(tiffctx,IWTIFF_TAG296_RESOLUTIONUNIT);
	}
	if(tiffctx->transferfunc_size>0) {
		append_tag(tiffctx,IWTIFF_TAG301_TRANSFERFUNCTION);
	}
	if(tiffctx->palette_size>0) {
		append_tag(tiffctx,IWTIFF_TAG320_COLORMAP);
	}
	if(tiffctx->has_alpha_channel) {
		append_tag(tiffctx,IWTIFF_TAG338_EXTRASAMPLES);
	}

	// Preliminaries. Figure out where we're going to put everything.

	ifd_size = 2 + 12*tiffctx->num_tags + 4;

	tmppos = tiffctx->curr_filepos + ifd_size;

	if(tiffctx->bitspersample_size>4) {
		tiffctx->bitspersample_offset = tmppos;
		tmppos += tiffctx->bitspersample_size;
	}

	if(tiffctx->pixdens_size>4) {
		tiffctx->pixdens_offset = tmppos;
		tmppos += tiffctx->pixdens_size;
	}

	if(tiffctx->transferfunc_size>4) {
		tiffctx->transferfunc_offset = tmppos;
		tmppos += tiffctx->transferfunc_size;
	}

	if(tiffctx->palette_size>4) {
		tiffctx->palette_offset = tmppos;
		tmppos += tiffctx->palette_size;
	}

	// Put the bitmap last
	tiffctx->bitmap_offset = tmppos;

	buf = iw_mallocz(tiffctx->ctx,ifd_size);
	if(!buf) goto done;

	// Set the "number of entries" field.

	iwtiff_set_ui16(&buf[0],tiffctx->num_tags);

	for(i=0;i<tiffctx->num_tags;i++) {
		write_tag_to_ifd(tiffctx,tiffctx->taglist[i],&buf[2+12*i]);
	}

	// The "next IFD" pointer
	iwtiff_set_ui32(&buf[2+12*tiffctx->num_tags],0);

	// Write the whole IFD to the file
	iwtiff_write(tiffctx,buf,ifd_size);

	// Write metadata that didn't fit inline in the IFD.

	// "BitsPerSample" is sometimes larger than 4 bytes, and needs to written
	// after the IFD.
	if(tiffctx->bitspersample_offset>0) {
		for(i=0;i<tiffctx->samplesperpixel;i++) {
			iwtiff_write_ui16(tiffctx,tiffctx->bitspersample);
		}
	}

	iwtiff_write_density(tiffctx);

	iwtiff_write_transferfunction(tiffctx);

	// Palette is always too large to be inlined.
	iwtiff_write_palette(tiffctx);

done:
	if(buf) iw_free(buf);
}

static int iwtiff_write_main(struct iwtiffwritecontext *tiffctx)
{
	struct iw_image *img;
	iw_byte *dstrow = NULL;
	size_t dstbpr;
	int j;
	const iw_byte *srcrow;

	img = tiffctx->img;

	tiffctx->samplesperpixel = 1; // default

	if(img->imgtype==IW_IMGTYPE_RGB) {
		tiffctx->photometric = IWTIFF_PHOTO_RGB;
		tiffctx->bitspersample = (img->bit_depth>8)?16:8;
		tiffctx->samplesperpixel = 3;
	}
	else if(img->imgtype==IW_IMGTYPE_RGBA) {
		tiffctx->photometric = IWTIFF_PHOTO_RGB;
		tiffctx->bitspersample = (img->bit_depth>8)?16:8;
		tiffctx->samplesperpixel = 4;
		tiffctx->has_alpha_channel = 1;
	}
	else if(img->imgtype==IW_IMGTYPE_PALETTE) {
		if(!tiffctx->pal) goto done;
		if(tiffctx->palette_is_gray) {
			// 1- or 4-bpp grayscale
			tiffctx->photometric = IWTIFF_PHOTO_MINISBLACK;
			if(tiffctx->pal->num_entries<=2) {
				tiffctx->bitspersample = 1;
			}
			else {
				tiffctx->bitspersample = 4;
			}
		}
		else {
			tiffctx->photometric = IWTIFF_PHOTO_PALETTE;
			if(tiffctx->pal->num_entries<=16) {
				tiffctx->palentries=16;
				tiffctx->bitspersample = 4;
			}
			else {
				tiffctx->palentries=256;
				tiffctx->bitspersample = 8;
			}
		}
	}
	else if(img->imgtype==IW_IMGTYPE_GRAY) {
		tiffctx->photometric = IWTIFF_PHOTO_MINISBLACK;
		tiffctx->bitspersample = (img->bit_depth>8)?16:8;
	}
	else if(img->imgtype==IW_IMGTYPE_GRAYA) {
		tiffctx->photometric = IWTIFF_PHOTO_MINISBLACK;
		tiffctx->bitspersample = (img->bit_depth>8)?16:8;
		tiffctx->samplesperpixel = 2;
		tiffctx->has_alpha_channel = 1;
	}
	else {
		iw_set_error(tiffctx->ctx,"Internal: Bad image type for TIFF");
		goto done;
	}

	tiffctx->bitsperpixel = tiffctx->bitspersample * tiffctx->samplesperpixel;
	tiffctx->bitspersample_size = 2*tiffctx->samplesperpixel;

	dstbpr = iwtiff_calc_bpr(tiffctx->bitsperpixel,img->width);

	tiffctx->bitmap_size = dstbpr * img->height;
	tiffctx->palette_size = tiffctx->palentries*6;
	tiffctx->pixdens_size = 16;

	// Figure out whether we will write a TransferFunction table.

	// If we do write such a table, this will be the size of it:
	tiffctx->transferfunc_numentries = 1 << tiffctx->bitspersample;
	// For paletted images, it is illogical that the number of entries in the
	// table is based on the depth of the *indices* into the color table. It
	// ought to be based on the depth of the *samples* in the color table
	// (which is always 16 bits). Nevertheless, this is what the spec says to
	// do. And, granted, a 128KB TransferFunction table would be inconveniently
	// large for most paletted images, even if it is logical.

	// We almost always always write a TransferFunction tag, even if it is the
	// same as the default colorspace. According to the TIFF 6 spec, the default
	// is gamma=2.2 for color images, and gamma=1.0 for grayscale images. But,
	// realistically, most viewers have to assume the default is sRGB. (Note
	// that the TIFF 6 spec predates sRGB, so you can't blame it for not
	// mentioning sRGB.) So, it isn't really safe to assume any default.

	if(tiffctx->bitspersample==1) {
		// TransferFunction is irrelevant for bilevel images.
		tiffctx->transferfunc_numentries = 0;
	}

	if(iw_get_value(tiffctx->ctx,IW_VAL_NO_CSLABEL)) {
		tiffctx->transferfunc_numentries = 0;
	}

	tiffctx->transferfunc_size = 2*tiffctx->transferfunc_numentries;

	// File header
	iwtiff_write_file_header(tiffctx);

	iwtiff_write_ifd(tiffctx);

	// Pixels
	dstrow = iw_mallocz(tiffctx->ctx,dstbpr);
	if(!dstrow) goto done;

	for(j=0;j<img->height;j++) {
		srcrow = &img->pixels[j*img->bpr];
		if(tiffctx->bitspersample==16) {
			switch(tiffctx->bitsperpixel) {
			case 64: iwtiff_convert_row16bps(srcrow,dstrow,img->width,4); break; // RGBA16
			case 48: iwtiff_convert_row16bps(srcrow,dstrow,img->width,3); break; // RGB16
			case 32: iwtiff_convert_row16bps(srcrow,dstrow,img->width,2); break; // GA16
			case 16: iwtiff_convert_row16bps(srcrow,dstrow,img->width,1); break; // G16
			}
		}
		else {
			switch(tiffctx->bitsperpixel) {
			case 32: iwtiff_convert_row8bps(srcrow,dstrow,img->width,4); break; // RGBA8
			case 24: iwtiff_convert_row8bps(srcrow,dstrow,img->width,3); break; // RGB8
			case 16: iwtiff_convert_row8bps(srcrow,dstrow,img->width,2); break; // GA8
			case 8: iwtiff_convert_row8bps(srcrow,dstrow,img->width,1); break; // G8 or palette8
			case 4: iwtiff_convert_row4(srcrow,dstrow,img->width); break; // G4 or palette4
			case 1: iwtiff_convert_row1(srcrow,dstrow,img->width); break; // G1
			}
		}
		iwtiff_write(tiffctx,dstrow,dstbpr);
	}

done:
	if(dstrow) iw_free(dstrow);
	return 1;
}

IW_IMPL(int) iw_write_tiff_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	struct iwtiffwritecontext *tiffctx = NULL;
	int retval=0;
	struct iw_image img1;

	memset(&img1,0,sizeof(struct iw_image));

	tiffctx = iw_mallocz(ctx,sizeof(struct iwtiffwritecontext));
	if(!tiffctx) goto done;

	tiffctx->ctx = ctx;

	tiffctx->iodescr=iodescr;

	iw_get_output_image(ctx,&img1);
	tiffctx->img = &img1;

	tiffctx->palette_is_gray = iw_get_value(ctx,IW_VAL_OUTPUT_PALETTE_GRAYSCALE);

	iw_get_output_colorspace(ctx,&tiffctx->csdescr);

	if(tiffctx->img->imgtype==IW_IMGTYPE_PALETTE) {
		tiffctx->pal = iw_get_output_palette(ctx);
		if(!tiffctx->pal) goto done;
	}

	iwtiff_write_main(tiffctx);

	retval=1;

done:
	if(tiffctx->iodescr->close_fn)
		(*tiffctx->iodescr->close_fn)(ctx,tiffctx->iodescr);
	if(tiffctx) iw_free(tiffctx);
	return retval;
}
