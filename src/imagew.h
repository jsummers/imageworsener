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

#include <stddef.h> // for size_t
#ifdef IW_INCLUDE_UTIL_FUNCTIONS
#include <stdarg.h> // for va_list
#endif
#ifndef IW_WINDOWS
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __GNUC__
#define iw_gnuc_attribute __attribute__
#else
#define iw_gnuc_attribute(x)
#endif

#define IW_EXPORT(x)  x

// The version of the IW header files.
// Use iw_get_version_int() to get the version at runtime.
#define IW_VERSION_INT           0x010301


//// Codes for use with iw_get_value/iw_set_value.

// The client can set API_VERSION to IW_VERSION_INT, to tell the library what
// version of the imagew.h header file it used.
// This can also be set to 0 (the default), in which case IW will assume the
// correct header file version was used.
// This may allow some compatibility problems to be detected and/or avoided.
// This value can also be set via the iw_create_context() function.
#define IW_VAL_API_VERSION       1

// Obosolete
#define IW_VAL_PRECISION         10

// If ==1, convert to grayscale.
#define IW_VAL_CVT_TO_GRAYSCALE  11

// Suggested compression algorithm
#define IW_VAL_COMPRESSION       12

// If ==1, disable all gamma correction.
#define IW_VAL_DISABLE_GAMMA     13

#define IW_VAL_NO_CSLABEL        14

// If ==1, after the first resize pass, clamp sample values to the
// normal displayable range.
#define IW_VAL_INT_CLAMP         15

// IW_GSF_*
#define IW_VAL_GRAYSCALE_FORMULA 16

#define IW_VAL_EDGE_POLICY_X     17
#define IW_VAL_EDGE_POLICY_Y     18

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

#define IW_VAL_PREF_UNITS        26

#define IW_VAL_JPEG_QUALITY      30 // OBSOLETE. Use iw_set_option("jpeg:quality").
#define IW_VAL_JPEG_SAMP_FACTOR_H  31 // OBSOLETE. Use iw_set_option("jpeg:sampling").
#define IW_VAL_JPEG_SAMP_FACTOR_V  32 // OBSOLETE. Use iw_set_option("jpeg:sampling").
#define IW_VAL_DEFLATE_CMPR_LEVEL  33 // OBSOLETE. Use iw_set_option("deflate:cmprlevel").

// Nonzero if the palette is a fully-populated and sorted 1-, 2-, 4-,
// or 8-bit grayscale palette.
#define IW_VAL_OUTPUT_PALETTE_GRAYSCALE 35

#define IW_VAL_OUTPUT_INTERLACED 36

// If a solid color background is to be applied, and the input file contains
// a suggested background color, use the suggested color.
#define IW_VAL_USE_BKGD_LABEL    37

#define IW_VAL_WEBP_QUALITY      38 // OBSOLETE. Use iw_set_option("webp:quality").

// The page ("image", "frame", whatever) to read from a multi-page file.
// The first page is 1. 0=default.
#define IW_VAL_PAGE_TO_READ      39

// Used with GIF files.
#define IW_VAL_INCLUDE_SCREEN    40

#define IW_VAL_JPEG_ARITH_CODING 41 // OBSOLETE. Use iw_set_option("jpeg:arith","").

#define IW_VAL_TRANSLATE_X       42
#define IW_VAL_TRANSLATE_Y       43

// 1 = the BMP file being read lacks a FILEHEADER.
// (Files are always *written* with a fileheader.)
#define IW_VAL_BMP_NO_FILEHEADER 44

// Maximum size (before cropping) of an input image that is considered to be
// valid.
#define IW_VAL_MAX_WIDTH         45
#define IW_VAL_MAX_HEIGHT        46

// Requested BMP version to write.
#define IW_VAL_BMP_VERSION       47 // OBSOLETE. Use iw_set_option("bmp:version").

// If set, try not to write a background color label.
#define IW_VAL_NO_BKGD_LABEL     48

// Request the output image be labeled with this rendering intent.
#define IW_VAL_INTENT            49 // IW_INTENT_*

#define IW_VAL_OUTPUT_SAMPLE_TYPE 50 // IW_SAMPLETYPE_*

// Suggested output color type
#define IW_VAL_OUTPUT_COLOR_TYPE 51 // OBSOLETE. Use iw_set_option("jpeg:colortype").

// This does not actually determine the output file format, but it may be
// needed by encoders that support multiple formats.
#define IW_VAL_OUTPUT_FORMAT     52

// Make a negative image (in target colorspace).
#define IW_VAL_NEGATE_TARGET     53

// File formats.
#define IW_FORMAT_UNKNOWN  0
#define IW_FORMAT_PNG      1
#define IW_FORMAT_JPEG     2
#define IW_FORMAT_BMP      3
#define IW_FORMAT_TIFF     4
#define IW_FORMAT_MIFF     5
#define IW_FORMAT_WEBP     6
#define IW_FORMAT_GIF      7

// PNM is a collective name for {PBM, PGM, PPM}.
// When reading a file, we'll consider everything to be PNM.
// When writing a file, we may need to distinguish between the subtypes.
#define IW_FORMAT_PNM      8
#define IW_FORMAT_PBM      9
#define IW_FORMAT_PGM      10
#define IW_FORMAT_PPM      11
#define IW_FORMAT_PAM      12

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

// ALWAYSSRGB means we don't know how to write colorspace labels for this
// format, and we assume images are sRGB. (unused, deprecated)
#define IW_PROFILE_ALWAYSSRGB    0x0080
#define IW_PROFILE_BINARYTRNS    0x0100 // Supports color-keyed transparency

#define IW_PROFILE_PAL1          0x0200
#define IW_PROFILE_PAL2          0x0400
#define IW_PROFILE_PAL4          0x0800
#define IW_PROFILE_PAL8          0x1000

#define IW_PROFILE_ALWAYSLINEAR  0x2000
#define IW_PROFILE_HDRI          0x4000 // Supports floating-point samples
#define IW_PROFILE_REDUCEDBITDEPTHS 0x8000 // Supports non-default maxcolorcodes.
#define IW_PROFILE_PNG_BKGD      0x10000 // Has PNG-style background colors
#define IW_PROFILE_RGB8_BKGD     0x20000 // Background colors are 8-bit RGB.
#define IW_PROFILE_RGB16_BKGD    0x40000 // Background colors are 16-bit RGB.

#define IW_RESIZETYPE_AUTO       0x01
#define IW_RESIZETYPE_NULL       0x02
#define IW_RESIZETYPE_NEAREST    0x10
#define IW_RESIZETYPE_MIX        0x11
#define IW_RESIZETYPE_BOX        0x20
#define IW_RESIZETYPE_TRIANGLE   0x21
#define IW_RESIZETYPE_QUADRATIC  0x22
#define IW_RESIZETYPE_GAUSSIAN   0x23
#define IW_RESIZETYPE_HERMITE    0x24
#define IW_RESIZETYPE_CUBIC      0x25
#define IW_RESIZETYPE_BOXAVG     0x26
#define IW_RESIZETYPE_SINC       0x30
#define IW_RESIZETYPE_LANCZOS    0x31
#define IW_RESIZETYPE_HANNING    0x32
#define IW_RESIZETYPE_BLACKMAN   0x33

#define IW_IMGTYPE_GRAY    0x0001
#define IW_IMGTYPE_GRAYA   0x0101
#define IW_IMGTYPE_RGB     0x0010
#define IW_IMGTYPE_RGBA    0x0110
#define IW_IMGTYPE_PALETTE 0x1000  // Used only during optimization / output.

#define IW_IMGTYPE_IS_GRAY(x)   (((x)&0x001)?1:0)
#define IW_IMGTYPE_HAS_ALPHA(x) (((x)&0x100)?1:0)

#define IW_SAMPLETYPE_UINT          0
#define IW_SAMPLETYPE_FLOATINGPOINT 1

#define IW_COMPRESSION_AUTO     0
#define IW_COMPRESSION_NONE     1
#define IW_COMPRESSION_ZIP      2
#define IW_COMPRESSION_LZW      3
#define IW_COMPRESSION_JPEG     4
#define IW_COMPRESSION_RLE      5

// The IW_COLORTYPE symbols are obsolete.
#define IW_COLORTYPE_DEFAULT    0
#define IW_COLORTYPE_RGB        1
#define IW_COLORTYPE_YCBCR      2

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
#define IW_CSTYPE_REC709    3

// These must be the same as the PNG definitions.
#define IW_SRGB_INTENT_PERCEPTUAL 0 // Deprecated
#define IW_SRGB_INTENT_RELATIVE   1 // Deprecated
#define IW_SRGB_INTENT_SATURATION 2 // Deprecated
#define IW_SRGB_INTENT_ABSOLUTE   3 // Deprecated

#define IW_INTENT_UNKNOWN    0
#define IW_INTENT_DEFAULT    1
#define IW_INTENT_NONE       2
#define IW_INTENT_PERCEPTUAL 10
#define IW_INTENT_RELATIVE   11
#define IW_INTENT_SATURATION 12
#define IW_INTENT_ABSOLUTE   13

#define IW_DIMENSION_H 0 // The horizontal (x) dimension.
#define IW_DIMENSION_V 1 // The vertical (y) dimension.

// Do not modify the SUBTYPE definitions. They may be used as array indices, etc.
#define IW_DITHERFAMILY_NONE         0
#define  IW_DITHERSUBTYPE_DEFAULT      0
#define IW_DITHERFAMILY_ORDERED      1 // (default subtype = 8x8 dispersed pattern)
#define  IW_DITHERSUBTYPE_HALFTONE     1 // A sample "halftone" pattern.
#define IW_DITHERFAMILY_ERRDIFF      2 // Error-diffusion dithering
#define  IW_DITHERSUBTYPE_FS           0
#define  IW_DITHERSUBTYPE_JJN          1 // Jarvis, Judice, and Ninke filter
#define  IW_DITHERSUBTYPE_STUCKI       2
#define  IW_DITHERSUBTYPE_BURKES       3
#define  IW_DITHERSUBTYPE_SIERRA3      4
#define  IW_DITHERSUBTYPE_SIERRA2      5
#define  IW_DITHERSUBTYPE_SIERRA42A    6
#define  IW_DITHERSUBTYPE_ATKINSON     7
#define IW_DITHERFAMILY_RANDOM       3 // (default subtype = color channels use different patterns)
#define  IW_DITHERSUBTYPE_SAMEPATTERN  1 // color channels use the same pattern

// Density codes used by the API (iw_image.density_code).
#define IW_DENSITY_UNKNOWN         0
#define IW_DENSITY_UNITS_UNKNOWN   1
#define IW_DENSITY_UNITS_PER_METER 2

// Preferred measuring system. May be used when selecting density units to
// write to the image file.
#define IW_PREF_UNITS_DEFAULT    0
#define IW_PREF_UNITS_METRIC     1
#define IW_PREF_UNITS_IMPERIAL   2

// Grayscale formula
#define IW_GSF_STANDARD      0
#define IW_GSF_COMPATIBLE    1
#define IW_GSF_WEIGHTED      2
#define IW_GSF_ORDERBYVALUE  3

#define IW_EDGE_POLICY_REPLICATE  1  // Replicate the pixels at the image edge.
#define IW_EDGE_POLICY_STANDARD   2  // Use available samples if any are within radius; otherwise replicate.
#define IW_EDGE_POLICY_TRANSPARENT 3

// Reorientation codes, for use with iw_reorient_image().
// Note that these do not represent an orientation; they represent a *change*
// in orientation.
// These definitions must not be changed.
#define IW_REORIENT_NOCHANGE    0
#define IW_REORIENT_FLIP_H      1
#define IW_REORIENT_FLIP_V      2
#define IW_REORIENT_ROTATE_180  3
#define IW_REORIENT_TRANSPOSE   4
#define IW_REORIENT_ROTATE_90   5
#define IW_REORIENT_ROTATE_270  6
#define IW_REORIENT_TRANSVERSE  7

// Optimizations that IW is allowed to do:
#define IW_OPT_GRAYSCALE    1   // optimize color to grayscale
#define IW_OPT_PALETTE      2   // optimize to paletted images
#define IW_OPT_16_TO_8      3   // reduce >8 bits to 8 bits if possible
#define IW_OPT_STRIP_ALPHA  4   // strip superfluous alpha channels
#define IW_OPT_BINARY_TRNS  5   // optimize to color-keyed binary transparency

#define IW_TRANSLATEFLAG_FORMAT      0x0001 // Translating a format string
#define IW_TRANSLATEFLAG_POSTFORMAT  0x0002 // Translating a formatted string
#define IW_TRANSLATEFLAG_ERRORMSG    0x0010
#define IW_TRANSLATEFLAG_WARNINGMSG  0x0020

#define IW_MALLOCFLAG_ZEROMEM     0x01
#define IW_MALLOCFLAG_NOERRORS    0x02

#ifdef IW_WINDOWS
#define iw_byte     unsigned char
#define iw_uint16   unsigned short
#define iw_int32    int
#define iw_uint32   unsigned int
#define iw_int64    __int64
#define iw_uint64   unsigned __int64
#else
#define iw_byte     uint8_t
#define iw_uint16   uint16_t
#define iw_int32    int32_t
#define iw_uint32   uint32_t
#define iw_int64    int64_t
#define iw_uint64   uint64_t
#endif
#define iw_float32  float
#define iw_float64  double

// Colorspace descriptor
struct iw_csdescr {
	int cstype; // IW_CSTYPE_*
	int srgb_intent; // (deprecated)
	double gamma; // used if CSTYPE==IW_CSTYPE_GAMMA
};

// An RGBA color. Samples normally range from 0.0 to 1.0.
// Colorspace depends on context.
struct iw_color {
	double c[4]; // Indexed by IW_CHANNELTYPE[Red..Alpha]
};

// An input or output image
struct iw_image {
	int imgtype;  // IW_IMGTYPE_*
	int bit_depth;
	int sampletype; // IW_SAMPLETYPE_*

	// This is the logical width and height, which does not necessarily
	// indicate the order of the pixels in ->pixels.
	// (The order is indicated by ->orient_transform.)
	int width, height;

	// Caution: Multi-byte samples with an integer data type use big-endian
	// byte order, while floating-point samples use the native byte order of
	// the host system (usually little-endian).
	iw_byte *pixels;
	size_t bpr; // bytes per row

	// Describes orientation transformations that need to be made to the
	// pixels.
	// Used with input images only.
	unsigned int orient_transform;

	int native_grayscale; // For input images: Was the image encoded as grayscale?
	int density_code; // IW_DENSITY_*
	double density_x, density_y;
	int has_colorkey_trns;
	unsigned int colorkey[3]; // Indexed by IW_CHANNELTYPE_[RED..BLUE]
	int reduced_maxcolors;
	unsigned int maxcolorcode[5];  // Indexed by IW_CHANNELTYPE_[RED..GRAY]

	int has_bkgdlabel; // For output images only.
	struct iw_color bkgdlabel;

	int rendering_intent; // Valid for both input and output images.
};

struct iw_rgba8color {
	iw_byte r, g, b, a;
};

struct iw_palette {
	int num_entries;
	struct iw_rgba8color entry[256];
};

struct iw_context;

struct iw_iodescr;
typedef int (*iw_readfn_type)(struct iw_context *ctx, struct iw_iodescr *iodescr, void *buf, size_t nbytes, size_t *pbytesread);
typedef int (*iw_writefn_type)(struct iw_context *ctx, struct iw_iodescr *iodescr, const void *buf, size_t nbytes);
typedef int (*iw_closefn_type)(struct iw_context *ctx, struct iw_iodescr *iodescr);
typedef int (*iw_getfilesizefn_type)(struct iw_context *ctx, struct iw_iodescr *iodescr, iw_int64 *pfilesize);
typedef int (*iw_seekfn_type)(struct iw_context *ctx, struct iw_iodescr *iodescr, iw_int64 offset, int whence);
typedef int (*iw_tellfn_type)(struct iw_context *ctx, struct iw_iodescr *iodescr, iw_int64 *pfileptr);

// I/O descriptor
struct iw_iodescr {
	// A generic "file pointer" the app can use.
	void *fp;

	// Application-defined I/O functions.
	// All functions must return 1 on success, and 0 on failure.
	// Whether a particular function is required depends on the module used to
	// process the file, and is beyond the scope of the core library.

	// Must read and return all bytes requested, except on end-of-file or error.
	// On success, set *pbytesread to nbytes.
	// On end of file, set *pbytesread to the number of bytes read (0 to nbytes-1),
	// and return 1.
	iw_readfn_type read_fn;

	// Must write all bytes supplied.
	iw_writefn_type write_fn;

	// Optional "close" function. IW will never call this, but the app can use
	// it for convenience.
	iw_closefn_type close_fn;

	// Return the file size.
	// Must leave the file position at the beginning of the file (or must not
	// modify it).
	iw_getfilesizefn_type getfilesize_fn;

	// Seek to the given file position. The 'whence' parameter takes the same
	// values as that of the standard fseek() function.
	iw_seekfn_type seek_fn;

	// Return the current file position.
	iw_tellfn_type tell_fn;
};

// Allocate n bytes of memory. Return NULL on failure.
// If the IW_MALLOCFLAG_ZEROMEM flag is set, the new memory must be initialized
// to all zero bytes.
// IW will not attempt to allocate memory blocks larger than the limit set by
// iw_set_max_malloc().
typedef void* (*iw_mallocfn_type)(void *userdata, unsigned int flags, size_t n);

// Free memory allocated with the appropriate malloc function.
// IW will not call this function with mem set to NULL.
typedef void (*iw_freefn_type)(void *userdata, void *mem);

// A struct containing data that may be needed in iw_create_context().
// iw_create_context() does not do very much, but does need to allocate memory,
// so we can't wait until after it returns to define custom memory allocation
// functions.
struct iw_init_params {
	int api_version; // See IW_VAL_API_VERSION.
	void *userdata;

	// The mallocfn and freefn functions are optional, and can be set to NULL.
	// If one is set, they must both be set.
	// For details, see the definition of iw_mallocfn_type and and iw_freefn_type.
	iw_mallocfn_type mallocfn;
	iw_freefn_type freefn;
};

// 'params' points to a struct that the caller must allocate, and set any
// fields it needs to use. Unused fields must be set to all zero bytes.
// 'params' can be NULL, if the caller does not need it for anything.
// 'params' need not remain valid after iw_create_context() returns.
IW_EXPORT(struct iw_context*) iw_create_context(struct iw_init_params *params);

IW_EXPORT(void) iw_destroy_context(struct iw_context *ctx);

IW_EXPORT(int) iw_process_image(struct iw_context *ctx);

// Rotate and/or mirror the image. 'x' is an IW_REORIENT_ code.
// Must be called after the input image has been read (and you probably want
// to call it before its height, width, and density are queried).
// Note that this function performs an immediate action. It is not a setting.
IW_EXPORT(void) iw_reorient_image(struct iw_context *ctx, unsigned int x);

// Set the translation hook function. Strings such as error messages will be
//  passed to this function.
// dst: a buffer supplied by the library.
//  If the caller chooses to translate the string, it must put the new
//  (nul-terminated) string in dst, and return nonzero. If not, it must return 0.
// flags: A combination of IW_TRANSLATEFLAG_ flags.
//   If the caller translates format strings (the _FORMAT flag), it must be
//   careful not to change the format specifiers.
typedef int (*iw_translatefn_type)(struct iw_context *ctx,
	unsigned int flags, char *dst, size_t dstlen, const char *src);
IW_EXPORT(void) iw_set_translate_fn(struct iw_context *ctx, iw_translatefn_type xlatefn);

// Returns nonzero if an error occurred.
IW_EXPORT(int) iw_get_errorflag(struct iw_context *ctx);

// If iw_get_errorflag() indicates an error, the error message can be retrieved
// with iw_get_errormsg().
// Caller supplies buf. buflen = buf size in chars.
// Returns an extra pointer to buf.
IW_EXPORT(const char*) iw_get_errormsg(struct iw_context *ctx, char *buf, int buflen);

// An arbitrary pointer that the caller can use.
// This can also be set via iw_create_context().
IW_EXPORT(void) iw_set_userdata(struct iw_context *ctx, void *userdata);
IW_EXPORT(void*) iw_get_userdata(struct iw_context *ctx);

typedef void (*iw_warningfn_type)(struct iw_context *ctx, const char *msg);
IW_EXPORT(void) iw_set_warning_fn(struct iw_context *ctx, iw_warningfn_type warnfn);

// Set the maximum amount of memory to allocate at one time.
IW_EXPORT(void) iw_set_max_malloc(struct iw_context *ctx, size_t n);

// The full size of the output image, in pixels.
IW_EXPORT(void) iw_set_output_canvas_size(struct iw_context *ctx, int w, int h);

// The size of the part of the output image that corresponds to the input image.
// By default, this is the same as the "canvas size", but by calling
// iw_set_output_image_size() you can change that (e.g. to add a border).
// If you use this, you should probably use set the IW_VAL_TRANSLATE_X and
// IW_VAL_TRANSLATE_Y values.
IW_EXPORT(void) iw_set_output_image_size(struct iw_context *ctx, double w, double h);

// Crop before resizing.
IW_EXPORT(void) iw_set_input_crop(struct iw_context *ctx, int x, int y, int w, int h);

// Inform IW about the features of your intended output file format.
// n is a bitwise combination of IW_PROFILE_* values.
// iw_get_profile_by_fmt() can be used to get value for n.
IW_EXPORT(void) iw_set_output_profile(struct iw_context *ctx, unsigned int n);

IW_EXPORT(void) iw_set_output_depth(struct iw_context *ctx, int bps);

// Set the number of bits for a specific channel. For example, for 6 bits,
// set n=63. This only works if the output format can support it.
IW_EXPORT(void) iw_set_output_max_color_code(struct iw_context *ctx, int channeltype, int n);

IW_EXPORT(void) iw_set_dither_type(struct iw_context *ctx, int channeltype, int family, int subtype);

// Set the max number of shades per color (or alpha) channel.
// The shades are evenly distributed in the target color space.
IW_EXPORT(void) iw_set_color_count(struct iw_context *ctx, int channeltype, int c);

// param1: For "cubic", the B parameter. For "lanczos" etc., the number of lobes.
// param2: For "cubic", the C parameter.
// blur: 1.0 is normal. >1.0 blurs the image, <1.0 sharpens (&aliases) the image.
IW_EXPORT(void) iw_set_resize_alg(struct iw_context *ctx, int dimension, int family,
    double blur, double param1, double param2);

// Channeltype is an IW_CHANNELTYPE code.
// dimension: IW_DIMENSION_H: Horizontal, positive=right
//            IW_DIMENSION_V: Vertical, positive=down
IW_EXPORT(void) iw_set_channel_offset(struct iw_context *ctx, int channeltype, int dimension, double offs);

// Also call iw_set_value(...,IW_VAL_GRAYSCALE_FORMULA,...), to set the actual formula.
IW_EXPORT(void) iw_set_grayscale_weights(struct iw_context *ctx, double r, double g, double b);

// Color values are on a scale from 0 to 1, in the input colorspace.
IW_EXPORT(void) iw_set_input_bkgd_label(struct iw_context *ctx, double r, double g, double b);
IW_EXPORT(void) iw_set_input_bkgd_label_2(struct iw_context *ctx, const struct iw_color *clr);

// Color values are on a scale from 0 to 1, in linear colorspace.
IW_EXPORT(void) iw_set_output_bkgd_label(struct iw_context *ctx, double r, double g, double b); // deprecated
IW_EXPORT(void) iw_set_output_bkgd_label_2(struct iw_context *ctx, const struct iw_color *clr);

// The background color to apply to the image.
// Color values are on a scale from 0 to 1, in a linear colorspace.
// This will be overridden if IW_VAL_USE_BKGD_LABEL is set, and the input file
// contains a background color label.
IW_EXPORT(void) iw_set_apply_bkgd(struct iw_context *ctx, double r, double g, double b); // deprecated
IW_EXPORT(void) iw_set_apply_bkgd_2(struct iw_context *ctx, const struct iw_color *clr);

// Must also call iw_set_applybkgd. This sets the second bkgd color, and the checkerboard size in pixels.
IW_EXPORT(void) iw_set_bkgd_checkerboard(struct iw_context *ctx, int checkersize, double r2, double g2, double b2); // deprecated
IW_EXPORT(void) iw_set_bkgd_checkerboard_2(struct iw_context *ctx, int checkersize, const struct iw_color *clr);
IW_EXPORT(void) iw_set_bkgd_checkerboard_origin(struct iw_context *ctx, int x, int y);

// Must be called *after* reading the file, or it will be overwritten.
// IW copies the struct that the caller passes.
IW_EXPORT(void) iw_set_output_colorspace(struct iw_context *ctx, const struct iw_csdescr *csdescr);

// Must be called *after* reading the file, or it will be overwritten.
// IW copies the struct that the caller passes.
IW_EXPORT(void) iw_set_input_colorspace(struct iw_context *ctx, const struct iw_csdescr *csdescr);

// Although the iw_csdescr structure is publicly visible, these functions are
// the preferred way to set its fields.
// The struct must be allocated by the caller, but does not need to be
// initialized or freed in any special way.
IW_EXPORT(void) iw_make_linear_csdescr(struct iw_csdescr *cs);
IW_EXPORT(void) iw_make_srgb_csdescr(struct iw_csdescr *cs, int srgb_intent); // Deprecated
IW_EXPORT(void) iw_make_srgb_csdescr_2(struct iw_csdescr *cs);
IW_EXPORT(void) iw_make_rec709_csdescr(struct iw_csdescr *cs);
IW_EXPORT(void) iw_make_gamma_csdescr(struct iw_csdescr *cs, double gamma);

IW_EXPORT(int) iw_get_input_density(struct iw_context *ctx,
   double *px, double *py, int *pcode);
IW_EXPORT(void) iw_set_output_density(struct iw_context *ctx,
   double x, double y, int code);

IW_EXPORT(void) iw_set_random_seed(struct iw_context *ctx, int randomize, int rand_seed);

// opt: an IW_OPT_* code.
// n: 0 to disable this class of optimizations.
IW_EXPORT(void) iw_set_allow_opt(struct iw_context *ctx, int opt, int n);

// Caller allocates the pixels with (preferably) iw_malloc_large().
// The memory will be freed by IW.
// A copy is made of the img structure itself.
IW_EXPORT(void) iw_set_input_image(struct iw_context *ctx, const struct iw_image *img);

// Caller supplies an (uninitialized) iw_image structure, which the
// function fills in.
IW_EXPORT(void) iw_get_output_image(struct iw_context *ctx, struct iw_image *img);

// Caller supplies an (uninitialized) iw_ccdescr structure, which the
// function fills in.
IW_EXPORT(void) iw_get_output_colorspace(struct iw_context *ctx, struct iw_csdescr *csdescr);

IW_EXPORT(const struct iw_palette*) iw_get_output_palette(struct iw_context *ctx);

IW_EXPORT(void) iw_set_value(struct iw_context *ctx, int code, int n);
IW_EXPORT(int) iw_get_value(struct iw_context *ctx, int code);

IW_EXPORT(void) iw_set_value_dbl(struct iw_context *ctx, int code, double n);
IW_EXPORT(double) iw_get_value_dbl(struct iw_context *ctx, int code);

IW_EXPORT(void) iw_set_option(struct iw_context *ctx, const char *name, const char *val);
IW_EXPORT(const char*) iw_get_option(struct iw_context *ctx, const char *name);

IW_EXPORT(void) iw_set_error(struct iw_context *ctx, const char *s);
IW_EXPORT(void) iw_set_errorf(struct iw_context *ctx, const char *fmt, ...)
  iw_gnuc_attribute ((format (printf, 2, 3)));
IW_EXPORT(void) iw_warning(struct iw_context *ctx, const char *s);
IW_EXPORT(void) iw_warningf(struct iw_context *ctx, const char *fmt, ...)
  iw_gnuc_attribute ((format (printf, 2, 3)));

// Returns the number of bytes in the data type used to store a sample
// internally.
IW_EXPORT(int) iw_get_sample_size(void);

IW_EXPORT(double) iw_convert_sample_to_linear(double v, const struct iw_csdescr *csdescr);
IW_EXPORT(double) iw_convert_sample_from_linear(double v, const struct iw_csdescr *csdescr);

// Returns one component of an iw_color, as an integer scaled as requested.
IW_EXPORT(unsigned int) iw_color_get_int_sample(struct iw_color *clr, int channel, unsigned int maxcolorcode);

// Returns an integer representing the IW version.
// For example, 0x010203 would be version 1.2.3.
IW_EXPORT(int) iw_get_version_int(void);

// Next two functions:
// The ctx param is to allow for the possibility of localization. It can be NULL.
// Returns a pointer to s.
IW_EXPORT(char*) iw_get_version_string(struct iw_context *ctx, char *s, int s_len);
IW_EXPORT(char*) iw_get_copyright_string(struct iw_context *ctx, char *s, int s_len);

// A helper function you can use to help deal with strings received
// from the IW library.
IW_EXPORT(void) iw_utf8_to_ascii(const char *src, char *dst, int dstlen);

IW_EXPORT(unsigned int) iw_get_profile_by_fmt(int fmt);

// Returns an IW_FORMAT_* code based on the supplied filename.
IW_EXPORT(int) iw_detect_fmt_from_filename(const char *fn);

// Returns a pointer to a static string like "PNG", "JPEG", etc.
// If format is unknown, returns NULL.
IW_EXPORT(const char*) iw_get_fmt_name(int fmt);

// Returns an IW_FORMAT_* code based on the (partial) memory-mapped file supplied.
// In most cases, 16 bytes is sufficient.
IW_EXPORT(int) iw_detect_fmt_of_file(const iw_byte *buf, size_t buflen);

IW_EXPORT(int) iw_is_input_fmt_supported(int fmt);
IW_EXPORT(int) iw_is_output_fmt_supported(int fmt);

IW_EXPORT(int) iw_read_png_file(struct iw_context *ctx, struct iw_iodescr *iodescr);
IW_EXPORT(int) iw_write_png_file(struct iw_context *ctx, struct iw_iodescr *iodescr);
IW_EXPORT(char*) iw_get_libpng_version_string(char *s, int s_len);
IW_EXPORT(char*) iw_get_zlib_version_string(char *s, int s_len);
IW_EXPORT(int) iw_read_jpeg_file(struct iw_context *ctx, struct iw_iodescr *iodescr);
IW_EXPORT(int) iw_write_jpeg_file(struct iw_context *ctx, struct iw_iodescr *iodescr);
IW_EXPORT(char*) iw_get_libjpeg_version_string(char *s, int s_len);
IW_EXPORT(int) iw_read_bmp_file(struct iw_context *ctx, struct iw_iodescr *iodescr);
IW_EXPORT(int) iw_write_bmp_file(struct iw_context *ctx, struct iw_iodescr *iodescr);
IW_EXPORT(int) iw_write_tiff_file(struct iw_context *ctx, struct iw_iodescr *iodescr);
IW_EXPORT(int) iw_read_miff_file(struct iw_context *ctx, struct iw_iodescr *iodescr);
IW_EXPORT(int) iw_write_miff_file(struct iw_context *ctx, struct iw_iodescr *iodescr);
IW_EXPORT(int) iw_read_webp_file(struct iw_context *ctx, struct iw_iodescr *iodescr);
IW_EXPORT(int) iw_write_webp_file(struct iw_context *ctx, struct iw_iodescr *iodescr);
IW_EXPORT(int) iw_read_gif_file(struct iw_context *ctx, struct iw_iodescr *iodescr);
IW_EXPORT(int) iw_read_pnm_file(struct iw_context *ctx, struct iw_iodescr *iodescr);
// The output format can be refined by setting IW_VAL_OUTPUT_FORMAT.
IW_EXPORT(int) iw_write_pnm_file(struct iw_context *ctx, struct iw_iodescr *iodescr);
IW_EXPORT(int) iw_read_pam_file(struct iw_context *ctx, struct iw_iodescr *iodescr);
IW_EXPORT(int) iw_write_pam_file(struct iw_context *ctx, struct iw_iodescr *iodescr);
IW_EXPORT(char*) iw_get_libwebp_dec_version_string(char *s, int s_len);
IW_EXPORT(char*) iw_get_libwebp_enc_version_string(char *s, int s_len);

IW_EXPORT(int) iw_read_file_by_fmt(struct iw_context *ctx,
	struct iw_iodescr *iodescr, int fmt);
IW_EXPORT(int) iw_write_file_by_fmt(struct iw_context *ctx,
	struct iw_iodescr *writedescr, int fmt);

// iw_enable_zlib() must be called to enable zlib compression in modules for
// which it is optional.
// Note: iw_read_file_by_fmt and iw_write_file_by_fmt call iw_enable_zlib
// automatically.
IW_EXPORT(void) iw_enable_zlib(struct iw_context *ctx);

#ifdef IW_INCLUDE_UTIL_FUNCTIONS

// Functions and definitions used by the auxiliary library modules, but which
// are extraneous to the main purpose of the library.
// Applications are welcome to define IW_INCLUDE_UTIL_FUNCTIONS and use these
// functions if they wish.

IW_EXPORT(void) iw_translate(struct iw_context *ctx, unsigned int flags,
  char *dst, size_t dstlen, const char *src);
IW_EXPORT(void) iw_translatev(struct iw_context *ctx, unsigned int flags,
  char *dst, size_t dstlen, const char *fmt, va_list ap);
IW_EXPORT(void) iw_translatef(struct iw_context *ctx, unsigned int flags,
  char *dst, size_t dstlen, const char *fmt, ...);

IW_EXPORT(void) iw_set_errorv(struct iw_context *ctx, const char *fmt, va_list ap);
IW_EXPORT(void) iw_warningv(struct iw_context *ctx, const char *fmt, va_list ap);

IW_EXPORT(void) iw_strlcpy(char *dst, const char *src, size_t dstlen);
IW_EXPORT(void) iw_vsnprintf(char *buf, size_t buflen, const char *fmt, va_list ap);
IW_EXPORT(void) iw_snprintf(char *buf, size_t buflen, const char *fmt, ...)
  iw_gnuc_attribute ((format (printf, 3, 4)));
IW_EXPORT(int) iw_stricmp(const char *s1, const char *s2);

IW_EXPORT(void) iw_zeromem(void *mem, size_t n);

// Tell IW which color code represents maximum intensity.
IW_EXPORT(void) iw_set_input_max_color_code(struct iw_context *ctx, int input_channel, int c);

IW_EXPORT(int) iw_imgtype_num_channels(int t);

IW_EXPORT(size_t) iw_calc_bytesperrow(int num_pixels, int bits_per_pixel);

// Utility function to check that the supplied dimensions are
// considered valid by IW. If not, generates a warning and returns 0.
IW_EXPORT(int) iw_check_image_dimensions(struct iw_context *ctx, int w, int h);

IW_EXPORT(int) iw_is_valid_density(double density_x, double density_y, int density_code);

IW_EXPORT(int) iw_file_to_memory(struct iw_context *ctx, struct iw_iodescr *iodescr,
  void **pmem, iw_int64 *psize);

// Various memory allocation functions.
// In general, they allocate a block of memory of size n.
// On failure, they generate an error (unless the IW_MALLOCFLAG_NOERRORS flag
// is set) and return NULL.
// They fail if an attempt is made to allocate a block larger than the amount
// set by iw_set_max_malloc().
IW_EXPORT(void*) iw_malloc_ex(struct iw_context *ctx, unsigned int flags, size_t n);
IW_EXPORT(void*) iw_malloc(struct iw_context *ctx, size_t n);
// "mallocz" initializes the memory to all 0 bytes.
IW_EXPORT(void*) iw_mallocz(struct iw_context *ctx, size_t n);
// iw_malloc_large is the same as iw_malloc, but allocates a block of memory of
// size n1*n2. This function is careful to avoid integer overflow.
IW_EXPORT(void*) iw_malloc_large(struct iw_context *ctx, size_t n1, size_t n2);

IW_EXPORT(void*) iw_realloc_ex(struct iw_context *ctx, unsigned int flags,
	void *m, size_t oldn, size_t n);
IW_EXPORT(void*) iw_realloc(struct iw_context *ctx,
	void *m, size_t oldn, size_t n);
IW_EXPORT(char*) iw_strdup(struct iw_context *ctx, const char *s);

// Free memory allocated by an iw_malloc* function.
// If mem is NULL, does nothing.
IW_EXPORT(void) iw_free(struct iw_context *ctx, void *mem);

#define IW_ENDIAN_BIG    0
#define IW_ENDIAN_LITTLE 1
IW_EXPORT(int) iw_get_host_endianness(void);

IW_EXPORT(void) iw_set_ui16le(iw_byte *b, unsigned int n);
IW_EXPORT(void) iw_set_ui32le(iw_byte *b, unsigned int n);
IW_EXPORT(void) iw_set_ui16be(iw_byte *b, unsigned int n);
IW_EXPORT(void) iw_set_ui32be(iw_byte *b, unsigned int n);
IW_EXPORT(unsigned int) iw_get_ui16le(const iw_byte *b);
IW_EXPORT(int) iw_get_i32le(const iw_byte *b);
IW_EXPORT(unsigned int) iw_get_ui32le(const iw_byte *b);
IW_EXPORT(unsigned int) iw_get_ui16be(const iw_byte *b);
IW_EXPORT(unsigned int) iw_get_ui32be(const iw_byte *b);
IW_EXPORT(unsigned int) iw_get_ui16_e(const iw_byte *b, int endian);
IW_EXPORT(unsigned int) iw_get_ui32_e(const iw_byte *b, int endian);

IW_EXPORT(int) iw_max_color_to_bitdepth(unsigned int mc);

IW_EXPORT(int) iw_parse_number_list(const char *s, int max_numbers, double *results);
IW_EXPORT(double) iw_parse_number(const char *s);
IW_EXPORT(int) iw_parse_int(const char *s);
IW_EXPORT(int) iw_round_to_int(double x);

struct iw_zlib_context;

typedef struct iw_zlib_context* (*iw_zlib_inflate_init_type)(struct iw_context *ctx);
typedef void (*iw_zlib_inflate_end_type)(struct iw_zlib_context *zctx);
typedef int (*iw_zlib_inflate_item_type)(struct iw_zlib_context *zctx,
	iw_byte *src, size_t srclen, iw_byte *dst, size_t dstlen);

typedef struct iw_zlib_context* (*iw_zlib_deflate_init_type)(struct iw_context *ctx);
typedef void (*iw_zlib_deflate_end_type)(struct iw_zlib_context *zctx);
typedef int (*iw_zlib_deflate_item_type)(struct iw_zlib_context *zctx,
	iw_byte *src, size_t srclen, iw_byte *dst, size_t dstlen, size_t *pdstused);

struct iw_zlib_module {
	iw_zlib_inflate_init_type inflate_init;
	iw_zlib_inflate_end_type inflate_end;
	iw_zlib_inflate_item_type inflate_item;
	iw_zlib_deflate_init_type deflate_init;
	iw_zlib_deflate_end_type deflate_end;
	iw_zlib_deflate_item_type deflate_item;
};

IW_EXPORT(void) iw_set_zlib_module(struct iw_context *ctx, struct iw_zlib_module *z);
IW_EXPORT(struct iw_zlib_module*) iw_get_zlib_module(struct iw_context *ctx);

#endif // IW_INCLUDE_UTIL_FUNCTIONS


#ifdef __cplusplus
}
#endif

#endif // IMAGEW_H
