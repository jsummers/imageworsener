// imagew-internals.h
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

#define IW_INCLUDE_UTIL_FUNCTIONS
#include "imagew.h"

#define IW_COPYRIGHT_YEAR "2011" "\xe2\x80\x93" "2012"

#ifdef IW_WINDOWS
#define IW_INLINE __inline
#else
#define IW_INLINE inline
#endif

#define IW_MSG_MAX 200 // The usual max length of error messages, etc.

#ifdef IW_SAMPLE_TYPE
typedef IW_SAMPLE_TYPE IW_SAMPLE;
#else
typedef double IW_SAMPLE;
#endif

#ifdef IW_64BIT
#define IW_DEFAULT_MAX_DIMENSION 1000000
#define IW_DEFAULT_MAX_MALLOC 2000000000000
#else
#define IW_DEFAULT_MAX_DIMENSION 40000 // Must be less than sqrt(2^31).
#define IW_DEFAULT_MAX_MALLOC 2000000000
#endif

#define IW_BKGD_STRATEGY_EARLY 1 // Apply background before resizing
#define IW_BKGD_STRATEGY_LATE  2 // Apply background after resizing

#define IW_NUM_CHANNELTYPES 5 // 5, for R,G,B, Alpha, Gray
#define IW_CI_COUNT 4 // Number of channelinfo structs (=4, for R, G, B, A)

struct iw_rgb_color {
	IW_SAMPLE c[3]; // Indexed by IW_CHANNELTYPE[Red..Blue]
};

struct iw_rr_ctx; // "resize rows" state; see imagew-resize.c.

// "Raw" settings from the application.
struct iw_resize_settings {
	int family;
	int edge_policy;
	int use_offset;
	int disable_rrctx_cache;
	double param1; // 'B' in Mitchell-Netravali cubics. "lobes" in Lanczos, etc.
	double param2; // 'C' in Mitchell-Netravali cubics.
	double blur_factor;
	double translate; // Amount to move the image, before applying any channel offsets.
	double channel_offset[3]; // Indexed by IW_CHANNELTYPE_[Red..Blue]
	struct iw_rr_ctx *rrctx;
};

struct iw_channelinfo_in {
	int channeltype;
	int disable_fast_get_sample;
	double maxcolorcode_dbl;
	int maxcolorcode_int;
};

struct iw_channelinfo_intermed {
	int channeltype;

	int cvt_to_grayscale; // (on input)
	int corresponding_input_channel; // (or the first of 3 channels if cvt_to_grayscale)

	int corresponding_output_channel; // Can be -1 if no such channel.

	IW_SAMPLE bkgd_color_lin; // Used if ctx->apply_bkgd && bkgd_strategy==EARLY

	int need_unassoc_alpha_processing; // Is this a color channel in an image with transparency?
};

struct iw_channelinfo_out {
	int ditherfamily;
	int dithersubtype;
	int channeltype;

	// If restricting to a number of colors, colors are evenly
	// spaced (as evenly spaced as possible) in the target color space.
	int color_count; // 0=default

	double maxcolorcode_dbl;
	int maxcolorcode_int;

	int use_nearest_color_table;

	IW_SAMPLE bkgd_color_lin; // Used if ctx->apply_bkgd
	IW_SAMPLE bkgd2_color_lin; // Used if ctx->apply_bkgd && bkgd_checkerboard
};

struct iw_prng; // Defined imagew-util.c

// Tracks the current image properties. May change as we optimize the image.
struct iw_opt_ctx {
	int height, width;
	int imgtype;
	int bit_depth;
	size_t bpr;

	// A pointer to the current pixels. May point to tmp_pixels, or
	// to ctx->img2.pixels.
	const iw_byte *pixelsptr;

	// A place for optimized pixels. If this is non-NULL, it will be
	// freed when IW is finished.
	iw_byte *tmp_pixels;

	int has_transparency;
	int has_partial_transparency;
	int has_16bit_precision;
	int has_color;
	int palette_is_grayscale;

	struct iw_palette *palette;

	int has_colorkey_trns;
	unsigned int colorkey[3]; // Indexed by IW_CHANNELTYPE_[RED..BLUE]
};

struct iw_context {
	int caller_api_version;
	int use_count;
	unsigned int output_profile;

	// Precision of intermediate samples, 32 (=32) or 64 (=whatever IW_SAMPLE is)
	int precision;

	iw_mallocfn_type mallocfn;
	iw_freefn_type freefn;

	int num_in_pix;
	int num_out_pix;
	IW_SAMPLE *in_pix; // A single row of source samples to resample.
	IW_SAMPLE *out_pix; // The resulting resampled row.

	// The "64" data is only actually only 64-bit if IW_SAMPLE is 64-bit.
	IW_SAMPLE *intermediate64;
	IW_SAMPLE *intermediate_alpha64;
	IW_SAMPLE *final_alpha64;

	iw_float32 *intermediate32;
	iw_float32 *intermediate_alpha32;
	iw_float32 *final_alpha32;

	int caller_set_output_csdescr;
	int warn_invalid_output_csdescr;

	struct iw_channelinfo_in img1_ci[IW_CI_COUNT];

	struct iw_image img1;
	struct iw_csdescr img1cs;
	int img1_imgtype_logical;

	int img1_numchannels_physical;
	int img1_numchannels_logical;
	int img1_alpha_channel_index;

	// The suggested background color stored in the input file.
	int img1_bkgd_label_set;
	struct iw_rgb_color img1_bkgd_label; // img1.bkgd_color_* (source colorspace, then converted to linear)
	int use_bkgd_label; // Prefer the bkgd color from the input file.

	struct iw_channelinfo_intermed intermed_ci[IW_CI_COUNT];
	int intermed_imgtype;
	int intermed_numchannels;
	int intermed_alpha_channel_index;
	int intermed_width, intermed_height;

	struct iw_image img2;
	struct iw_csdescr img2cs;
	struct iw_channelinfo_out img2_ci[IW_CI_COUNT];
	int img2_numchannels;

	int ditherfamily_by_channeltype[IW_NUM_CHANNELTYPES]; // Indexed by IW_CHANNELTYPE_[Red..Gray]
	int dithersubtype_by_channeltype[IW_NUM_CHANNELTYPES]; // Indexed by IW_CHANNELTYPE_[Red..Gray]
	int uses_errdiffdither;
	struct iw_prng *prng; // Pseudorandom number generator state

	// Indexed by IW_DIMENSION_*.
	struct iw_resize_settings resize_settings[2];

	int to_grayscale;

	// Requested color counts; 0 = "not set"
	int color_count_req[IW_NUM_CHANNELTYPES]; // Indexed by IW_CHANNELTYPE_[Red..Gray]

	int apply_bkgd;
	int apply_bkgd_strategy; // IW_BKGD_STRATEGY_*
	int caller_set_bkgd;
	struct iw_rgb_color bkgd; // The (primary) background color that will be applied.
	int bkgd_checkerboard; // valid if apply_bkgd is set. 0=solid, 1=checkerboard
	int bkgd_check_size;
	int bkgd_check_origin[2]; // Indexed by IW_DIMENSION_*
	struct iw_rgb_color bkgd2; // The secondary background color that will be applied.

	void *userdata;
	iw_translatefn_type translate_fn;
	iw_warningfn_type warning_fn;

	int output_depth_req; // Bits/sample requested by the caller.
	int output_maxcolorcode_req[IW_NUM_CHANNELTYPES];

	int input_maxcolorcode_int;  // Based on the source image's full bitdepth
	double input_maxcolorcode;

	int support_reduced_input_bitdepths;

	int disable_output_lookup_tables;
	int reduced_output_maxcolor_flag;  // Are there any reduced output maxcolorcodes?

	// Max number of rows for error-diffusion dithering, including current row.
#define IW_DITHER_MAXROWS 3
	// Error accumulators for error-diffusion dithering.
	IW_SAMPLE *dither_errors[IW_DITHER_MAXROWS]; // 0 is the current row.

	int randomize; // 0 to use random_seed, nonzero to use a different seed every time.
	int random_seed;

	size_t max_malloc;
	int max_width, max_height;

	int error_flag;
	char *error_msg;

	struct iw_opt_ctx optctx;

	int no_gamma; // Disable gamma correction. (IW_VAL_DISABLE_GAMMA)
	int intclamp; // Clamp the intermediate samples to the 0.0-1.0 range.
	int no_cslabel; // Disable writing of a colorspace label to the output file.
	int grayscale_formula; // IW_GSF_*
	double grayscale_weight[3];
	int pref_units; // IW_PREF_UNITS_*

	// Optimization codes. Can be set to 0 to disallow this optimization
	iw_byte opt_grayscale; // RGB-to-grayscale
	iw_byte opt_palette;   // Palette images
	iw_byte opt_16_to_8;   // Reduce >8 bitdepth to 8
	iw_byte opt_strip_alpha; // RGBA->RGB or GA->G
	iw_byte opt_binary_trns; // Color-keyed binary transparency

	int canvas_width, canvas_height;
	int input_start_x, input_start_y, input_w, input_h;

	// These are not used by the core library, but codecs may use them:
	int compression; // IW_COMPRESSION_*. Suggested compression algorithm.
	int page_to_read;
	int include_screen;
	int jpeg_quality;
	int jpeg_samp_factor_h, jpeg_samp_factor_v; // 0 means default
	int jpeg_arith_coding; // nonzero = use arithmetic coding
	int pngcmprlevel;
	int deflatecmprlevel;
	int interlaced;
	int bmp_no_fileheader;
	int bmp_version; // requested BMP file version to write
	double webp_quality;

	// Color correction tables, to improve performance.
	double *input_color_corr_table;
	// This is not for converting linear to the output colorspace; it's the
	// same as input_color_corr_table except that it might have a different
	// number of entries, and might be for a different colorspace.
	double *output_rev_color_corr_table;

	double *nearest_color_table;

	struct iw_zlib_module *zlib_module;
};

// Defined imagew-util.c
struct iw_prng *iwpvt_prng_create(struct iw_context *ctx);
void iwpvt_prng_destroy(struct iw_context *ctx, struct iw_prng *prng);
void iwpvt_prng_set_random_seed(struct iw_prng *prng, int s);
iw_uint32 iwpvt_prng_rand(struct iw_prng *prng); // Returns a pseudorandom number.
int iwpvt_util_randomize(struct iw_prng *prng); // Returns the random seed that was used.
void* iwpvt_default_malloc(void *userdata, unsigned int flags, size_t n);
void iwpvt_default_free(void *userdata, void *mem);

// Defined in imagew-resize.c
struct iw_rr_ctx *iwpvt_resize_rows_init(struct iw_context *ctx,
  struct iw_resize_settings *rs, int channeltype);
void iwpvt_resize_rows_done(struct iw_context *ctx, struct iw_rr_ctx *rrctx);
void iwpvt_resize_row_main(struct iw_context *ctx, struct iw_rr_ctx *rrctx);

// Defined in imagew-opt.c
void iwpvt_optimize_image(struct iw_context *ctx);
