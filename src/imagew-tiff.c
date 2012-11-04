// imagew-tiff.c
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

#include "imagew-config.h"

#include <stdlib.h>
#include <string.h>

#define IW_INCLUDE_UTIL_FUNCTIONS
#include "imagew.h"

#define IWTIFF_MAX_TAGS 20

struct iwtiffwcontext {
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

static void iwtiff_write(struct iwtiffwcontext *wctx, const void *buf, size_t n)
{
	(*wctx->iodescr->write_fn)(wctx->ctx,wctx->iodescr,buf,n);
}

static void iwtiff_write_ui16(struct iwtiffwcontext *wctx, unsigned int n)
{
	iw_byte buf[2];
	iw_set_ui16le(buf,n);
	iwtiff_write(wctx,buf,2);
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

static void iwtiff_write_file_header(struct iwtiffwcontext *wctx)
{
	iw_byte buf[8];
	buf[0] = 73;
	buf[1] = 73;
	iw_set_ui16le(&buf[2],42);
	iw_set_ui32le(&buf[4],8); // Pointer to IFD
	iwtiff_write(wctx,buf,8);
	wctx->curr_filepos = 8;
}

static void iwtiff_write_density(struct iwtiffwcontext *wctx)
{
	iw_byte buf[16];
	unsigned int denom;
	unsigned int x,y;

	if(wctx->img->density_code==IW_DENSITY_UNITS_PER_METER) {
		if(wctx->write_density_in_cm) {
			x = (unsigned int)(0.5+10.0*wctx->img->density_x);
			y = (unsigned int)(0.5+10.0*wctx->img->density_y);
		}
		else {
			x = (unsigned int)(0.5+25.4*wctx->img->density_x);
			y = (unsigned int)(0.5+25.4*wctx->img->density_y);
		}
		denom=1000;
	}
	else if(wctx->img->density_code==IW_DENSITY_UNITS_UNKNOWN) {
		x = (unsigned int)(0.5+1000.0*wctx->img->density_x);
		y = (unsigned int)(0.5+1000.0*wctx->img->density_y);
		denom=1000;
	}
	else {
		x=1;
		y=1;
		denom=1;
	}

	iw_set_ui32le(&buf[0],x);
	iw_set_ui32le(&buf[4],denom);
	iw_set_ui32le(&buf[8],y);
	iw_set_ui32le(&buf[12],denom);
	iwtiff_write(wctx,buf,16);
}

// The TransferFunction tag is one of several ways to store colorspace
// information in a TIFF file. It's a very inefficient way to do it, but
// the other methods aren't really any better.
static void iwtiff_write_transferfunction(struct iwtiffwcontext *wctx)
{
	iw_byte *buf = NULL;
	unsigned int i;
	double targetsample;
	double linear;
	double maxvalue;

	buf = iw_mallocz(wctx->ctx,wctx->transferfunc_size);
	if(!buf) return;

	maxvalue = (double)(wctx->transferfunc_numentries-1);
	for(i=0;i<wctx->transferfunc_numentries;i++) {
		targetsample = ((double)i)/maxvalue;

		linear = iw_convert_sample_to_linear(targetsample,&wctx->csdescr);

		iw_set_ui16le(&buf[i*2],(unsigned int)(0.5+65535.0*linear));
	}
	iwtiff_write(wctx,buf,wctx->transferfunc_size);
	iw_free(wctx->ctx,buf);
}

static void iwtiff_write_palette(struct iwtiffwcontext *wctx)
{
	int c;
	int i;
	iw_byte *buf = NULL;
	unsigned int v;

	buf = iw_malloc(wctx->ctx,wctx->palette_size);
	if(!buf) return;

	if(wctx->palentries<1) return;

	// Palette samples are always 16-bit in TIFF files.
	// IW does not support generating palettes which contain colors that can't
	// be represented at 8 bits, so not every image that could be written
	// as a paletted TIFF will be.

	// Palette is organized so that all the red values go first,
	// then green, blue.

	for(c=0;c<3;c++) {
		for(i=0;i<wctx->palentries;i++) {
			if(i<wctx->pal->num_entries) {
				if(c==2) v=wctx->pal->entry[i].b;
				else if(c==1) v=wctx->pal->entry[i].g;
				else v=wctx->pal->entry[i].r;
				v |= (v<<8);
				iw_set_ui16le(&buf[c*wctx->palentries*2+i*2],v);
			}
		}
	}
	iwtiff_write(wctx,buf,wctx->palette_size);

	iw_free(wctx->ctx,buf);
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

static void write_tag_to_ifd(struct iwtiffwcontext *wctx,int tagnum,iw_byte *buf)
{
	iw_set_ui16le(&buf[0],tagnum);
	iw_set_ui16le(&buf[2],IWTIFF_UINT16); // tag type (default=short)
	iw_set_ui32le(&buf[4],1); // value count (default=1)
	iw_set_ui32le(&buf[8],0); // value or offset (default=0)

	switch(tagnum) {
	case IWTIFF_TAG256_IMAGEWIDTH:
		iw_set_ui16le(&buf[2],IWTIFF_UINT32);
		iw_set_ui32le(&buf[8],wctx->img->width);
		break;
	case IWTIFF_TAG257_IMAGELENGTH:
		iw_set_ui16le(&buf[2],IWTIFF_UINT32);
		iw_set_ui32le(&buf[8],wctx->img->height);
		break;
	case IWTIFF_TAG258_BITSPERSAMPLE:
		iw_set_ui32le(&buf[4],wctx->samplesperpixel); // value count
		if(wctx->bitspersample_size<=4) {
			iw_set_ui16le(&buf[8],wctx->bitspersample);
			if(wctx->samplesperpixel==2) {
				iw_set_ui16le(&buf[10],wctx->bitspersample);
			}
		}
		else {
			iw_set_ui32le(&buf[8],wctx->bitspersample_offset);
		}
		break;
	case IWTIFF_TAG259_COMPRESSION:
		iw_set_ui16le(&buf[8],1);
		break;
	case IWTIFF_TAG262_PHOTOMETRIC:
		iw_set_ui16le(&buf[8],wctx->photometric);
		break;
	case IWTIFF_TAG282_XRESOLUTION:
		iw_set_ui16le(&buf[2],IWTIFF_RATIONAL);
		iw_set_ui32le(&buf[8],wctx->pixdens_offset);
		break;
	case IWTIFF_TAG283_YRESOLUTION:
		iw_set_ui16le(&buf[2],IWTIFF_RATIONAL);
		iw_set_ui32le(&buf[8],wctx->pixdens_offset+8);
		break;
	case IWTIFF_TAG296_RESOLUTIONUNIT:
		// 1==no units, 2=pixels/inch, 3=pixels/cm
		if(wctx->img->density_code==IW_DENSITY_UNITS_PER_METER) {
			iw_set_ui16le(&buf[8],(wctx->write_density_in_cm)?3:2);
		}
		else {
			iw_set_ui16le(&buf[8],1);
		}
		break;
	case IWTIFF_TAG273_STRIPOFFSETS:
		iw_set_ui16le(&buf[2],IWTIFF_UINT32);
		iw_set_ui32le(&buf[8],wctx->bitmap_offset);
		break;
	case IWTIFF_TAG277_SAMPLESPERPIXEL:
		iw_set_ui16le(&buf[8],wctx->samplesperpixel);
		break;
	case IWTIFF_TAG278_ROWSPERSTRIP:
		iw_set_ui16le(&buf[2],IWTIFF_UINT32);
		iw_set_ui32le(&buf[8],wctx->img->height);
		break;
	case IWTIFF_TAG279_STRIPBYTECOUNTS:
		iw_set_ui16le(&buf[2],IWTIFF_UINT32);
		iw_set_ui32le(&buf[8],(unsigned int)wctx->bitmap_size);
		break;
	case IWTIFF_TAG320_COLORMAP:
		iw_set_ui32le(&buf[4],3*wctx->palentries);
		iw_set_ui32le(&buf[8],wctx->palette_offset);
		break;
	case IWTIFF_TAG301_TRANSFERFUNCTION:
		iw_set_ui32le(&buf[4],wctx->transferfunc_numentries);
		iw_set_ui32le(&buf[8],wctx->transferfunc_offset);
		break;
	case IWTIFF_TAG338_EXTRASAMPLES:
		iw_set_ui16le(&buf[8],2); // 2 = Unassociated alpha
		break;
	}
}

// Remember that we are going to include the given tag.
static void append_tag(struct iwtiffwcontext *wctx, unsigned short tagnum)
{
	if(wctx->num_tags >= IWTIFF_MAX_TAGS) return;
	wctx->taglist[wctx->num_tags] = tagnum;
	wctx->num_tags++;
}

// Writes the IFD, and meta data
static void iwtiff_write_ifd(struct iwtiffwcontext *wctx)
{
	unsigned int tmppos;
	unsigned int ifd_size;
	iw_byte *buf = NULL;
	int i;

	// Note that tags must be appended in increasing numeric order.

	append_tag(wctx,IWTIFF_TAG256_IMAGEWIDTH);
	append_tag(wctx,IWTIFF_TAG257_IMAGELENGTH);
	append_tag(wctx,IWTIFF_TAG258_BITSPERSAMPLE);
	append_tag(wctx,IWTIFF_TAG259_COMPRESSION);
	append_tag(wctx,IWTIFF_TAG262_PHOTOMETRIC);
	append_tag(wctx,IWTIFF_TAG273_STRIPOFFSETS);
	append_tag(wctx,IWTIFF_TAG277_SAMPLESPERPIXEL);
	append_tag(wctx,IWTIFF_TAG278_ROWSPERSTRIP);
	append_tag(wctx,IWTIFF_TAG279_STRIPBYTECOUNTS);

	if(wctx->pixdens_size>0) {

		// Decide whether we'll write the density in dots/cm.
		if(wctx->img->density_code==IW_DENSITY_UNITS_PER_METER) {
			int pref_units;
			pref_units = iw_get_value(wctx->ctx,IW_VAL_PREF_UNITS);
			if(pref_units==IW_PREF_UNITS_METRIC)
			{
				wctx->write_density_in_cm = 1;
			}
		}

		append_tag(wctx,IWTIFF_TAG282_XRESOLUTION);
		append_tag(wctx,IWTIFF_TAG283_YRESOLUTION);
		append_tag(wctx,IWTIFF_TAG296_RESOLUTIONUNIT);
	}
	if(wctx->transferfunc_size>0) {
		append_tag(wctx,IWTIFF_TAG301_TRANSFERFUNCTION);
	}
	if(wctx->palette_size>0) {
		append_tag(wctx,IWTIFF_TAG320_COLORMAP);
	}
	if(wctx->has_alpha_channel) {
		append_tag(wctx,IWTIFF_TAG338_EXTRASAMPLES);
	}

	// Preliminaries. Figure out where we're going to put everything.

	ifd_size = 2 + 12*wctx->num_tags + 4;

	tmppos = wctx->curr_filepos + ifd_size;

	if(wctx->bitspersample_size>4) {
		wctx->bitspersample_offset = tmppos;
		tmppos += wctx->bitspersample_size;
	}

	if(wctx->pixdens_size>4) {
		wctx->pixdens_offset = tmppos;
		tmppos += wctx->pixdens_size;
	}

	if(wctx->transferfunc_size>4) {
		wctx->transferfunc_offset = tmppos;
		tmppos += wctx->transferfunc_size;
	}

	if(wctx->palette_size>4) {
		wctx->palette_offset = tmppos;
		tmppos += wctx->palette_size;
	}

	// Put the bitmap last
	wctx->bitmap_offset = tmppos;

	buf = iw_mallocz(wctx->ctx,ifd_size);
	if(!buf) goto done;

	// Set the "number of entries" field.

	iw_set_ui16le(&buf[0],wctx->num_tags);

	for(i=0;i<wctx->num_tags;i++) {
		write_tag_to_ifd(wctx,wctx->taglist[i],&buf[2+12*i]);
	}

	// The "next IFD" pointer
	iw_set_ui32le(&buf[2+12*wctx->num_tags],0);

	// Write the whole IFD to the file
	iwtiff_write(wctx,buf,ifd_size);

	// Write metadata that didn't fit inline in the IFD.

	// "BitsPerSample" is sometimes larger than 4 bytes, and needs to written
	// after the IFD.
	if(wctx->bitspersample_offset>0) {
		for(i=0;i<wctx->samplesperpixel;i++) {
			iwtiff_write_ui16(wctx,wctx->bitspersample);
		}
	}

	iwtiff_write_density(wctx);

	iwtiff_write_transferfunction(wctx);

	// Palette is always too large to be inlined.
	iwtiff_write_palette(wctx);

done:
	if(buf) iw_free(wctx->ctx,buf);
}

static int iwtiff_write_main(struct iwtiffwcontext *wctx)
{
	struct iw_image *img;
	iw_byte *dstrow = NULL;
	size_t dstbpr;
	int j;
	const iw_byte *srcrow;

	img = wctx->img;

	wctx->samplesperpixel = 1; // default

	if(img->imgtype==IW_IMGTYPE_RGB) {
		wctx->photometric = IWTIFF_PHOTO_RGB;
		wctx->bitspersample = (img->bit_depth>8)?16:8;
		wctx->samplesperpixel = 3;
	}
	else if(img->imgtype==IW_IMGTYPE_RGBA) {
		wctx->photometric = IWTIFF_PHOTO_RGB;
		wctx->bitspersample = (img->bit_depth>8)?16:8;
		wctx->samplesperpixel = 4;
		wctx->has_alpha_channel = 1;
	}
	else if(img->imgtype==IW_IMGTYPE_PALETTE) {
		if(!wctx->pal) goto done;
		if(wctx->palette_is_gray) {
			// 1- or 4-bpp grayscale
			wctx->photometric = IWTIFF_PHOTO_MINISBLACK;
			if(wctx->pal->num_entries<=2) {
				wctx->bitspersample = 1;
			}
			else {
				wctx->bitspersample = 4;
			}
		}
		else {
			wctx->photometric = IWTIFF_PHOTO_PALETTE;
			if(wctx->pal->num_entries<=16) {
				wctx->palentries=16;
				wctx->bitspersample = 4;
			}
			else {
				wctx->palentries=256;
				wctx->bitspersample = 8;
			}
		}
	}
	else if(img->imgtype==IW_IMGTYPE_GRAY) {
		wctx->photometric = IWTIFF_PHOTO_MINISBLACK;
		wctx->bitspersample = (img->bit_depth>8)?16:8;
	}
	else if(img->imgtype==IW_IMGTYPE_GRAYA) {
		wctx->photometric = IWTIFF_PHOTO_MINISBLACK;
		wctx->bitspersample = (img->bit_depth>8)?16:8;
		wctx->samplesperpixel = 2;
		wctx->has_alpha_channel = 1;
	}
	else {
		iw_set_error(wctx->ctx,"Internal: Bad image type for TIFF");
		goto done;
	}

	wctx->bitsperpixel = wctx->bitspersample * wctx->samplesperpixel;
	wctx->bitspersample_size = 2*wctx->samplesperpixel;

	dstbpr = iwtiff_calc_bpr(wctx->bitsperpixel,img->width);

	wctx->bitmap_size = dstbpr * img->height;
	wctx->palette_size = wctx->palentries*6;
	wctx->pixdens_size = 16;

	// Figure out whether we will write a TransferFunction table.

	// If we do write such a table, this will be the size of it:
	wctx->transferfunc_numentries = 1 << wctx->bitspersample;
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

	if(wctx->bitspersample==1) {
		// TransferFunction is irrelevant for bilevel images.
		wctx->transferfunc_numentries = 0;
	}

	if(iw_get_value(wctx->ctx,IW_VAL_NO_CSLABEL)) {
		wctx->transferfunc_numentries = 0;
	}

	wctx->transferfunc_size = 2*wctx->transferfunc_numentries;

	// File header
	iwtiff_write_file_header(wctx);

	iwtiff_write_ifd(wctx);

	// Pixels
	dstrow = iw_mallocz(wctx->ctx,dstbpr);
	if(!dstrow) goto done;

	for(j=0;j<img->height;j++) {
		srcrow = &img->pixels[j*img->bpr];
		if(wctx->bitspersample==16) {
			switch(wctx->bitsperpixel) {
			case 64: iwtiff_convert_row16bps(srcrow,dstrow,img->width,4); break; // RGBA16
			case 48: iwtiff_convert_row16bps(srcrow,dstrow,img->width,3); break; // RGB16
			case 32: iwtiff_convert_row16bps(srcrow,dstrow,img->width,2); break; // GA16
			case 16: iwtiff_convert_row16bps(srcrow,dstrow,img->width,1); break; // G16
			}
		}
		else {
			switch(wctx->bitsperpixel) {
			case 32: iwtiff_convert_row8bps(srcrow,dstrow,img->width,4); break; // RGBA8
			case 24: iwtiff_convert_row8bps(srcrow,dstrow,img->width,3); break; // RGB8
			case 16: iwtiff_convert_row8bps(srcrow,dstrow,img->width,2); break; // GA8
			case 8: iwtiff_convert_row8bps(srcrow,dstrow,img->width,1); break; // G8 or palette8
			case 4: iwtiff_convert_row4(srcrow,dstrow,img->width); break; // G4 or palette4
			case 1: iwtiff_convert_row1(srcrow,dstrow,img->width); break; // G1
			}
		}
		iwtiff_write(wctx,dstrow,dstbpr);
	}

done:
	if(dstrow) iw_free(wctx->ctx,dstrow);
	return 1;
}

IW_IMPL(int) iw_write_tiff_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	struct iwtiffwcontext *wctx = NULL;
	int retval=0;
	struct iw_image img1;

	iw_zeromem(&img1,sizeof(struct iw_image));

	wctx = iw_mallocz(ctx,sizeof(struct iwtiffwcontext));
	if(!wctx) goto done;

	wctx->ctx = ctx;

	wctx->iodescr=iodescr;

	iw_get_output_image(ctx,&img1);
	wctx->img = &img1;

	wctx->palette_is_gray = iw_get_value(ctx,IW_VAL_OUTPUT_PALETTE_GRAYSCALE);

	iw_get_output_colorspace(ctx,&wctx->csdescr);

	if(wctx->img->imgtype==IW_IMGTYPE_PALETTE) {
		wctx->pal = iw_get_output_palette(ctx);
		if(!wctx->pal) goto done;
	}

	iwtiff_write_main(wctx);

	retval=1;

done:
	if(wctx) iw_free(ctx,wctx);
	return retval;
}
