/*
 * minix_xfm_v2.c
 * Simple X11 file manager for Minix 3.4.0 - Version 2.0
 * Added selection highlighting and keyboard navigation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#define WINDOW_W 600
#define WINDOW_H 400
#define LINE_HEIGHT 16
#define MARGIN 5

typedef struct Entry {
    char name[256];
    int is_dir;
} Entry;

static Display *dpy;
static Window win;
static GC gc;
static XFontStruct *fontinfo;
static unsigned long black_pixel, white_pixel;

static Entry entries[1000];
static int nentries = 0;
static int selected = 0;
static char cwd[1024];

static void read_dir(const char *path)
{
    DIR *d;
    struct dirent *de;
    struct stat st;
    char full[1024];

    d = opendir(path);
    if (!d) {
        perror("opendir");
        return;
    }

    nentries = 0;
    
    /* Add .. for navigation */
    if (strcmp(path, "/") != 0) {
        strcpy(entries[nentries].name, "..");
        entries[nentries].is_dir = 1;
        nentries++;
    }

    while ((de = readdir(d)) != NULL && nentries < 1000) {
        if (strcmp(de->d_name, ".") == 0) continue;
        
        strncpy(entries[nentries].name, de->d_name, 255);
        
        if (path[strlen(path)-1] == '/')
            sprintf(full, "%s%s", path, de->d_name);
        else
            sprintf(full, "%s/%s", path, de->d_name);

        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode))
            entries[nentries].is_dir = 1;
        else
            entries[nentries].is_dir = 0;

        nentries++;
    }
    closedir(d);
    
    selected = 0; /* Reset selection */
}

static void draw_list(void)
{
    int i;
    char display[300];
    int y;

    /* Clear with white */
    XSetForeground(dpy, gc, white_pixel);
    XFillRectangle(dpy, win, gc, 0, 0, WINDOW_W, WINDOW_H);
    XSetForeground(dpy, gc, black_pixel);

    for (i = 0; i < nentries; i++) {
        y = MARGIN + i * LINE_HEIGHT;
        
        /* Draw selection highlight */
        if (i == selected) {
            XSetForeground(dpy, gc, 0xCCCCCC);
            XFillRectangle(dpy, win, gc, 0, y, WINDOW_W, LINE_HEIGHT);
            XSetForeground(dpy, gc, black_pixel);
        }
        
        if (entries[i].is_dir)
            sprintf(display, "[DIR] %s", entries[i].name);
        else
            sprintf(display, "      %s", entries[i].name);
            
        XDrawString(dpy, win, gc, MARGIN, y + fontinfo->ascent, 
                   display, strlen(display));
    }
    
    /* Show current directory */
    XDrawString(dpy, win, gc, MARGIN, WINDOW_H - MARGIN, cwd, strlen(cwd));
}

static void open_entry(int idx)
{
    char newpath[1024];
    char *p;

    if (idx < 0 || idx >= nentries) return;

    if (entries[idx].is_dir) {
        if (strcmp(entries[idx].name, "..") == 0) {
            p = strrchr(cwd, '/');
            if (!p || p == cwd)
                strcpy(cwd, "/");
            else
                *p = '\0';
        } else {
            if (strcmp(cwd, "/") == 0)
                sprintf(newpath, "/%s", entries[idx].name);
            else
                sprintf(newpath, "%s/%s", cwd, entries[idx].name);
            strcpy(cwd, newpath);
        }
        read_dir(cwd);
        draw_list();
    } else {
        int pid = fork();
        if (pid == 0) {
            char filepath[1024];
            if (strcmp(cwd, "/") == 0)
                sprintf(filepath, "/%s", entries[idx].name);
            else
                sprintf(filepath, "%s/%s", cwd, entries[idx].name);
                
            execlp("xterm", "xterm", "-e", "vi", filepath, NULL);
            _exit(1);
        }
    }
}

static void handle_click(int y)
{
    int idx = (y - MARGIN) / LINE_HEIGHT;
    if (idx >= 0 && idx < nentries) {
        selected = idx;
        draw_list();
        open_entry(idx);
    }
}

static void handle_keypress(XKeyEvent *keyev)
{
    KeySym ks;
    char buf[16];
    int len;

    len = XLookupString(keyev, buf, sizeof(buf), &ks, NULL);
    
    if (len > 0) {
        if (buf[0] == 'q') {
            XCloseDisplay(dpy);
            exit(0);
        } else if (buf[0] == '\n' || buf[0] == '\r') {
            open_entry(selected);
        }
    } else {
        /* Arrow keys */
        if (ks == XK_Up) {
            if (selected > 0) selected--;
            draw_list();
        } else if (ks == XK_Down) {
            if (selected < nentries-1) selected++;
            draw_list();
        }
    }
}

int main(int argc, char **argv)
{
    XEvent ev;

    if (getcwd(cwd, sizeof(cwd)) == NULL)
        strcpy(cwd, "/");

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "Can't open display\n");
        return 1;
    }

    black_pixel = BlackPixel(dpy, 0);
    white_pixel = WhitePixel(dpy, 0);

    win = XCreateSimpleWindow(dpy, RootWindow(dpy, 0), 
                             0, 0, WINDOW_W, WINDOW_H, 1, 
                             black_pixel, white_pixel);
    XSelectInput(dpy, win, ExposureMask | ButtonPressMask | KeyPressMask);
    XStoreName(dpy, win, "Minix FM v2.0");
    XMapWindow(dpy, win);

    fontinfo = XLoadQueryFont(dpy, "fixed");
    if (!fontinfo) fontinfo = XLoadQueryFont(dpy, "6x13");

    gc = XCreateGC(dpy, win, 0, NULL);
    if (fontinfo)
        XSetFont(dpy, gc, fontinfo->fid);

    read_dir(cwd);

    while (1) {
        XNextEvent(dpy, &ev);
        if (ev.type == Expose) {
            draw_list();
        } else if (ev.type == ButtonPress) {
            handle_click(ev.xbutton.y);
        } else if (ev.type == KeyPress) {
            handle_keypress(&ev.xkey);
        }
    }

    return 0;
}
