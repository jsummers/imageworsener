// imagew-gif.c
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

// This is a self-contained GIF image decoder.
// It supports a single image only, so it does not support animated GIFs,
// or any GIF where the main image is constructed from multiple sub-images.

#include "imagew-config.h"

#include <stdlib.h>
#include <string.h>

#define IW_INCLUDE_UTIL_FUNCTIONS
#include "imagew.h"

struct iwgifrcontext {
	struct iw_iodescr *iodescr;
	struct iw_context *ctx;
	struct iw_image *img;

	struct iw_csdescr csdescr;
	int page; // Page to read. 1=first

	int screen_width, screen_height; // Same as .img->width, .img->height
	int image_width, image_height;
	int image_left, image_top;

	int include_screen; // Do we paint the image onto the "screen", or just extract it?
	int screen_initialized;
	int pages_seen;
	int interlaced;
	int bytes_per_pixel;
	int has_transparency;
	int has_bg_color;
	int bg_color_index;
	int trans_color_index;

	size_t pixels_set; // Number of pixels decoded so far
	size_t total_npixels; // Total number of pixels in the "image" (not the "screen")

	iw_byte **row_pointers;

	struct iw_palette colortable;

	// A buffer used when reading the GIF file.
	// The largest block we need to read is a 256-color palette.
	iw_byte rbuf[768];
};

static int iwgif_read(struct iwgifrcontext *rctx,
		iw_byte *buf, size_t buflen)
{
	int ret;
	size_t bytesread = 0;

	ret = (*rctx->iodescr->read_fn)(rctx->ctx,rctx->iodescr,
		buf,buflen,&bytesread);
	if(!ret || bytesread!=buflen) {
		return 0;
	}
	return 1;
}

static int iwgif_read_file_header(struct iwgifrcontext *rctx)
{
	if(!iwgif_read(rctx,rctx->rbuf,6)) return 0;
	if(rctx->rbuf[0]!='G' || rctx->rbuf[1]!='I' || rctx->rbuf[2]!='F') {
		iw_set_error(rctx->ctx,"Not a GIF file");
		return 0;
	}
	return 1;
}

static int iwgif_read_screen_descriptor(struct iwgifrcontext *rctx)
{
	int has_global_ct;
	int global_ct_size;
	int aspect_ratio_code;

	// The screen descriptor is always 7 bytes in size.
	if(!iwgif_read(rctx,rctx->rbuf,7)) return 0;
	rctx->screen_width = (int)iw_get_ui16le(&rctx->rbuf[0]);
	rctx->screen_height = (int)iw_get_ui16le(&rctx->rbuf[2]);
	// screen_width and _height may be updated in iwgif_init_screen().

	has_global_ct = (int)((rctx->rbuf[4]>>7)&0x01);

	if(has_global_ct) {
		// Size of global color table
		global_ct_size = (int)(rctx->rbuf[4]&0x07);
		rctx->colortable.num_entries = 1<<(1+global_ct_size);

		// Background color
		rctx->bg_color_index = (int)rctx->rbuf[5];
		if(rctx->bg_color_index < rctx->colortable.num_entries)
			rctx->has_bg_color = 1;
	}

	aspect_ratio_code = (int)rctx->rbuf[6];
	if(aspect_ratio_code!=0) {
		// [aspect ratio = (pixel width)/(pixel height)]
		rctx->img->density_code = IW_DENSITY_UNITS_UNKNOWN;
		rctx->img->density_x = 64000.0/(double)(aspect_ratio_code + 15);
		rctx->img->density_y = 1000.0;
	}

	return 1;
}

// Read a global or local palette into memory.
// ct->num_entries must be set by caller
static int iwgif_read_color_table(struct iwgifrcontext *rctx, struct iw_palette *ct)
{
	int i;
	if(ct->num_entries<1) return 1;

	if(!iwgif_read(rctx,rctx->rbuf,3*ct->num_entries)) return 0;
	for(i=0;i<ct->num_entries;i++) {
		ct->entry[i].r = rctx->rbuf[3*i+0];
		ct->entry[i].g = rctx->rbuf[3*i+1];
		ct->entry[i].b = rctx->rbuf[3*i+2];
	}
	return 1;
}

static int iwgif_skip_subblocks(struct iwgifrcontext *rctx)
{
	iw_byte subblock_size;

	while(1) {
		// Read the subblock size
		if(!iwgif_read(rctx,rctx->rbuf,1)) return 0;

		subblock_size = rctx->rbuf[0];
		// A size of 0 marks the end of the subblocks.
		if(subblock_size==0) return 1;

		// Read the subblock's data
		if(!iwgif_read(rctx,rctx->rbuf,(size_t)subblock_size)) return 0;
	}
}

// We need transparency information, so we have to process graphic control
// extensions.
static int iwgif_read_graphic_control_ext(struct iwgifrcontext *rctx)
{
	int retval;

	// Read 6 bytes:
	//  The first is the subblock size, which must be 4.
	//  The last is the block terminator.
	//  The middle 4 contain the actual data.
	if(!iwgif_read(rctx,rctx->rbuf,6)) goto done;

	if(rctx->rbuf[0]!=4) goto done;
	if(rctx->rbuf[5]!=0) goto done;
	rctx->has_transparency = (int)((rctx->rbuf[1])&0x01);
	if(rctx->has_transparency) {
		rctx->trans_color_index = (int)rctx->rbuf[4];
	}

	retval=1;
done:
	return retval;
}

static int iwgif_read_extension(struct iwgifrcontext *rctx)
{
	int retval=0;
	iw_byte ext_type;

	if(!iwgif_read(rctx,rctx->rbuf,1)) goto done;
	ext_type=rctx->rbuf[0];

	switch(ext_type) {
	case 0xf9:
		if(rctx->page == rctx->pages_seen+1) {
			if(!iwgif_read_graphic_control_ext(rctx)) goto done;
		}
		else {
			// This extension's scope does not include the image we're
			// processing, so ignore it.
			if(!iwgif_skip_subblocks(rctx)) goto done;
		}
		break;
	default:
		if(!iwgif_skip_subblocks(rctx)) goto done;
	}

	retval=1;
done:
	return retval;
}

// Set the (rctx->pixels_set + offset)th pixel in the logical image to the
// color represented by palette entry #coloridx.
static void iwgif_record_pixel(struct iwgifrcontext *rctx, unsigned int coloridx,
		int offset)
{
	struct iw_image *img;
	unsigned int r,g,b,a;
	size_t pixnum;
	size_t xi,yi; // position in image coordinates
	size_t xs,ys; // position in screen coordinates
	iw_byte *ptr;

	img = rctx->img;

	// Figure out which pixel to set.

	pixnum = rctx->pixels_set + offset;
	xi = pixnum%rctx->image_width;
	yi = pixnum/rctx->image_width;
	xs = rctx->image_left + xi;
	ys = rctx->image_top + yi;

	// Make sure the coordinate is within the image, and on the screen.
	if(yi>=(size_t)rctx->image_height) return;
	if(xs>=(size_t)rctx->screen_width) return;
	if(ys>=(size_t)rctx->screen_height) return;

	// Because of how we de-interlace, it's not obvious whether the Y coordinate
	// is on the screen. The easiest way is to check if the row pointer is NULL.
	if(rctx->row_pointers[yi]==NULL) return;

	// Figure out what color to set the pixel to.

	if(coloridx<(unsigned int)rctx->colortable.num_entries) {
		r=rctx->colortable.entry[coloridx].r;
		g=rctx->colortable.entry[coloridx].g;
		b=rctx->colortable.entry[coloridx].b;
		a=rctx->colortable.entry[coloridx].a;
	}
	else {
		return; // Illegal palette index
	}

	// Set the pixel.

	ptr = &rctx->row_pointers[yi][rctx->bytes_per_pixel*xi];
	ptr[0]=r; ptr[1]=g; ptr[2]=b;
	if(img->imgtype==IW_IMGTYPE_RGBA) {
		ptr[3]=a;
	}
}

////////////////////////////////////////////////////////
//                    LZW decoder
////////////////////////////////////////////////////////

struct lzw_tableentry {
	iw_uint16 parent; // pointer to previous table entry (if not a root code)
	iw_uint16 length;
	iw_byte firstchar;
	iw_byte lastchar;
};

struct lzwdeccontext {
	unsigned int root_codesize;
	unsigned int current_codesize;
	int eoi_flag;
	unsigned int oldcode;
	unsigned int pending_code;
	unsigned int bits_in_pending_code;
	unsigned int num_root_codes;
	int ncodes_since_clear;

	unsigned int clear_code;
	unsigned int eoi_code;
	unsigned int last_code_added;

	unsigned int ct_used; // Number of items used in the code table
	struct lzw_tableentry ct[4096]; // Code table
};

static void lzw_init(struct lzwdeccontext *d, unsigned int root_codesize)
{
	unsigned int i;

	iw_zeromem(d,sizeof(struct lzwdeccontext));

	d->root_codesize = root_codesize;
	d->num_root_codes = 1<<d->root_codesize;
	d->clear_code = d->num_root_codes;
	d->eoi_code = d->num_root_codes+1;
	for(i=0;i<d->num_root_codes;i++) {
		d->ct[i].parent = 0;
		d->ct[i].length = 1;
		d->ct[i].lastchar = (iw_byte)i;
		d->ct[i].firstchar = (iw_byte)i;
	}
}

static void lzw_clear(struct lzwdeccontext *d)
{
	d->ct_used = d->num_root_codes+2;
	d->current_codesize = d->root_codesize+1;
	d->ncodes_since_clear=0;
	d->oldcode=0;
}

// Decode an LZW code to one or more pixels, and record it in the image.
static void lzw_emit_code(struct iwgifrcontext *rctx, struct lzwdeccontext *d,
		unsigned int first_code)
{
	unsigned int code;
	code = first_code;

	// An LZW code may decode to more than one pixel. Note that the pixels for
	// an LZW code are decoded in reverse order (right to left).

	while(1) {
		iwgif_record_pixel(rctx, (unsigned int)d->ct[code].lastchar, (int)(d->ct[code].length-1));
		if(d->ct[code].length<=1) break;
		// The codes are structured as a "forest" (multiple trees).
		// Go to the parent code, which will have a length 1 less than this one.
		code = (unsigned int)d->ct[code].parent;
	}

	// Track the total number of pixels decoded in this image.
	rctx->pixels_set += d->ct[first_code].length;
}

// Add a code to the dictionary.
// Sets d->last_code_added to the position where it was added.
// Returns 1 if successful, 0 if table is full.
static int lzw_add_to_dict(struct lzwdeccontext *d, unsigned int oldcode, iw_byte val)
{
	static const unsigned int last_code_of_size[] = {
		// The first 3 values are unused.
		0,0,0,7,15,31,63,127,255,511,1023,2047,4095
	};
	unsigned int newpos;

	if(d->ct_used>=4096) {
		d->last_code_added = 0;
		return 0;
	}

	newpos = d->ct_used;
	d->ct_used++;

	d->ct[newpos].parent = (iw_uint16)oldcode;
	d->ct[newpos].length = d->ct[oldcode].length + 1;
	d->ct[newpos].firstchar = d->ct[oldcode].firstchar;
	d->ct[newpos].lastchar = val;

	// If we've used the last code of this size, we need to increase the codesize.
	if(newpos == last_code_of_size[d->current_codesize]) {
		if(d->current_codesize<12) {
			d->current_codesize++;
		}
	}

	d->last_code_added = newpos;
	return 1;
}

// Process a single LZW code that was read from the input stream.
static int lzw_process_code(struct iwgifrcontext *rctx, struct lzwdeccontext *d,
		unsigned int code)
{
	if(code==d->eoi_code) {
		d->eoi_flag=1;
		return 1;
	}

	if(code==d->clear_code) {
		lzw_clear(d);
		return 1;
	}

	d->ncodes_since_clear++;

	if(d->ncodes_since_clear==1) {
		// Special case for the first code.
		lzw_emit_code(rctx,d,code);
		d->oldcode = code;
		return 1;
	}

	// Is code in code table?
	if(code < d->ct_used) {
		// Yes, code is in table.
		lzw_emit_code(rctx,d,code);

		// Let k = the first character of the translation of the code.
		// Add <oldcode>k to the dictionary.
		lzw_add_to_dict(d,d->oldcode,d->ct[code].firstchar);
	}
	else {
		// No, code is not in table.
		if(d->oldcode>=d->ct_used) {
			iw_set_error(rctx->ctx,"GIF decoding error");
			return 0;
		}

		// Let k = the first char of the translation of oldcode.
		// Add <oldcode>k to the dictionary.
		if(lzw_add_to_dict(d,d->oldcode,d->ct[d->oldcode].firstchar)) {
			// Write <oldcode>k to the output stream.
			lzw_emit_code(rctx,d,d->last_code_added);
		}
	}
	d->oldcode = code;

	return 1;
}

// Decode as much as possible of the provided LZW-encoded data.
// Any unfinished business is recorded, to be continued the next time
// this function is called.
static int lzw_process_bytes(struct iwgifrcontext *rctx, struct lzwdeccontext *d,
	iw_byte *data, size_t data_size)
{
	size_t i;
	int b;
	int retval=0;

	for(i=0;i<data_size;i++) {
		// Look at the bits one at a time.
		for(b=0;b<8;b++) {
			if(d->eoi_flag) { // Stop if we've seen an EOI (end of image) code.
				retval=1;
				goto done;
			}

			if(data[i]&(1<<b))
				d->pending_code |= 1<<d->bits_in_pending_code;
			d->bits_in_pending_code++;

			// When we get enough bits to form a complete LZW code, process it.
			if(d->bits_in_pending_code >= d->current_codesize) {
				if(!lzw_process_code(rctx,d,d->pending_code)) goto done;
				d->pending_code=0;
				d->bits_in_pending_code=0;
			}
		}
	}
	retval=1;

done:
	return retval;
}

////////////////////////////////////////////////////////

// Allocate and set up the global "screen".
static int iwgif_init_screen(struct iwgifrcontext *rctx)
{
	struct iw_image *img;
	int bg_visible=0;
	int retval=0;

	if(rctx->screen_initialized) return 1;
	rctx->screen_initialized = 1;

	img = rctx->img;

	if(!rctx->include_screen) {
		// If ->include_screen is disabled, pretend the screen is the same size as
		// the GIF image, and pretend the GIF image is positioned at (0,0).
		rctx->screen_width = rctx->image_width;
		rctx->screen_height = rctx->image_height;
		rctx->image_left = 0;
		rctx->image_top = 0;
	}

	img->width = rctx->screen_width;
	img->height = rctx->screen_height;
	if(!iw_check_image_dimensions(rctx->ctx,img->width,img->height)) {
		return 0;
	}

	if(rctx->image_left>0 || rctx->image_top>0 ||
		(rctx->image_left+rctx->image_width < rctx->screen_width) ||
		(rctx->image_top+rctx->image_height < rctx->screen_height) )
	{
		// Image does not cover the entire "screen". We'll make the exposed
		// regions of the screen transparent, so set a flag to let us know we
		// need an image type that supports transparency.
		bg_visible = 1;
	}

	// Allocate IW image
	if(rctx->has_transparency || bg_visible) {
		rctx->bytes_per_pixel=4;
		img->imgtype = IW_IMGTYPE_RGBA;
	}
	else {
		rctx->bytes_per_pixel=3;
		img->imgtype = IW_IMGTYPE_RGB;
	}
	img->bit_depth = 8;
	img->bpr = rctx->bytes_per_pixel * img->width;

	img->pixels = (iw_byte*)iw_malloc_large(rctx->ctx, img->bpr, img->height);
	if(!img->pixels) goto done;

	// Start by clearing the screen to black, or transparent black.
	iw_zeromem(img->pixels,img->bpr*img->height);

	retval=1;
done:
	return retval;
}

// Make an array of pointers into the global screen, which point to the
// start of each row in the local image. This will be useful for
// de-interlacing.
static int iwgif_make_row_pointers(struct iwgifrcontext *rctx)
{
	struct iw_image *img;
	int pass;
	int startrow, rowskip;
	int rowcount;
	int row;

	if(rctx->row_pointers) iw_free(rctx->ctx,rctx->row_pointers);
	rctx->row_pointers = (iw_byte**)iw_malloc(rctx->ctx, sizeof(iw_byte*)*rctx->image_height);
	if(!rctx->row_pointers) return 0;

	img = rctx->img;

	if(rctx->interlaced) {
		// Image is interlaced. Rearrange the row pointers, so that it will be
		// de-interlaced as it is decoded.
		rowcount=0;
		for(pass=1;pass<=4;pass++) {
			if(pass==1) { startrow=0; rowskip=8; }
			else if(pass==2) { startrow=4; rowskip=8; }
			else if(pass==3) { startrow=2; rowskip=4; }
			else { startrow=1; rowskip=2; }

			for(row=startrow;row<rctx->image_height;row+=rowskip) {
				if(rctx->image_top+row < rctx->screen_height) {
					rctx->row_pointers[rowcount] = &img->pixels[(rctx->image_top+row)*img->bpr + (rctx->image_left)*rctx->bytes_per_pixel];
				}
				else {
					rctx->row_pointers[rowcount] = NULL;
				}
				rowcount++;
			}
		}
	}
	else {
		// Image is not interlaced.
		for(row=0;row<rctx->image_height;row++) {
			if(rctx->image_top+row < rctx->screen_height) {
				rctx->row_pointers[row] = &img->pixels[(rctx->image_top+row)*img->bpr + (rctx->image_left)*rctx->bytes_per_pixel];
			}
			else {
				rctx->row_pointers[row] = NULL;
			}
		}
	}
	return 1;
}

static int iwgif_skip_image(struct iwgifrcontext *rctx)
{
	int has_local_ct;
	int local_ct_size;
	int ct_num_entries;
	int retval=0;

	// Read image header information
	if(!iwgif_read(rctx,rctx->rbuf,9)) goto done;

	has_local_ct = (int)((rctx->rbuf[8]>>7)&0x01);
	if(has_local_ct) {
		local_ct_size = (int)(rctx->rbuf[8]&0x07);
		ct_num_entries = 1<<(1+local_ct_size);
	}

	// Skip the local color table
	if(has_local_ct) {
		if(!iwgif_read(rctx,rctx->rbuf,3*ct_num_entries)) goto done;
	}

	// Skip the LZW code size
	if(!iwgif_read(rctx,rctx->rbuf,1)) goto done;

	// Skip the image pixels
	if(!iwgif_skip_subblocks(rctx)) goto done;

	// Reset anything that might have been set by a graphic control extension.
	// Their scope is the first image that follows them.
	rctx->has_transparency = 0;

	retval=1;

done:
	return retval;
}

static int iwgif_read_image(struct iwgifrcontext *rctx)
{
	int retval=0;
	struct lzwdeccontext d;
	size_t subblocksize;
	int has_local_ct;
	int local_ct_size;

	unsigned int root_codesize;

	// Read image header information
	if(!iwgif_read(rctx,rctx->rbuf,9)) goto done;

	rctx->image_left = (int)iw_get_ui16le(&rctx->rbuf[0]);
	rctx->image_top = (int)iw_get_ui16le(&rctx->rbuf[2]);
	// image_left and _top may be updated in iwgif_init_screen().

	rctx->image_width = (int)iw_get_ui16le(&rctx->rbuf[4]);
	rctx->image_height = (int)iw_get_ui16le(&rctx->rbuf[6]);
	if(rctx->image_width<1 || rctx->image_height<1) {
		iw_set_error(rctx->ctx, "Invalid image dimensions");
		goto done;
	}

	rctx->interlaced = (int)((rctx->rbuf[8]>>6)&0x01);

	has_local_ct = (int)((rctx->rbuf[8]>>7)&0x01);
	if(has_local_ct) {
		local_ct_size = (int)(rctx->rbuf[8]&0x07);
		rctx->colortable.num_entries = 1<<(1+local_ct_size);
	}

	if(has_local_ct) {
		// We only support one image, so we don't need to keep both a global and a
		// local color table. If an image has both, the local table will overwrite
		// the global one.
		if(!iwgif_read_color_table(rctx,&rctx->colortable)) goto done;
	}

	// Make the transparent color transparent.
	if(rctx->has_transparency) {
	    rctx->colortable.entry[rctx->trans_color_index].a = 0;
	}

	// Read LZW code size
	if(!iwgif_read(rctx,rctx->rbuf,1)) goto done;
	root_codesize = (unsigned int)rctx->rbuf[0];

	// The spec does not allow the "minimum code size" to be less than 2.
	// Sizes >=12 are impossible to support.
	// There's no reason for the size to be larger than 8, but the spec
	// does not seem to forbid it.
	if(root_codesize<2 || root_codesize>11) {
		iw_set_error(rctx->ctx,"Invalid LZW minimum code size");
		goto done;
	}

	// The creation of the global "screen" was deferred until now, to wait until
	// we know whether the image has transparency.
	// (And if !rctx->include_screen, to wait until we know the size of the image.)
	if(!iwgif_init_screen(rctx)) goto done;

	rctx->total_npixels = (size_t)rctx->image_width * (size_t)rctx->image_height;

	if(!iwgif_make_row_pointers(rctx)) goto done;

	lzw_init(&d,root_codesize);
	lzw_clear(&d);

	while(1) {
		// Read size of next subblock
		if(!iwgif_read(rctx,rctx->rbuf,1)) goto done;
		subblocksize = (size_t)rctx->rbuf[0];
		if(subblocksize==0) break;

		// Read next subblock
		if(!iwgif_read(rctx,rctx->rbuf,subblocksize)) goto done;
		if(!lzw_process_bytes(rctx,&d,rctx->rbuf,subblocksize)) goto done;

		if(d.eoi_flag) break;

		// Stop if we reached the end of the image. We don't care if we've read an
		// EOI code or not.
		if(rctx->pixels_set >= rctx->total_npixels) break;
	}

	retval=1;

done:
	return retval;
}

static int iwgif_read_main(struct iwgifrcontext *rctx)
{
	int retval=0;
	int i;
	int image_found=0;

	// Make all colors opaque by default.
	for(i=0;i<256;i++) {
		rctx->colortable.entry[i].a=255;
	}

	if(!iwgif_read_file_header(rctx)) goto done;

	if(!iwgif_read_screen_descriptor(rctx)) goto done;

	// Read global color table
	if(!iwgif_read_color_table(rctx,&rctx->colortable)) goto done;

	// Tell IW the background color.
	if(rctx->has_bg_color) {
		iw_set_input_bkgd_label(rctx->ctx,
			((double)rctx->colortable.entry[rctx->bg_color_index].r)/255.0,
			((double)rctx->colortable.entry[rctx->bg_color_index].g)/255.0,
			((double)rctx->colortable.entry[rctx->bg_color_index].b)/255.0);
	}

	// The remainder of the file consists of blocks whose type is indicated by
	// their initial byte.

	while(!image_found) {
		// Read block type
		if(!iwgif_read(rctx,rctx->rbuf,1)) goto done;

		switch(rctx->rbuf[0]) {
		case 0x21: // extension
			if(!iwgif_read_extension(rctx)) goto done;
			break;
		case 0x2c: // image
			rctx->pages_seen++;
			if(rctx->page == rctx->pages_seen) {
				if(!iwgif_read_image(rctx)) goto done;
				image_found=1;
			}
			else {
				if(!iwgif_skip_image(rctx)) goto done;
			}
			break;
		case 0x3b: // file trailer
			// We stop after we decode an image, so if we ever see a file
			// trailer, something's wrong.
			if(rctx->pages_seen==0)
				iw_set_error(rctx->ctx,"No image in file");
			else
				iw_set_error(rctx->ctx,"Image not found");
			goto done;
		default:
			iw_set_error(rctx->ctx,"Invalid or unsupported GIF file");
			goto done;
		}
	}

	retval=1;

done:
	return retval;
}

IW_IMPL(int) iw_read_gif_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	struct iw_image img;
	struct iwgifrcontext *rctx = NULL;
	int retval=0;

	iw_zeromem(&img,sizeof(struct iw_image));
	rctx = iw_mallocz(ctx,sizeof(struct iwgifrcontext));
	if(!rctx) goto done;

	rctx->ctx = ctx;
	rctx->iodescr = iodescr;
	rctx->img = &img;

	// Assume GIF images are sRGB.
	iw_make_srgb_csdescr_2(&rctx->csdescr);

	rctx->page = iw_get_value(ctx,IW_VAL_PAGE_TO_READ);
	if(rctx->page<1) rctx->page = 1;

	rctx->include_screen = iw_get_value(ctx,IW_VAL_INCLUDE_SCREEN);

	if(!iwgif_read_main(rctx))
		goto done;

	iw_set_input_image(ctx, &img);

	iw_set_input_colorspace(ctx,&rctx->csdescr);

	retval = 1;

done:
	if(!retval) {
		iw_set_error(ctx,"Failed to read GIF file");
		// If we didn't call iw_set_input_image, 'img' still belongs to us,
		// so free its contents.
		iw_free(ctx, img.pixels);
	}

	if(rctx) {
		if(rctx->row_pointers) iw_free(ctx,rctx->row_pointers);
		iw_free(ctx,rctx);
	}

	return retval;
}
