// imagew-internals.h
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

#define IW_INCLUDE_UTIL_FUNCTIONS
#include "imagew.h"

#define IW_COPYRIGHT_YEAR "2011"

#ifdef IW_WINDOWS
#define IW_INLINE __inline
#else
#define IW_INLINE inline
#endif

#define IW_ERRMSG_MAX 200

#if defined(IWCONFIG_SAMPLE_DOUBLE)
#define IW_SAMPLE double
#elif defined(IWCONFIG_SAMPLE_FLOAT)
#define IW_SAMPLE float
#else
#define IW_SAMPLE double
#endif

#define iw_float32 float
#define iw_float64 double

#ifdef IW_64BIT
#define IW_MAX_DIMENSION 1000000
#define IW_DEFAULT_MAX_MALLOC 2000000000000
#else
#define IW_MAX_DIMENSION 40000 // Must be less than sqrt(2^31).
#define IW_DEFAULT_MAX_MALLOC 2000000000
#endif

#define IW_BKGD_STRATEGY_EARLY 1 // Apply background before resizing
#define IW_BKGD_STRATEGY_LATE  2 // Apply background after resizing

struct iw_rgb_color {
	IW_SAMPLE c[3]; // Indexed by IW_CHANNELTYPE[Red..Blue]
};

struct iw_weight_struct {
	int src_pix;
	int dst_pix;
	double weight;
};

struct iw_weightlist_struct {
	int used;
	int alloc;
	int isvalid;
	struct iw_weight_struct *w;
};

struct iw_resize_settings {
	int family;
	double radius; // Also = "lobes" in Lanczos, etc.
	double param1; // 'B' in Mitchell-Netravali cubics.
	double param2; // 'C' in Mitchell-Netravali cubics.
	double blur_factor;
	double channel_offset[3]; // Indexed by IW_CHANNELTYPE_[Red..Blue]
};

struct iw_channelinfo_in {
	int channeltype;
};

struct iw_channelinfo_intermed {
	int channeltype;

	int cvt_to_grayscale; // (on input)
	int corresponding_input_channel; // (or the first of 3 channels if cvt_to_grayscale)

	int corresponding_output_channel; // Can be -1 if no such channel.

	IW_SAMPLE bkgd_color_lin; // Used if ctx->apply_bkgd && bkgd_strategy==EARLY
};

struct iw_channelinfo_out {
	int ditherfamily;
	int dithersubtype;
	int channeltype;

	// If restricting to a number of colors, colors are evenly
	// spaced (as evenly spaced as possible) in the target color space.
	int color_count; // 0=default, otherwise ideally it should be 2, 4, 16, 18, 52, 86

	IW_SAMPLE bkgd_color_lin; // Used if ctx->apply_bkgd
	IW_SAMPLE bkgd2_color_lin; // Used if ctx->apply_bkgd
};

// Tracks the current image properties. May change as we optimize the image.
struct iw_opt_ctx {
	int height, width;
	int imgtype;
	int bit_depth;
	size_t bpr;

	// A pointer to the current pixels. May point to tmp_pixels, or
	// to ctx->img2.pixels.
	const unsigned char *pixelsptr;

	// A place for optimized pixels. If this is non-NULL, it will be
	// freed when IW is finished.
	unsigned char *tmp_pixels;

	int has_transparency;
	int has_partial_transparency;
	int has_16bit_precision;
	int has_color;
	int palette_is_grayscale;

	struct iw_palette *palette;

	int has_colorkey_trns;
	unsigned int colorkey_r, colorkey_g, colorkey_b;
};

struct iw_context {
	unsigned int output_profile;

	int num_in_pix;
	int num_out_pix;
	IW_SAMPLE *in_pix; // A single row of pixels
	IW_SAMPLE *out_pix; // A single row of pixels

	int num_tmp_row;

	// Samples are normally in the range 0.0 and 1.0.
	// However, intermediate samples are allowed to have values beyond this
	// range. This is important when using resampling filters that have
	// "overshoot". If we were to clamp the intermediate values to the normal
	// range, it could make the algorithm "non-separable". That is, the final
	// image would not be the same as if we had resized both dimensions
	// simultaneously.

	IW_SAMPLE *intermediate;
	IW_SAMPLE *intermediate_alpha;
	IW_SAMPLE *final_alpha;

	int caller_set_output_csdescr;
	int warn_invalid_output_csdescr;

	struct iw_channelinfo_in img1_ci[4];

	struct iw_image img1;
	struct iw_csdescr img1cs;

	int img1_numchannels;
	int img1_alpha_channel_index;

	// The suggested background color stored in the input file.
	int img1_bkgd_label_set;
	struct iw_rgb_color img1_bkgd_label; // img1.bkgd_color_* (source colorspace, then converted to linear)
	int use_bkgd_label; // Prefer the bkgd color from the input file.

	struct iw_channelinfo_intermed intermed_ci[4];
	int intermed_imgtype;
	int intermed_numchannels;
	int intermed_alpha_channel_index;
	int intermed_width, intermed_height;

	struct iw_image img2;
	struct iw_csdescr img2cs;
	struct iw_channelinfo_out img2_ci[4];
	int img2_numchannels;

	int ditherfamily_by_channeltype[5]; // Indexed by IW_CHANNELTYPE_[Red..Gray]
	int dithersubtype_by_channeltype[5]; // Indexed by IW_CHANNELTYPE_[Red..Gray]
	int uses_errdiffdither;
	int uses_r2dither;

	// Algorithms to use when changing the horizontal size.
	// Indexed by IW_DIMENSION_*.
	struct iw_resize_settings resize_settings[2];
	struct iw_resize_settings resize_settings_alpha;
	int use_resize_settings_alpha;

	int to_grayscale;

	int offset_color_channels; // Set if any of the color channel offsets may be nonzero.

	// For color counts, 0 = "not set"
	int color_count[5]; // Indexed by IW_CHANNELTYPE_[Red..Gray]

	int apply_bkgd;
	int apply_bkgd_strategy; // IW_BKGD_STRATEGY_*
	int colorspace_of_bkgd;
	int caller_set_bkgd; // FIXME: remove this
	struct iw_rgb_color bkgd; // The (primary) background color that will be applied.
	int bkgd_checkerboard; // valid if apply_bkgd is set. 0=solid, 1=checkerboard
	int bkgd_check_size;
	int bkgd_check_origin[2]; // Indexed by IW_DIMENSION_*
	struct iw_rgb_color bkgd2; // The secondary background color that will be applied.

	void *userdata;
	iw_warningfn_type warning_fn;

	// The nominal bits/sample of img2_pixels.
	int output_depth; // TODO: This is the same as img2.bit_depth. We could remove one of them.
	double output_maxcolorcode;

	double input_maxcolorcode;
	
	int support_reduced_input_bitdepths; // Set this to use the next 3 fields.
	int significant_bits[5];     // 0 means default. Indexed by IW_CHANNELTYPE_[Red..Gray]
	int insignificant_bits[5];   // How much we have to shift the samples.
	double input_maxcolorcode_ext[5];

	// Max number of rows for error-diffusion dithering, including current row.
#define IW_DITHER_MAXROWS 3
	// Error accumulators for error-diffusion dithering.
	IW_SAMPLE *dither_errors[IW_DITHER_MAXROWS]; // 0 is the current row.

	float *random_dither_pattern;

	int randomize; // 0 to use random_seed, nonzero to use a different seed every time.
	int random_seed;

	size_t max_malloc;

	int error_flag;
	char *error_msg;

	struct iw_opt_ctx optctx;

	int no_gamma; // Disable gamma correction. (IW_VAL_DISABLE_GAMMA)
	int intclamp; // clamp the intermediate samples to the 0.0-1.0 range
	int no_cslabel; // Disable writing of a colorspace label to the output file.
	int edge_policy;
	int grayscale_formula;
	int density_policy; // IW_DENSITY_POLICY_*

	// Optimization codes. Can be set to 0 to disallow this optimization
	unsigned char opt_grayscale; // RGB-to-grayscale
	unsigned char opt_palette;   // Palette images
	unsigned char opt_16_to_8;   // Reduce >8 bitdepth to 8
	unsigned char opt_strip_alpha; // RGBA->RGB or GA->G
	unsigned char opt_binary_trns; // Color-keyed binary transparency

	int canvas_width, canvas_height;
	int input_start_x, input_start_y, input_w, input_h;

	// Not used by the core library, but codecs may use this:
	int jpeg_quality;
	int jpeg_samp_factor_h, jpeg_samp_factor_v; // 0 means default
	int pngcmprlevel;
	int interlaced;
	double webp_quality;

	// Color correction tables, to improve performance.
	double *input_color_corr_table;
	// This is not for converting linear to the output colorspace; it's the
	// same as input_color_corr_table except that it might have a different
	// number of entries (and might be from a different colorspace).
	double *output_rev_color_corr_table;

	struct iw_weightlist_struct weightlist;
	double cur_offset;

	const struct iw_stringtableentry *stringtable[IW_NUMSTRINGTABLES];
};

// Defined imagew-util.c
void iwpvt_util_set_random_seed(int s);
void iwpvt_util_randomize(void);

// Defined in imagew-resize.c
void iwpvt_resize_row_precalculate(struct iw_context *ctx, struct iw_resize_settings *rs, int channeltype);
void iwpvt_resize_row_main(struct iw_context *ctx, struct iw_resize_settings *rs, int dimension);
void iwpvt_weightlist_free(struct iw_context *ctx);

// Defined in imagew-opt.c
void iwpvt_optimize_image(struct iw_context *ctx);

enum iw_corestrings {
	iws_err_msg_not_avail=1,
	iws_nomem,
	iws_warn_reduce_to_8,
	iws_warn_disable_offset_grayscale,
	iws_warn_trans_incomp_format,
	iws_warn_trans_incomp_offset,
	iws_warn_chkb_incomp_offset,
	iws_warn_output_forced_srgb,
	iws_warn_output_forced_linear,
	iws_output_prof_not_set,
	iws_internal_error,
	iws_internal_unk_strategy,
	iws_image_too_large,
	iws_dimensions_too_large,
	iws_dimensions_invalid,
	iws_warn_fltpt_no_posterize,
	iws_copyright
};

const char *iwpvt_get_string(struct iw_context *ctx, int s);
void iwpvt_errf(struct iw_context *ctx, int n, ...);
void iwpvt_err(struct iw_context *ctx, int n);
void iwpvt_warn(struct iw_context *ctx, int n);
