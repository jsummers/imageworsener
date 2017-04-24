// imagew-internals.h
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

#define IW_INCLUDE_UTIL_FUNCTIONS
#include "imagew.h"

#define IW_COPYRIGHT_YEAR "2011" "\xe2\x80\x93" "2017"

#ifdef IW_WINDOWS
#define IW_INLINE __inline
#else
#define IW_INLINE inline
#endif

#define IW_MSG_MAX 200 // The usual max length of error messages, etc.

// Data type used for samples during some internal calculations
typedef double iw_tmpsample;

#ifdef IW_64BIT
#define IW_DEFAULT_MAX_DIMENSION 40000
#define IW_DEFAULT_MAX_MALLOC 2000000000
#else
#define IW_DEFAULT_MAX_DIMENSION 40000 // Must be less than sqrt(2^31).
#define IW_DEFAULT_MAX_MALLOC 2000000000
#endif

#define IW_BKGD_STRATEGY_EARLY 1 // Apply background before resizing
#define IW_BKGD_STRATEGY_LATE  2 // Apply background after resizing

#define IW_NUM_CHANNELTYPES 5 // 5, for R,G,B, Alpha, Gray
#define IW_CI_COUNT 4 // Number of channelinfo structs (=4, for R, G, B, A)

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
	double out_true_size; // Size onto which to map the input image.
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

	double bkgd_color_lin; // Used if ctx->apply_bkgd && bkgd_strategy==EARLY

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

	double bkgd1_color_lin; // Used if ctx->apply_bkgd
	double bkgd2_color_lin; // Used if ctx->apply_bkgd && bkgd_checkerboard
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

	int has_bkgdlabel;
	unsigned int bkgdlabel[4]; // Indexed by IW_CHANNELTYPE_[RED..ALPHA]
};

struct iw_option_struct {
	char *name;
	char *val;
};

// Used to help separate settings that were requested by the caller,
// and that might not always be respected, or applicable.
struct iw_req_struct {
	int output_depth; // Bits/sample requested by the caller.
	int output_sample_type; // Reserved for future expansion.
	int output_maxcolorcode[IW_NUM_CHANNELTYPES];

	// Requested color counts; 0 = "not set"
	int color_count[IW_NUM_CHANNELTYPES]; // Indexed by IW_CHANNELTYPE_[Red..Gray]

	// Image size requested by user. The actual size to use is stored in .resize_settings.
	int out_true_valid;
	double out_true_width, out_true_height;

	int output_rendering_intent;

	int output_cs_valid;
	struct iw_csdescr output_cs;

	int suppress_output_cslabel;
	int negate_target;

	int bkgd_valid;
	int bkgd_checkerboard; // 1=caller requested a checkerboard background
	struct iw_color bkgd; // The requested (primary) background color (linear colorspace).
	struct iw_color bkgd2; // The requested secondary background color.

	int output_bkgd_label_valid;
	struct iw_color output_bkgd_label; // Uses linear colorspace

	int use_bkgd_label_from_file; // Prefer the bkgd color from the input file.
	int suppress_output_bkgd_label;

	// These are not used by the core library, but codecs may use them:
	int output_format;
	int compression; // IW_COMPRESSION_*. Suggested compression algorithm.
	int page_to_read;
	int include_screen;
	int jpeg_samp_factor_h, jpeg_samp_factor_v; // 0 means default
	int interlaced;
	int bmp_no_fileheader;

	struct iw_option_struct *options;
	int options_count;
	int options_numalloc;
};

struct iw_context {
	int caller_api_version;
	int use_count;
	unsigned int output_profile;

	iw_mallocfn_type mallocfn;
	iw_freefn_type freefn;

	iw_float32 *intermediate32;
	iw_float32 *intermediate_alpha32;
	iw_float32 *final_alpha32;

	struct iw_channelinfo_in img1_ci[IW_CI_COUNT];

	struct iw_image img1;
	struct iw_csdescr img1cs;
	int img1_imgtype_logical;

	int img1_numchannels_physical;
	int img1_numchannels_logical;
	int img1_alpha_channel_index;

	// The suggested background color read from the input file.
	int img1_bkgd_label_set;
	struct iw_color img1_bkgd_label_inputcs;
	struct iw_color img1_bkgd_label_lin; // img1.bkgd_color_*

	struct iw_channelinfo_intermed intermed_ci[IW_CI_COUNT];
	int intermed_imgtype;
	int intermed_numchannels;
	int intermed_alpha_channel_index;
	int intermed_canvas_width, intermed_canvas_height;

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

	int apply_bkgd; // 1 = We will be applying a background color.
	int apply_bkgd_strategy; // IW_BKGD_STRATEGY_*
	int bkgd_checkerboard; // valid if apply_bkgd is set. 0=solid, 1=checkerboard
	int bkgd_check_size;
	int bkgd_check_origin[2]; // Indexed by IW_DIMENSION_*

#define IW_BKGD_COLOR_SOURCE_NONE 0 // Use a default color
#define IW_BKGD_COLOR_SOURCE_FILE 1 // Use ctx->img1_bkgd_label_lin
#define IW_BKGD_COLOR_SOURCE_REQ  2 // Use ctx->req.bkgd
	int bkgd_color_source; // Valid if .apply_bkgd is set.

	// Background color alpha samples. (The color samples are stored in
	// iw_channelinfo_out.)
	double bkgd1alpha, bkgd2alpha;

	void *userdata;
	iw_translatefn_type translate_fn;
	iw_warningfn_type warning_fn;

	int input_maxcolorcode_int;  // Based on the source image's full bitdepth
	double input_maxcolorcode;

	int support_reduced_input_bitdepths;

	int disable_output_lookup_tables;
	int reduced_output_maxcolor_flag;  // Are there any reduced output maxcolorcodes?

	// Max number of rows for error-diffusion dithering, including current row.
#define IW_DITHER_MAXROWS 3
	// Error accumulators for error-diffusion dithering.
	double *dither_errors[IW_DITHER_MAXROWS]; // 0 is the current row.

	int randomize; // 0 to use random_seed, nonzero to use a different seed every time.
	int random_seed;

	size_t max_malloc;
	int max_width, max_height;

	int error_flag;
	char *error_msg;

	struct iw_opt_ctx optctx;

	int no_gamma; // Disable gamma correction. (IW_VAL_DISABLE_GAMMA)
	int intclamp; // Clamp the intermediate samples to the 0.0-1.0 range.
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

	struct iw_req_struct req;

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
char* iwpvt_strdup_dbl(struct iw_context *ctx, double n);

// Defined in imagew-resize.c
struct iw_rr_ctx *iwpvt_resize_rows_init(struct iw_context *ctx,
  struct iw_resize_settings *rs, int channeltype, int num_in_pix, int num_out_pix);
void iwpvt_resize_rows_done(struct iw_rr_ctx *rrctx);
void iwpvt_resize_row_main(struct iw_rr_ctx *rrctx, iw_tmpsample *in_pix, iw_tmpsample *out_pix);

// Defined in imagew-opt.c
void iwpvt_optimize_image(struct iw_context *ctx);
