// imagew-bmp.c
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

#include "imagew-config.h"

#include <stdio.h> // for SEEK_SET
#include <stdlib.h>
#include <string.h>

#define IW_INCLUDE_UTIL_FUNCTIONS
#include "imagew.h"

struct iwbmpwritecontext {
	int include_file_header;
	int bitcount;
	int palentries;
	int compressed;
	size_t palsize;
	size_t unc_dst_bpr;
	size_t unc_bitssize;
	struct iw_iodescr *iodescr;
	struct iw_context *ctx;
	struct iw_image *img;
	const struct iw_palette *pal;
	size_t total_written;
};

static size_t iwbmp_calc_bpr(int bpp, size_t width)
{
	return ((bpp*width+31)/32)*4;
}

static void iwbmp_write(struct iwbmpwritecontext *bmpctx, const void *buf, size_t n)
{
	(*bmpctx->iodescr->write_fn)(bmpctx->ctx,bmpctx->iodescr,buf,n);
	bmpctx->total_written+=n;
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

	iw_zeromem(fileheader,sizeof(fileheader));
	fileheader[0] = 66; // 'B'
	fileheader[1] = 77; // 'M'

	// This will be overwritten later, if the bitmap was compressed.
	iw_set_ui32le(&fileheader[ 2],14+40+(unsigned int)bmpctx->palsize+
		(unsigned int)bmpctx->unc_bitssize); // bfSize

	iw_set_ui32le(&fileheader[10],14+40+(unsigned int)bmpctx->palsize); // bfOffBits
	iwbmp_write(bmpctx,fileheader,14);
}

static void iwbmp_write_bmp_header(struct iwbmpwritecontext *bmpctx)
{
	unsigned int dens_x, dens_y;
	unsigned int cmpr;
	iw_byte header[40];

	iw_zeromem(header,sizeof(header));

	iw_set_ui32le(&header[ 0],40);      // biSize
	iw_set_ui32le(&header[ 4],bmpctx->img->width);  // biWidth
	iw_set_ui32le(&header[ 8],bmpctx->img->height); // biHeight
	iw_set_ui16le(&header[12],1);    // biPlanes
	iw_set_ui16le(&header[14],bmpctx->bitcount);   // biBitCount

	cmpr = 0; // BI_RGB
	if(bmpctx->compressed) {
		if(bmpctx->bitcount==8) cmpr = 1; // BI_RLE8
		else if(bmpctx->bitcount==4) cmpr = 2; // BI_RLE4
	}
	iw_set_ui32le(&header[16],cmpr); // biCompression

	iw_set_ui32le(&header[20],(unsigned int)bmpctx->unc_bitssize); // biSizeImage

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
			iw_zeromem(buf,4);
		}
		iwbmp_write(bmpctx,buf,4);
	}
}

struct rle_context {
	struct iw_context *ctx;
	struct iwbmpwritecontext *bmpctx;
	const iw_byte *srcrow;

	size_t img_width;
	int cur_row; // current row; 0=top (last)

	// Position in srcrow of the first byte that hasn't been written to the
	// output file
	size_t pending_data_start;

	// Current number of uncompressible bytes that haven't been written yet
	// (starting at pending_data_start)
	size_t unc_len;

	// Current number of identical bytes that haven't been written yet
	// (starting at pending_data_start+unc_len)
	size_t run_len;

	// The value of the bytes referred to by run_len.
	// Valid if run_len>0.
	iw_byte run_byte;

	size_t total_bytes_written; // Bytes written, after compression
};

//============================ RLE8 encoder ============================

// TODO: The RLE8 and RLE4 encoders are more different than they should be.
// The RLE8 encoder could probably be made more similar to the (more
// complicated) RLE4 encoder.

static void rle8_write_unc(struct rle_context *rctx)
{
	int i;
	iw_byte dstbuf[2];

	if(rctx->unc_len<1) return;
	if(rctx->unc_len>=3 && (rctx->unc_len&1)) {
		iw_set_error(rctx->ctx,"Internal: RLE encode error 4");
		return;
	}
	if(rctx->unc_len>254) {
		iw_set_error(rctx->ctx,"Internal: RLE encode error 5");
		return;
	}

	if(rctx->unc_len<3) {
		// The minimum length for a noncompressed run is 3. For shorter runs
		// write them "compressed".
		for(i=0;i<rctx->unc_len;i++) {
			dstbuf[0] = 0x01;  // count
			dstbuf[1] = rctx->srcrow[i+rctx->pending_data_start]; // value
			iwbmp_write(rctx->bmpctx,dstbuf,2);
			rctx->total_bytes_written+=2;
		}
	}
	else {
		dstbuf[0] = 0x00;
		dstbuf[1] = (iw_byte)rctx->unc_len;
		iwbmp_write(rctx->bmpctx,dstbuf,2);
		rctx->total_bytes_written+=2;
		iwbmp_write(rctx->bmpctx,&rctx->srcrow[rctx->pending_data_start],rctx->unc_len);
		rctx->total_bytes_written+=rctx->unc_len;
		if(rctx->unc_len&0x1) {
			// Need a padding byte if the length was odd. (This shouldn't
			// happen, because we never write odd-length UNC segments.)
			dstbuf[0] = 0x00;
			iwbmp_write(rctx->bmpctx,dstbuf,1);
			rctx->total_bytes_written+=1;
		}
	}

	rctx->pending_data_start+=rctx->unc_len;
	rctx->unc_len=0;
}

static void rle8_write_unc_and_run(struct rle_context *rctx)
{
	iw_byte dstbuf[2];

	rle8_write_unc(rctx);

	if(rctx->run_len<1) {
		return;
	}
	if(rctx->run_len>255) {
		iw_set_error(rctx->ctx,"Internal: RLE encode error 6");
		return;
	}

	dstbuf[0] = (iw_byte)rctx->run_len;
	dstbuf[1] = rctx->run_byte;
	iwbmp_write(rctx->bmpctx,dstbuf,2);
	rctx->total_bytes_written+=2;

	rctx->pending_data_start+=rctx->run_len;
	rctx->run_len=0;
}

// The RLE format used by BMP files is pretty simple, but I've gone to some
// effort to optimize it for file size, which makes for a complicated
// algorithm.
// The overall idea:
// We defer writing data until certain conditions are met. In the meantime,
// we split the unwritten data into two segments:
//  "UNC": data classified as uncompressible
//  "RUN": data classified as compressible. All bytes in this segment must be
//    identical.
// The RUN segment always follows the UNC segment.
// For each byte in turn, we examine the current state, and do one of a number
// of things, such as:
//    - add it to RUN
//    - add it to UNC (if there is no RUN)
//    - move RUN into UNC, then add it to RUN (or to UNC)
//    - move UNC and RUN to the file, then make it the new RUN
// Then, we check to see if we've accumulated enough data that something needs
// to be written out.
static int rle8_compress_row(struct rle_context *rctx)
{
	int i;
	iw_byte dstbuf[2];
	iw_byte next_byte;
	int retval = 0;

	rctx->pending_data_start=0;
	rctx->unc_len=0;
	rctx->run_len=0;

	for(i=0;i<rctx->img_width;i++) {

		// Read the next byte.
		next_byte = rctx->srcrow[i];

		// --------------------------------------------------------------
		// Add the byte we just read to either the UNC or the RUN data.

		if(rctx->run_len>0 && next_byte==rctx->run_byte) {
			// Byte fits in the current run; add it.
			rctx->run_len++;
		}
		else if(rctx->run_len==0) {
			// We don't have a RUN, so we can put this byte there.
			rctx->run_len = 1;
			rctx->run_byte = next_byte;
		}
		else if(rctx->unc_len==0 && rctx->run_len==1) {
			// We have one previous byte, and it's different from this one.
			// Move it to UNC, and make this one the RUN.
			rctx->unc_len++;
			rctx->run_byte = next_byte;
		}
		else if(rctx->unc_len>0 && rctx->run_len<(rctx->unc_len==1 ? 3 : 4)) {
			// We have a run, but it's not long enough to be beneficial.
			// Convert it to uncompressed bytes.
			// A good rule is that a run length of 4 or more (3 or more if
			// unc_len=1) should always be run-legth encoded.
			rctx->unc_len += rctx->run_len;
			rctx->run_len = 0;
			// If UNC is now odd and >1, add the next byte to it to make it even.
			// Otherwise, add it to RUN.
			if(rctx->unc_len>=3 && (rctx->unc_len&0x1)) {
				rctx->unc_len++;
			}
			else {
				rctx->run_len = 1;
				rctx->run_byte = next_byte;
			}
		}
		else {
			// Nowhere to put the byte: write out everything, and start fresh.
			rle8_write_unc_and_run(rctx);
			rctx->run_len = 1;
			rctx->run_byte = next_byte;
		}

		// --------------------------------------------------------------
		// If we hit certain high water marks, write out the current data.

		if(rctx->unc_len>=254) {
			// Our maximum size for an UNC segment.
			rle8_write_unc(rctx);
		}
		else if(rctx->unc_len>0 && (rctx->unc_len+rctx->run_len)>254) {
			// It will not be possible to coalesce the RUN into the UNC (it
			// would be too big) so write out the UNC.
			rle8_write_unc(rctx);
		}
		else if(rctx->run_len>=255) {
			// The maximum size for an RLE segment.
			rle8_write_unc_and_run(rctx);
		}

		// --------------------------------------------------------------
		// Sanity checks. These can be removed if we're sure the algorithm
		// is bug-free.

		// We don't allow unc_len to be odd (except temporarily), except
		// that it can be 1.
		// What's special about 1 is that if we add another byte to it, it
		// increases the cost. For 3,5,...,253, we can add another byte for
		// free, so we should never fail to do that.
		if((rctx->unc_len&0x1) && rctx->unc_len!=1) {
			iw_set_errorf(rctx->ctx,"Internal: BMP RLE encode error 1");
			goto done;
		}

		// unc_len can be at most 252 at this point.
		// If it were 254, it should have been written out already.
		if(rctx->unc_len>252) {
			iw_set_error(rctx->ctx,"Internal: BMP RLE encode error 2");
			goto done;
		}

		// run_len can be at most 254 at this point.
		// If it were 255, it should have been written out already.
		if(rctx->run_len>254) {
			iw_set_error(rctx->ctx,"Internal: BMP RLE encode error 3");
			goto done;
		}
	}

	// End of row. Write out anything left over.
	rle8_write_unc_and_run(rctx);

	// Write an end-of-line marker (0 0), or if this is the last row,
	// an end-of-bitmap marker (0 1).
	dstbuf[0]=0x00;
	dstbuf[1]= (rctx->cur_row==0)? 0x01 : 0x00;
	iwbmp_write(rctx->bmpctx,dstbuf,2);
	rctx->total_bytes_written+=2;

	retval = 1;

done:
	return retval;
}

//============================ RLE4 encoder ============================

// Calculate the most efficient way to split a run of uncompressible pixels.
// This only finds the first place to split the run. If the run is still
// over 255 pixels, call it again to find the next split.
static size_t rle4_get_best_unc_split(size_t n)
{
	// For <=255 pixels, we can never do better than storing it as one run.
	if(n<=255) return n;

	// With runs of 252, we can store 252/128 = 1.96875 pixels/byte.
	// With runs of 255, we can store 255/130 = 1.96153 pixels/byte.
	// Hence, using runs of 252 is the most efficient way to store a large
	// number of uncompressible pixels.
	// (Lengths other than 252 or 255 are no help.)
	// However, there are three exceptional cases where, if we split at 252,
	// the most efficient encoding will no longer be possible:
	if(n==257 || n==510 || n==765) return 255;

	return 252;
}

// Returns the incremental cost of adding a pixel to the current UNC
// (which is always either 0 or 2).
// To derive this function, I calculated the optimal cost of every length,
// and enumerated the exceptions to the (n%4)?0:2 rule.
// The exceptions are mostly caused by the cases where
// rle4_get_best_unc_split() returns 255 instead of 252.
static int rle4_get_incr_unc_cost(struct rle_context *rctx)
{
	int n;
	int m;

	n = (int)rctx->unc_len;

	if(n==2 || n==255 || n==257 || n==507 || n==510) return 2;
	if(n==256 || n==508) return 0;

	if(n>=759) {
		m = n%252;
		if(m==3 || m==6 || m==9) return 2;
		if(m==4 || m==8) return 0;
	}

	return (n%4)?0:2;
}

static void rle4_write_unc(struct rle_context *rctx)
{
	iw_byte dstbuf[128];
	size_t pixels_to_write;
	size_t bytes_to_write;

	if(rctx->unc_len<1) return;

	// Note that, unlike the RLE8 encoder, we allow this function to be called
	// with uncompressed runs of arbitrary length.

	while(rctx->unc_len>0) {
		pixels_to_write = rle4_get_best_unc_split(rctx->unc_len);

		if(pixels_to_write<3) {
			// The minimum length for an uncompressed run is 3. For shorter runs
			// write them "compressed".
			dstbuf[0] = (iw_byte)pixels_to_write;
			dstbuf[1] = (rctx->srcrow[rctx->pending_data_start]<<4);
			if(pixels_to_write>1)
				dstbuf[1] |= (rctx->srcrow[rctx->pending_data_start+1]);

			// The actual writing will occur below. Just indicate how many bytes
			// of dstbuf[] to write.
			bytes_to_write = 2;
		}
		else {
			size_t i;

			// Write the length of the uncompressed run.
			dstbuf[0] = 0x00;
			dstbuf[1] = (iw_byte)pixels_to_write;
			iwbmp_write(rctx->bmpctx,dstbuf,2);
			rctx->total_bytes_written+=2;

			// Put the data to write in dstbuf[].
			bytes_to_write = 2*((pixels_to_write+3)/4);
			iw_zeromem(dstbuf,bytes_to_write);

			for(i=0;i<pixels_to_write;i++) {
				if(i&0x1) dstbuf[i/2] |= rctx->srcrow[rctx->pending_data_start+i];
				else dstbuf[i/2] = rctx->srcrow[rctx->pending_data_start+i]<<4;
			}
		}

		iwbmp_write(rctx->bmpctx,dstbuf,bytes_to_write);
		rctx->total_bytes_written += bytes_to_write;
		rctx->unc_len -= pixels_to_write;
		rctx->pending_data_start += pixels_to_write;
	}
}

static void rle4_write_unc_and_run(struct rle_context *rctx)
{
	iw_byte dstbuf[2];

	rle4_write_unc(rctx);

	if(rctx->run_len<1) {
		return;
	}
	if(rctx->run_len>255) {
		iw_set_error(rctx->ctx,"Internal: RLE encode error 6");
		return;
	}

	dstbuf[0] = (iw_byte)rctx->run_len;
	dstbuf[1] = rctx->run_byte;
	iwbmp_write(rctx->bmpctx,dstbuf,2);
	rctx->total_bytes_written+=2;

	rctx->pending_data_start+=rctx->run_len;
	rctx->run_len=0;
}

static int rle4_compress_row(struct rle_context *rctx)
{
	int i;
	iw_byte dstbuf[2];
	iw_byte next_pix;
	int retval = 0;

	rctx->pending_data_start=0;
	rctx->unc_len=0;
	rctx->run_len=0;

	for(i=0;i<rctx->img_width;i++) {

		// Read the next pixel
		next_pix = rctx->srcrow[i];

		// --------------------------------------------------------------
		// Add the pixel we just read to either the UNC or the RUN data.

		if(rctx->run_len==0) {
			// We don't have a RUN, so we can put this pixel there.
			rctx->run_len = 1;
			rctx->run_byte = next_pix<<4;
		}
		else if(rctx->run_len==1) {
			// If the run is 1, we can always add a 2nd pixel
			rctx->run_byte |= next_pix;
			rctx->run_len++;
		}
		else if(rctx->run_len>=2 && (rctx->run_len&1)==0 && next_pix==(rctx->run_byte>>4)) {
			// pixel fits in the current run; add it.
			rctx->run_len++;
		}
		else if(rctx->run_len>=3 && (rctx->run_len&1) && next_pix==(rctx->run_byte&0x0f)) {
			// pixel fits in the current run; add it.
			rctx->run_len++;
		}
		else if(rctx->unc_len==0 && rctx->run_len==2) {
			// We have one previous byte, and it's different from this one.
			// Move it to UNC, and make this one the RUN.
			rctx->unc_len+=rctx->run_len;
			rctx->run_byte = next_pix<<4;
			rctx->run_len = 1;
		}
		else if(rctx->unc_len>0 && rctx->run_len<(rctx->unc_len<=2 ? 6 : 8)) {
			// TODO: The above logic probably isn't optimal.

			// We have a run, but it's not long enough to be beneficial.
			// Convert it to uncompressed bytes.
			rctx->unc_len += rctx->run_len;

			// Put the next byte in RLE. (It might get moved to UNC, below.)
			rctx->run_len = 1;
			rctx->run_byte = next_pix<<4;
		}
		else {
			// Nowhere to put the byte: write out everything, and start fresh.
			rle4_write_unc_and_run(rctx);
			rctx->run_len = 1;
			rctx->run_byte = next_pix<<4;
		}

		// --------------------------------------------------------------
		// If any RUN bytes that can be added to UNC for free, do so.
		while(rctx->unc_len>0 && rctx->run_len>0 && rle4_get_incr_unc_cost(rctx)==0) {
			rctx->unc_len++;
			rctx->run_len--;
		}

		// --------------------------------------------------------------
		// If we hit certain high water marks, write out the current data.

		if(rctx->run_len>=255) {
			// The maximum size for an RLE segment.
			rle4_write_unc_and_run(rctx);
		}

		// --------------------------------------------------------------
		// Sanity check(s). This can be removed if we're sure the algorithm
		// is bug-free.

		// run_len can be at most 254 at this point.
		// If it were 255, it should have been written out already.
		if(rctx->run_len>255) {
			iw_set_error(rctx->ctx,"Internal: BMP RLE encode error 3");
			goto done;
		}
	}

	// End of row. Write out anything left over.
	rle4_write_unc_and_run(rctx);

	// Write an end-of-line marker (0 0), or if this is the last row,
	// an end-of-bitmap marker (0 1).
	dstbuf[0]=0x00;
	dstbuf[1]= (rctx->cur_row==0)? 0x01 : 0x00;
	iwbmp_write(rctx->bmpctx,dstbuf,2);
	rctx->total_bytes_written+=2;

	retval = 1;

done:
	return retval;
}

//======================================================================

// Seek back and write the "file size" and "bits size" fields.
static int rle_patch_file_size(struct iwbmpwritecontext *bmpctx,size_t rlesize)
{
	iw_byte buf[4];

	if(!bmpctx->iodescr->seek_fn) {
		iw_set_error(bmpctx->ctx,"Writing compressed BMP requires a seek function");
		return 0;
	}

	// Patch the file size
	(*bmpctx->iodescr->seek_fn)(bmpctx->ctx,bmpctx->iodescr,2,SEEK_SET);
	iw_set_ui32le(buf,(unsigned int)(14+40+bmpctx->palsize+rlesize));
	iwbmp_write(bmpctx,buf,4);

	// Patch the "bits" size
	(*bmpctx->iodescr->seek_fn)(bmpctx->ctx,bmpctx->iodescr,14+20,SEEK_SET);
	iw_set_ui32le(buf,(unsigned int)rlesize);
	iwbmp_write(bmpctx,buf,4);

	(*bmpctx->iodescr->seek_fn)(bmpctx->ctx,bmpctx->iodescr,0,SEEK_END);
	return 1;
}

static int iwbmp_write_pixels_compressed(struct iwbmpwritecontext *bmpctx,
	struct iw_image *img)
{
	struct rle_context rctx;
	int j;
	int retval = 0;

	iw_zeromem(&rctx,sizeof(struct rle_context));

	rctx.ctx = bmpctx->ctx;
	rctx.bmpctx = bmpctx;
	rctx.total_bytes_written = 0;
	rctx.img_width = img->width;

	for(j=img->height-1;j>=0;j--) {
		// Compress and write a row of pixels
		rctx.srcrow = &img->pixels[j*img->bpr];
		rctx.cur_row = j;

		if(bmpctx->bitcount==4) {
			if(!rle4_compress_row(&rctx)) goto done;
		}
		else if(bmpctx->bitcount==8) {
			if(!rle8_compress_row(&rctx)) goto done;
		}
		else {
			goto done;
		}
	}

	// Back-patch the 'file size' and 'bits size' fields
	rle_patch_file_size(bmpctx,rctx.total_bytes_written);

	retval = 1;
done:
	return retval;
}

static void iwbmp_write_pixels_uncompressed(struct iwbmpwritecontext *bmpctx,
	struct iw_image *img)
{
	int j;
	iw_byte *dstrow = NULL;
	const iw_byte *srcrow;

	dstrow = iw_mallocz(bmpctx->ctx,bmpctx->unc_dst_bpr);
	if(!dstrow) goto done;

	for(j=img->height-1;j>=0;j--) {
		srcrow = &img->pixels[j*img->bpr];
		switch(bmpctx->bitcount) {
		case 24: iwbmp_convert_row24(srcrow,dstrow,img->width); break;
		case 8: iwbmp_convert_row8(srcrow,dstrow,img->width); break;
		case 4: iwbmp_convert_row4(srcrow,dstrow,img->width); break;
		case 1: iwbmp_convert_row1(srcrow,dstrow,img->width); break;
		}
		iwbmp_write(bmpctx,dstrow,bmpctx->unc_dst_bpr);
	}

done:
	if(dstrow) iw_free(bmpctx->ctx,dstrow);
	return;
}

static int iwbmp_write_main(struct iwbmpwritecontext *bmpctx)
{
	struct iw_image *img;
	int cmpr_req;
	int retval = 0;

	img = bmpctx->img;

	cmpr_req = iw_get_value(bmpctx->ctx,IW_VAL_COMPRESSION);

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

	if(cmpr_req==IW_COMPRESSION_RLE && (bmpctx->bitcount==4 || bmpctx->bitcount==8)) {
		bmpctx->compressed = 1;
	}

	bmpctx->unc_dst_bpr = iwbmp_calc_bpr(bmpctx->bitcount,img->width);
	bmpctx->unc_bitssize = bmpctx->unc_dst_bpr * img->height;
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
	if(bmpctx->compressed) {
		if(!iwbmp_write_pixels_compressed(bmpctx,img)) goto done;
	}
	else {
		iwbmp_write_pixels_uncompressed(bmpctx,img);
	}

	retval = 1;
done:
	//if(dstrow) iw_free(bmpctx->ctx,dstrow);
	return retval;
}

IW_IMPL(int) iw_write_bmp_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	struct iwbmpwritecontext bmpctx;
	int retval=0;
	struct iw_image img1;

	iw_zeromem(&img1,sizeof(struct iw_image));

	iw_zeromem(&bmpctx,sizeof(struct iwbmpwritecontext));

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
	return retval;
}
