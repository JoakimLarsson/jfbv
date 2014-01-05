jfbv
====

 jfbv - a simple jpeg viewer

 Author: Joakim Larsson, joakim@bildrulle.nu
 Version: 1.0b
 Copyright (c) 2007, Joakim Larsson
 Licence: See source code

 jfbv is a commandline jpeg viewer for a Linux framebuffer device. It is developed
 as an excersize in libjpeg and image transformations in general and will only be 
 useful as a helper app when displaying a jpeg image on a framebuffer is needed.

 FEATURES
 ========
 - centered rotating in 90 degrees steps
 - centered positioning for images smaller than framebuffer boundaries
 - clipping of unscaled images exceeding frambuffer boundaries
 - scale to fit
 - panoration of unscaled images
 - stdin as input using '-' as filename
 - crude alpha blending 
 
 LIMITATIONS
 ===========
 - only 32 bit RGBa or 16 bit RGB565 framebuffers
 - only first framebuffer device /dev/fb0
 - no upscaling or downscale below "fit scale"
 
 COMPILATION
 ===========
 gcc -o jfbv main.c -ljpeg
 

 USAGE
 =====
 NOTE: Requires a framebuffer. To turn it on in Ubuntu pass a kernel argument like 
 vga=792 at boot time. Gnome will still trash the image unless you use it from 
 console (ie rescue mode).

   jfbv <filename> [<rot>] [<scale>] [<xpan>] [<ypan>] [<mix>]

   <rot> = 
   0 -  no rotation (default)
   1 -  90 degree rotation
   2 - 180 degree rotation
   3 - 270 degree rotation
 
   <scale> = 
   0 - best effort fit framebuffer (default)
   1 - 1:1 (no scaling)
   
   <xpan> =
   0  - centered x position
   >=1 - pixel offset to the left side
   
   <ypan> =
   0  - centered x position
   >=1 - pixel offset to the top
   
   <mix> =
   0     - whipe framebuffer before blit of bitmap
   1     - opaque (non transparent) blit of bitmap to framebuffer
   2-255 - alpha value to use for alpha blending mix to frambuffer
   
 TODO
======
   - Usage printout

CONTRIBUTORS
============
   Michael Huber 
     - rotate270() function
   http://www.daniweb.com/software-development/c/code/216791/alpha-blend-algorithm 
     - alpha blending algorithm

