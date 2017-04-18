// imagew-miff.c
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

// !!! Portability warning: This module assumes that the host system uses
// standard IEEE 754 floating point format.

#include "imagew-config.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define IW_INCLUDE_UTIL_FUNCTIONS
#include "imagew.h"

struct iwmiffrcontext {
	int host_endian;
	struct iw_iodescr *iodescr;
	struct iw_context *ctx;
	struct iw_image *img;
	int read_error_flag;
	int error_flag;
	int has_alpha;
	int is_grayscale;
	int profile_length;
	int density_units; // 0=unknown, 1=cm
	int density_known;
	int compression;
	int miff_bitdepth;
	double density_x, density_y;
	struct iw_csdescr csdescr;

	iw_byte *cbuf; // A buffer for compressed data
	size_t cbuf_alloc;

	// zmod: Pointer to the struct containing the zlib functions. If this is NULL,
	// it means zlib compression is not supported.
	struct iw_zlib_module *zmod;
	struct iw_zlib_context *zctx;
};

static int iwmiff_read(struct iwmiffrcontext *rctx,
		iw_byte *buf, size_t buflen)
{
	int ret;
	size_t bytesread = 0;

	ret = (*rctx->iodescr->read_fn)(rctx->ctx,rctx->iodescr,
		buf,buflen,&bytesread);
	if(!ret || bytesread!=buflen) {
		rctx->read_error_flag=1;
		return 0;
	}
	return 1;
}

static iw_byte iwmiff_read_byte(struct iwmiffrcontext *rctx)
{
	iw_byte buf[1];
	int ret;
	size_t bytesread = 0;

	// TODO: buffering

	ret = (*rctx->iodescr->read_fn)(rctx->ctx,rctx->iodescr,
		buf,1,&bytesread);
	if(!ret || bytesread!=1) {
		rctx->read_error_flag=1;
		return '\0';
	}
	return buf[0];
}

static unsigned int iwmiff_read_uint32(struct iwmiffrcontext *rctx)
{
	iw_byte buf[4];
	int ret;
	ret = iwmiff_read(rctx,buf,4);
	if(!ret) return 0;
	return iw_get_ui32be(buf);
}

static int iwmiff_read_zip_compressed_row(struct iwmiffrcontext *rctx,
	iw_byte *buf, size_t buflen)
{
	size_t cmprsize;
	int retval=0;
	int ret;

	// If we don't have a decompression object yet, create one.
	if(!rctx->zctx) {
		rctx->zctx = rctx->zmod->inflate_init(rctx->ctx);
		if(!rctx->zctx) goto done;
	}

	// In compressed MIFF files, each row of pixels is compressed independently,
	// and preceded by a byte count.
	cmprsize = iwmiff_read_uint32(rctx);
	if(rctx->read_error_flag) goto done;

	// When compressing, zlib supposedly never increases the size by more than
	// 5 bytes per 16K. If the byte count is much more than that, give up.
	if(cmprsize > buflen+100+buflen/1024) {
		iw_set_error(rctx->ctx,"MIFF: Unsupported file or invalid Zip-compressed data");
		goto done;
	}

	// If necessary, allocate a buffer to read the row into.
	if(rctx->cbuf_alloc < cmprsize) {
		if(rctx->cbuf) {
			iw_free(rctx->ctx,rctx->cbuf);
			rctx->cbuf = NULL;
		}
	}
	if(!rctx->cbuf) {
		rctx->cbuf = iw_malloc(rctx->ctx, cmprsize+1024);
		if(!rctx->cbuf) goto done;
	}

	// Read a row of compressed data from the file
	ret = iwmiff_read(rctx,rctx->cbuf,cmprsize);
	if(!ret) goto done;

	// Decompress the row
	ret = rctx->zmod->inflate_item(rctx->zctx,rctx->cbuf,cmprsize,buf,buflen);
	if(!ret) goto done;

	retval = 1;
done:
	return retval;
}

static int iwmiff_read_and_uncompress_row(struct iwmiffrcontext *rctx,
	iw_byte *buf, size_t buflen)
{
	if(rctx->compression==IW_COMPRESSION_NONE) {
		return iwmiff_read(rctx,buf,buflen);
	}
	else if(rctx->compression==IW_COMPRESSION_ZIP) {
		return iwmiff_read_zip_compressed_row(rctx,buf,buflen);
	}
	return 0;
}

static void iwmiff_parse_density(struct iwmiffrcontext *rctx, const char *val)
{
	char *p;

	p=strchr(val,'x');
	if(!p) return;
	rctx->density_x = atof(val);
	rctx->density_y = atof(&p[1]);
	rctx->density_known = 1;
}

static int hexdigit_value(char d)
{
	if(d>='0' && d<='9') return ((int)d)-'0';
	if(d>='a' && d<='f') return ((int)d)+10-'a';
	if(d>='A' && d<='F') return ((int)d)+10-'A';
	return 0;
}

static double hexvalue4(const char *s)
{
	return ((double)(4096*hexdigit_value(s[0]) + 256*hexdigit_value(s[1]) +
		16*hexdigit_value(s[2]) + hexdigit_value(s[3])))/65535.0;
}

static void iwmiff_parse_bkgd_color(struct iwmiffrcontext *rctx, const char *val)
{
	struct iw_color clr;
	size_t len;

	len = strlen(val);

	// The only color format we support is #rrrrggggbbbb[aaaa].
	if((len!=13 && len!=17) || val[0]!='#') {
		return;
	}
	clr.c[0] = hexvalue4(&val[1]);
	clr.c[1] = hexvalue4(&val[5]);
	clr.c[2] = hexvalue4(&val[9]);
	if(len==17)
		clr.c[3] = hexvalue4(&val[13]);
	else
		clr.c[3] = 1.0;
	iw_set_input_bkgd_label_2(rctx->ctx, &clr);
}

// Called for each attribute in the header of a MIFF file.
static void iwmiff_found_attribute(struct iwmiffrcontext *rctx,
  const char *name, const char *val)
{
	double tmpd;

	if(rctx->error_flag) return;

	if(!strcmp(name,"matte")) {
		if(val[0]=='T') rctx->has_alpha = 1;
	}
	else if(!strcmp(name,"class")) {
		if(iw_stricmp(val,"DirectClass")) {
			iw_set_error(rctx->ctx,"MIFF: Unsupported image class");
			rctx->error_flag=1;
		}
	}
	else if(!strcmp(name,"columns")) {
		rctx->img->width = atoi(val);
	}
	else if(!strcmp(name,"rows")) {
		rctx->img->height = atoi(val);
	}
	else if(!strcmp(name,"colorspace")) {
		if(!iw_stricmp(val,"RGB")) {
			;
		}
		else if(!iw_stricmp(val,"sRGB")) {
			iw_make_srgb_csdescr_2(&rctx->csdescr);
		}
		else if(!iw_stricmp(val,"Gray")) {
			rctx->is_grayscale = 1;
		}
		else {
			iw_set_error(rctx->ctx,"MIFF: Unsupported colorspace");
			rctx->error_flag=1;
		}
	}
	else if(!strcmp(name,"rendering-intent")) {
		if(!iw_stricmp(val,"perceptual")) {
			rctx->img->rendering_intent = IW_INTENT_PERCEPTUAL;
		}
		else if(!iw_stricmp(val,"relative")) {
			rctx->img->rendering_intent = IW_INTENT_RELATIVE;
		}
		else if(!iw_stricmp(val,"saturation")) {
			rctx->img->rendering_intent = IW_INTENT_SATURATION;
		}
		else if(!iw_stricmp(val,"absolute")) {
			rctx->img->rendering_intent = IW_INTENT_ABSOLUTE;
		}
		else {
			iw_warning(rctx->ctx,"MIFF: Unrecognized rendering-intent");
		}
	}
	else if(!strcmp(name,"depth")) {
		rctx->miff_bitdepth = atoi(val);
		if(rctx->miff_bitdepth!=32 && rctx->miff_bitdepth!=64) {
			iw_set_errorf(rctx->ctx,"MIFF: Unsupported bit depth (%d)",rctx->miff_bitdepth);
			rctx->error_flag=1;
		}
		rctx->img->bit_depth = 32;
	}
	else if(!strcmp(name,"compression")) {
		if(!iw_stricmp(val,"None")) {
			rctx->compression = IW_COMPRESSION_NONE;
		}
		else if(!iw_stricmp(val,"Zip")) {
			if(rctx->zmod) {
				rctx->compression = IW_COMPRESSION_ZIP;
			}
			else {
				iw_set_error(rctx->ctx,"MIFF: Zip compression is not supported");
				rctx->error_flag=1;
			}
		}
		else {
			iw_set_error(rctx->ctx,"MIFF: Unsupported compression");
			rctx->error_flag=1;
		}
	}
	else if(!strcmp(name,"gamma")) {
		tmpd =  atof(val);
		if(tmpd>=0.00001 && tmpd<=10.0) {
			iw_make_gamma_csdescr(&rctx->csdescr,1.0/tmpd);
		}
	}
	else if(!strcmp(name,"background-color")) {
		iwmiff_parse_bkgd_color(rctx,val);
	}
	else if(!strcmp(name,"quantum:format")) {
		if(iw_stricmp(val,"floating-point")) {
			iw_set_error(rctx->ctx,"MIFF: Unsupported sample format");
			rctx->error_flag=1;
		}
	}
	else if(!strcmp(name,"units")) {
		if(!iw_stricmp(val,"PixelsPerCentimeter")) {
			rctx->density_units=1;
		}
	}
	else if(!strcmp(name,"resolution")) {
		iwmiff_parse_density(rctx,val);
	}
	else if(!strncmp(name,"profile:",8)) {
		// The data in a "profile" item is its size in the data section.
		// We don't support processing this data, but we have to keep track of its
		// total size, so we can skip over it.
		rctx->profile_length += atoi(val);
	}
}

static int iwmiff_read_header(struct iwmiffrcontext *rctx)
{
	char name[101];
	char val[101];
	int namelen;
	int vallen;
	char b;
	int is_whitespace;

#define STATE_NEUTRAL 0
#define STATE_READING_NAME 1
#define STATE_READING_VALUE 2
#define STATE_READING_VALUE_INQUOTE 3
	int st;

	name[0]='\0'; namelen=0;
	val[0]='\0'; vallen=0;

	st=STATE_NEUTRAL;

	while(1) {
		if(rctx->error_flag) return 0;

		b=(char)iwmiff_read_byte(rctx);
		if(rctx->read_error_flag) {
			return 0;
		}

		is_whitespace = (b>=0 && b<=32);

		if(st==STATE_NEUTRAL) {
			if(is_whitespace) continue;
			st=STATE_READING_NAME;
		}

		if(st==STATE_READING_NAME) {
			if(namelen==0 && b==':') {
				// End of header section
				st=STATE_NEUTRAL;
				break;
			}

			if(b=='=') {
				st=STATE_READING_VALUE;
				continue;
			}

			// Append char to name
			if(namelen<100) {
				name[namelen]=b;
				namelen++;
				name[namelen]='\0';
			}
		}

		if(st==STATE_READING_VALUE) {
			if(vallen==0 && b=='{') {
				st=STATE_READING_VALUE_INQUOTE;
				continue;
			}
		}

		if( (st==STATE_READING_VALUE && is_whitespace) ||
			(st==STATE_READING_VALUE_INQUOTE && b=='}') )
		{
			// Unquoted value terminated by whitespace, or
			// quoted value terminated by '}'.
			iwmiff_found_attribute(rctx,name,val);
			name[0]='\0'; namelen=0;
			val[0]='\0'; vallen=0;
			st=STATE_NEUTRAL;
			continue;
		}

		if(st==STATE_READING_VALUE || st==STATE_READING_VALUE_INQUOTE) {
			// Append char to val
			if(vallen<100) {
				val[vallen]=b;
				vallen++;
				val[vallen]='\0';
			}
			continue;
		}
	}

	// Skip the byte after the ":", which is usually Ctrl-Z.
	(void)iwmiff_read_byte(rctx);
	if(rctx->read_error_flag || rctx->error_flag) {
		return 0;
	}

	return 1;
}

static void iwmiffr_convert_row32(struct iwmiffrcontext *rctx,
  const iw_byte *src, iw_byte *dst, int nsamples)
{
	int i;
	int k;

	if(rctx->host_endian==IW_ENDIAN_LITTLE) {
		for(i=0;i<nsamples;i++) {
			for(k=0;k<4;k++) {
				dst[i*4+k] = src[i*4+3-k];
			}
		}
	}
	else {
		memcpy(dst,src,4*nsamples);
	}
}

static void iwmiffr_convert_row64_32(struct iwmiffrcontext *rctx,
  const iw_byte *src, iw_byte *dst, int nsamples)
{
	int i;
	int k;
	// !!! Portability warning: Using a union in this way may be nonportable,
	// and/or may violate strict-aliasing rules.
	union su_union64 {
		iw_byte c[8];
		iw_float64 f;
	} volatile su64;

	union su_union32 {
		iw_byte c[4];
		iw_float32 f;
	} volatile su32;

	for(i=0;i<nsamples;i++) {
		// Read 64-bit float.
		if(rctx->host_endian==IW_ENDIAN_LITTLE) {
			for(k=0;k<8;k++) {
				su64.c[k] = src[i*8+7-k];
			}
		}
		else {
			for(k=0;k<8;k++) {
				su64.c[k] = src[i*8+k];
			}
		}

		// Convert 64-bit float to 32-bit float.
		su32.f = (iw_float32)su64.f;

		// Write 32-bit float.
		for(k=0;k<4;k++) {
			dst[i*4+k] = su32.c[k];
		}
	}
}

static int iwmiff_skip_bytes(struct iwmiffrcontext *rctx, size_t n)
{
	iw_byte buf[2048];
	size_t amt;
	size_t remaining = n;
	while(remaining>0) {
		amt = remaining;
		if(amt>2048) amt=2048;
		if(!iwmiff_read(rctx,buf,amt)) return 0;
		remaining-=amt;
	}
	return 1;
}

// Skip over the ICC color profile, if present.
static int iwmiff_read_icc_profile(struct iwmiffrcontext *rctx)
{
	if(rctx->profile_length<1) return 1;
	return iwmiff_skip_bytes(rctx,(size_t)rctx->profile_length);
}

static int iwmiff_read_pixels(struct iwmiffrcontext *rctx)
{
	int samples_per_pixel;
	int samples_per_row;
	size_t tmprowsize;
	iw_byte *tmprow = NULL;
	int retval=0;
	int j;
	struct iw_image *img;

	img = rctx->img;

	samples_per_pixel = iw_imgtype_num_channels(img->imgtype);
	samples_per_row = samples_per_pixel * img->width;

	tmprowsize = (rctx->miff_bitdepth/8)*samples_per_row;
	tmprow = iw_mallocz(rctx->ctx,tmprowsize);
	if(!tmprow) goto done;

	img->bpr = tmprowsize;

	img->pixels = (iw_byte*)iw_malloc_large(rctx->ctx, img->bpr, img->height);
	if(!img->pixels) goto done;

	for(j=0;j<img->height;j++) {
		if(!iwmiff_read_and_uncompress_row(rctx,tmprow,tmprowsize))
			goto done;

		// There are two possibilities for {miff_bitdepth, img->bit_depth}:
		//  32->32, 64->32
		if(rctx->miff_bitdepth==64) {
			iwmiffr_convert_row64_32(rctx,tmprow,&img->pixels[j*img->bpr],samples_per_row);
		}
		else if(rctx->miff_bitdepth==32) {
			iwmiffr_convert_row32(rctx,tmprow,&img->pixels[j*img->bpr],samples_per_row);
		}
	}

	retval=1;

done:
	if(tmprow) free(tmprow);
	if(rctx->zmod && rctx->zctx) {
		rctx->zmod->inflate_end(rctx->zctx);
		rctx->zctx = NULL;
	}
	if(rctx->cbuf) {
		iw_free(rctx->ctx,rctx->cbuf);
		rctx->cbuf = NULL;
	}
	return retval;
}

IW_IMPL(int) iw_read_miff_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	struct iw_image img;
	struct iwmiffrcontext rctx;
	int retval=0;

	iw_zeromem(&rctx,sizeof(struct iwmiffrcontext));
	iw_zeromem(&img,sizeof(struct iw_image));

	rctx.ctx = ctx;
	rctx.host_endian = iw_get_host_endianness();
	rctx.iodescr = iodescr;
	rctx.img = &img;
	rctx.compression = IW_COMPRESSION_NONE;
	rctx.zmod = iw_get_zlib_module(ctx);

	// Assume sRGB by default
	iw_make_srgb_csdescr_2(&rctx.csdescr);

	img.sampletype = IW_SAMPLETYPE_FLOATINGPOINT;

	if(!iwmiff_read_header(&rctx))
		goto done;

	if(img.bit_depth!=32 && img.bit_depth!=64) {
		iw_set_error(ctx, "MIFF: Unsupported or unset bit depth");
		goto done;

	}

	if(!iw_check_image_dimensions(rctx.ctx,img.width,img.height))
		goto done;

	if(rctx.is_grayscale) {
		img.imgtype = rctx.has_alpha ? IW_IMGTYPE_GRAYA : IW_IMGTYPE_GRAY;
		img.native_grayscale = 1;
	}
	else {
		img.imgtype = rctx.has_alpha ? IW_IMGTYPE_RGBA : IW_IMGTYPE_RGB;
	}

	if(rctx.density_known && rctx.density_units==1) {
		img.density_x = rctx.density_x*100.0;
		img.density_y = rctx.density_y*100.0;
		img.density_code = IW_DENSITY_UNITS_PER_METER;
	}

	if(!iwmiff_read_icc_profile(&rctx))
		goto done;

	if(!iwmiff_read_pixels(&rctx))
		goto done;

	iw_set_input_image(ctx, &img);

	iw_set_input_colorspace(ctx,&rctx.csdescr);

	retval = 1;

done:
	if(!retval) {
		iw_set_error(ctx,"Failed to read MIFF file");
		iw_free(ctx, img.pixels);
	}
	return retval;
}

struct iwmiffwcontext {
	int has_alpha;
	int host_endian;
	int compression;
	struct iw_iodescr *iodescr;
	struct iw_context *ctx;
	struct iw_image *img;

	iw_byte *cbuf; // A buffer for compressed data
	size_t cbuf_alloc;

	struct iw_zlib_module *zmod;
	struct iw_zlib_context *zctx;
};

static void iwmiff_write(struct iwmiffwcontext *wctx, const void *buf, size_t n)
{
	(*wctx->iodescr->write_fn)(wctx->ctx,wctx->iodescr,buf,n);
}

static void iwmiff_write_uint32(struct iwmiffwcontext *wctx, unsigned int n)
{
	iw_byte buf[4];

	iw_set_ui32be(buf,n);
	iwmiff_write(wctx,buf,4);
}

static void iwmiff_write_sz(struct iwmiffwcontext *wctx, const char *s)
{
	iwmiff_write(wctx,s,strlen(s));
}

static void iwmiff_writef(struct iwmiffwcontext *wctx, const char *fmt, ...)
  iw_gnuc_attribute ((format (printf, 2, 3)));

static void iwmiff_writef(struct iwmiffwcontext *wctx, const char *fmt, ...)
{
	char buf[500];
	va_list ap;

	va_start(ap, fmt);
	iw_vsnprintf(buf,sizeof(buf),fmt,ap);
	va_end(ap);

	iwmiff_write_sz(wctx,buf);
}

static void write_bkgdlabel(struct iwmiffwcontext *wctx)
{
	char tmpbuf[32];
	unsigned int bkgd[4];
	int k;

	for(k=0;k<4;k++) {
		bkgd[k] = iw_color_get_int_sample(&wctx->img->bkgdlabel,k,65535);
	}

	if(k<=0) return;
	if(bkgd[3]>=65535) {
		// opaque background
		iw_snprintf(tmpbuf,sizeof(tmpbuf),"#%04x%04x%04x",bkgd[0],bkgd[1],bkgd[2]);
	}
	else {
		iw_snprintf(tmpbuf,sizeof(tmpbuf),"#%04x%04x%04x%04x",bkgd[0],bkgd[1],bkgd[2],bkgd[3]);
	}

	iwmiff_writef(wctx,"background-color=%s\n",tmpbuf);
}

static void iwmiff_write_header(struct iwmiffwcontext *wctx)
{
	const char *tmps;

	iwmiff_write_sz(wctx,"id=ImageMagick  version=1.0\n");
	iwmiff_writef(wctx,"class=DirectClass  colors=0  matte=%s\n",wctx->has_alpha?"True":"False");
	iwmiff_writef(wctx,"columns=%d  rows=%d  depth=%d\n",wctx->img->width,
		wctx->img->height,wctx->img->bit_depth);

	if(wctx->img->imgtype==IW_IMGTYPE_GRAY) {
		iwmiff_write_sz(wctx,"type=Grayscale\ncolorspace=Gray\n");
	}
	else if(wctx->img->imgtype==IW_IMGTYPE_GRAYA) {
		iwmiff_write_sz(wctx,"type=GrayscaleMatte\ncolorspace=Gray\n");
	}
	else {
		iwmiff_write_sz(wctx,"colorspace=RGB\n");
	}

	iwmiff_write_sz(wctx,"compression=");
	switch(wctx->compression) {
	case IW_COMPRESSION_ZIP:
		iwmiff_write_sz(wctx,"Zip");
		break;
	default:
		iwmiff_write_sz(wctx,"None");
	}

	iwmiff_write_sz(wctx,"  quality=0\n");

	if(wctx->img->density_code==IW_DENSITY_UNITS_PER_METER) {
		iwmiff_write_sz(wctx,"units=PixelsPerCentimeter\n");
		iwmiff_writef(wctx,"resolution=%.2fx%.2f\n",wctx->img->density_x/100.0,wctx->img->density_y/100.0);
	}

	switch(wctx->img->rendering_intent) {
	case IW_INTENT_PERCEPTUAL: tmps="Perceptual"; break;
	case IW_INTENT_RELATIVE:   tmps="Relative";   break;
	case IW_INTENT_SATURATION: tmps="Saturation"; break;
	case IW_INTENT_ABSOLUTE:   tmps="Absolute";   break;
	default: tmps=NULL;
	}
	if(tmps) {
		iwmiff_writef(wctx,"rendering-intent=%s\n", tmps);
	}

	iwmiff_write_sz(wctx,"gamma=1.0\n");

	if(wctx->img->has_bkgdlabel) {
		write_bkgdlabel(wctx);
	}

	iwmiff_write_sz(wctx,"quantum:format={floating-point}\n");

	iwmiff_write(wctx,"\x0c\x0a\x3a\x1a",4);
}

static void iwmiffw_convert_row32(struct iwmiffwcontext *wctx,
	const iw_byte *srcrow, iw_byte *dstrow, int numsamples)
{
	int i,j;

	// Binary sections of the MIFF format use big-endian byte order.
	if(wctx->host_endian==IW_ENDIAN_LITTLE) {
		for(i=0;i<numsamples;i++) {
			for(j=0;j<4;j++) {
				dstrow[i*4+j] = srcrow[i*4+3-j];
			}
		}
	}
	else {
		memcpy(dstrow,srcrow,4*numsamples);
	}
}

static int iwmiff_write_zip_compressed_row(struct iwmiffwcontext *wctx,
	iw_byte *buf, size_t buflen)
{
	int retval=0;
	int ret;
	size_t out_used;

	// If we don't have a compression object yet, create one.
	if(!wctx->zctx) {
		wctx->zctx = wctx->zmod->deflate_init(wctx->ctx);
		if(!wctx->zctx) goto done;
	}

	// Allocate a buffer for the compressed data, if necessary.
	// We assume that all rows are the same size, so we'll never have to
	// increase the buffer size.
	if(!wctx->cbuf) {
		wctx->cbuf_alloc = buflen+100+buflen/1024;
		wctx->cbuf = iw_malloc(wctx->ctx,wctx->cbuf_alloc);
		if(!wctx->cbuf) goto done;
	}

	ret = wctx->zmod->deflate_item(wctx->zctx,buf,buflen,wctx->cbuf,wctx->cbuf_alloc,&out_used);
	if(!ret) goto done;

	// Write the 'count' that precedes each segment of compressed data.
	iwmiff_write_uint32(wctx,(unsigned int)out_used);
	iwmiff_write(wctx,wctx->cbuf,out_used);
	retval=1;
done:
	return retval;
}

static int iwmiff_compress_and_write_row(struct iwmiffwcontext *wctx,
	iw_byte *dstrow, size_t dstbpr)
{
	if(wctx->compression==IW_COMPRESSION_NONE) {
		iwmiff_write(wctx,dstrow,dstbpr);
		return 1;
	}
	else if(wctx->compression==IW_COMPRESSION_ZIP) {
		return iwmiff_write_zip_compressed_row(wctx,dstrow,dstbpr);
	}
	return 0;
}

static int iwmiff_write_main(struct iwmiffwcontext *wctx)
{
	struct iw_image *img;
	iw_byte *dstrow = NULL;
	size_t dstbpr;
	int j;
	const iw_byte *srcrow;
	int bytes_per_sample;
	int num_channels;
	int cmpr_req;
	int retval=0;

	img = wctx->img;

	if(img->sampletype!=IW_SAMPLETYPE_FLOATINGPOINT) {
		iw_set_error(wctx->ctx,"Internal: Bad image type for MIFF");
		goto done;
	}

	switch(img->imgtype) {
	case IW_IMGTYPE_GRAY:
		num_channels=1;
		break;
	case IW_IMGTYPE_GRAYA:
		wctx->has_alpha=1;
		num_channels=2;
		break;
	case IW_IMGTYPE_RGB:
		num_channels=3;
		break;
	case IW_IMGTYPE_RGBA:
		wctx->has_alpha=1;
		num_channels=4;
		break;
	default:
		goto done;
	}

	bytes_per_sample = img->bit_depth / 8;

	// dstbpr = number of bytes per row stored in the file.
	// For grayscale images, not all bytes are used. Extra bytes will be stored
	// in the file with the value 0.
	dstbpr = bytes_per_sample * num_channels * img->width;

	cmpr_req = iw_get_value(wctx->ctx,IW_VAL_COMPRESSION);

	// If the caller requested no compression, do that.
	// Otherwise use Zip compression if the zlib module is available.
	switch(cmpr_req) {
	case IW_COMPRESSION_NONE:
		wctx->compression=IW_COMPRESSION_NONE;
		break;
	default:
		if(wctx->zmod)
			wctx->compression=IW_COMPRESSION_ZIP;
		else
			wctx->compression=IW_COMPRESSION_NONE;
	}

	iwmiff_write_header(wctx);

	dstrow = iw_mallocz(wctx->ctx,dstbpr);
	if(!dstrow) goto done;

	for(j=0;j<img->height;j++) {
		srcrow = &img->pixels[j*img->bpr];
		iwmiffw_convert_row32(wctx,srcrow,dstrow,img->width*num_channels);
		if(!iwmiff_compress_and_write_row(wctx,dstrow,dstbpr)) goto done;
	}

	retval = 1;
done:
	if(dstrow) iw_free(wctx->ctx,dstrow);
	if(wctx->zmod && wctx->zctx) {
		wctx->zmod->deflate_end(wctx->zctx);
		wctx->zctx = NULL;
	}
	if(wctx->cbuf) {
		iw_free(wctx->ctx,wctx->cbuf);
		wctx->cbuf = NULL;
	}
	return retval;
}

IW_IMPL(int) iw_write_miff_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	struct iwmiffwcontext wctx;
	int retval=0;
	struct iw_image img1;

	iw_zeromem(&img1,sizeof(struct iw_image));

	iw_zeromem(&wctx,sizeof(struct iwmiffwcontext));

	wctx.ctx = ctx;

	wctx.iodescr=iodescr;

	wctx.host_endian=iw_get_host_endianness();
	wctx.zmod=iw_get_zlib_module(ctx);

	iw_get_output_image(ctx,&img1);
	wctx.img = &img1;

	if(!iwmiff_write_main(&wctx)) goto done;

	retval=1;
done:
	return retval;
}
