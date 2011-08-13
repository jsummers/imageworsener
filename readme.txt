ImageWorsener is a raster image scaling and processing utility.
Version 0.9.4
Copyright (c) 2011 Jason Summers  <jason1@pobox.com>

Web site: http://entropymine.com/imageworsener/

All nontrivial automated image processing causes a loss of information.
While ImageWorsener will degrade your images, its goal is to degrade them as
little as possible.

===========================================================================

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA

===========================================================================

A copy of the GNU General Public License is included in the file
COPYING.txt.

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
 - The command-line utility fully supports PNG, JPEG, and WebP files, and has
   partial support for GIF, BMP, TIFF, and MIFF.
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


Documentation for the ImageWorsener C library
---------------------------------------------

There is no documentation for the library at this time, aside from bits and
pieces in the imagew.h header file. Use of the library for anything serious is
discouraged, as the API may change in incompatible ways before version 1.0 is
released.


Documentation for the "imagew" command line utility
---------------------------------------------------

Synopsis
    imagew [options] <input-file> <output-file>


Options may appear anywhere on the command line, even after the file names.
The order of the options usually does not matter.

Numbers on the command line may be specified as rational numbers, using a
slash. For example, instead of "-w x0.666666666", you can use "-w x2/3".

Options:

 -width <n>  -height <n>
 -w <n>  -h <n>
   The width and height of the output image. <n> is normally the number of
   pixels.
   If you use a prefix of "x", for example "-w x1.5", then the new size will
   instead be that number multiplied by the size of the source image.

   If neither -width nor -height are specified, the dimensions of the source
   image will be used.
   If one dimension is specified, but not the other, the image will be
   resized to the dimension that was specified, while retaining its aspect
   ratio.
   If both dimensions are specified, the result depends on whether the
   -bestfit option was used. If it was not, image will be stretched to
   the exact dimensions specified, which may change its aspect ratio.

 -bestfit
   If both -width and -height are also specified, the image will be "best fit"
   into those dimensions. If not, has no effect.

 -nobestfit
   Disables -bestfit. This is the default, so it has no effect.

 -infmt <fmt>  -outfmt <fmt>
   Specifies the image file format of the input or output file. If not used,
   imagew will try to figure out the format based on the contents of the file,
   or the file name.

   Valid values for <fmt>:
     png: PNG
     jpeg, jpg: JPEG
     webp: WebP
     gif: GIF (-infmt only)
     bmp: BMP (-outfmt only)
     tiff, tif: TIFF (-outfmt only)
     miff: MIFF (experimental; limited support)

 -depth <n>
   The general number of bits of precision used per color channel in the
   output image. Currently, the only depths supported are 8 and 16. The
   default is 8 if the source image has a depth of 8 or less; otherwise 16
   (if supported by the output format). Within this overall depth, you can
   reduce the number of colors that will actually be used, by using the
   "-cc" options.
   Note that this doesn't necessarily determine the depth used in the output
   file. If the image can be encoded at a smaller depth with no loss of
   information, IW may choose to do that.
   If you are writing to a MIFF file, then samples will be written as
   floating-point numbers, and your options for -depth are 32 and 64.

 -filter <name> (-filterx -filtery -filteralpha)
   The resizing algorithm to use. (It would be more accurate if this option
   were named "-resizealgorithm", but that's too hard to type.) Default is
   "auto".

   With -filterx and -filtery, you can use different algorithms for the
   horizontal and vertical dimensions. (This is possible because IW supports
   only "separable" resizing algorithms.) This may occasionally be useful for
   something, such as if you need to enlarge an image in one dimension while
   reducing it in the other.

   The -filteralpha option lets you use a different algorithm for the alpha
   channel.

   IW uses the filter even if the image size isn't being changed. Many
   filters will leave the image unchanged in that case, but some (such as
   bspline, mitchell, gaussian) will cause it to be blurred, at least
   slightly.

   Some filters are not particularly good for general purposes. The main
   filters you should consider using are mix, mitchell, catrom, lanczos2, and
   lanczos.

   Full list:

    nearest, point
     Nearest-neighbor resizing.

    mix
     Pixel mixing, a.k.a. area map. Not good for enlarging images, unless you
     want a pixellated effect.

    box
     Box filter.

    triangle, linear
     Linear interpolation, a.k.a. bilinear interpolation, a.k.a. triangle
     filter, a.k.a. tent filter.

    gaussian
     A Gaussian filter.

    quadratic
     Quadratic interpolation.

    cubic<B>,<C>
     Generalized cubic interpolation as defined by Mitchell-Netravali.
     The normal range of both B and C is from 0 to 1 (though IW allows you to
     exceed that range).

     Note that the term "cubic" or "bicubic" means different things to
     different applications. Sometimes it's IW's "bspline" filter, sometimes
     "mitchell", sometimes "catrom", and occasionally "cubic0,1" or something
     else.

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

    hanning<n>
     A Hanning filter. This is a windowed sinc filter, similar to Lanczos.
     n is the number of "lobes", and defaults to 4.

    sinc<n>
     An unwindowed sinc filter. n is the number of "lobes", and defaults to 4.

    auto
     The default. IW will select a filter to use. Currently uses "catrom" for
     enlarging, "mix" for reducing, and usually "null" if the size is not
     being changed.

    null
     No resizing. No need to specify this, but it's documented because it may
     be selected by the "auto" method.

 -blur <n> (-blurx -blury -bluralpha)
   Adjust the width of the resampling filter.

   This is really a parameter of the resampling filter: if you use -blur,
   you must also use -filter. "-blur" does not work with some algorithms,
   specifically "mix" and "nearest".

   The default value is 1.0. Larger values blur the image more. A value
   smaller than 1 may sharpen the image, at the expense of aliasing. It may
   also make a total mess of it.

 -edge <name>
   The strategy for dealing with the pixels near the edges of images.

   The simplest strategy is "r" (for "replicate"). When a scaling algorithm
   needs to know the value of a sample beyond the edge of the image, the color
   of the nearest sample within the image is used instead.

   The default strategy is "s" (for "standard"). Instead of inventing samples
   that are beyond the edge of the source image, give extra weight to the
   smaller-than-usual number of samples that are available. If IW decides that
   this strategy cannot reliably be used for one reason or another, it will use
   strategy "r" instead. One reason that it may not be usable is if you're
   using a channel offset, because that can shift the output samples too far
   away from any input samples.

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

 -grayscaleformula <name>
   The formula to use when converting a color image to grayscale.
   "s" = IW's standard formula (approx. 0.21R + 0.72G + 0.07B).
   "c" = "Compatibility" formula (approx. 0.30R + 0.59G + 0.11B).
    The "c" formula should generally only be used with -nogamma.

 -bkgd <color1>[,<color2>]
   Apply a background color to the transparent or partially-transparent parts
   of the image. This is the only way to remove tranparency from an image.

   The color uses an HTML-like format with 3, 6, or 12 hex digits. For
   example:
    -bkgd f00          = bright red
    -bkgd ff0000       = the same red color as above
    -bkgd ffff00000000 = the same red color as above
    -bkgd 999          = medium gray

   Background colors are always specified in the sRGB color space. They will
   be converted to whatever colorspace is used by the image.

   If you supply two colors, a checkerboard background will be used.

 -checkersize <n>
   For checkerboard backgrounds, specifies the size of the squares in pixels.
   The default is 16.

 -checkerorigin <x>,<y>
   For checkerboard backgrounds, adjust the position of the checkerboard
   background.

 -usebkgdlabel
   If the input file contains a background color label (a PNG bKGD chunk),
   and you used the -bkgd option, IW has to decide which of those background
   color to prefer. Normally, it prefers the color from the -bkgd option. But
   if you use the -usebkgdlabel option, it will prefer the color from the
   input file.

 -cc <n>  (-cccolor -ccalpha -ccred -ccgreen -ccblue -ccgray)
   Posterization. "cc" stands for "color count".
   The maximum number of different values (brightness levels, opacity levels)
   to use in each channel, including the alpha channel if present.
   The available values will be distributed as evenly as possible from among
   the possible values (based on -depth) in the output color space. An
   optimized palette is not used. If you use "-cc", consider also using
   "-dither".

   The -ccX options let you have a different setting for different channels.
   If you specify overlapping options, the most specific option will have
   priority.
   -cccolor affects all channels except the alpha channel.
   -ccalpha affects only the alpha channel. Use "-ccalpha 2" for binary
     transparency.
   -ccgray applies only if you force grayscale output, using "-grayscale".

   For example, for 16-bits-per-pixel color, use "-ccred 32 -ccgreen 64
   -ccblue 32".

 -dither <dithertype> (-dithercolor -ditheralpha -ditherred -dithergreen
                       -ditherblue -dithergray)
   Enable dithering.

   Dither types available:
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
   The colorspace to use for the output image. Note that the output image
   should *look* about the same regardless of what colorspace you choose.
   (If it doesn't, maybe your viewer doesn't support color correction.)

   <colorspace> =  linear | gamma<gamma> | srgb | srgb[prsa]

   <gamma> = The gamma value. This is an "image gamma", like 2.2 (not a "file
    gamma" like 0.4545"). "g1.0" is the same as "linear".

   "srgb": sRGB colorspace. Can optionally be followed by a letter indicating
    the "rendering intent". p=perceptual, r=relative, s=saturation, a=absolute.

   The default is to use the same colorspace as the input image, if possible.
   If the output file format does not support different colorspaces (or IW
   does not know how to handle that), then this option will have no effect.
   IW will always select a colorspace that will work with the output format.

 -inputcs <colorspace>
   Assume the input image is in the given colorspace. This is intended to be
   used in case IW cannot correctly figure out the colorspace automatically.
   Although it can be used to apply a gamma-correction operation to the image,
   that's not really what it's intended for.
   If you use this, it's a good idea to also specify the output colorspace
   using "-cs".

 -nogamma
   Disable all color correction.
   The main purpose of this option is to let you try to replicate what some
   other applications might do.
   Note that there are many *wrong* ways to handle color correction, and this
   only recreates one of them. For example, some apps might do gamma correction
   when the image is read in, but not when it is resized. That's not what
   -nogamma will do.

   If you use this with "-grayscale", you should probably also use
   "-grayscaleformula c".

   This does not affect the colorspace label that will be written to the output
   file. If a label is written, it may not be the label you want.

 -nocslabel
   Do not write a colorspace label to the output image file (if applicable).
   This does not affect the image processing.
   May be be useful for dealing with defective web browsers (I'm looking at
   *you*, Firefox) that display sRGB image colors differently than they
   display sRGB CSS/HTML colors.

 -density <density-policy>
   Control how the density label (i.e. pixels per inch) of the output image is
   calculated.

   Density policies available:
    "auto": The default. Currently, writes a density only if the image size is
       not being changed.
    "none": Do not write a density.
    "keep": Use the same density as the source image.
    "adjust": Adjust the density so that the target image is the same physical
       size as the source image.

   This is limited by the target image format's support for a density labels.
   Some formats do not support them, while others require them.

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

 -pngcmprlevel <n>
   zlib-style compression level setting for PNG files, from 0 (no compression)
   to 9 (best, slowest). Default is (probably) 6.

 -jpegquality <n>
   libjpeg-style quality setting to use if a JPEG file is written. Default is
   (probably) 75.

 -jpegsampling <x>,<y>
   The sampling factors to use if a color JPEG file is written. For example, 2
   means the chroma channels will have 1/2 as many samples as the luma
   channel. For highest quality, use "1,1". Default is (probably) "2,2".
   Each factor must be between 1 and 4. Not all combinations are allowed, for
   reasons unknown to the author of IW.

 -webpquality <n>
   WebP-style quality setting to use if a WebP file is written. This is on a
   scale from 0 to 100. Default is 80.

 -noinfo
   Suppess informational messages.

 -nowarn
   Suppess warnings.

 -quiet
   Suppress informational messages and warnings.

 -version
   Display the version number of IW, and of the libraries it uses.

 -help
   Display a brief help message.

--- End ---
