
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h> // for int32_t, int8_t, etc.
#include <malloc.h>
#include <string.h> // for memcpy
#include <assert.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/Xutil.h> // for XDestroyImage
#include <X11/extensions/Xfixes.h> // link Xfixes, for XFixesGetCursorImage
#include <X11/extensions/XShm.h> // link Xext, for XShmCreateImage



#include <sys/ipc.h> // for shmget
#include <sys/shm.h> // for shmget


#include "bmp.h"


// TODO: PNG-Encoder
// https://github.com/lvandeve/lodepng
// https://lodev.org/lodepng/
BMPImage * CreateBitmapFromScan0(uint16_t bitsPerPixel, int32_t w, int32_t h, uint8_t* scan0)
{
    BMPImage *new_image = (BMPImage *)malloc(sizeof(*new_image));
    BMPHeader *header = (BMPHeader *)malloc(sizeof(*header));

    new_image->header = *header;
    new_image->header.type = MAGIC_VALUE;
    new_image->header.bits_per_pixel = bitsPerPixel;
    new_image->header.width_px = w;
    new_image->header.height_px = h;
    new_image->header.image_size_bytes = computeImageSize(&new_image->header);
    new_image->header.size = BMP_HEADER_SIZE + new_image->header.image_size_bytes;
    new_image->header.dib_header_size = DIB_HEADER_SIZE;
    new_image->header.offset = (uint32_t) sizeof(BMPHeader);
    new_image->header.num_planes = 1;
    new_image->header.compression = 0;
    new_image->header.reserved1 = 0;
    new_image->header.reserved2 = 0;
    new_image->header.num_colors = 0;
    new_image->header.important_colors = 0;

    new_image->header.x_resolution_ppm = 3780; // image->header.x_resolution_ppm;
    new_image->header.y_resolution_ppm = 3780; // image->header.y_resolution_ppm;

    // If height_px is negative, the bitmap is a top-down DIB with the origin at the upper left corner.
    new_image->header.height_px = -h;  // upside down correction

    new_image->data = (uint8_t*)malloc(sizeof(*new_image->data) * new_image->header.image_size_bytes);
    memcpy(new_image->data, scan0, new_image->header.image_size_bytes);

    return new_image;
}


void WriteBitmapToFile(const char *filename, int bitsPerPixel, int width, int height, const void* buffer)
{
    BMPImage * image = CreateBitmapFromScan0((uint16_t) bitsPerPixel, (int32_t)width, (int32_t)height, (uint8_t*)buffer);
    char *error = NULL;
    write_image(filename, image, &error);
}


static int _XlibErrorHandler(Display *display, XErrorEvent *event)
{
    fprintf(stderr, "An error occured detecting the mouse position\n");
    return True;
}



// https://stackoverflow.com/questions/28300149/is-there-a-list-of-all-xfixes-cursor-types
// https://ffmpeg.org/doxygen/2.8/common_8h.html
#define 	FFMAX(a, b)   ((a) > (b) ? (a) : (b))
#define 	FFMIN(a, b)   ((a) > (b) ? (b) : (a))

// http://www.staroceans.org/myprojects/ffplay/libavdevice/x11grab.c
static void paint_mouse_pointer(Display *dpy, XImage *image) //, struct x11grab *s)
{
    // int x_off = s->x_off;
    // int y_off = s->y_off;
    // int width = s->width;
    // int height = s->height;
    // Display *dpy = s->dpy;

    int x_off = 0;
    int y_off = 0;
    int width = image->width;
    int height = image->height;

    XFixesCursorImage *xcim;
    int x, y;
    int line, column;
    int to_line, to_column;
    int pixstride = image->bits_per_pixel >> 3;
    /* Warning: in its insanity, xlib provides unsigned image data through a
     * char* pointer, so we have to make it uint8_t to make things not break.
     * Anyone who performs further investigation of the xlib API likely risks
     * permanent brain damage. */
    uint8_t *pix = image->data;
    Cursor c;
    Window w;
    XSetWindowAttributes attr;

    // Code doesn't currently support 16-bit or PAL8
    if (image->bits_per_pixel != 24 && image->bits_per_pixel != 32)
        return;

    // c = XCreateFontCursor(dpy, XC_left_ptr);
    // w = DefaultRootWindow(dpy);
    // attr.cursor = c;
    // XChangeWindowAttributes(dpy, w, CWCursor, &attr);

    xcim = XFixesGetCursorImage(dpy);

    x = xcim->x - xcim->xhot;
    y = xcim->y - xcim->yhot;

    to_line = FFMIN((y + xcim->height), (height + y_off));
    to_column = FFMIN((x + xcim->width), (width + x_off));

    for (line = FFMAX(y, y_off); line < to_line; line++)
    {
        for (column = FFMAX(x, x_off); column < to_column; column++)
        {
            int  xcim_addr = (line - y) * xcim->width + column - x;
            int image_addr = ((line - y_off) * width + column - x_off) * pixstride;
            int r = (uint8_t)(xcim->pixels[xcim_addr] >>  0);
            int g = (uint8_t)(xcim->pixels[xcim_addr] >>  8);
            int b = (uint8_t)(xcim->pixels[xcim_addr] >> 16);
            int a = (uint8_t)(xcim->pixels[xcim_addr] >> 24);

            if (a == 255)
            {
                pix[image_addr+0] = r;
                pix[image_addr+1] = g;
                pix[image_addr+2] = b;
            }
            else if (a)
            {
                // pixel values from XFixesGetCursorImage come premultiplied by alpha
                pix[image_addr+0] = r + (pix[image_addr+0]*(255-a) + 255/2) / 255;
                pix[image_addr+1] = g + (pix[image_addr+1]*(255-a) + 255/2) / 255;
                pix[image_addr+2] = b + (pix[image_addr+2]*(255-a) + 255/2) / 255;
            }
        }
    }

    XFree(xcim);
    xcim = NULL;
}


void ScreenshotByXGetImage(Display *display)
{
    // https://stackoverflow.com/questions/24988164/c-fast-screenshots-in-linux-for-use-with-opencv
    Window root = DefaultRootWindow(display); // window: unsigned long
    XWindowAttributes window_attributes = {0};
    XGetWindowAttributes(display, root, &window_attributes);
    XImage* img = XGetImage(display, root, 0, 0 , window_attributes.width, window_attributes.height, AllPlanes, ZPixmap);
    int bitsPerPixel = img->bits_per_pixel;
    printf("Bits per Pixel: %d\n", bitsPerPixel);

    paint_mouse_pointer(display, img);

    // BMPImage * foo = CreateBitmapFromScan0(uint16_t bitsPerPixel, int32_t w, int32_t h, uint8_t* scan0);
    const char *filename = "/tmp/lol.bmp";
    WriteBitmapToFile(filename, bitsPerPixel, (int)window_attributes.width, (int)window_attributes.height, (const void*) img->data);

    unsigned char *pixels = malloc(window_attributes.width * window_attributes.height * 4);
    XDestroyImage(img);
    free(pixels);
}


void ScreenshotBySharedMemory(Display *display)
{
    Window root = DefaultRootWindow(display); // window: unsigned long
    XWindowAttributes window_attributes = {0};
    XGetWindowAttributes(display, root, &window_attributes);

    Screen *screen = window_attributes.screen; // struct screen
    XShmSegmentInfo shminfo;

    XImage* ximg = XShmCreateImage(display, DefaultVisualOfScreen(screen), DefaultDepthOfScreen(screen), ZPixmap, NULL, &shminfo, window_attributes.width, window_attributes.height);
    shminfo.shmid = shmget(IPC_PRIVATE, ximg->bytes_per_line * ximg->height, IPC_CREAT|0777);
    shminfo.shmaddr = ximg->data = (char*)shmat(shminfo.shmid, 0, 0);
    shminfo.readOnly = False;
    if(shminfo.shmid < 0)
        puts("Fatal shminfo error!");;
    Status s1 = XShmAttach(display, &shminfo);
    printf("XShmAttach() %s\n", s1 ? "success!" : "failure!");

    //#define AllPlanes 		((unsigned long)~0L)
    // XShmGetImage(display, root, ximg, 0, 0, 0x00ffffff);
    // #define Bool int
    Bool res = XShmGetImage(display, root, ximg, 0, 0, AllPlanes);

    paint_mouse_pointer(display, ximg);


    const char *filename = "/tmp/test.bmp";
    WriteBitmapToFile(filename, (int) ximg->bits_per_pixel, (int)window_attributes.width, (int)window_attributes.height, (const void*) ximg->data);

    XDestroyImage(ximg);
    XShmDetach(display, &shminfo);
    shmdt(shminfo.shmaddr);
}


int GetAllScreenMouse(Display *display)
{
    int number_of_screens = XScreenCount(display); // http://www.polarhome.com/service/man/?qf=XScreenCount&tf=2&of=HP-UX&sf=
    // https://tronche.com/gui/x/xlib/display/display-macros.html#DefaultScreen
    fprintf(stderr, "There are %d screens available in this X session\n", number_of_screens);

    Window *root_windows = malloc(sizeof(Window) * number_of_screens);


    for (int i = 0; i < number_of_screens; i++)
    {
        root_windows[i] = XRootWindow(display, i);
    }


    Bool result; // #define Bool int
    Window window_returned;
    int root_x, root_y;
    int win_x, win_y;
    unsigned int mask_return;

    for (int i = 0; i < number_of_screens; i++)
    {
        result = XQueryPointer(display, root_windows[i], &window_returned,
                               &window_returned, &root_x, &root_y, &win_x, &win_y,
                               &mask_return);
        if (result == True)
        {
            break;
        }
    }

    if (result != True)
    {
        fprintf(stderr, "No mouse found.\n");
        return -1;
    }

    printf("Mouse is at (%d,%d)\n", root_x, root_y);

    free(root_windows);
    return 0;
}


// https://stackoverflow.com/questions/22891351/structure-of-xfixescursorimage
// Notes: img in the code is the XFixesCursorImage, and do not trust the field 'cursor_serial'
// in order to determine if cursors are different of each other,
// because sometimes this field is just 0. Not sure why.
void GetXFixesCursorImageData(XFixesCursorImage* img)
{
    unsigned char r,g,b,a;
    unsigned short row,col,pos;


    const char *filename = "/tmp/cursor.bmp";

    printf("Cw; %d, Ch: %d\n", img->width, img->height);
    printf("Hw; %d, Hz: %d\n", img->xhot, img->yhot);
    WriteBitmapToFile(filename, (int) 32, (int)img->width, (int)img->height, (const void*) img->pixels);
    return;

    for(pos = row = 0;row<img->height; row++)
    {
        for(col=0;col < img->width;col++,pos++)
        {
            a = (unsigned char)((img->pixels[pos] >> 24) & 0xff);
            r = (unsigned char)((img->pixels[pos] >> 16) & 0xff);
            g = (unsigned char)((img->pixels[pos] >>  8) & 0xff);
            b = (unsigned char)((img->pixels[pos] >>  0) & 0xff);

            // if(a == 0)
            if(r == 0 && g  == 0 && b  == 0 && a == 0)
                continue;

            // put_pixel_in_ximage(img->x+col,img->y+row, convert_to_ximage_pixel(r,g,b,a));
            fprintf(stdout, "Pixel[%d, %d] = rgba(%d,%d,%d,%d);\n", col, row, r, g, b, a);
        }
    }

}


void PrintCursorInfo(Display *display)
{
    // nm -D /usr/lib/x86_64-linux-gnu/libXfixes.so.3
    // nm -D /usr/lib/x86_64-linux-gnu/libXfixes.so.3 | grep "XFixesGetCursorImage"
    XFixesCursorImage *cursor = XFixesGetCursorImage(display); // > /usr/lib/x86_64-linux-gnu/libXfixes.so.3 (0x00007fcf0a98e000)
    fprintf(stdout, "Cursor Name: %s\n", cursor->name);
    fprintf(stdout, "Cursor Coordinates: (%d, %d)\n", cursor->x, cursor->y);
    fprintf(stdout, "Cursor Size: (%d, %d)\n", cursor->width, cursor->height);
    fprintf(stdout, "Cursor Serial: %ld\n", cursor->cursor_serial);
    GetXFixesCursorImageData(cursor);


    /*
    locate libXfixes.so | sed '/flatpak/d;/snap/d;'
    /usr/lib/i386-linux-gnu/libXfixes.so.3
    /usr/lib/i386-linux-gnu/libXfixes.so.3.1.0
    /usr/lib/x86_64-linux-gnu/libXfixes.so
    /usr/lib/x86_64-linux-gnu/libXfixes.so.3
    /usr/lib/x86_64-linux-gnu/libXfixes.so.3.1.0
    */

}





// gcc pointerposition.c -o pointerposition -lX11 -lXext -lXfixes
// gcc fbserver.c -lX11 -lXext -lXdamage -lXfixes -lXtst
// echo "#include <sys/socket.h>" | gcc -E -dM -
// echo "#include <X11/Xlib.h>" | gcc -E -dM -
// https://stackoverflow.com/questions/2224334/gcc-dump-preprocessor-defines
int main(int argc, char* argv[])
{
    XSetErrorHandler(_XlibErrorHandler);

    printf("Bitness: %d-Bit\n", sizeof(void*)*8);
    printf("size_t-Size in Bytes: %d\n", sizeof(size_t)); // long
    printf("unsigned short-Size in Bytes: %d\n", sizeof(unsigned short)); // long


    Display *display = XOpenDisplay(NULL);
    assert(display);

    GetAllScreenMouse(display);
    ScreenshotByXGetImage(display);
    ScreenshotBySharedMemory(display);
    PrintCursorInfo(display);

    XCloseDisplay(display);

    printf("Finished\n");
    return EXIT_SUCCESS;
}
