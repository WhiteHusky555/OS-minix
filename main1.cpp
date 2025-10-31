/*
 * minix_xfm_v1.c
 * Simple X11 file manager for Minix 3.4.0 - Version 1.0
 * Basic directory listing and file opening
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

#define WINDOW_W 600
#define WINDOW_H 400
#define LINE_HEIGHT 16
#define MARGIN 5

/* Simple entry structure */
typedef struct Entry {
    char name[256];
    int is_dir;
} Entry;

/* Global state */
static Display *dpy;
static Window win;
static GC gc;
static XFontStruct *fontinfo;

static Entry entries[1000];
static int nentries = 0;
static char cwd[1024];

/* Read directory contents */
static void read_dir(const char *path)
{
    DIR *d;
    struct dirent *de;
    struct stat st;
    char full[1024];
    int i = 0;

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
        
        /* Build full path */
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
}

/* Draw the file list */
static void draw_list(void)
{
    int i;
    char display[300];
    int y;

    /* Clear window */
    XClearWindow(dpy, win);

    for (i = 0; i < nentries; i++) {
        y = MARGIN + i * LINE_HEIGHT + fontinfo->ascent;
        
        if (entries[i].is_dir)
            sprintf(display, "[DIR] %s", entries[i].name);
        else
            sprintf(display, "      %s", entries[i].name);
            
        XDrawString(dpy, win, gc, MARGIN, y, display, strlen(display));
    }
}

/* Open file or directory */
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
        /* Simple file opening with xterm */
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

/* Handle mouse clicks */
static void handle_click(int y)
{
    int idx = (y - MARGIN) / LINE_HEIGHT;
    if (idx >= 0 && idx < nentries) {
        open_entry(idx);
    }
}

int main(int argc, char **argv)
{
    XEvent ev;

    /* Get current directory */
    if (getcwd(cwd, sizeof(cwd)) == NULL)
        strcpy(cwd, "/");

    /* X11 initialization */
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "Can't open display\n");
        return 1;
    }

    win = XCreateSimpleWindow(dpy, RootWindow(dpy, 0), 
                             0, 0, WINDOW_W, WINDOW_H, 1, 
                             BlackPixel(dpy, 0), WhitePixel(dpy, 0));
    XSelectInput(dpy, win, ExposureMask | ButtonPressMask);
    XStoreName(dpy, win, "Minix FM v1.0");
    XMapWindow(dpy, win);

    /* Load font */
    fontinfo = XLoadQueryFont(dpy, "fixed");
    if (!fontinfo) fontinfo = XLoadQueryFont(dpy, "6x13");

    /* Create graphics context */
    gc = XCreateGC(dpy, win, 0, NULL);
    if (fontinfo)
        XSetFont(dpy, gc, fontinfo->fid);

    /* Read initial directory */
    read_dir(cwd);

    /* Main event loop */
    while (1) {
        XNextEvent(dpy, &ev);
        if (ev.type == Expose) {
            draw_list();
        } else if (ev.type == ButtonPress) {
            handle_click(ev.xbutton.y);
        }
    }

    return 0;
}
