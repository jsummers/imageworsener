// imagew.h
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file included with the
// ImageWorsener distribution.

#ifndef IMAGEW_H
#define IMAGEW_H

#ifndef IMAGEW_CONFIG_H
// Don't want imagew.h to depend on imagew-config.h, but we need
// the definition of IW_WINDOWS.
// This code is duplicated in imagew-config.h.
#if defined(_WIN32) && !defined(__GNUC__)
#define IW_WINDOWS
#endif
#endif

#ifndef TCHAR
#ifdef IW_WINDOWS
#include <tchar.h>
#else
#define TCHAR     char
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

// The version of the IW header files.
// Use iw_get_version_int() to get the version at runtime.
#define IW_VERSION_INT           0x000901


//// Codes for use with iw_get_value/iw_set_value.

// 0=US-ASCII, 1=Unicode (UTF8 or UTF16, depending on whether TCHAR is 1 or 2 bytes)
#define IW_VAL_CHARSET           10

// If ==1, convert to grayscale.
#define IW_VAL_CVT_TO_GRAYSCALE  11

#define IW_VAL_EDGE_POLICY       12

// If ==1, disable all gamma correction.
#define IW_VAL_DISABLE_GAMMA     13

#define IW_VAL_NO_CSLABEL        14

// If ==1, after the first resize pass, clamp sample values to the
// normal displayable range.
#define IW_VAL_INT_CLAMP         15

// 0=standard formula, 1=compatibility formula
#define IW_VAL_GRAYSCALE_FORMULA 16

// Test whether the input image was encoded as grayscale.
// (Assumes the image-reading function set the iw_image::native_grayscale
// flag correctly.)
#define IW_VAL_INPUT_NATIVE_GRAYSCALE 20

#define IW_VAL_INPUT_WIDTH       21
#define IW_VAL_INPUT_HEIGHT      22

// These return information about the image as it was read into memory.
// This may be different from the actual file on disk.
#define IW_VAL_INPUT_IMAGE_TYPE  23
#define IW_VAL_INPUT_DEPTH       24

#define IW_VAL_JPEG_QUALITY      30
#define IW_VAL_JPEG_SAMP_FACTOR_H  31
#define IW_VAL_JPEG_SAMP_FACTOR_V  32
#define IW_VAL_PNG_CMPR_LEVEL    33

// Nonzero if the palette is a fully-populated and sorted 1-, 2-, 4-,
// or 8-bit grayscale palette.
#define IW_VAL_OUTPUT_PALETTE_GRAYSCALE 35

#define IW_VAL_OUTPUT_INTERLACED 36

// If a solid color background is to be applied, and the input file contains
// a suggested background color, use the suggested color.
#define IW_VAL_USE_BKGD_LABEL    37

// These codes are used tell IW about the capabilities of the output format,
// so that it can make good decisions about what to do.

// Means the format supports some type of transparency.
#define IW_PROFILE_TRANSPARENCY  0x0001

// Means the format supports 8-bit or 16-bit grayscale.
#define IW_PROFILE_GRAYSCALE     0x0002

// Means each palette entry can include an alpha component.
#define IW_PROFILE_PALETTETRNS   0x0004

#define IW_PROFILE_GRAY1         0x0008
#define IW_PROFILE_GRAY2         0x0010
#define IW_PROFILE_GRAY4         0x0020

// IW_PROFILE_16BPS means the format supports 16 bits-per-sample for every
// non-paletted color format that it supports at 8bps.
#define IW_PROFILE_16BPS         0x0040

#define IW_PROFILE_ALWAYSSRGB    0x0080
#define IW_PROFILE_BINARYTRNS    0x0100 // Supports color-keyed transparency

#define IW_PROFILE_PAL1       0x0200
#define IW_PROFILE_PAL2       0x0400
#define IW_PROFILE_PAL4       0x0800
#define IW_PROFILE_PAL8       0x1000

#define IW_PROFILE_PNG   0x1f7f // all but ALWAYSSRGB
#define IW_PROFILE_BMP   0x1a80 // PAL1,PAL4,PAL8,ALWAYSSRGB
#define IW_PROFILE_JPEG  0x0082 // GRAYSCALE,ALWAYSSRGB
#define IW_PROFILE_TIFF  0x186b // TRANSPARENCY,GRAYSCALE,GRAY{1,4},PAL{4,8},16BPS

#define IW_RESIZETYPE_AUTO          0x01
#define IW_RESIZETYPE_NULL          0x02
#define IW_RESIZETYPE_NEAREST       0x03
#define IW_RESIZETYPE_MIX           0x04
#define IW_FIRST_RESAMPLING_FILTER  0x10
#define IW_RESIZETYPE_BOX           0x10
#define IW_RESIZETYPE_LINEAR        0x11
#define IW_RESIZETYPE_QUADRATIC     0x12
#define IW_RESIZETYPE_GAUSSIAN      0x20
#define IW_RESIZETYPE_HERMITE       0x21
#define IW_RESIZETYPE_CUBIC         0x22
#define IW_RESIZETYPE_LANCZOS       0x30
#define IW_RESIZETYPE_HANNING       0x31
#define IW_RESIZETYPE_BLACKMAN      0x32
#define IW_RESIZETYPE_SINC          0x33

#define IW_IMGTYPE_GRAY    0x0001
#define IW_IMGTYPE_GRAYA   0x0101
#define IW_IMGTYPE_RGB     0x0010
#define IW_IMGTYPE_RGBA    0x0110
#define IW_IMGTYPE_PALETTE 0x1000  // Used only during optimization / output.
#define IW_IMGTYPE_GRAY1   0x2000  // Used only during optimization / output.

#define IW_IMGTYPE_IS_GRAY(x)   (((x)&0x001)?1:0)
#define IW_IMGTYPE_HAS_ALPHA(x) (((x)&0x100)?1:0)

// The CHANNELTYPE definitions must not be changed. They are used as array indices.
#define IW_CHANNELTYPE_RED    0
#define IW_CHANNELTYPE_GREEN  1
#define IW_CHANNELTYPE_BLUE   2
#define IW_CHANNELTYPE_ALPHA  3
#define IW_CHANNELTYPE_GRAY   4
#define IW_CHANNELTYPE_ALL      10
#define IW_CHANNELTYPE_NONALPHA 11

// Colorspace families.
#define IW_CSTYPE_SRGB      0
#define IW_CSTYPE_LINEAR    1
#define IW_CSTYPE_GAMMA     2

// These must be the same as the PNG definitions.
#define IW_sRGB_INTENT_PERCEPTUAL 0
#define IW_sRGB_INTENT_RELATIVE   1
#define IW_sRGB_INTENT_SATURATION 2
#define IW_sRGB_INTENT_ABSOLUTE   3

#define IW_DIMENSION_H 0 // The horizontal (x) dimension.
#define IW_DIMENSION_V 1 // The vertical (y) dimension.

#define IW_DITHERTYPE_NONE         0x000
#define IW_DITHERTYPE_ORDERED      0x002
#define IW_DITHERTYPE_RANDOM       0x003 // color channels use different patterns
#define IW_DITHERTYPE_RANDOM2      0x004 // color channels use the same pattern
#define IW_DITHERTYPE_HALFTONE     0x005 // A sample "halftone" ordered dither
#define IW_DITHERTYPE_FS           0x101
#define IW_DITHERTYPE_JJN          0x102 // Jarvis, Judice, and Ninke filter
#define IW_DITHERTYPE_STUCKI       0x103
#define IW_DITHERTYPE_BURKES       0x104
#define IW_DITHERTYPE_SIERRA3      0x105
#define IW_DITHERTYPE_SIERRA2      0x106
#define IW_DITHERTYPE_SIERRA42A    0x107
#define IW_DITHERTYPE_ATKINSON     0x108

#define IW_DITHER_IS_FS_LIKE(x) (((x)&0x100)?1:0)

#define IW_DENSITY_UNKNOWN         0
#define IW_DENSITY_UNITS_UNKNOWN   1
#define IW_DENSITY_UNITS_PER_METER 2

#define IW_EDGE_POLICY_REPLICATE  1  // Replicate the pixels at the image edge.
#define IW_EDGE_POLICY_STANDARD   2  // Use available samples if any are within radius; otherwise replicate.

#define IW_BKGDCOLORSPACE_SRGB         0
#define IW_BKGDCOLORSPACE_LINEAR       1
#define IW_BKGDCOLORSPACE_SAMEASOUTPUT 10

// Optimizations that IW is allowed to do:
#define IW_OPT_GRAYSCALE    1   // optimize color to grayscale
#define IW_OPT_PALETTE      2   // optimize to paletted images
#define IW_OPT_16_TO_8      3   // reduce >8 bits to 8 bits if possible
#define IW_OPT_STRIP_ALPHA  4   // strip superfluous alpha channels
#define IW_OPT_BINARY_TRNS  5   // optimize to color-keyed binary transparency

// Colorspace descriptor
struct iw_csdescr {
	int cstype; // IW_CSTYPE_
	int sRGB_intent; // used if CSTYPE==IW_CSTYPE_SRGB
	double gamma; // used if CSTYPE==IW_CSTYPE_GAMMA
};

// An input or output image
struct iw_image {
	int imgtype;  // IW_IMGTYPE_*
	int bit_depth;
	int width, height;
	unsigned char *pixels;
	size_t bpr; // bytes per row
	int native_grayscale; // For input images: Was the image encoded as grayscale?
	int density_code; // IW_DENSITY_*
	double density_x, density_y;
	int has_colorkey_trns;
	unsigned int colorkey_r, colorkey_g, colorkey_b;
};

struct iw_rgba8color {
	unsigned char r, g, b, a;
};

struct iw_palette {
	int num_entries;
	struct iw_rgba8color entry[256];
};

struct iw_context;

struct iw_context *iw_create_context(void);
void iw_destroy_context(struct iw_context *ctx);

int iw_process_image(struct iw_context *ctx);

// Returns an extra pointer to buf.
// buflen = buf size in TCHARs.
const TCHAR *iw_get_errormsg(struct iw_context *ctx, TCHAR *buf, int buflen);
int iw_get_errorflag(struct iw_context *ctx);

void iw_set_userdata(struct iw_context *ctx, void *userdata);
void *iw_get_userdata(struct iw_context *ctx);

typedef void (*iw_warningfn_type)(struct iw_context *ctx, const TCHAR *msg);
void iw_set_warning_fn(struct iw_context *ctx, iw_warningfn_type warnfn);

// Set the maximum amount to allocate at one time.
void iw_set_max_malloc(struct iw_context *ctx, size_t n);

void iw_set_output_canvas_size(struct iw_context *ctx, int w, int h);

// Crop before resizing.
void iw_set_input_crop(struct iw_context *ctx, int x, int y, int w, int h);

void iw_set_output_profile(struct iw_context *ctx, unsigned int n);

void iw_set_output_depth(struct iw_context *ctx, int bps);

void iw_set_dither_type(struct iw_context *ctx, int channeltype, int d);

// Set the max number of shades per color (or alpha) channel.
// The shades are evenly distributed in the target color space.
void iw_set_color_count(struct iw_context *ctx, int channeltype, int c);

// blur: 1.0 is normal. >1.0 blurs the image, <1.0 sharpens (&aliases) the image.
// Not supported by all resize algorithms.
void iw_set_resize_alg(struct iw_context *ctx, int channeltype, int dimension, int family,
    double blur, double param1, double param2);

// Channeltype is an IW_CHANNELTYPE code.
// dimension: IW_DIMENSION_H: Horizontal, positive=right
//            IW_DIMENSION_V: Vertical, positive=down
void iw_set_channel_offset(struct iw_context *ctx, int channeltype, int dimension, double offs);

// Set the significant bits to something less than is stored in the file.
void iw_set_input_sbit(struct iw_context *ctx, int channeltype, int d);

// Color values are on a scale from 0 to 1, in the input colorspace.
void iw_set_input_bkgd_label(struct iw_context *ctx, double r, double g, double b);

// 'cs' indicates the colorspace of the samples given by the caller (IW_BKGDCOLORSPACE_*).
void iw_set_applybkgd(struct iw_context *ctx, int cs, double r, double g, double b);

// Must also call iw_set_applybkgd. This sets the second bkgd color, and the checkerboard size in pixels.
void iw_set_bkgd_checkerboard(struct iw_context *ctx, int checkersize, double r2, double g2, double b2);
void iw_set_bkgd_checkerboard_origin(struct iw_context *ctx, int x, int y);

// Must be called *after* reading the file, or it will be overwritten.
// IW copies the struct that the caller passes.
void iw_set_output_colorspace(struct iw_context *ctx, const struct iw_csdescr *csdescr,
			int warn_if_invalid);

// Must be called *after* reading the file, or it will be overwritten.
// IW copies the struct that the caller passes.
void iw_set_input_colorspace(struct iw_context *ctx, const struct iw_csdescr *csdescr);

int iw_get_input_image_density(struct iw_context *ctx,
   double *px, double *py, int *pcode);

void iw_set_random_seed(struct iw_context *ctx, int randomize, int rand_seed);

void iw_set_allow_opt(struct iw_context *ctx, int opt, int n);

// Caller allocates the pixels with (preferably) iw_malloc_large().
// The memory will be freed by IW.
void iw_set_input_image(struct iw_context *ctx,
		const struct iw_image *img);

// Caller supplies an (uninitialized) iw_image structure, which the
// function fills in.
void iw_get_output_image(struct iw_context *ctx, struct iw_image *img);

// Caller supplies an (uninitialized) iw_ccdescr structure, which the
// function fills in.
void iw_get_output_colorspace(struct iw_context *ctx, struct iw_csdescr *csdescr);

double iw_convert_sample_to_linear(double v, const struct iw_csdescr *csdescr);

const struct iw_palette *iw_get_output_palette(struct iw_context *ctx);

void iw_set_value(struct iw_context *ctx, int code, int n);
int iw_get_value(struct iw_context *ctx, int code);

void iw_seterror(struct iw_context *ctx, const TCHAR *fmt, ...);
void iw_warning(struct iw_context *ctx, const TCHAR *fmt, ...);

// Allocates a block of memory. Does not check the value of n.
// Returns NULL on failure.
void *iw_malloc_lowlevel(size_t n);
void *iw_realloc_lowlevel(void *m, size_t n);

// Allocates a block of memory of size n. On failure, generates an error
// and returns NULL.
// This function verifies that the memory block is not larger than the
// amount set by iw_set_max_malloc().
void *iw_malloc(struct iw_context *ctx, size_t n);
void *iw_realloc(struct iw_context *ctx, void *m, size_t n);

// Same as iw_malloc, but allocates a block of memory of size n1*n2.
// This function is careful to avoid integer overflow.
void *iw_malloc_large(struct iw_context *ctx, size_t n1, size_t n2);

// Free memory allocated by an iw_malloc* function.
// If mem is NULL, does nothing.
void iw_free(void *mem);

// Utility function to check that the supplied dimensions are
// considered valid by IW. If not, generates a warning and returns 0.
int iw_check_image_dimensons(struct iw_context *ctx, int w, int h);

// Returns an integer representing the IW version.
// For example, 0x010203 would be version 1.2.3.
int iw_get_version_int(void);

// cset: See documentation of IW_VAL_CHARSET.
// Returns a pointer to s.
TCHAR *iw_get_version_string(TCHAR *s, int s_len, int cset);
TCHAR *iw_get_copyright_string(TCHAR *s, int s_len, int cset);


// These shouldn't really be public, but are used by the png/jpeg modules.
size_t iw_calc_bytesperrow(int num_pixels, int bits_per_pixel);
void iw_snprintf(TCHAR *buf, size_t buflen, const TCHAR *fmt, ...);


struct iw_iodescr;
typedef int (*iw_readfn_type)(struct iw_context *ctx, struct iw_iodescr *iodescr, void *buf, size_t nbytes, size_t *pbytesread);
typedef int (*iw_writefn_type)(struct iw_context *ctx, struct iw_iodescr *iodescr, const void *buf, size_t nbytes);
typedef int (*iw_closefn_type)(struct iw_context *ctx, struct iw_iodescr *iodescr);
// I/O descriptor
struct iw_iodescr {
	// An arbitrary pointer the app can use.
	void *fp;

	// Application-defined I/O functions:

	// Must read and return all bytes requested, except on end-of-file or error.
	// On success, set *pbytesread to nbytes, and return 1.
	// On end of file, set *pbytesread to the number of bytes read (0 to nbytes-1),
	// and return 1.
	// On error, return 0.
	iw_readfn_type read_fn;

	// Must write all bytes supplied.
	// On success, return 1.
	// On error, return 0.
	iw_writefn_type write_fn;

	// Optional "close" function.
	iw_closefn_type close_fn;
};

int iw_read_png_file(struct iw_context *ctx, struct iw_iodescr *iodescr);
int iw_write_png_file(struct iw_context *ctx, struct iw_iodescr *iodescr);
TCHAR *iw_get_libpng_version_string(TCHAR *s, int s_len, int cset);
TCHAR *iw_get_zlib_version_string(TCHAR *s, int s_len, int cset);

int iw_read_jpeg_file(struct iw_context *ctx, struct iw_iodescr *iodescr);
int iw_write_jpeg_file(struct iw_context *ctx, struct iw_iodescr *iodescr);
TCHAR *iw_get_libjpeg_version_string(TCHAR *s, int s_len, int cset);
int iw_write_bmp_file(struct iw_context *ctx, struct iw_iodescr *iodescr);
int iw_write_tiff_file(struct iw_context *ctx, struct iw_iodescr *iodescr);

#ifdef __cplusplus
}
#endif

#endif // IMAGEW_H
