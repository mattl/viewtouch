/*
 * Copyright ViewTouch, Inc., 1995, 1996, 1997, 1998  
  
 *   This program is free software: you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation, either version 3 of the License, or 
 *   (at your option) any later version.
 * 
 *   This program is distributed in the hope that it will be useful, 
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 *   GNU General Public License for more details. 
 * 
 *   You should have received a copy of the GNU General Public License 
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. 
 *
 * loader_main.cc - revision 8 (March 2006)
 * Implementation of system starting command
 */

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <X11/Intrinsic.h>
#include <X11/keysym.h>
#include <X11/Shell.h>
#include <X11/Xft/Xft.h>
#include <Xm/MainW.h>
#include <Xm/MwmUtil.h>
#include <Xm/Xm.h>
#include <csignal>
#include <cstring>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#ifdef DMALLOC
#include <dmalloc.h>
#endif

#include "build_number.h"
#include "basic.hh"
#include "generic_char.hh"
#include "logger.hh"
#include "utility.hh"


/**** Defintions ****/
#define SOCKET_FILE    "/tmp/vt_main"
#define COMMAND_FILE   VIEWTOUCH_PATH "/bin/.vtpos_command"
#define FONT_NAME "utopia,serif-14:style=regular:dpi=100"


#define WIN_WIDTH  640
#define WIN_HEIGHT 240

#ifdef DEBUG
int debug_mode = 1;
#else
int debug_mode = 0;
#endif


/**** Globals ****/
Display     *Dis = NULL;
Window       Win = 0;
XftFont     *loaderFont = NULL;
XftDraw     *xftdraw = NULL;
XftColor     xftBlack, xftWhite;
int          SocketNo = 0;
int          ColorBlack = 0;
int          ColorWhite = 1;
int          screen_no = 0;

char Message[1024] = "";
char KBInput[1024] = "";
int  Progress = 0;
int  GetInput = 0;
char BuildNumber[] = BUILD_NUMBER;

/**** Functions ****/
void ExitLoader()
{
    if (SocketNo)
        close(SocketNo);
    if (xftdraw) {
        logmsg(LOG_DEBUG, "Freeing 'black' XftColor\n");
        XftColorFree(Dis, DefaultVisual(Dis, screen_no),
            DefaultColormap(Dis, screen_no), &xftBlack);
        logmsg(LOG_DEBUG, "Freeing 'white' XftColor\n");
        XftColorFree(Dis, DefaultVisual(Dis, screen_no),
            DefaultColormap(Dis, screen_no), &xftWhite);
        logmsg(LOG_DEBUG, "Freeing XftDraw *\n");
        XftDrawDestroy(xftdraw);
        logmsg(LOG_DEBUG, "Closing Xft loader font\n");
        XftFontClose(Dis, loaderFont);
    }
    if (Dis) {
        logmsg(LOG_DEBUG, "Closing X display\n");
        XCloseDisplay(Dis);
    }
    exit(0);
}

int UpdateWindow(const char *str = NULL)
{
    if (str)
        strcpy(Message, str);

    XftDrawRect(xftdraw, &xftWhite, 0, 0, WIN_WIDTH, WIN_HEIGHT);

    int len = strlen(Message);
    if (len > 0)
    {
        int lines = 1;
        char msg[4096];
        int msgidx = 0;
        int idx = 0;
        int ww;
        int hh = 0;
        XGlyphInfo extents;
        for (idx = 0; idx < len; idx++)
        {
            if (Message[idx] == '\\')
                lines += 1;
        }
        idx = 0;

        hh = (WIN_HEIGHT - (lines * loaderFont->height)) / 2;
        while (idx < len && msgidx < 4096)
        {
            msgidx = 0;
            while (Message[idx] != '\\' && Message[idx] != '\0' && idx < len)
            {
                msg[msgidx] = Message[idx];
                idx += 1;
                msgidx += 1;
            }
            idx += 1;
            msg[msgidx] = '\0';
            XftTextExtentsUtf8(Dis, loaderFont, (unsigned const char *)msg, msgidx, &extents);
            ww = (WIN_WIDTH - extents.width) / 2;
            XftDrawStringUtf8(xftdraw, &xftBlack, loaderFont, ww, hh,
                (unsigned const char *)msg, msgidx);
            hh += loaderFont->height;
        }
    }

    XFlush(Dis);
    return 0;
}

int UpdateKeyboard(const char *str = NULL)
{
    genericChar prompt[256] = "Temporary Key:";
    XGlyphInfo extents;
    int len;

    if (str)
        snprintf(KBInput, 4096, "%s_", str);

    // erase first
    XftDrawRect(xftdraw, &xftWhite, 1, WIN_HEIGHT - (3 * loaderFont->height),
        WIN_WIDTH - 2, 3 * loaderFont->height);

    len = strlen(prompt);
    XftTextExtents8(Dis, loaderFont, (unsigned const char *)prompt, len, &extents);
    XftDrawStringUtf8(xftdraw, &xftBlack, loaderFont,
        (WIN_WIDTH - extents.width) / 2,
        WIN_HEIGHT - 2 * loaderFont->height,
        (unsigned const char *)prompt, len);

    len = strlen(KBInput);
    XftTextExtents8(Dis, loaderFont, (unsigned const char *)KBInput, len, &extents);
    XftDrawStringUtf8(xftdraw, &xftBlack, loaderFont,
        (WIN_WIDTH - extents.width) / 2,
        WIN_HEIGHT - loaderFont->height,
        (unsigned const char *)KBInput, len);

    XFlush(Dis);
    return 0;
}

void ExposeCB(Widget widget, XtPointer client_data, XEvent *event,
              Boolean *okay)
{
    XExposeEvent *e = (XExposeEvent *) event;
    if (e->count <= 0)
    {
        UpdateWindow();
        if (GetInput)
            UpdateKeyboard();
    }
}

void KeyPressCB(Widget widget, XtPointer client_data, XEvent *event, Boolean *okay)
{
    static genericChar buffer[256] = "";
    static genericChar keybuff[32] = "";

    if (GetInput)
    {
        XKeyEvent *e = (XKeyEvent *) event;
        KeySym key = 0;
        int bufflen = strlen(buffer);

        int len = XLookupString(e, keybuff, 31, &key, NULL);
        if (len < 0)
            len = 0;
        keybuff[len] = '\0';
        
        switch (key)
        {
        case XK_Return:
            // send the entered string (or quit) to vt_main
            if (bufflen < 1)
                write(SocketNo, "quit", 4);
            else
                write(SocketNo, buffer, strlen(buffer));
            // clear the string for another run
            GetInput = 0;
            memset(buffer, '\0', 256);
            memset(keybuff, '\0', 32);
            break;
        case XK_BackSpace:
            if (bufflen > 0)
                buffer[bufflen - 1] = '\0';
            break;
        default:
            if (((keybuff[0] >= '0' && keybuff[0] <= '9') ||
                (keybuff[0] >= 'A' && keybuff[0] <= 'F')  ||
                (keybuff[0] >= 'a' && keybuff[0] <= 'f')) &&
                // temp key len is actually only 20
                (strlen(buffer) <= 32))
            {
                keybuff[0] = toupper(keybuff[0]);
                strcat(buffer, keybuff);
            }
            break;
        }
        UpdateKeyboard(buffer);
    }
}

void SocketInputCB(XtPointer client_data, int *fid, XtInputId *id)
{
    static genericChar buffer[256];
    static const genericChar *c = buffer;

    int no = read(SocketNo, c, 1);
    if (no == 1)
    {
        if (*c == '\0')
        {
            c = buffer;
            if (strcmp(buffer, "done") == 0)
                ExitLoader();
            else
                UpdateWindow(buffer);
        }
        else if (*c == '\r')
        {
            GetInput = 1;
            *c = '\0';
            UpdateKeyboard("");
        }
        else
            ++c;
    }
}

void SignalFn(int my_signal)
{
    logmsg(LOG_ERR, "Caught fatal signal (%d), exiting.\n", my_signal);
    ExitLoader();
}

void SetPerms()
{
    genericChar emp_data[] = VIEWTOUCH_PATH "/dat/employee.dat.bak2";
    int fd;

    if ((fd = open(emp_data, O_RDONLY)) >= 0) {
        /* chmod 666 file */
        fchmod(fd, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
        close(fd);
    }
}

int WriteArgList(int argc, const genericChar *argv[])
{
    int retval = 0, argidx = 0, fd;

    fd = open(COMMAND_FILE, O_CREAT | O_TRUNC | O_WRONLY, 0700);

    if (fd > 0)
    {
        for (argidx = 0; argidx < argc; argidx++)
        {
            write(fd, argv[argidx], strlen(argv[argidx]));
            write(fd, " ", 1);
        }
        close(fd);
        chmod(COMMAND_FILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        retval = 1;
    }
    return retval;
}

int main(int argc, const genericChar *argv[])
{
    SetPerms();

    // Set up signal interrupts
    signal(SIGINT,  SignalFn);

    // Parse command line options
    int net_off = 0;
    int purge = 0;
    int notrace = 0;
    const char *data_path = NULL;
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "help") == 0)
        {
            printf("Command line options:\n"
                   " path    or -p <dirname> specify data directory\n"
                   " help    or -h           display this help message\n"
                   " netoff  or -n           no network devices started\n"
#if DEBUG
                   " notrace or -t           disable FnTrace, debug mode only\n"
#endif
                   " version or -v           display the build number and exit\n\n");
            return 0;
        }
        else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "path") == 0)
        {
            ++i;
            if (i >= argc)
            {
                logmsg(LOG_ERR, "No path name given");
                return 1;
            }
            data_path = argv[i];
        }
        else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "netoff") == 0)
        {
            net_off = 1;
        }
        else if (strcmp(argv[i], "purge") == 0)
        {
            purge = 1;
        }
#if DEBUG
        else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "notrace") == 0)
        {
            notrace = 1;
        }
#endif
        else if (strcmp(argv[i], "version") == 0)
        {
            // return version for vt_update
            printf("1\n");
            return 0;
        }
        else if (strcmp(argv[i], "-v") == 0)
        {
            printf("POS Version:  %s\n", BuildNumber);
            exit(1);
        }
    }

    // Write command line options to a file so that vt_main knows how to restart
    // the system
    WriteArgList(argc, argv);

    // Xt toolkit init & window
    Widget shell = NULL;
    int dis_width = 0, dis_height = 0;
    XtToolkitInitialize();

    XtAppContext app = XtCreateApplicationContext();

    Dis = XtOpenDisplay(app, NULL, NULL, NULL, NULL, 0, &argc, argv);
    if (Dis == NULL)
    {
        logmsg(LOG_ERR, "Unable to open display\n");
        ExitLoader();
    }

    screen_no  = DefaultScreen(Dis);
    dis_width  = DisplayWidth(Dis, screen_no);
    dis_height = DisplayHeight(Dis, screen_no);
    ColorBlack = BlackPixel(Dis, screen_no);
    ColorWhite = WhitePixel(Dis, screen_no);

    loaderFont = XftFontOpenName(Dis, screen_no, FONT_NAME);
    if (loaderFont == NULL)
    {
        logmsg(LOG_ERR, "Unable to load font\n");
        ExitLoader();
    }

    Arg args[] = {
        { XtNx,                (dis_width - WIN_WIDTH) / 2   },
        { XtNy,                (dis_height - WIN_HEIGHT) / 2 },
        { XtNwidth,            WIN_WIDTH                     },
        { XtNheight,           WIN_HEIGHT                    },
        { XtNborderWidth,      0                             },
        { "minWidth",          WIN_WIDTH                     },
        { "minHeight",         WIN_HEIGHT                    },
        { "maxWidth",          WIN_WIDTH                     },
        { "maxHeight",         WIN_HEIGHT                    },
        { "mwmDecorations",    0                             },
        { "mappedWhenManaged", False                         },
    };

    shell = XtAppCreateShell("Welcome to POS", NULL,
                             applicationShellWidgetClass, Dis, args, XtNumber(args));
    XtRealizeWidget(shell);

    Win = XtWindow(shell);
    xftdraw = XftDrawCreate(Dis, Win, DefaultVisual(Dis, screen_no), DefaultColormap(Dis, screen_no));
    XftColorAllocName(Dis, DefaultVisual(Dis, screen_no),
        DefaultColormap(Dis, screen_no), "black", &xftBlack);
    XftColorAllocName(Dis, DefaultVisual(Dis, screen_no),
        DefaultColormap(Dis, screen_no), "white", &xftWhite);

    XtAddEventHandler(shell, ExposureMask, FALSE, ExposeCB, NULL);
    XtAddEventHandler(shell, KeyPressMask, FALSE, KeyPressCB, NULL);

    // Setup Connection
    struct sockaddr_un server_adr, client_adr;
    server_adr.sun_family = AF_UNIX;
    strcpy(server_adr.sun_path, SOCKET_FILE);
    unlink(SOCKET_FILE);

    char str[256];
    int dev = socket(AF_UNIX, SOCK_STREAM, 0);
    if (dev <= 0)
    {
        logmsg(LOG_ERR, "Failed to open socket '%s'", SOCKET_FILE);
    }
    else if (bind(dev, (struct sockaddr *) &server_adr, SUN_LEN(&server_adr)) < 0)
    {
        logmsg(LOG_ERR, "Failed to bind socket '%s'", SOCKET_FILE);
    }
    else
    {
        Uint len;

        sprintf(str, VIEWTOUCH_PATH "/bin/vt_main %s&", SOCKET_FILE);
//        sprintf(str, "/usr/toolworks/totalview.8.2.0-1/bin/totalview \"/usr/viewtouch/bin/vt_main %s&\"", SOCKET_FILE);
        system(str);
        listen(dev, 1);
        len = sizeof(client_adr);
        SocketNo = accept(dev, (struct sockaddr *) &client_adr, &len);
        if (SocketNo <= 0)
        {
            logmsg(LOG_ERR, "Failed to start main module");
            return 0;
        }
    }

    if (dev)
        close(dev);
    unlink(SOCKET_FILE);

    // Show Window
    XtMapWidget(shell);
    XFlush(Dis);

    // Send Commands
    const genericChar *displaystr = DisplayString(Dis);
    int displaylen = strlen(displaystr);
    write(SocketNo, "display ", 8);
    write(SocketNo, displaystr, displaylen + 1);
    if (data_path)
    {
        write(SocketNo, "datapath ", 9);
        write(SocketNo, data_path, strlen(data_path)+1);
    }
    if (net_off)
        write(SocketNo, "netoff", 7);
    if (purge)
        write(SocketNo, "purge", 6);
    if (notrace)
        write(SocketNo, "notrace", 8);
    write(SocketNo, "done", 5);

    // Read Status Messages
    XtAppAddInput(app, SocketNo, (XtPointer) XtInputReadMask,
                  (XtInputCallbackProc) SocketInputCB, NULL);

    XEvent event;
    for (;;)
    {
        XtAppNextEvent(app, &event);
        switch (event.type)
        {
        case MappingNotify:
            XRefreshKeyboardMapping((XMappingEvent *) &event); break;
        }
        XtDispatchEvent(&event);
    }
    return 0;
}
