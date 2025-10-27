/*
 Very small file manager for X11 (C++)
 Minimal dependencies: X11, POSIX (dirent.h, unistd.h)
 Features:
  - Lists files in current directory
  - Move selection with Up/Down keys
  - Enter: if directory -> cd into it; else -> tries to open with xdg-open (fallback to less)
  - Backspace: go up (cd ..)
  - q: quit

 Compile:
   g++ mini_x11_file_manager.cpp -o mini_fm -lX11
 Run:
   ./mini_fm

 Notes: very small educational example. On Minix you may need to adjust the open command
 (xdg-open may not exist). Also make sure X server is running and X11 development headers/libs installed.
*/

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pwd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>

using namespace std;

struct Entry {
    string name;
    bool is_dir;
};

static vector<Entry> entries;
static int sel = 0;
static int offset = 0;

void read_dir(const char* path) {
    entries.clear();
    DIR* d = opendir(path);
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d))) {
        string n(e->d_name);
        if (n == ".") continue;
        struct stat st;
        if (stat(n.c_str(), &st) == 0) {
            bool isd = S_ISDIR(st.st_mode);
            entries.push_back({n, isd});
        } else {
            entries.push_back({n, false});
        }
    }
    closedir(d);
    sort(entries.begin(), entries.end(), [](const Entry&a,const Entry&b){
        if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir; // dirs first
        return a.name < b.name;
    });
    sel = 0; offset = 0;
}

int main(){
    // Start in current directory
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, ".");
    read_dir(cwd);

    Display* dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "Cannot open X display\n");
        return 1;
    }
    int scr = DefaultScreen(dpy);
    Window root = RootWindow(dpy, scr);
    unsigned long white = WhitePixel(dpy, scr);
    unsigned long black = BlackPixel(dpy, scr);

    int winw = 600, winh = 400;
    Window win = XCreateSimpleWindow(dpy, root, 100, 100, winw, winh, 1, black, white);
    XStoreName(dpy, win, "mini-fm");

    XSelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask);
    XMapWindow(dpy, win);

    // Font
    XFontStruct* font = XLoadQueryFont(dpy, "fixed");
    if (!font) font = XLoadQueryFont(dpy, "-*-helvetica-*-r-*-*-12-*-*-*-*-*-*-*");
    int fh = font ? font->ascent + font->descent : 14;

    GC gc = XCreateGC(dpy, win, 0, NULL);
    if (font) XSetFont(dpy, gc, font->fid);

    Atom wmDelete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wmDelete, 1);

    bool running = true;
    XEvent ev;
    while (running) {
        XNextEvent(dpy, &ev);
        if (ev.type == Expose || ev.type == ConfigureNotify) {
            // redraw
            XClearWindow(dpy, win);
            // draw header with cwd
            if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, "?");
            string header = string("cwd: ") + cwd + "  (q:quit, Enter:open, Backspace:up)";
            XDrawString(dpy, win, gc, 6, fh, header.c_str(), header.size());

            int y = fh * 2;
            int h = winh - y - 10;
            int lines = h / fh;
            for (int i = 0; i < lines; ++i) {
                int idx = offset + i;
                if (idx >= (int)entries.size()) break;
                string s = entries[idx].name;
                if (entries[idx].is_dir) s = string("[" ) + s + "]";
                int x = 8;
                int baseline = y + i * fh + font->ascent;
                if (idx == sel) {
                    // draw selection rectangle
                    XFillRectangle(dpy, win, gc, x-4, y + i*fh - font->ascent/2, winw - 16, fh);
                    XSetForeground(dpy, gc, white);
                    XDrawString(dpy, win, gc, x, baseline, s.c_str(), s.size());
                    XSetForeground(dpy, gc, black);
                } else {
                    XDrawString(dpy, win, gc, x, baseline, s.c_str(), s.size());
                }
            }
        } else if (ev.type == KeyPress) {
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            if (ks == XK_q || ks == XK_Q) {
                running = false;
            } else if (ks == XK_Up) {
                if (sel > 0) sel--;
                if (sel < offset) offset = sel;
                XClearWindow(dpy, win);
                XEvent e2; e2.type = Expose; XSendEvent(dpy, win, False, ExposureMask, &e2);
            } else if (ks == XK_Down) {
                if (sel+1 < (int)entries.size()) sel++;
                // adjust offset to keep selection visible
                int lines = (winh - fh*2 - 10) / fh;
                if (sel >= offset + lines) offset = sel - lines + 1;
                XClearWindow(dpy, win);
                XEvent e2; e2.type = Expose; XSendEvent(dpy, win, False, ExposureMask, &e2);
            } else if (ks == XK_Return) {
                if (entries.empty()) continue;
                string target = entries[sel].name;
                if (entries[sel].is_dir) {
                    if (chdir(target.c_str()) == 0) {
                        read_dir(".");
                    }
                } else {
                    // try xdg-open, fallback to less
                    string cmd = string("xdg-open '") + target + "' 2>/dev/null &";
                    int r = system(cmd.c_str());
                    if (r == -1) {
                        string cmd2 = string("less '") + target + "' &";
                        system(cmd2.c_str());
                    }
                }
                XClearWindow(dpy, win);
                XEvent e2; e2.type = Expose; XSendEvent(dpy, win, False, ExposureMask, &e2);
            } else if (ks == XK_BackSpace) {
                if (chdir("..") == 0) read_dir(".");
                XClearWindow(dpy, win);
                XEvent e2; e2.type = Expose; XSendEvent(dpy, win, False, ExposureMask, &e2);
            }
        } else if (ev.type == ClientMessage) {
            if ((Atom)ev.xclient.data.l[0] == wmDelete) running = false;
        }
    }

    XFreeGC(dpy, gc);
    if (font) XFreeFont(dpy, font);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
