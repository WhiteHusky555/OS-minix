/*
 * minix_xfm.c
 * Simple X11 file manager for Minix 3.4.0
 * ANSI C version compatible with old compilers
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#define WINDOW_W 800
#define WINDOW_H 600
#define MARGIN 8
#define LINE_HEIGHT 18
#define LIST_X (MARGIN)
#define LIST_Y (MARGIN)
#define LIST_W (WINDOW_W - 2*MARGIN)
#define LIST_H (WINDOW_H - 2*MARGIN)

/* Simple entry structure */
typedef struct Entry {
    char *name;
    int is_dir;
} Entry;

/* Global state */
static Display *dpy;
static int screen_num;
static Window win;
static GC gc;
static XFontStruct *fontinfo;
static unsigned long black_pixel, white_pixel;

static Entry *entries = NULL;
static int nentries = 0;
static int selected = -1;
static char cwd[1024];

/* Double-click detection */
static Time last_click_time = 0;
static int last_click_index = -1;

/* External viewer command */
#define DEFAULT_VIEWER "xterm -e vi"
static char *viewer_argv[16];

/* Forward declarations */
static void setup_viewer(void);
static void read_dir(const char *path);
static void draw_list(void);
static void open_entry(int idx);
static int y_to_index(int y);
static void handle_event(XEvent *ev);
static void sigchld_handler(int sig);

/* Utility: set viewer argv from env or default */
static void setup_viewer(void)
{
    char *env = getenv("FILE_VIEWER");
    char buf[512];
    char *p;
    int i = 0;

    if (env && env[0] != '\0') {
        strncpy(buf, env, sizeof(buf)-1);
        buf[sizeof(buf)-1] = '\0';
    } else {
        strncpy(buf, DEFAULT_VIEWER, sizeof(buf)-1);
        buf[sizeof(buf)-1] = '\0';
    }

    p = strtok(buf, " \t");
    while (p != NULL && i < 15) {
        viewer_argv[i] = strdup(p);
        i++;
        p = strtok(NULL, " \t");
    }
    viewer_argv[i] = NULL;
}

/* Read directory contents into entries[] */
static void read_dir(const char *path)
{
    DIR *d;
    struct dirent *de;
    struct stat st;
    Entry *tmp;
    int cap = 0;
    int i;
    char full[1024];

    /* free old entries */
    if (entries != NULL) {
        for (i = 0; i < nentries; i++) {
            free(entries[i].name);
        }
        free(entries);
        entries = NULL;
        nentries = 0;
    }

    d = opendir(path);
    if (!d) {
        perror("opendir");
        return;
    }

    /* include .. for going up, unless we are at root */
    if (strcmp(path, "/") != 0) {
        cap = 16;
        entries = (Entry*)malloc(sizeof(Entry) * cap);
        if (entries == NULL) return;
        entries[0].name = strdup("..");
        entries[0].is_dir = 1;
        nentries = 1;
    } else {
        cap = 16;
        entries = (Entry*)malloc(sizeof(Entry) * cap);
        if (entries == NULL) {
            closedir(d);
            return;
        }
        nentries = 0;
    }

    while ((de = readdir(d)) != NULL) {
        /* skip '.' */
        if (strcmp(de->d_name, ".") == 0) continue;

        if (nentries + 1 > cap) {
            cap *= 2;
            tmp = (Entry*)realloc(entries, sizeof(Entry) * cap);
            if (!tmp) break; /* OOM */
            entries = tmp;
        }

        entries[nentries].name = strdup(de->d_name);

        /* determine if directory */
        if (path[strlen(path)-1] == '/') {
            sprintf(full, "%s%s", path, de->d_name);
        } else {
            sprintf(full, "%s/%s", path, de->d_name);
        }

        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
            entries[nentries].is_dir = 1;
        } else {
            entries[nentries].is_dir = 0;
        }

        nentries++;
    }
    closedir(d);
}

/* Draw the visible list */
static void draw_list(void)
{
    int i;
    int lines = LIST_H / LINE_HEIGHT;
    char display[1024];
    int y;

    /* clear */
    XSetForeground(dpy, gc, white_pixel);
    XFillRectangle(dpy, win, gc, 0, 0, WINDOW_W, WINDOW_H);

    XSetForeground(dpy, gc, black_pixel);
    for (i = 0; i < nentries && i < lines; i++) {
        y = LIST_Y + i * LINE_HEIGHT + fontinfo->ascent;
        if (i == selected) {
            /* draw selection rectangle */
            XSetForeground(dpy, gc, 0xAAAAAA);
            XFillRectangle(dpy, win, gc, LIST_X, LIST_Y + i * LINE_HEIGHT, 
                          LIST_W, LINE_HEIGHT);
            XSetForeground(dpy, gc, black_pixel);
        }
        if (entries[i].is_dir) {
            sprintf(display, "%s/", entries[i].name);
        } else {
            sprintf(display, "%s", entries[i].name);
        }
        XDrawString(dpy, win, gc, LIST_X + 4, y, display, strlen(display));
    }

    /* draw cwd at bottom */
    XDrawString(dpy, win, gc, LIST_X, WINDOW_H - MARGIN, cwd, strlen(cwd));
}

/* Open a file or change directory */
static void open_entry(int idx)
{
    char newpath[1024];
    char filepath[1024];
    char *p;
    int pid;
    int i;
    char *argv[20];
    
    if (idx < 0 || idx >= nentries) return;

    if (entries[idx].is_dir) {
        /* change directory */
        if (strcmp(entries[idx].name, "..") == 0) {
            p = strrchr(cwd, '/');
            if (!p || p == cwd) {
                /* go to root */
                strcpy(cwd, "/");
            } else {
                *p = '\0';
            }
        } else {
            if (strcmp(cwd, "/") == 0) {
                sprintf(newpath, "/%s", entries[idx].name);
            } else {
                sprintf(newpath, "%s/%s", cwd, entries[idx].name);
            }
            strncpy(cwd, newpath, sizeof(cwd)-1);
            cwd[sizeof(cwd)-1] = '\0';
        }
        read_dir(cwd);
        selected = -1;
        draw_list();
    } else {
        /* open file with configured viewer */
        pid = fork();
        if (pid == 0) {
            /* child */
            if (strcmp(cwd, "/") == 0) {
                sprintf(filepath, "/%s", entries[idx].name);
            } else {
                sprintf(filepath, "%s/%s", cwd, entries[idx].name);
            }

            /* assemble argv: viewer_argv + filepath + NULL */
            for (i = 0; viewer_argv[i] != NULL && i < 15; i++) {
                argv[i] = viewer_argv[i];
            }
            argv[i] = filepath;
            argv[i+1] = NULL;

            /* detach from X, exec viewer */
            setsid();
            execvp(argv[0], argv);
            /* if exec fails, try /bin/sh */
            execlp("/bin/sh", "sh", "-c", viewer_argv[0], (char*)NULL);
            /* failed: exit child */
            _exit(127);
        } else if (pid > 0) {
            /* parent: don't wait */
            return;
        } else {
            perror("fork");
        }
    }
}

/* Convert window Y to entry index */
static int y_to_index(int y)
{
    int rel = y - LIST_Y;
    if (rel < 0) return -1;
    return rel / LINE_HEIGHT;
}

/* Handle X events */
static void handle_event(XEvent *ev)
{
    int idx;
    Time ct;
    KeySym ks;
    char buf[16];
    int len;
    
    if (ev->type == Expose) {
        draw_list();
    } else if (ev->type == ButtonPress) {
        idx = y_to_index(ev->xbutton.y);
        ct = ev->xbutton.time;
        if (idx >= 0 && idx < nentries) {
            selected = idx;
            draw_list();
            /* detect double click: same index and within 400 ms */
            if (last_click_index == idx && last_click_time != 0 && 
                (ct - last_click_time) <= 400) {
                open_entry(idx);
                last_click_time = 0;
                last_click_index = -1;
            } else {
                last_click_time = ct;
                last_click_index = idx;
            }
        }
    } else if (ev->type == KeyPress) {
        len = XLookupString(&ev->xkey, buf, sizeof(buf), &ks, NULL);
        if (len > 0) {
            if (buf[0] == 'q' || buf[0] == 'Q') {
                /* quit */
                XCloseDisplay(dpy);
                exit(0);
            } else if (buf[0] == '\n' || buf[0] == '\r') {
                if (selected >= 0) open_entry(selected);
            }
        } else {
            /* arrow keys */
            if (ks == XK_Up) {
                if (selected > 0) selected--;
                draw_list();
            } else if (ks == XK_Down) {
                if (selected < nentries-1) selected++;
                draw_list();
            }
        }
    }
}

static void sigchld_handler(int sig)
{
    /* reap children to avoid zombies */
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        /* empty */
    }
}

int main(int argc, char **argv)
{
    XEvent ev;
    unsigned long valuemask = 0;
    XGCValues values;
    int i;

    /* initial cwd */
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        strcpy(cwd, "/");
    }

    setup_viewer();

    /* read initial directory */
    read_dir(cwd);

    /* X init */
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "Unable to open X display.\n");
        return 1;
    }
    screen_num = DefaultScreen(dpy);
    black_pixel = BlackPixel(dpy, screen_num);
    white_pixel = WhitePixel(dpy, screen_num);

    win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen_num), 
                             0, 0, WINDOW_W, WINDOW_H, 1, 
                             black_pixel, white_pixel);
    XSelectInput(dpy, win, ExposureMask | ButtonPressMask | KeyPressMask);
    XStoreName(dpy, win, "minix_xfm");
    XMapWindow(dpy, win);

    fontinfo = XLoadQueryFont(dpy, "fixed");
    if (!fontinfo) {
        fontinfo = XLoadQueryFont(dpy, "6x13");
    }
    if (!fontinfo) {
        fprintf(stderr, "Warning: couldn't load font\n");
        /* Continue with default font */
        fontinfo = XQueryFont(dpy, XGContextFromGC(DefaultGC(dpy, screen_num)));
    }

    gc = XCreateGC(dpy, win, valuemask, &values);
    if (fontinfo) {
        XSetFont(dpy, gc, fontinfo->fid);
    }

    /* signal handler for children */
    signal(SIGCHLD, sigchld_handler);

    /* main loop */
    while (1) {
        XNextEvent(dpy, &ev);
        handle_event(&ev);
    }

    /* cleanup (unreachable) */
    XFreeGC(dpy, gc);
    XCloseDisplay(dpy);
    return 0;
}
