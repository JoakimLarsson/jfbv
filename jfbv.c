/*
* jfbv - a simple jpeg viewer
*
* Author: Joakim Larsson, joakim@bildrulle.nu
* Version: 1.0b
* Copyright (c) 2007, Joakim Larsson
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in the
*       documentation and/or other materials provided with the distribution.
*     * Neither the name of the originating download site nor the
*       names of its contributors may be used to endorse or promote products
*       derived from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY <copyright holder> ``AS IS'' AND ANY
* EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL <copyright holder> BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
 * jfbv is a commandline jpeg viewer for a Linux framebuffer device. It is developed
 * as an excersize in libjpeg and image transformations in general and will only be 
 * useful as a helper app when displaying a jpeg image on a framebuffer is needed.
 *
 * FEATURES
 * ========
 * - centered rotating in 90 degrees steps
 * - centered positioning for images smaller than framebuffer boundaries
 * - clipping of unscaled images exceeding frambuffer boundaries
 * - scale to fit
 * - panoration of unscaled images
 * - stdin as input using '-' as filename
 * - crude alpha blending 
 *
 * LIMITATIONS
 * ===========
 * - only 32 bit RGBa or 16 bit RGB565 framebuffers
 * - only first framebuffer device /dev/fb0
 * - no upscaling
 * - down scaling from fit or below original size doesn't work
 *
 * COMPILATION
 * ===========
 * gcc -o jfbv jfbv.c -ljpeg
 *
 */

/*
 * USAGE:
 *
 * NOTE: Requires a framebuffer. To turn it on in Ubuntu pass a kernel argument like vga=792 at
 * boot time. Gnome will still trash the image unless you use it from console (ie rescue mode).
 *
 * jfbv <filename> [<rot>] [<scale>] [<xpan>] [<ypan>] [<mix>]
 *
 * <rot> = 
 * 0 -  no rotation (default)
 * 1 -  90 degree rotation
 * 2 - 180 degree rotation
 * 3 - 270 degree rotation
 *
 * <scale> = 
 * 0 - best effort fit framebuffer (default)
 * 1 - 1:1 (no scaling)
 *
 * <xpan> =
 * 0  - centered x position
 * >=1 - pixel offset to the left side
 *
 * <ypan> =
 * 0  - centered x position
 * >=1 - pixel offset to the top
 *
 * <mix> =
 * 0     - whipe framebuffer before blit of bitmap
 * 1     - opaque (non transparent) blit of bitmap to framebuffer
 * 2-255 - alpha value to use for alpha blending mix to frambuffer
 *
 * TODO:
 *============
 * - Usage printout
 *
 * CONTRIBUTORS
 *=============
 * Michael Huber - rotate270() function
 * http://www.daniweb.com/software-development/c/code/216791/alpha-blend-algorithm - alpha blending algorithm
 *
 */

#include <linux/fb.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <jpeglib.h>
#include <setjmp.h>

/* prototypes */
static unsigned int alphamix(const unsigned int bg, const unsigned int src, unsigned int a);
void rotate270(char *dp, char *sp, int xres, int yres, int c);
void rotate90(char *dp, char *sp, int xres, int yres, int c);
void jpeg_cb_error_exit(j_common_ptr cinfo) __attribute__ ((noreturn));

/* 
 * Standard callbacks for libjpeg 
 */
struct r_jpeg_error_mgr
{
    struct jpeg_error_mgr pub;
    jmp_buf envbuffer;
};

void jpeg_cb_error_exit(j_common_ptr cinfo)
{
    struct r_jpeg_error_mgr *mptr;
    mptr=(struct r_jpeg_error_mgr*) cinfo->err;
    (*cinfo->err->output_message) (cinfo);
    longjmp(mptr->envbuffer,1);
}

/*
 * Rotation routines 
 */
void rotate270(char *dp, char *sp, int xres, int yres, int c)
{
    int x,y;

    printf("Rotating 270 degrees\n");

    for(y = 0; y < xres ; y++)
        for(x = 0; x < yres*c ; x+=c)
            memcpy(dp + x + y*yres*c ,sp + (xres - 1 - y)*c + x * xres  ,c);
}

void rotate90(char *dp, char *sp, int xres, int yres, int c)
{
  int x, y, z, pos, tmp;

  printf("Rotating 90 degrees\n");

  pos = 0;
  for (x = 0; x < xres; x++){
    for (y = yres - 1; y >= 0; y--){
      tmp = (x + y * xres) * c;
      for (z = 0; z < c; z++){
	dp[pos++] = sp[tmp + z];  
      }
    }
  }
}

/* alpha blend routine */
inline static unsigned int alphamix(const unsigned int bg, const unsigned int src, unsigned int a)
{
  /* alpha blending the source and background colors */
  unsigned int rb = (((src & 0x00ff00ff) * a) +
		     ((bg & 0x00ff00ff) * (0xff - a))) & 0xff00ff00;
  unsigned int g = (((src & 0x0000ff00) * a) +
		    ((bg & 0x0000ff00) * (0xff - a))) & 0x00ff0000;
       
  return (src & 0xff000000) | ((rb | g) >> 8);
}

/*
 * Main function...
 */
int main(int argc, char **argv)
{
        FILE *ss;                   /* input file */
        int  fbd;                   /* framebuffer descriptor */
        void *fbm;                  /* framebuffer mmap pointer */
        int rotate,                 /* parsed commandline arguments */
            scaling, 
            xpan, 
            ypan;
        char *buffer,               /* buffer pointers */
             *workbuf;
        unsigned char *bp, *bp1, *bp2;
        unsigned int  last_scanline,   /* previous used scanline after scaling */
                      scanline_offset, /* first pixel to be copied to framebuffer of a scanline */
                      start_scanline;  /* first scanline to be copied to framebuffer */

        unsigned int  sz;               /* boundary check for buffers */
        unsigned int  i, j;             /* Loop variables */
        unsigned int  fb_bitmap_width,  /* width of the final rotated bitmap */
	              fb_bitmap_height, /* height of the final rotated bitmap*/
                      bitmap_width,	/* width of the scaled unrotated decoded bitmap */ 
                      bitmap_height,    /* height of the scaled unrotated decoded bitmap */ 
                      c;                /* number of bytes in decoded bitmap */
        unsigned int ox, oy,            /* offset inside framebuffer to target area for bitmap */
                     image_width,       /* original image width */
                     image_height,      /* original image height  */
                     fb_maxx,           /* framebuffer width  */
	             fb_maxy,           /* framebuffer height */
	             fb_bits,           /* framebuffer depth  */
	             fb_bytes;          /* pixel storage size  */
        int          fb0;               /* framebuffer decriptor */
        int          clr;               /* input argument weather to clear framebuffer */
        struct fb_var_screeninfo fb_info; /* info structure for framebuffer */

        float	       scale1, scale2, scale; /* Scale factors */

        /* Libjpeg stuff */
        JSAMPLE *lb;                          /* line buffer */
        struct jpeg_decompress_struct cinfo;  /* jpeg image info structure */
        struct jpeg_decompress_struct *ciptr; /* pointer to info structure */
        struct r_jpeg_error_mgr emgr;         /* jpeg decoder error manager structure */

	// Set up error handlers
	ciptr = &cinfo;
	ciptr->err = jpeg_std_error(&emgr.pub);
	emgr.pub.error_exit = jpeg_cb_error_exit;

	/* Set up decompressor object */
	jpeg_create_decompress(ciptr);
	  
	if(setjmp(emgr.envbuffer) == 1){

	  // FATAL ERROR - Free the object and return...
	  printf("Problems while setting up decoder.... exiting\n");
	  jpeg_destroy_decompress(ciptr);
	  return 1;
	}

	if (argc > 1){
	  if (strncmp(argv[1], "-", 1) == 0){
	    printf("using stdin\n");
	    ss = stdin;
	  }
	  else {
	    if ( (ss = fopen(argv[1], "rb")) != NULL ){
	      struct stat fstat;
	      if (stat(argv[1], &fstat) == 0 && S_ISREG(fstat.st_mode)){
		printf("Opens %s\n", argv[1]);
	      }
	      else{
		printf("File %s is not a regular file\n", argv[1]);
		exit(1);
	      }
	    }
	    else {
	      printf("Can't open file %s\n", argv[1]);
	      exit(1);
	    }
	  }
	  rotate   = argc > 2 ? atoi(argv[2]) : 0; /* 0-3 to rotate 0, 90, 180 and 270 degrees */
	  scaling  = argc > 3 ? atoi(argv[3]) : 0; /* 0 = best fit, 1 = no scale               */
	  xpan     = argc > 4 ? atoi(argv[4]) : 0; /* X panoration of bitmap relative origo    */
	  ypan     = argc > 5 ? atoi(argv[5]) : 0; /* Y panoration of bitmap relative origo    */
	  clr      = argc > 6 ? atoi(argv[6]) : 0; /* Clear screen or not                      */
	}
	else {
	  printf("Invocation error\n");
	  exit(1);
	}

	/* Setup  input manager */
	jpeg_stdio_src(ciptr, ss);

	/* prepare and start the decompressor */
	jpeg_read_header(ciptr,TRUE);
	ciptr->out_color_space = JCS_RGB;

	/* 
	 * Get framebuffer resolution 
	 *    fb_maxx - max X resolution of framebuffer
	 *    fb_maxy - max Y resolution of framebuffer
	 */
	if ((fb0 = open("/dev/fb0", O_RDWR)) == -1){
	  printf("Can't open framebuffer\n");
	  exit(1);
	}
	if (ioctl(fb0, FBIOGET_VSCREENINFO, &fb_info) != 0){
	  printf("Can't get resolution\n");
	  exit(1);
	}
	fb_maxx = fb_info.xres;
	fb_maxy = fb_info.yres;
	fb_bits = fb_info.bits_per_pixel;
	fb_bytes = ((fb_bits == 32 || fb_bits == 24) ? 4 : (fb_bits == 16 ? 2 : 1));
	printf("Red %d %d %d\n",   fb_info.red.offset, fb_info.red.length, fb_info.red.msb_right);
	printf("Green %d %d %d\n", fb_info.green.offset, fb_info.green.length, fb_info.green.msb_right);
	printf("Blue %d %d %d\n",  fb_info.blue.offset, fb_info.blue.length, fb_info.blue.msb_right);
	close(fb0);

	/* 
	 * Get image resolution
	 *    image_width  - width of original image
	 *    image_height - height of original image
	 *    c            - number of components, normally 3 bytes per pixel
	 */
	ciptr->scale_denom = 1;
	jpeg_calc_output_dimensions(ciptr);
	image_width  = ciptr->output_width;
	image_height = ciptr->output_height;
	c  = ciptr->output_components;

	/* 
	 * Determine bitmap scale, resolution and offsets
         *    bitmap_width  - width of decoded/rotated image fragment to be displayed
         *    bitmap_height - height of decoded/rotated image fragment to be displayed
         *    scanline_offset - start offset on each decoded image pixel row copied to pixmap 
         *    start_scanline  - start decoded image pixel row copied to pixmap 
         */
	scale = 1;
	switch(rotate){
	case 1:
	case 3:  /* 90 or 270 degrees */

	  /* Setup scale to fit screen if required */
	  if (scaling == 0){
	    scale1 = image_width  / (float) fb_maxy;
	    scale2 = image_height / (float) fb_maxx;
	    scale  = scale1 > scale2 ? scale1 : scale2;

	    /* We don't support upscale */
	    if (scale < 1.0){
	      scale = 1.0;
	    }
	  }
	  else{
	    scale = 1.0;
	  }

          /* scaled image dimensions  */
	  bitmap_width   = (float) image_width / scale;
	  bitmap_height  = (float) image_height / scale;

	  /* is the scaled rotated image narrower than the framebuffer? */
	  if (bitmap_height < fb_maxx){
	    ox = (fb_maxx - bitmap_height) / 2;                  /* So let us center it       */
	    start_scanline = 0;                                  /* And we need all lines     */
	    xpan = abs(xpan) > ox ? 
	      (int) ox * (xpan / abs(xpan)) : xpan;              /* Adjust panoration         */
	    ox += xpan;                                          /* Panorate X                */
	  }
	  else{
	    ox = 0;                                    /* nope, the width will be filled out   */
	    start_scanline = (bitmap_height - fb_maxx) / 2;    /* exclude excessive scanlines  */
	    xpan = abs(xpan) > start_scanline ? 
	      (int) start_scanline * (xpan / abs(xpan)) : xpan;  /* Adjust panoration          */
	    start_scanline += xpan;                              /* Panorate X                 */
	    bitmap_height = fb_maxx;                             /* Clip bitmap to framebuffer */
	  }

	  /* is the scaled rotated image lower than the framebuffer? */
	  if (bitmap_width < fb_maxy){
	    oy = (fb_maxy - bitmap_width) / 2;                   /* So let us center it        */
	    scanline_offset = 0;                                 /* And we will use all pixels */
	    ypan = abs(ypan) > oy ? 
	      (int) oy * (ypan / abs(ypan)) : ypan;              /* Adjust panoration */
	    oy += ypan;                                          /* Panorate Y */
	  }
	  else{
	    oy = 0;                                     /* nope, the height will be filled out */
	    scanline_offset =  (bitmap_width - fb_maxy) / 2;     /* discard excessive pixels   */
	    ypan = abs(ypan) > scanline_offset ? 
	      (int) scanline_offset * (ypan / abs(ypan)) : ypan; /* Adjust panoration          */
	    scanline_offset += ypan;                             /* Panorate Y                 */
	    bitmap_width = fb_maxy;                              /* Clip bitmap to framebuffer */
	  }
	  break;
	case 0: 
	case 2: /* 0 or 180 degrees */

	  /* Setup scale to fit screen if required */
	  if (scaling == 0){
	    scale1 = image_width  / (float) fb_maxx;
	    scale2 = image_height / (float) fb_maxy;
	    scale  = scale1 > scale2 ? scale1 : scale2;

	    /* We don't support upscale */
	    if (scale < 1.0){
	      scale = 1.0;
	    }
	  }
	  else{
	    scale = 1.0;
	  }

          /* scaled image dimensions  */
	  bitmap_width   = (float) image_width  / scale;
	  bitmap_height  = (float) image_height / scale;

	  /* is the scaled image narrower than the framebuffer? */
	  if (bitmap_width < fb_maxx){
	    ox = (fb_maxx - bitmap_width) / 2;                  /* So let us center it         */
	    scanline_offset = 0;                                /* and we need all pixels      */
	    xpan = abs(xpan) > ox ? 
	      (int) ox * (xpan / abs(xpan)) : xpan;             /* Adjust panoration           */
	    ox += xpan;                                         /* Panorate X                  */
	  }
	  else{
	    ox = 0;                                      /* nope, the width will be filled out */
	    scanline_offset = (bitmap_width - fb_maxx) / 2;      /* avoid excessive pixels     */
	    xpan = abs(xpan) > scanline_offset ? 
	      (int) scanline_offset * (xpan / abs(xpan)) : xpan; /* Adjust panoration          */
	    bitmap_width = fb_maxx;                              /* Clip bitmap to framebuffer */
	    scanline_offset += xpan;                             /* Panorate X                 */
	  }

	  /* is the scaled image lower than the framebuffer? */
	  if (bitmap_height < fb_maxy){
	    oy = (fb_maxy - bitmap_height) / 2;                  /* So let us center it       */
	    start_scanline = 0;                                  /* and we need all lines     */
	    ypan = abs(ypan) > oy ? 
	      (int) oy * (ypan / abs(ypan)) : ypan;              /* Adjust panoration         */
	    oy += ypan;                                          /* Panorate Y                */
	  }
	  else{
	    oy = 0;                                     /* nope, the height will be filled out */
	    start_scanline = (bitmap_height - fb_maxy) / 2;      /* skip uneeded scanlines     */
	    ypan = abs(ypan) > start_scanline ? 
	      (int) start_scanline * (ypan / abs(ypan)) : ypan;  /* Adjust panoration          */
	    start_scanline += ypan;                              /* Panorate Y                 */
	    bitmap_height = fb_maxy;                             /* Clip bitmap to framebuffer */
	  }
	  break;
	default:
	  printf ("Unknown rotation, exiting...\n");
	  exit(1);
	}

	/* Debug outputs */
	printf("Image width and height      : %dx%dx%d\n", image_width,   image_height, c);
	printf("Fb width, height and depth  : %dx%dx%d(%d)\n", fb_maxx, fb_maxy, fb_bits, fb_bytes);
	printf("Centering offset            : %dx%d\n", ox, oy);
	printf("Panoration                  : %dx%d\n", xpan, ypan);
	printf("Bitmap width and height     : %dx%d\n", bitmap_width, bitmap_height);
	printf("Will create %d from %d pixels from offset %d from each line starting at line %d\n", 
	       bitmap_width, image_width, scanline_offset, start_scanline);
	printf("Scale: %02f\n", scale);

	/* 
	 * Allocate buffers
	 *    lb      - scanline buffer to get one pixel row at a time from libjpeg in
	 *    buffer  - buffer to hold bitmap
	 *    workbuf - buffer to hold rotated bitmap
	 *    bp      - pointer to active bitmap buffer
	 */
	lb      = (JSAMPLE *)(*ciptr->mem->alloc_small)((j_common_ptr) ciptr, JPOOL_IMAGE, c*image_width);
	buffer  = (JSAMPLE *)(*ciptr->mem->alloc_small)((j_common_ptr) ciptr, JPOOL_IMAGE, fb_bytes * bitmap_width * bitmap_height);
	workbuf = (JSAMPLE *)(*ciptr->mem->alloc_small)((j_common_ptr) ciptr, JPOOL_IMAGE, fb_bytes * bitmap_width * bitmap_height);
	bp      = buffer;

	/* 
	 * Decode JPEG image line by line
	 */

	/* Initiate libjpeg */
	jpeg_start_decompress(ciptr);

	sz = 0;
	last_scanline = 0;

	/* As long as we got scanlines and buffer left (An in case of bugs check) */
	while (ciptr->output_scanline < image_height && sz <= (fb_bytes * bitmap_width * bitmap_height)){

	  /* Get a scanline */
	  jpeg_read_scanlines(ciptr, &lb, 1);

	  /* Do we have a displayable scanline? */
	  if (ciptr->output_scanline > start_scanline){

	    /* Are we still within the bitmap area? */
	    if (ciptr->output_scanline - start_scanline < bitmap_height * scale ){

	      /* Is this scanline already displayed  */
	      if ((unsigned int)((float)ciptr->output_scanline / scale) != last_scanline){

		last_scanline = (int)((float)ciptr->output_scanline / scale);

		/* Convert scanline from Jpeg BGR to framebuffer RGBa */
		switch(fb_bytes){
		case 4: /* 32 bit framebuffer */
		  for (i = 0; i < bitmap_width; i++){
		    bp[sz + i * 4 + 0] = lb[(int)((int)((float) i * scale) + scanline_offset) * c + 2];
		    bp[sz + i * 4 + 1] = lb[(int)((int)((float) i * scale) + scanline_offset) * c + 1];
		    bp[sz + i * 4 + 2] = lb[(int)((int)((float) i * scale) + scanline_offset) * c];
		    bp[sz + i * 4 + 3] = 0xff; 
		  }
		  break;
		case 2: /* 16 bit framebuffer, assuming RGB565 format (Raspberry Pi)*/
		  for (i = 0; i < bitmap_width; i++){
		    unsigned char Red = lb[(int)((int)((float) i * scale) + scanline_offset) * c ];
		    unsigned char Green = lb[(int)((int)((float) i * scale) + scanline_offset) * c  + 1];
		    unsigned char Blue = lb[(int)((int)((float) i * scale) + scanline_offset) * c  + 2];

		    bp[sz + i * 2 + 0] = ((Green << 3) & 0xe0) | ((Blue >> 3) & 0x1f);
		    bp[sz + i * 2 + 1] = (Red & 0xf8) | ((Green >> 5) & 0x07);
		  }
		  break;
		}

		sz += bitmap_width * fb_bytes;
	      }
	    }
	  }
	}
	
	/* 
	 * Rotate if neccesarry 
	 *   bp will point to a buffer with the rotated bitmap to copy 
	 */
	bp = buffer;
	fb_bitmap_width  = bitmap_width;
	fb_bitmap_height = bitmap_height;
	if (rotate){
	  bp1 = bp;
	  bp2 = workbuf;
	  /* memcpy(bp2, bp1, sizeof(fb_maxx * fb_maxy * fb_bytes)); */ // Not needed!? 
	  switch(rotate){
	  case 1: /* 90 degrees */
	    rotate90(bp2, bp1, bitmap_width, bitmap_height, fb_bytes);
	    fb_bitmap_width  = bitmap_height;
	    fb_bitmap_height = bitmap_width;
	    bp = bp2;
	    break;
	  case 2: /* 180 degrees */
	    rotate90(bp2, bp1, bitmap_width,  bitmap_height, fb_bytes);
	    rotate90(bp1, bp2, bitmap_height, bitmap_width, fb_bytes);
	    bp = bp1;
	    break;
	  case 3: /* 270 degrees */
	    rotate270(bp2, bp1, bitmap_width, bitmap_height, fb_bytes);
	    fb_bitmap_width  = bitmap_height;
	    fb_bitmap_height = bitmap_width;
	    bp = bp2;
	    break;
	  default:
	    printf("Unknown rotation\n");
	    break;
	  }
	}
	
        /*
	 * Copy the decoded bitmap centered to the framebuffer
	 */

	/* Open and memory map framebuffer */
	if ( (fbd = open("/dev/fb0", O_RDWR)) < 0){
	  perror("Can't open framebuffer");
	  exit(1);
	}
	if ( (fbm = mmap(NULL, fb_maxx * fb_maxy * fb_bytes, PROT_WRITE | PROT_READ, MAP_SHARED, fbd, 0)) == MAP_FAILED){
	  perror("Can't map framebuffer");
	  exit(1);	  
	}

        /* blacken display */
	if (clr == 0){
	  memset(fbm, 0, fb_maxx * fb_maxy * fb_bytes);
	}

	if (clr == 0 || clr == 1){
	  /* copy buffer to fb */
	  for (i = 0; i < fb_bitmap_height; i++){
	    memcpy( (void *)((unsigned long)fbm + fb_bytes * (fb_maxx * (i + oy) + ox)), 
		    bp + i * fb_bitmap_width * fb_bytes, 
		    fb_bitmap_width * fb_bytes);
	  }
	}
	else if (clr >= 2 && clr <= 255){
	  /* alpha mix buffer to fb */
	  for (i = 0; i < fb_bitmap_height - 2; i++){
	    for (j = 0; j < fb_bitmap_width; j++){
	      *(unsigned int *)((unsigned long)fbm + fb_bytes * (fb_maxx * (i + oy) + ox + j)) =
		alphamix( *(const unsigned int *)((unsigned long)fbm + 
						    fb_bytes * (fb_maxx * (i + oy) + ox + j)),
			    *(const unsigned int *)((unsigned long)bp + 
						    fb_bytes * (i * fb_bitmap_width + j)), clr);
	    }
	  }
	}

        /* clean up */
	munmap(fbm, fb_maxy * fb_maxx * fb_bytes);
	close((unsigned int) fbd);

	jpeg_finish_decompress(ciptr);
	jpeg_destroy_decompress(ciptr);
	
	fclose(ss);

	/* Be happy */
	exit(0);
}
