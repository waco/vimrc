/*-----------------------------------------------------------------------------
 * Copyright (c) 2009       
 * Kazuo Ishii        - <k-ishii at wb4.so-net.ne.jp> original version(ckw)
 * Yukihiro Nakadaira - <yukihiro.nakadaira at gmail.com> original version(vimproc)
 * Shougo Matsushita  - <Shougo.Matsu at gmail.com> modified version
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *---------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <stdarg.h>

/* For GetConsoleWindow() for Windows 2000 or later. */
#define WINVER        0x0500
#define _WIN32_WINNT  0x0500

#include <windows.h>
#include <winbase.h>
#include <shellapi.h>
#if 0
# include <winsock2.h>
#endif
#define _POSIX_
#include <fcntl.h>
#include <io.h>

#include "vimstack.c"

const int debug = 0;

#ifdef _MSC_VER
# define EXPORT __declspec(dllexport)
#else
# define EXPORT
#endif

/* API */
EXPORT const char *vp_dlopen(char *args);      /* [handle] (path) */
EXPORT const char *vp_dlclose(char *args);     /* [] (handle) */

EXPORT const char *vp_file_open(char *args);   /* [fd] (path, flags, mode) */
EXPORT const char *vp_file_close(char *args);  /* [] (fd) */
EXPORT const char *vp_file_read(char *args);   /* [hd, eof] (fd, nr, timeout) */
EXPORT const char *vp_file_write(char *args);  /* [nleft] (fd, hd, timeout) */

EXPORT const char *vp_pipe_open(char *args);   /* [pid, [fd] * npipe]
                                                  (npipe, argc, [argv]) */
EXPORT const char *vp_pipe_close(char *args);  /* [] (fd) */
EXPORT const char *vp_pipe_read(char *args);   /* [hd, eof] (fd, nr, timeout) */
EXPORT const char *vp_pipe_write(char *args);  /* [nleft] (fd, hd, timeout) */

EXPORT const char *vp_pty_open(char *args);    /* [pid, fd, ttyname]
                                                  (width, height, argc, [argv]) */
EXPORT const char *vp_pty_close(char *args);   /* [] (fd) */
EXPORT const char *vp_pty_read(char *args);    /* [hd, eof] (fd, nr, timeout) */
EXPORT const char *vp_pty_write(char *args);   /* [nleft] (fd, hd, timeout) */
EXPORT const char *vp_pty_get_winsize(char *args); /* [width, height] (fd) */
EXPORT const char *vp_pty_set_winsize(char *args); /* [] (fd, width, height) */

EXPORT const char *vp_kill(char *args);        /* [] (pid, sig) */
EXPORT const char *vp_waitpid(char *args);     /* [cond, status] (pid) */

EXPORT const char *vp_socket_open(char *args); /* [socket] (host, port) */
EXPORT const char *vp_socket_close(char *args);/* [] (socket) */
EXPORT const char *vp_socket_read(char *args); /* [hd, eof] (socket, nr, timeout) */
EXPORT const char *vp_socket_write(char *args);/* [nleft] (socket, hd, timeout) */

EXPORT const char *vp_open(char *args);      /* [] (path) */

static BOOL ExitRemoteProcess(HANDLE hProcess, UINT uExitCode);

/* --- */

#define VP_ARGC_MAX 256
#define VP_READ_BUFSIZE 2048

static const char *
lasterror()
{
    static char lpMsgBuf[512];
    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, GetLastError(), 0,
            lpMsgBuf, 512, NULL);
    return lpMsgBuf;
}

#define open _open
#define close _close
#define read _read
#define write _write

#define CSI_WndCols(csi) ((csi)->srWindow.Right - (csi)->srWindow.Left +1)
#define CSI_WndRows(csi) ((csi)->srWindow.Bottom - (csi)->srWindow.Top +1)

static vp_stack_t _result = VP_STACK_NULL;

const char *
vp_dlopen(char *args)
{
    vp_stack_t stack;
    char *path;
    HINSTANCE handle;

    VP_RETURN_IF_FAIL(vp_stack_from_args(&stack, args));
    VP_RETURN_IF_FAIL(vp_stack_pop_str(&stack, &path));

    handle = LoadLibrary(path);
    if (handle == NULL)
        return lasterror();
    vp_stack_push_num(&_result, "%p", handle);
    return vp_stack_return(&_result);
}

const char *
vp_dlclose(char *args)
{
    vp_stack_t stack;
    HINSTANCE handle;

    VP_RETURN_IF_FAIL(vp_stack_from_args(&stack, args));
    VP_RETURN_IF_FAIL(vp_stack_pop_num(&stack, "%p", &handle));

    if (!FreeLibrary(handle))
        return lasterror();
    vp_stack_free(&_result);
    return NULL;
}


const char *
vp_file_open(char *args)
{
    vp_stack_t stack;
    char *path;
    char *flags;
    int mode;  /* used when flags have O_CREAT */
    int f = 0;
    int fd;

    VP_RETURN_IF_FAIL(vp_stack_from_args(&stack, args));
    VP_RETURN_IF_FAIL(vp_stack_pop_str(&stack, &path));
    VP_RETURN_IF_FAIL(vp_stack_pop_str(&stack, &flags));
    VP_RETURN_IF_FAIL(vp_stack_pop_num(&stack, "%d", &mode));

#ifdef O_RDONLY
    if (strstr(flags, "O_RDONLY"))      f |= O_RDONLY;
#endif
#ifdef O_WRONLY
    if (strstr(flags, "O_WRONLY"))      f |= O_WRONLY;
#endif
#ifdef O_RDWR
    if (strstr(flags, "O_RDRW"))        f |= O_RDWR;
#endif
#ifdef O_NONBLOCK
    if (strstr(flags, "O_NONBLOCK"))    f |= O_NONBLOCK;
#endif
#ifdef O_APPEND
    if (strstr(flags, "O_APPEND"))      f |= O_APPEND;
#endif
#ifdef O_CEAT
    if (strstr(flags, "O_CREAT"))       f |= O_CREAT;
#endif
#ifdef O_EXCL
    if (strstr(flags, "O_EXCL"))        f |= O_EXCL;
#endif
#ifdef O_TRUNC
    if (strstr(flags, "O_TRUNC"))       f |= O_TRUNC;
#endif
#ifdef O_SHLOCK
    if (strstr(flags, "O_SHLOCK"))      f |= O_SHLOCK;
#endif
#ifdef O_EXLOCK
    if (strstr(flags, "O_EXLOCK"))      f |= O_EXLOCK;
#endif
#ifdef O_DIRECT
    if (strstr(flags, "O_DIRECT"))      f |= O_DIRECT;
#endif
#ifdef O_FSYNC
    if (strstr(flags, "O_FSYNC"))       f |= O_FSYNC;
#endif
#ifdef O_NOFOLLOW
    if (strstr(flags, "O_NOFOLLOW"))    f |= O_NOFOLLOW;
#endif
#ifdef O_TEMPORARY
    if (strstr(flags, "O_TEMPORARY"))   f |= O_TEMPORARY;
#endif
#ifdef O_RANDOM
    if (strstr(flags, "O_RANDOM"))      f |= O_RANDOM;
#endif
#ifdef O_SEQUENTIAL
    if (strstr(flags, "O_SEQENTIAL"))   f |= O_SEQUENTIAL;
#endif
#ifdef O_BINARY
    if (strstr(flags, "O_BINARY"))      f |= O_BINARY;
#endif
#ifdef O_TEXT
    if (strstr(flags, "O_TEXT"))        f |= O_TEXT;
#endif
#ifdef O_INHERIT
    if (strstr(flags, "O_INHERIT"))     f |= O_INHERIT;
#endif
#ifdef _O_SHORT_LIVED
    if (strstr(flags, "O_SHORT_LIVED")) f |= _O_SHORT_LIVED;
#endif

    fd = open(path, f, mode);
    if (fd == -1)
        return vp_stack_return_error(&_result, "open() error: %s",
                strerror(errno));
    vp_stack_push_num(&_result, "%d", fd);
    return vp_stack_return(&_result);
}

const char *
vp_file_close(char *args)
{
    vp_stack_t stack;
    int fd;

    VP_RETURN_IF_FAIL(vp_stack_from_args(&stack, args));
    VP_RETURN_IF_FAIL(vp_stack_pop_num(&stack, "%d", &fd));

    if (close(fd) == -1)
        return vp_stack_return_error(&_result, "close() error: %s",
                strerror(errno));
    return NULL;
}

const char *
vp_file_read(char *args)
{
    vp_stack_t stack;
    int fd;
    int nr;
    int timeout;
    DWORD ret;
    int n;
    char buf[VP_READ_BUFSIZE];

    VP_RETURN_IF_FAIL(vp_stack_from_args(&stack, args));
    VP_RETURN_IF_FAIL(vp_stack_pop_num(&stack, "%d", &fd));
    VP_RETURN_IF_FAIL(vp_stack_pop_num(&stack, "%d", &nr));
    VP_RETURN_IF_FAIL(vp_stack_pop_num(&stack, "%d", &timeout));

    vp_stack_push_str(&_result, ""); /* initialize */
    while (nr != 0) {
        ret = WaitForSingleObject((HANDLE)_get_osfhandle(fd), timeout);
        if (ret == WAIT_FAILED) {
            return vp_stack_return_error(&_result, "WaitForSingleObject() error: %s",
                    lasterror());
        } else if (ret == WAIT_TIMEOUT) {
            /* timeout */
            break;
        }
        if (nr > 0)
            n = read(fd, buf, (VP_READ_BUFSIZE < nr) ? VP_READ_BUFSIZE : nr);
        else
            n = read(fd, buf, VP_READ_BUFSIZE);
        if (n == -1) {
            return vp_stack_return_error(&_result, "read() error: %s",
                    strerror(errno));
        } else if (n == 0) {
            /* eof */
            vp_stack_push_num(&_result, "%d", 1);
            return vp_stack_return(&_result);
        }
        /* decrease stack top for concatenate. */
        _result.top--;
        vp_stack_push_bin(&_result, buf, n);
        if (nr > 0)
            nr -= n;
        /* try read more bytes without waiting */
        timeout = 0;
    }
    vp_stack_push_num(&_result, "%d", 0);
    return vp_stack_return(&_result);
}

const char *
vp_file_write(char *args)
{
    vp_stack_t stack;
    int fd;
    char *buf;
    size_t size;
    int timeout;
    size_t nleft;
    DWORD ret;
    int n;

    VP_RETURN_IF_FAIL(vp_stack_from_args(&stack, args));
    VP_RETURN_IF_FAIL(vp_stack_pop_num(&stack, "%d", &fd));
    VP_RETURN_IF_FAIL(vp_stack_pop_bin(&stack, &buf, &size));
    VP_RETURN_IF_FAIL(vp_stack_pop_num(&stack, "%d", &timeout));

    nleft = 0;
    while (nleft < size) {
        ret = WaitForSingleObject((HANDLE)_get_osfhandle(fd), timeout);
        if (ret == WAIT_FAILED) {
            return vp_stack_return_error(&_result, "WaitForSingleObject() error: %s",
                    lasterror());
        } else if (ret == WAIT_TIMEOUT) {
            /* timeout */
            break;
        }
        n = write(fd, buf + nleft, size - nleft);
        if (n == -1) {
            return vp_stack_return_error(&_result, "write() error: %s",
                    strerror(errno));
        }
        nleft += n;
        /* try write more bytes without waiting */
        timeout = 0;
    }
    vp_stack_push_num(&_result, "%u", nleft);
    return vp_stack_return(&_result);
}

/*
 * http://support.microsoft.com/kb/190351/
 */
const char *
vp_pipe_open(char *args)
{
    vp_stack_t stack;
    int npipe;
    char *cmdline;
    HANDLE hInputWriteTmp, hInputRead, hInputWrite;
    HANDLE hOutputReadTmp, hOutputRead, hOutputWrite;
    HANDLE hErrorReadTmp, hErrorRead, hErrorWrite;
    SECURITY_ATTRIBUTES sa;
    PROCESS_INFORMATION pi;
    STARTUPINFO si;

    VP_RETURN_IF_FAIL(vp_stack_from_args(&stack, args));
    VP_RETURN_IF_FAIL(vp_stack_pop_num(&stack, "%d", &npipe));
    if (npipe != 2 && npipe != 3)
        return vp_stack_return_error(&_result, "npipe range error");
    VP_RETURN_IF_FAIL(vp_stack_pop_str(&stack, &cmdline));

    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&hInputRead, &hInputWriteTmp, &sa, 0))
        return vp_stack_return_error(&_result, "CreatePipe() error: %s",
                lasterror());
    if (!DuplicateHandle(GetCurrentProcess(),
                         hInputWriteTmp,
                         GetCurrentProcess(),
                         &hInputWrite,
                         0,
                         FALSE,
                         DUPLICATE_SAME_ACCESS))
        return vp_stack_return_error(&_result, "DuplicateHandle() error: %s",
                lasterror());
    if (!CloseHandle(hInputWriteTmp))
        return vp_stack_return_error(&_result, "CloseHandle() error: %s",
                lasterror());

    if (!CreatePipe(&hOutputReadTmp, &hOutputWrite, &sa, 0))
        return vp_stack_return_error(&_result, "CreatePipe() error: %s",
                lasterror());
    if (!DuplicateHandle(GetCurrentProcess(),
                         hOutputReadTmp,
                         GetCurrentProcess(),
                         &hOutputRead,
                         0,
                         FALSE,
                         DUPLICATE_SAME_ACCESS))
        return vp_stack_return_error(&_result, "DuplicateHandle() error: %s",
                lasterror());
    if (!CloseHandle(hOutputReadTmp))
        return vp_stack_return_error(&_result, "CloseHandle() error: %s",
                lasterror());

    if (npipe == 2) {
        if (!DuplicateHandle(GetCurrentProcess(),
                             hOutputWrite,
                             GetCurrentProcess(),
                             &hErrorWrite,
                             0,
                             TRUE,
                             DUPLICATE_SAME_ACCESS))
            return vp_stack_return_error(&_result, "DuplicateHandle() error: %s",
                    lasterror());
    } else {
        if (!CreatePipe(&hErrorReadTmp, &hErrorWrite, &sa, 0))
            return vp_stack_return_error(&_result, "CreatePipe() error: %s",
                    lasterror());

        if (!DuplicateHandle(GetCurrentProcess(),
                             hErrorReadTmp,
                             GetCurrentProcess(),
                             &hErrorRead,
                             0,
                             FALSE,
                             DUPLICATE_SAME_ACCESS))
            return vp_stack_return_error(&_result, "DuplicateHandle() error: %s",
                    lasterror());
        if (!CloseHandle(hErrorReadTmp))
            return vp_stack_return_error(&_result, "CloseHandle() error: %s",
                    lasterror());
    }
    
    ZeroMemory(&si, sizeof(STARTUPINFO));
    si.cb = sizeof(STARTUPINFO);
    /*si.dwFlags = STARTF_USESTDHANDLES;*/
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = hInputRead;
    si.hStdOutput = hOutputWrite;
    si.hStdError = hErrorWrite;

    if (!CreateProcess(NULL, cmdline, NULL, NULL, TRUE,
                        CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi))
                        /*0, NULL, NULL, &si, &pi))*/
                        /*DETACHED_PROCESS, NULL, NULL, &si, &pi))*/
                        /*CREATE_NO_WINDOW, NULL, NULL, &si, &pi))*/
        return vp_stack_return_error(&_result, "CreateProcess() error: %s",
                lasterror());

    if (!CloseHandle(pi.hThread))
        return vp_stack_return_error(&_result, "CloseHandle() error: %s",
                lasterror());

    if (!CloseHandle(hInputRead))
        return vp_stack_return_error(&_result, "CloseHandle() error: %s",
                lasterror());
    if (!CloseHandle(hOutputWrite))
        return vp_stack_return_error(&_result, "CloseHandle() error: %s",
                lasterror());
    if (!CloseHandle(hErrorWrite))
        return vp_stack_return_error(&_result, "CloseHandle() error: %s",
                lasterror());

    vp_stack_push_num(&_result, "%p", pi.hProcess);
    vp_stack_push_num(&_result, "%d", _open_osfhandle((long)hInputWrite, 0));
    vp_stack_push_num(&_result, "%d", _open_osfhandle((long)hOutputRead, _O_RDONLY));
    if (npipe == 3)
        vp_stack_push_num(&_result, "%d", _open_osfhandle((long)hErrorRead, _O_RDONLY));
    return vp_stack_return(&_result);
}

const char *
vp_pipe_close(char *args)
{
    vp_stack_t stack;
    int fd;

    VP_RETURN_IF_FAIL(vp_stack_from_args(&stack, args));
    VP_RETURN_IF_FAIL(vp_stack_pop_num(&stack, "%d", &fd));

    if (!CloseHandle((HANDLE)_get_osfhandle(fd)))
        return vp_stack_return_error(&_result, "CloseHandle() error: %s",
                lasterror());
    return NULL;
}

const char *
vp_pipe_read(char *args)
{
    vp_stack_t stack;
    int fd;
    int nr;
    int timeout;
    DWORD n;
    char buf[VP_READ_BUFSIZE];

    VP_RETURN_IF_FAIL(vp_stack_from_args(&stack, args));
    VP_RETURN_IF_FAIL(vp_stack_pop_num(&stack, "%d", &fd));
    VP_RETURN_IF_FAIL(vp_stack_pop_num(&stack, "%d", &nr));
    VP_RETURN_IF_FAIL(vp_stack_pop_num(&stack, "%d", &timeout));

    vp_stack_push_str(&_result, ""); /* initialize */
    while (nr != 0) {
        n = 0;
        if (!PeekNamedPipe((HANDLE)_get_osfhandle(fd), buf,
                (nr < 0) ? VP_READ_BUFSIZE : (VP_READ_BUFSIZE < nr) ? VP_READ_BUFSIZE : nr,
                &n, NULL, NULL))
        {
            /* can be ERROR_HANDLE_EOF? */
            if (GetLastError() == 0 || GetLastError() == ERROR_BROKEN_PIPE) {
                /* error or eof */
                if (n != 0) {
                    /* decrease stack top for concatenate. */
                    _result.top--;
                    vp_stack_push_bin(&_result, buf, n);
                }
                vp_stack_push_num(&_result, "%d", 1);
                return vp_stack_return(&_result);
            }
            return vp_stack_return_error(&_result, "PeekNamedPipe() error: %08X %s",
                    GetLastError(), lasterror());
        }
        if (n == 0) {
            break;
        }
        if (read(fd, buf, n) == -1)
            return vp_stack_return_error(&_result, "read() error: %s",
                    strerror(errno));
        /* decrease stack top for concatenate. */
        _result.top--;
        vp_stack_push_bin(&_result, buf, n);
        if (nr > 0)
            nr -= n;
        /* try read more bytes without waiting */
        timeout = 0;
    }
    vp_stack_push_num(&_result, "%d", 0);
    return vp_stack_return(&_result);
}

const char *
vp_pipe_write(char *args)
{
    return vp_file_write(args);
}

const char *
vp_pty_open(char *args)
{
    return "vp_pty_open() is not available";
}

const char *
vp_pty_close(char *args)
{
    return "vp_pty_close() is not available";
}

const char *
vp_pty_read(char *args)
{
    return "vp_pty_read() is not available";
}

const char *
vp_pty_write(char *args)
{
    return "vp_pty_write() is not available";
}

const char *
vp_pty_get_winsize(char *args)
{
    return "vp_pty_get_winsize() is not available";
}

const char *
vp_pty_set_winsize(char *args)
{
    return "vp_pty_set_winsize() is not available";
}

const char *
vp_kill(char *args)
{
    vp_stack_t stack;
    HANDLE handle;

    VP_RETURN_IF_FAIL(vp_stack_from_args(&stack, args));
    VP_RETURN_IF_FAIL(vp_stack_pop_num(&stack, "%p", &handle));

    /*if (!TerminateProcess(handle, 2) || !CloseHandle(handle))*/
        /*return vp_stack_return_error(&_result, "kill() error: %s",*/
                /*lasterror());*/
    if (!ExitRemoteProcess(handle, 2)) {
        return vp_stack_return_error(&_result, "kill() error: %s",
                lasterror());
    }

    return NULL;
}

/* Improved kill function. */
/* http://homepage3.nifty.com/k-takata/diary/2009-05.html */
static BOOL ExitRemoteProcess(HANDLE hProcess, UINT uExitCode)
{
    LPTHREAD_START_ROUTINE pfnExitProcess =
        (LPTHREAD_START_ROUTINE) GetProcAddress(
                GetModuleHandle("kernel32.dll"), "ExitProcess");
    if ((hProcess != NULL) && (pfnExitProcess != NULL)) {
        HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0,
                pfnExitProcess, (LPVOID) uExitCode, 0, NULL);
        if (hThread != NULL) {
            CloseHandle(hThread);
            return TRUE;
        }
    }
    return FALSE;
}

const char *
vp_waitpid(char *args)
{
    vp_stack_t stack;
    HANDLE handle;
    DWORD exitcode;

    VP_RETURN_IF_FAIL(vp_stack_from_args(&stack, args));
    VP_RETURN_IF_FAIL(vp_stack_pop_num(&stack, "%p", &handle));

    if (!GetExitCodeProcess(handle, &exitcode))
        return vp_stack_return_error(&_result,
                "GetExitCodeProcess() error: %s", lasterror());

    vp_stack_push_str(&_result, (exitcode == STILL_ACTIVE) ? "run" : "exit");
    vp_stack_push_num(&_result, "%u", exitcode);
    return vp_stack_return(&_result);
}

/*
 * This is based on socket.diff.gz written by Yasuhiro Matsumoto.
 * see: http://marc.theaimsgroup.com/?l=vim-dev&m=105289857008664&w=2
 */
static int sockets_number = 0;

const char *
vp_socket_open(char *args)
{
    vp_stack_t stack;
    char *host;
    char *port;
    int port_nr;
    int n;
    unsigned short nport;
    int sock;
    struct sockaddr_in sockaddr;
    struct hostent *hostent;
    struct servent *servent;

    VP_RETURN_IF_FAIL(vp_stack_from_args(&stack, args));
    VP_RETURN_IF_FAIL(vp_stack_pop_str(&stack, &host));
    VP_RETURN_IF_FAIL(vp_stack_pop_str(&stack, &port));

    if (sockets_number++ == 0)
    {
        WSADATA wsadata;
        WSAStartup(2, &wsadata);
    }

    if (sscanf(port, "%d%n", &port_nr, &n) == 1 && port[n] == '\0') {
        nport = htons(port_nr);
    } else {
        servent = getservbyname(port, NULL);
        if (servent == NULL)
            return vp_stack_return_error(&_result, "getservbyname() error: %s",
                    port);
        nport = servent->s_port;
    }

    sock = socket(PF_INET, SOCK_STREAM, 0);
    hostent = gethostbyname(host);
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = nport;
    sockaddr.sin_addr = *((struct in_addr*)*hostent->h_addr_list);

    if (connect(sock, (struct sockaddr*)&sockaddr, sizeof(struct sockaddr_in))
            == -1)
        return vp_stack_return_error(&_result, "connect() error: %s",
                strerror(errno));

    vp_stack_push_num(&_result, "%d", sock);
    return vp_stack_return(&_result);
}

const char *
vp_socket_close(char *args)
{
    vp_stack_t stack;
    int sock;

    VP_RETURN_IF_FAIL(vp_stack_from_args(&stack, args));
    VP_RETURN_IF_FAIL(vp_stack_pop_num(&stack, "%d", &sock));

    if (closesocket(sock) == SOCKET_ERROR) {
        return vp_stack_return_error(&_result, "closesocket() error: %d",
                WSAGetLastError());
    }
    if (--sockets_number == 0)
    {
        WSACleanup();
    }
    return NULL;
}

const char *
vp_socket_read(char *args)
{
    vp_stack_t stack;
    int sock;
    int nr;
    int timeout;
    struct timeval tv;
    int n;
    char buf[VP_READ_BUFSIZE];
    fd_set fdset;

    VP_RETURN_IF_FAIL(vp_stack_from_args(&stack, args));
    VP_RETURN_IF_FAIL(vp_stack_pop_num(&stack, "%d", &sock));
    VP_RETURN_IF_FAIL(vp_stack_pop_num(&stack, "%d", &nr));
    VP_RETURN_IF_FAIL(vp_stack_pop_num(&stack, "%d", &timeout));
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout - tv.tv_sec * 1000) * 1000;

    FD_ZERO(&fdset);
    FD_SET((unsigned)sock, &fdset);
    vp_stack_push_str(&_result, ""); /* initialize */
    while (nr != 0) {
        n = select(0, &fdset, NULL, NULL, (timeout == -1) ? NULL : &tv);
        if (n == SOCKET_ERROR) {
            return vp_stack_return_error(&_result, "select() error: %d",
                    WSAGetLastError());
        } else if (n == 0) {
            /* timeout */
            break;
        }
        if (nr > 0)
            n = recv(sock, buf,
                    (VP_READ_BUFSIZE < nr) ? VP_READ_BUFSIZE : nr, 0);
        else
            n = recv(sock, buf, VP_READ_BUFSIZE, 0);
        if (n == -1) {
            return vp_stack_return_error(&_result, "recv() error: %s",
                    strerror(errno));
        } else if (n == 0) {
            /* eof */
            vp_stack_push_num(&_result, "%d", 1);
            return vp_stack_return(&_result);
        }
        /* decrease stack top for concatenate. */
        _result.top--;
        vp_stack_push_bin(&_result, buf, n);
        if (nr > 0)
            nr -= n;
        /* try read more bytes without waiting */
        timeout = 0;
    }
    vp_stack_push_num(&_result, "%d", 0);
    return vp_stack_return(&_result);
}

const char *
vp_socket_write(char *args)
{
    vp_stack_t stack;
    int sock;
    char *buf;
    size_t size;
    int timeout;
    struct timeval tv;
    size_t nleft;
    int n;
    fd_set fdset;

    VP_RETURN_IF_FAIL(vp_stack_from_args(&stack, args));
    VP_RETURN_IF_FAIL(vp_stack_pop_num(&stack, "%d", &sock));
    VP_RETURN_IF_FAIL(vp_stack_pop_bin(&stack, &buf, &size));
    VP_RETURN_IF_FAIL(vp_stack_pop_num(&stack, "%d", &timeout));
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout - tv.tv_sec * 1000) * 1000;

    FD_ZERO(&fdset);
    FD_SET((unsigned)sock, &fdset);
    nleft = 0;
    while (nleft < size) {
        n = select(0, NULL, &fdset, NULL, (timeout == -1) ? NULL : &tv);
        if (n == SOCKET_ERROR) {
            return vp_stack_return_error(&_result, "select() error: %d",
                    WSAGetLastError());
        } else if (n == 0) {
            /* timeout */
            break;
        }
        n = send(sock, buf + nleft, size - nleft, 0);
        if (n == -1)
            return vp_stack_return_error(&_result, "send() error: %s",
                    strerror(errno));
        nleft += n;
        /* try write more bytes without waiting */
        timeout = 0;
    }
    vp_stack_push_num(&_result, "%u", nleft);
    return vp_stack_return(&_result);
}

const char *
vp_open(char *args)
{
    vp_stack_t stack;
    char *path;
    
    VP_RETURN_IF_FAIL(vp_stack_from_args(&stack, args));
    VP_RETURN_IF_FAIL(vp_stack_pop_str(&stack, &path));
    
    if ((int)ShellExecute(NULL, "open", path, NULL, NULL, SW_SHOWNORMAL) < 32) {
        return vp_stack_return_error(&_result, "ShellExecute() error: %s",
                lasterror());
    }

    return NULL;
}

/* 
 * vim:set sw=4 sts=4 et:
 */
