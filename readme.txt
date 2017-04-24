ImageWorsener is a raster image scaling and processing utility.
Version 1.3.1
Copyright (c) 2011-2017 Jason Summers  <jason1@pobox.com>

Web site: http://entropymine.com/imageworsener/

All nontrivial automated image processing causes a loss of information.
While ImageWorsener will degrade your images, its goal is to degrade them as
little as possible.

===========================================================================

This program is distributed under an MIT-style license. Refer to the
included COYPING.txt file for more information.

Versions up to 1.1.0 used the GPLv3+ license.

If source code is not included in this package, it may be downloaded from
the web site at <http://entropymine.com/imageworsener/>.

===========================================================================

If this package contains executable binaries, we need to state the following:

---

This software is based in part on the work of the Independent JPEG Group.

---

This software may include code from libwebp (part of the WebM project), which
has the following license:

| Copyright (c) 2010, Google Inc. All rights reserved.
|
| Redistribution and use in source and binary forms, with or without
| modification, are permitted provided that the following conditions are
| met:
|
|   * Redistributions of source code must retain the above copyright
|     notice, this list of conditions and the following disclaimer.
|
|   * Redistributions in binary form must reproduce the above copyright
|     notice, this list of conditions and the following disclaimer in
|     the documentation and/or other materials provided with the
|     distribution.
|
|   * Neither the name of Google nor the names of its contributors may
|     be used to endorse or promote products derived from this software
|     without specific prior written permission.
|
| THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
| "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
| LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
| A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
| HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
| SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
| LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
| DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
| THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
| (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
| OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


===========================================================================

For additional information about ImageWorsener, see the file technical.txt.

Primary features:
 - Resize
 - Posterize (reduce the number of colors)
 - Dither
 - Convert to grayscale
 - Apply background colors
 - "Channel offset"

ImageWorsener is focused on correctness, and not on performance. It is
relatively slow. Among the things it (hopefully) does right:
 - Gamma correction, and the sRGB colorspace (not just the gamma=2.2
   approximation).
 - Conversion to grayscale.
 - Processing of transparency.
 - All the image scaling algorithms that it implements.

Other information:
 - The command-line utility fully supports PNG, JPEG, BMP, and WebP files, and
   has partial support for GIF, TIFF, MIFF, and PPM/PGM/PBM/PAM.
 - The library is (more or less) not specific to a particular file format.
 - Full support for high color depth (16 bits per sample).
 - Some options can be set differently for the different dimensions
   (horizontal vs. vertical) or color channels (red, green, blue).
 - Has several options to enable "hacks" to make it behave differently. This
   can be useful if you're trying to make it behave the same as another
   application.
 - The package includes a simple regression testing script. The script
   requires a Unix-ish environment.
 - Intended to be compatible with Windows, Linux, and Cygwin. Includes project
   files for Visual Studio 2008.


The ImageWorsener C library
---------------------------

Unfortunately, there is no documentation for the library, aside from the
comments in the imagew.h header file. The original idea was to have a well-
designed, stable, and documented library, but that hasn't worked out very well.
The API won't be broken for no reason, but I can't make promises about
compatibility between versions.


Documentation for the "imagew" command line utility
---------------------------------------------------

Synopsis
    imagew [options] <input-file> <output-file>


Options may appear anywhere on the command line, even after the file names.
The order of the options usually does not matter.

<input-file> and <output-file> can be prefixed with a "scheme" and a colon, so
that resources other than regular files can be supported. If your filename may
contain a colon, prefix it with "file:". In the Windows version, you can use
"clip:" to refer to the clipboard.

A scheme of "stdin:" or "stdout:" means the standard input or output stream
(this may not be supported for all formats). As a shortcut, a file name of
"-" may be used instead.

Numbers on the command line may be specified as rational numbers, using a
slash. For example, instead of "-w x0.666666666", you can use "-w x2/3".

Options:

 -w <width>  -h <height>  (or -width <width>  -height <height>)
 -s <width>,<height>
 -S <width>,<height>
   The width and height of the output image. <width> or <height> is normally a
   number of pixels.

   Using the -s option, or specifying only one of the width or height, will
   cause the "bestfit" feature to be enabled.
   Using the -S option, or specifying both the height and width, will cause
   "bestfit" to be disabled, so the image will have the exact size you request.

   If you use a prefix of "x", for example "-w x1.5", then the new size will
   instead be that number multiplied by the size of the source image.

 -bestfit
 -nobestfit
   Explicitly enable or disable the "bestfit" feature. If enabled, the image's
   shape will not be changed.

 -noresize
   Ensures that the image will not be resized at all. This is only useful if
   the source image may have non-square pixels.
   Incompatible with -bestfit and the sizing options.

 -imagesize <width>,<height>
   The other sizing options set the size of the entire output image. Normally,
   the region of that image corresponding to the input image will be that same
   size. However, you can use -imagesize to make that region be a different
   size. For example, you can shrink the image, to effectively
   add a border around it.
   Note that <height> and <width> do not have to be integers.
   If you use -imagesize, you should probably also use -translate and
   "-edge t".

 -infmt <fmt>  -outfmt <fmt>
   Specifies the image file format of the input or output file. If not used,
   imagew will try to figure out the format based on the contents of the file,
   or the file name.

   Valid values for <fmt>:
     png: PNG
     jpeg, jpg: JPEG
     webp: WebP
     bmp: Windows BMP
     gif: GIF (-infmt only)
     tiff, tif: TIFF (-outfmt only)
     miff: MIFF (experimental; limited support)
     pnm, ppm, pgm, pbm: Netpbm formats (only the binary formats are supported,
       not the rare "plain"/ASCII variants)

 -depth <n>  (-depthgray, -depthalpha)
 -depth <r>,<g>,<b>[,<a>]
 -depthcc <cc>
   The general number of bits of precision used per color channel in the
   output image. Valid values for <n>:
    "8" is the default for most formats.
    "16" is supported for PNG and TIFF formats.
    "32" is used with MIFF format (floating point).
   Other depths are supported in some special cases. Use with caution -- this
    can be useful, but it may not do what you expect, and may disable
    optimizations. Consider whether you should use -cc instead.
   With BMP format, requesting an unusual depth will cause a 16 bits/pixel
    image to be written if the total number of bits is no more than 16,
    or a 32 bits/pixel image otherwise. The total number of bits may not be
    more than 32, and no channel may have more than 16 bits. Depths "5,6,5"
    and "5,5,5" are the most common and most portable.
   PNG format supports arbitrary depths (from 1 to 16), using "sBIT" chunks,
    but these are ignored by most image viewers.
   The PPM and PGM formats support any bit depth from 1 to 16.
   If you use a depth less than 8, consider using -dither.

   Within the overall depth, you can reduce the number of colors that will
   actually be used, by using the "-cc" options.
   Note that this doesn't necessarily determine the depth used in the output
   file. If the image can be encoded at a smaller depth with no loss of
   information, IW may choose to do that (see also -noopt).

   The -depthcc option sets the depth based on the number of color levels,
   instead of the number of bits used to represent a color level. For example,
   "-depthcc 32" is equivalent to "-depth 5". This allows for greater
   flexibility, if the output format supports it. Currently, it's only useful
   with PPM, PGM, and PAM formats.

 -sampletype <type>
   Request that the output samples be written as unsigned integers (type="u"),
   or floating-point (type="f"). This option currently has no effect.

 -filter <name> (-filterx -filtery)
   The resizing algorithm to use. (It would be more accurate if this option
   were named "-resizealgorithm", but that's too hard to type.) Default is
   "auto".

   With -filterx and -filtery, you can use different algorithms for the
   horizontal and vertical dimensions. (This is possible because IW supports
   only "separable" resizing algorithms.) This may occasionally be useful for
   something, such as if you need to enlarge an image in one dimension while
   reducing it in the other.

   IW uses the filter even if the image size isn't being changed. Many
   filters will leave the image unchanged in that case, but some (such as
   bspline, mitchell, gaussian) will cause it to be blurred, at least
   slightly.

   Some filters are not particularly good for general purposes. The main
   filters you should consider using are lanczos, mitchell, lanczos2, catrom,
   and mix.

   Full list:

    nearest, point
     Nearest-neighbor resizing.

    mix
     Pixel mixing, a.k.a. area map. Not good for enlarging images, unless you
     want a pixelated effect.

    box
     Box filter.

    boxavg
     A slightly modified box filter, which is more well-behaved and symmetric,
     but less standard. Refer to technical.txt for more information.

    triangle, linear
     Triangle filter. When upscaling, this is the same as (bi)linear
     interpolation. When downscaling, the term "linear interpolation" is
     ambiguous -- see the "-blur x" option.

    gaussian
     Gaussian filter, evaluated out to 4 sigma.

    quadratic
     Quadratic interpolation. A rough approximation of a Gaussian filter.

    cubic<B>,<C>
     Generalized cubic interpolation as defined by Mitchell-Netravali.
     The usual range of both B and C is from 0 to 1.

    keys<n>
     This is a subset of the cubic filters. The normal range of n is from
     0 to 0.5.
      "keys0"    = "cubic1,0" = "bspline"
      "keys1/3" = "cubic1/3,1/3" = "mitchell"
      "keys0.5"  = "cubic0,0.5" = "catrom"

    hermite
     Hermite filter. Identical to "cubic0,0".

    bspline
     B-Spline filter. Identical to "cubic1,0".

    mitchell
     Mitchell filter. Identical to "cubic1/3,1/3".

    catrom
     Catmull-Rom spline. Identical to "cubic0,0.5".

    lanczos<n>
     Lanczos filter. n is the number of "lobes", and defaults to 3.

    blackman<n>
     Blackman filter. This is a windowed sinc filter, similar to Lanczos.
     n is the number of "lobes", and defaults to 4.

    hanning<n>, hann<n>
     A Hanning filter. This is a windowed sinc filter, similar to Lanczos.
     n is the number of "lobes", and defaults to 4.

    sinc<n>
     An unwindowed sinc filter. n is the number of "lobes", and defaults to 4.
     For experimental use only. This filter will almost always produce poor
     results.

    auto
     The default. IW will select a filter to use. Currently uses "catrom",
     unless the size is not being changed and "null" can be used.

    null
     No resizing. No need to specify this, but it's documented because it may
     be selected by the "auto" method.

 -blur <n> (-blurx -blury)
 -blur x[<n>]
   Adjust the width of the resampling filter.

   This is really a parameter of the resampling filter: if you use -blur,
   you should also use -filter. "-blur" does not work with some algorithms,
   such as "nearest".

   The default value is 1.0. Larger values blur the image more. A value
   smaller than 1 may sharpen the image, at the expense of aliasing. But a
   value that's too small will cause some pixels to be missed completely,
   leaving black lines.

   If the "x" prefix is used, and the image is being downscaled, the blur
   factor will be multiplied by the scale factor. If <n> is not given, it
   defaults to 1.

   The purpose of the "x" option is to make it easy to perform simple
   interpolation. For example, use "-filter linear -blur x1" to do linear
   interpolation. This is not a good way to downscale images, but it is
   what many applications do.

 -edge <name> (-edgex -edgey)
   The strategy for dealing with the pixels near the edges of images.

   Strategies available:
    "s" (for "standard"): This is the default strategy. Instead of inventing
       samples that are beyond the edge of the source image, give extra weight
       to the smaller-than-usual number of samples that are available. If no
       samples are available, the pixel will be colored black or be
       transparent. This can happen if you use -translate or -offset.
    "r" (for "replicate"): Pixels beyond the edges of the image are assumed to
       be the color of the nearest pixel within the image.
    "t" (for "transparent"): Pixels beyond the edges of the image are assumed
       to be transparent. Note that this can be used with -bkgd.

 -intclamp
   IW always resizes the image first vertically, then horizontally, then
   "clamps" the sample values to the normal visible range (usually thought of
   as being from 0 to 255).
   With -intclamp, clamping is also done to the "intermediate" image, after
   the vertical-resize operation. This is not the *correct* thing to do,
   though the difference usually isn't noticeable. The purpose of this option
   is to let you try to replicate what some other applications do.
   Exception: If you are writing to a MIFF file, samples are never clamped,
   unless you use -intclamp, in which case both the intermediate and final
   samples are clamped.

 -reorient <operation>
   Rotate or mirror the image.

   Available operations:
    "5" or "rotate90": Rotate 90 degrees clockwise.
    "3" or "rotate180": Rotate 180 degrees.
    "6" or "rotate270": Rotate 270 degrees clockwise.
    "1" or "fliph": Flip horizontally (across a vertical axis).
    "2" or "flipv": Flip vertically (across a horizontal axis).
    "4" or "transpose": Flip across upper-left-to-lower-right axis.
    "7" or "transverse": Flip across upper-right-to-lower-left axis.
    "0": No change.

   Rotating/mirroring causes the source image to be interpreted as if it had a
   different orientation. So, all other options (-width, -crop, etc.) are based
   on the new orientation, not the original.

 -crop <x>,<y>,<width>,<height>
   Crop the source image before processing it. Pixels outside the specified
   area will be ignored.
   The parameters are in pixels. (0,0) is the upper-left pixel.
   If <width> or <height> is -1 or is not given, the area will extend to the
   right or bottom edge of the image.

 -grayscale
   Convert the image to grayscale.

   The image will be converted to grayscale early in the processing pipeline,
   and only a single grayscale channel (and possibly also an alpha channel)
   will be processed from then on. Occasionally that makes a difference, such
   as when using the "r" (random) dithering method.

 -condgrayscale
   Conditionally process the image as grayscale.

   If the input image is encoded as a grayscale image, behave as if
   "-grayscale" were used. This is not dependent on whether the image actually
   contains non-gray colors; it's about whether it's encoded in a way that
   could support non-gray colors. The exact meaning is dependent on the input
   file format.

   Be aware that, just because IW encoded an output image as grayscale,
   doesn't mean it processed it as grayscale. If the final image happens to
   contain only grayscale colors, IW may choose to optimize it by encoding it
   as grayscale. (So, you probably shouldn't use -condgrayscale on images that
   were created by IW, if you want predictable results.)

 -gsf <name> (or -grayscaleformula <name>)
   The formula to use when converting a color image to grayscale. Using -gsf
   implies -grayscale, unless -condgrayscale was used.
   Available options:
    s (default) = The standard formula, based on luminance. Equivalent to
     "w0.212655,0.715158,0.072187". This is usually what you should use.
    c = The "compatibility" formula. Equivalent to "w0.299,0.587,0.114".
     This should generally only be used with -nogamma.
    w<r>,<g>,<b> = Use custom weights for the red, green, and blue components.
     The weights will be normalized so that they add up to 1.
    v<max>,<mid>,<min> = Use custom weights for the component with the
     maximum, middle, and minimum value. That is, the first weight will be
     applied to whichever component of that pixel has the largest value, etc.
     "v1,0,0" and "v0.5,0,0.5" have been suggested as grayscale conversion
     formulas, though they should be used with caution.

 -negate
   Negate the image's colors. The operation is performed in the output
   colorspace. If a background color is applied to the image, it happens before
   negation, so the background will be negated. If a background color label is
   written to the file, it is not affected by -negate.

 -bkgd <color1>[,<color2>]
   Apply a background color to the transparent and partially-transparent parts
   of the image. This is the only way to remove transparency from an image.

   The color uses an HTML-like format with 3, 6, or 12 hex digits. For
   example:
    -bkgd f00          = bright red
    -bkgd ff0000       = the same red color as above
    -bkgd ffff00000000 = the same red color as above
    -bkgd 999          = medium gray

   Colors with transparency may also be specified, by using 4, 8, or 16 hex
   digits. The final component is the alpha value (0 means fully transparent).
   The alpha value will be ignored if the image format does not support
   transparency.

   Background colors are always specified in the sRGB color space. They will
   be converted to whatever colorspace is used by the image.

   If you supply two colors, a checkerboard background will be used.

   This does not affect the background color label that may be written to the
   output image's metadata. Use -bkgdlabel for that.

 -checkersize <n>
   For checkerboard backgrounds, specifies the size of the squares in pixels.
   The default is 16.

 -checkerorigin <x>,<y>
   For checkerboard backgrounds, adjust the position of the checkerboard
   background.

 -usebkgdlabel
   If the input file contains a background color label (e.g. a PNG bKGD chunk),
   and you used the -bkgd option, IW has to decide which of these background
   colors to prefer in the event that a background is applied to the image.
   Normally, it prefers the color from the -bkgd option. But if you use the
   -usebkgdlabel option, it will prefer the color from the input file.

 -bkgdlabel <color>
   Specify the background color to write to the output file's metadata, if
   supported by the file format. The color is always given in the sRGB
   colorspace. The format of <color> is the same as for the -bkgd option.

 -nobkgdlabel
   Do not copy the input file's background color label to the output file.
   Incompatible with -bkgdlabel.

 -cc <n>  (-cccolor -ccalpha -ccred -ccgreen -ccblue -ccgray)
 -cc <r>,<g>,<b>[,<a>]
   Posterization. "cc" stands for "color count".
   The maximum number of different values (brightness levels, opacity levels)
   to use in each channel, including the alpha channel if present.
   The available values will be distributed as evenly as possible from among
   the possible values (based on -depth) in the output color space. An
   optimized palette is not used. If you use "-cc", consider also using
   "-dither".

   The -cc option should usually not be used when writing to a lossy image
   format (JPEG, WebP). Lossy formats do not store colors precisely enough,
   so the resulting image will have more colors than requested.

   If you use -cc expecting to get a PNG image in a particular format, be aware
   that background color labels are not affected by -cc, and can prevent some
   image format optimizations from occurring. For example, "-grayscale -cc 2"
   will not always produce a 1-bpp grayscale image, unless you also use
   -nobkgdlabel.

   The -ccX options let you have a different setting for different channels.
   If you specify overlapping options, the most specific option will have
   priority.
   -cccolor affects all channels except the alpha channel.
   -ccalpha affects only the alpha channel. Use "-ccalpha 2" for binary
     transparency.
   -ccgray applies only if you force grayscale output, using "-grayscale".

 -dither <dithertype> (-dithercolor -ditheralpha -ditherred -dithergreen
                       -ditherblue -dithergray)
   Enable dithering.

   Dither types available:
    "none": No dithering.
    "f" or "fs": Floyd-Steinberg.
    "o": 8x8 Ordered dither.
    "halftone": A sample 8x8 halftone dither.
    "r": Random dither. See also the -randseed option.
    "r2": "Random2" dither - Same as Random, except that all color channels
          (but not alpha) use the same random pattern. The colors will be more
          consistent than with Random, but the image will be grainier.
    "sierra" or "sierra3"
    "sierra2"
    "sierralite"
    "jjn": Jarvis, Judice, and Ninke.
    "burkes"
    "atkinson"

   You can enable dithering on any image, no matter how many colors it uses.
   However, if lots of colors are available, the dithering effect will be
   invisible to the human eye (unless you use a paint program to turn the
   image's contrast *way* up). Normally, if you enable dithering, you should
   also use one or more of the "-cc" options to limit the number of colors
   available.

   If you are applying a background to an image with transparency, dithering
   is only done after the background has been applied, so -ditheralpha has no
   effect. There's no way to dither the alpha channel and then apply a
   background, unless you invoke IW multiple times using an intermediate image
   file.

 -cs <colorspace>
   The colorspace to use for the output image. Ideally, the image should will
   *look* about the same regardless of what colorspace you choose. But it
   might not if your viewer doesn't support color correction, or the image
   format doesn't support colorspace labels, or IW doesn't know how to write
   an appropriate label, or you use the -negate option. This is fairly safe to
   use when writing PNG files, but should be used with caution with most other
   formats.

   <colorspace> =  linear | gamma<gamma> | srgb | srgb[prsa] | rec709

   <gamma> = The gamma value. This is an "image gamma", like 2.2 (not a "file
    gamma" like 0.4545"). "g1.0" is the same as "linear".

   "srgb": sRGB colorspace. Can optionally be followed by a letter indicating
    the "rendering intent" (equivalent to using the -intent option):
    p=perceptual, r=relative, s=saturation, a=absolute.

   "rec709": The color response curve defined by ITU-R Recommendation BT.709
    (Rec. 709).

   By default IW will choose a colorspace that works with the output format;
   almost always sRGB.

 -intent <rendering-intent>
   Specify the color profile "rendering intent" setting to be written to the
   output file. This does not affect image processing.

   By default, the rendering intent of the input image will be used. If that's
   not possible, a default setting will be used that may depend on the output
   image format.

   Rendering intents available:
    "p" or "perceptual"
    "r" or "relative"
    "s" or "saturation"
    "a" or "absolute"
    "default": Ignore the intent of the input image.
    "none": Request that no rendering intent be written.

 -inputcs <colorspace>
   Assume the input image is in the given colorspace. This is intended to be
   used in case IW cannot correctly figure out the colorspace automatically.
   Although it can be used to apply a gamma-correction operation to the image,
   that's not really what it's intended for.

 -nogamma
   Disable all color correction.
   The main purpose of this option is to let you try to replicate what some
   other applications might do.
   Note that there are many *wrong* ways to handle color correction, and this
   only recreates one of them. For example, some apps might do gamma correction
   when the image is read in, but not when it is resized. That's not what
   -nogamma will do.

   If you use this with "-grayscale", you should probably also use "-gsf c".

   This does not affect the colorspace label that will be written to the output
   file. If a label is written, it may not be the label you want.

 -nocslabel
   Do not write a colorspace label to the output image file (if applicable).
   This does not affect the image processing.
   May be useful for dealing with defective web browsers (I'm looking at
   *you*, Firefox) that display sRGB image colors differently than they
   display sRGB CSS/HTML colors.

 -density <density-policy>
 -density <units-code><density>[,<density-y>]
   Control how the density label (i.e. pixels per inch) of the output image is
   calculated.

   Density policies available:
    "auto": The default. Currently, writes a density only if the image size is
       not being changed.
    "none": Do not write a density.
    "keep": Use the same density as the source image.
    "adjust": Adjust the density so that the target image is the same physical
       size as the source image.

   Instead of a policy, you can request a specific density. Use "i" for pixels
   per inch, or "c" for pixels per centimeter. For example, use
   "-density i300" for 300 dpi.

   This is limited by the target image format's support for a density labels.
   Some formats do not support them, while others require them.

 -translate [s]<x>,<y>
   Move the entire image by the given amount, measured in target pixels. This
   is mainly intended for use with -imagesize, or for fine-tuning the image's
   position relative to the pixel grid.
   Note that x and y do not have to be integers.
   If you use the "s" prefix, the measurements are in source pixels instead of
   target pixels.

 -offset<channel> <n> (-offsetred -offsetgreen -offsetblue -offsetrb
                       -offsetvred -offsetvgreen -offsetvblue -offsetvrb)
   While scaling the image, shift the position of a color channel by n output
   pixels. A fractional number of pixels is allowed.

   The options containing "v" shift the channel vertically; the others shift
   it horizontally.
   The "rb" options shift the red channel by the amount you specify, and the
   blue channel in the opposite direction by the same amount.

   Transparency is incompatible with this feature. If there's any chance your
   image has transparency, you should also use -bkgd. Otherwise, IW will
   select a background color that you probably won't like.

   Checkerboard backgrounds are currently not supported when -offsetX is used.

 -randseed <n>
   n is either an integer or the letter "r".
   The seed to use if IW needs to generate random numbers.
   "r" means to use a different random seed every time.
   Default is 0.

 -compress <name>
   Suggest the data compression method to use when writing the image.
   Recognized options:
    "none"
    "zip"
    "lzw"
    "jpeg"
    "rle"
   The MIFF format supports "zip" (the default) and "none".
   The BMP format supports "rle" and "none" (the default). RLE compression
    will only be used if the number of colors is 256 or fewer.
   For all other formats, this option currently has no effect.

 -colortype <name>
   Deprecated. Use "-opt jpeg:colortype=<name>".

 -interlace
   Write an interlaced PNG image, or a progressive JPEG image.

 -noopt <name>
   Disable a class of image storage optimizations. This option can be used
   more than once, to disable multiple optimizations.
   This is based on the properties of the unoptimized output image, not on
   the properties of the input image.
   Optimization names:
    "grayscale": Do not reduce color images to grayscale, even if all pixels
     are gray.
    "palette": Never convert to a paletted image.
    "reduceto8": Do not reduce an image with a bit depth of 16 to one with a
     bit depth of 8, even if it can be done losslessly.
    "stripalpha": Do not strip a superfluous alpha channel, or use binary or
     palette-based transparency.
    "binarytrns": Never use binary (color-keyed) transparency.
    "all": All of the above.

 -page <n>
   Select the page to read from a multi-page file. The first page is number 1.
   Currently, this only works with GIF files. It does not play through the GIF
   animation, so you might only get a partial image.

 -opt <format>:<option-name>=<value>
   Set a format-specific option. The syntax may be slightly inconvenient, but
   it allows for a large number of options to exist, without much trouble.
   Note that you will not get any warnings if you use an unrecognized option.
   Options:
    "bmp:version=<n>": The BMP file version to write. Currently supports "2"
      (Windows BMP v2, also known as OS/2 BMP v1), and "3" (the default; the
      standard version that's compatible with almost everything), and "5"
      (supports transparency).
    "deflate:cmprlevel=<n>": zlib-style compression level setting for
      zip/deflate/zlib compression. This applies to all PNG files, and to MIFF
      files that use zip compression.
      Values range from 0 (no compression) to 9 (best, slowest).
      "-1" can be used to mean "default", but the exact meaning of this is not
      well-defined.
    "jpeg:arith": When writing a JPEG file, use arithmetic coding instead of
      Huffman coding. This reduces the file size by about 5 to 10% for free,
      but many image viewers don't support JPEG files with arithmetic coding.
    "jpeg:bgycc": Enable libjpeg's "big gamut YCC" mode. (For experimental use
      only.)
    "jpeg:block=<n>": Set the DCT block size. Valid values are 1 to 16. (For
      experimental use only.)
    "jpeg:colortype=<name>": Suggest the color type to use for the output
      image. This option is recommended for experts only.
      JPEG color types:
       "rgb": If a color image is written to a JPEG file, leave it in RGB
         format instead of converting it to YCbCr. The resulting file will
         likely be larger and less portable.
       "rgb1": Use libjpeg's "reversible color transform" feature. (For
         experimental use only.)
       "ycbcr": Convert color JPEG images to YCbCr (the default).
    "jpeg:quality=<n>": libjpeg-style quality setting to use if a JPEG file is
      written. Default is (probably) 75.
    "jpeg:sampling=<x>,<y>": The sampling factors to use if a color JPEG file
      is written. For example, 2 means the chroma channels will have 1/2 as
      many samples as the luma channel. For highest quality, use "1,1". The
      default depends on the "jpeg:quality" setting. Each factor must be
      between 1 and 4. Not all combinations are allowed.
    "webp:quality": WebP-style quality setting to use if a WebP file is
      written. This is on a scale from 0 to 100. Default is 80.

 -includescreen
 -noincludescreen
   By default, a GIF image will be painted onto the GIF "screen". Use
   -noincludescreen to extract just the individual image.

 -zipcmprlevel <n>
   Deprecated. Same as "-opt deflate:cmprlevel=<n>".

 -jpegquality <n>
   Deprecated. Same as "-opt jpeg:quality=<n>".

 -jpegsampling <x>,<y>
   Deprecated. Same as "-opt jpeg:sampling=<x>,<y>".

 -jpegarith
   Deprecated. Same as "-opt jpeg:arith".

 -bmpversion <n>
   Deprecated. Same as "-opt bmp:version=<n>".

 -bmptrns
   Attempt to write a BMP image with transparency. Refer to technical.txt for
   more information.

 -webpquality <n>
   Deprecated. Same as "-opt webp:quality=<n>".

 -encoding <encoding>
   Set the encoding used for text output (informational and error messages).
   This is usually unnecessary, because IW can usually figure out what
   encoding to use. However, it may be useful if you're capturing or
   redirecting the output, or using a nonstandard terminal program.

   Encoding names:
    "auto": The default. On Windows, write Unicode text if writing to a
         console; otherwise write UTF-8. On non-Windows systems, try to detect
         whether to use UTF-8 or US-ASCII.
    "ascii": US-ASCII
    "utf8": UTF-8
    "utf16": (Windows only.) Write Unicode text if writing to a console;
         otherwise write little-endian UTF-16.

 -noinfo
   Suppress informational messages.

 -nowarn
   Suppress warnings.

 -quiet
   Suppress informational messages and warnings.

 -version
   Display the version number of IW, and of the libraries it uses.

 -help
   Display a brief help message.

--- End ---
