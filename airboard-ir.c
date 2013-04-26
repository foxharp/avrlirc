/*
 *
 *
 * airboard.c, based on avrlirc2udp.c
 *
 * the latest copy of this program is probably available here:
 * http://tinyurl.com/airboard-ir  (leads to sourceforge CVS)
 *
 * this daemon accepts infrared (IR) data from an Airboard
 * keyboard, also known as an SK-7100, made by Silitek, or
 * LiteOn, and rebadged by Motorola and gateway, perhaps among
 * others.
 *
 * as sold, the airboard has its own IR receiver.  the receiver
 * works well enough, except that a) the keyboard is PS/2-only (a
 * "dumb" PS/2-to-USB adapter won't convert it), and worse, b),
 * the mouse is serial-only -- a serial to PS/2 adapter won't
 * help.  with the disappearance of serial ports, and even of PS/2
 * ports, this otherwise excellent keyboard is becoming obsolete. 
 * having recently purchased a USB-only computer, and having USB
 * IR receivers on hand (avrlirc), it made sense to write a
 * "driver" for this keyboard.
 *
 * data comes from an avrlirc device (specified with '-t'). 
 * perhaps it could also be read from other lirc compatible input
 * devices, but no others have been tried.  (for more information
 * on avrlirc, see <http://www.foxharp.boston.ma.us/avrlirc>.)
 *
 * output from this program goes to three separate destinations:
 *
 *  - if '-a' is given, keyboard and mouse events will be
 *    injected into the kernel using the uinput driver, where
 *    they will automatically become available to the rest of the
 *    system.
 *
 *  - if '-s' is also used, it specifies a destination for the
 *    "special" multimedia buttons; their names will be sent to
 *    the given host/port pair.  (otherwise they will be
 *    "injected", along with the rest of the keys.)
 *
 *  - consumer IR codes will be sent to the lircd host and
 *    port, specified with '-h' and '-p'.
 *
 * regarding the path of the consumer IR codes:  all of the
 * functionality of avrlirc2udp remains.  namely, this will
 * transfer characters from an avrlirc device on a serial port to
 * an lircd daemon running the udp "driver".  the output of the
 * arvlirc device matches the expected lircd input pretty much
 * exactly.  we try to read pairs of bytes using the VMIN
 * parameter of the tty driver, but other than that, it's mainly
 * a data copy.
 *
 * with '-g', if the blue Fn key is held, the joystick sends scrolling
 * events rather than motion events.
 *
 * invocation:  as a critical system service, this program should be
 * invoked from inittab, or from /etc/event.d on systems running
 * upstart (ubuntu, possibly modern fedora).  a sample invocation line
 * might be:
 *  /usr/local/bin/airboard -f -r -t /dev/ttyUSB0 -w 10 -a -g -h lircdhost
 * 
 * because the keyboard and mouse on a system are a somewhat critical
 * service, provision is made for assigning elevated scheduling
 * priority to this program (-r).
 *
 *
 **********
 *
 * Copyright (C) 2009, Paul G Fox
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
 * A copy of the GNU General Public License is appended at the bottom
 * of this file.
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <syslog.h>
#include <termios.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/stat.h>
#include <linux/input.h>
#include <linux/uinput.h>

#include <linux/input.h>

char *me;

void
usage(void)
{
    fprintf(stderr,
        "usage: %s [options] -t <ttydev>\n"
        "  tty options:\n"
        "    '-H' for high speed tty (115200 instead of 38400).\n"
        "    '-w <S>' to poll the creation of ttydev at S second intervals.\n"
        "  airboard options:\n"
        "    '-a' to include support for the airboard keyboard.\n"
        "    '-s <host>:<port>' to divert airboard multimedia keys to\n"
        "        the specified UDP socket.\n"
        "    '-m NN,NN,NN,NN' to specify mouse acceleration parameters.\n"
        "        All 4 levels must be specified.  Default is 1,5,10,25.\n"
        "    '-g' to enable grab scrolling (using blue 'Fn' key)\n"
        "  lircd options:\n"
        "    '-h <lircd_host>' to specify the lircd host for CIR decoding\n"
        "    '-p <lircd_port>] (defaults to 8765).\n"
        "    '-T' make a TCP connection for CIR rather than UDP (uncommon).\n"
        "  daemon options:\n"
        "    '-f' to keep program in foreground.\n"
        "    '-l' use syslog, rather than stderr, for messages.\n"
        "    '-r' to run with elevated (sched_fifo) scheduling priority.\n"
        "    '-d' for debugging (repeate for more verbosity).\n"
        "    '-X' don't forward any IR commands or keystrokes (for debug).\n"
        " (%s requires root privileges if -a or -r is used.)\n"
        , me, me);
    exit(1);
}

/* are we still in the foreground? */
int daemonized;

/* log to syslog (instead of stderr) */
int logtosyslog;

/* higher values for more debug */
int debug;

/* suppress any actual tranmission or injection of data */
int noxmit;

/* handle input from the airboark sk-7100 silitek (and
 * motorola and gateway rebadged) keyboard.
 */
int airboard;

/* send "special" keys from the airboard keyboard
 *  (i.e. the multimedia keys) to this socket instead
 *  to uinput (which is where the regular keys go).
 */
char *spec_host;
int spec_port;

/* mouse acceleration values.  should really be done by the
 * window system
 */
int mouse_speeds[] = {0, 1, 5, 10, 25};

/* special key (currently the blue "Fn" key) enables grab scrolling */
int do_grabscroll;
int scrolling;
#define SCROLL_QUANTUM 15  /* "distance" before we emulate a scroll button */
int cumul_x_scroll, cumul_y_scroll;

/* default port for reports to lircd */
#define LIRCD_UDP_PORT 8765


/* see the "keyboard protocol" and "mouse protocol" comment blocks
 * for descriptions of the actual binary reports.
 */
#define KEY_WORD_LEN 19
#define MOUSE_WORD_LEN 30

#define IR_UP_MASK 0x801
#define IR_REPEAT 0x656d5

#define IR_MOUSE_PREFIX 0x7e6
#define IR_MOUSE_PREFIX_LEN 11


typedef struct key_desc {
    long ir_code;       /* IR code the airboard sends (9 bits) */
    char *name;         /* for debug, and actually sent for special keys */
    int event_code;     /* the event code for the uinput subsystem */
    char type;          /* regular/mouse/special */
} key_desc_t;

#define NUM_KEYS 128

#define TYPE_MOUSE   1
#define TYPE_SPECIAL 2
#define TYPE_REPEAT  3
#define TYPE_ALL_UP  4
#define TYPE_GRAB    5

extern key_desc_t keys[NUM_KEYS];  /* declared below */

extern char *optarg;
extern int optind, opterr, optopt;

static int uinp_fd = -1;

static void
report(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    if (logtosyslog && debug <= 1) {
        vsyslog(LOG_NOTICE, fmt, ap);
    } else {
        fprintf(stderr, "%s: ", me);
        vfprintf(stderr, fmt, ap);
        fputc('\n', stderr);
    }
}

static void
dbg(int level, const char *fmt, ...)
{
    va_list ap;

    if (debug < level) return;

    va_start(ap, fmt);
    if (logtosyslog && debug <= 1) {
        vsyslog(LOG_NOTICE, fmt, ap);
    } else {
        fputc(' ', stderr);
        vfprintf(stderr, fmt, ap);
        fputc('\n', stderr);
    }
}

static void
dbgchar(int level, int c)
{
    if (debug < level) return;

    if (logtosyslog && debug <= 1) {
        syslog(LOG_NOTICE, "%c", c);  // shouldn't happen 
    } else {
        fputc(c, stderr);
    }
}

void
die(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    if (logtosyslog && debug <= 1) {
        vsyslog(LOG_ERR, fmt, ap);
        syslog(LOG_ERR, "exiting -- %m");
    } else {
        fprintf(stderr, "%s: ", me);
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, " - %s", strerror(errno));
        fputc('\n', stderr);
    }
    exit(1);
}

/* Manage the uinput device */

void
deinit_uinput_device(void)
{
    if (uinp_fd < 0) return;

    /* Destroy the input device */
    if (ioctl(uinp_fd, UI_DEV_DESTROY) < 0)
        die("destroy of uinput dev failed\n");

    /* Close the UINPUT device */
    close(uinp_fd);
    uinp_fd = -1;
}

int
setup_uinput(void)
{
    struct uinput_user_dev uinp;
    key_desc_t *keyp;
    int e;

    uinp_fd = open("/dev/input/uinput", O_WRONLY | O_NDELAY);
    if (uinp_fd < 0) {
        uinp_fd = open("/dev/uinput", O_WRONLY | O_NDELAY);
        if (uinp_fd < 0) {
            report(
                "Unable to open either /dev/input/uinput or /dev/uinput\n");
            return -1;
        }
    }

    atexit(deinit_uinput_device);

    memset(&uinp, 0, sizeof(uinp));     // Intialize the uInput device to NULL

    strncpy(uinp.name, "airboard IR driver", UINPUT_MAX_NAME_SIZE);
    uinp.id.version = 4;
    uinp.id.bustype = BUS_VIRTUAL;

    for (keyp = keys; keyp < &keys[sizeof(keys)/sizeof(keys[0])]; keyp++) {
        if (!keyp->event_code)
            continue;
        if (ioctl(uinp_fd, UI_SET_KEYBIT, keyp->event_code) < 0) {
            report("uinput setup failed, key '%s'\n", keyp->name);
            return -1;
        }
    }

    e = 0;
    if (!++e || ioctl(uinp_fd, UI_SET_EVBIT, EV_KEY) < 0 || // keys
        !++e || ioctl(uinp_fd, UI_SET_EVBIT, EV_REP) < 0 ||
        !++e || ioctl(uinp_fd, UI_SET_EVBIT, EV_REL) < 0 || // mouse
        !++e || ioctl(uinp_fd, UI_SET_RELBIT, REL_X) < 0 ||
        !++e || ioctl(uinp_fd, UI_SET_RELBIT, REL_Y) < 0 ||
        !++e || ioctl(uinp_fd, UI_SET_RELBIT, REL_WHEEL) < 0 ||
        !++e || ioctl(uinp_fd, UI_SET_RELBIT, REL_HWHEEL) < 0 ||
        !++e || write(uinp_fd, &uinp, sizeof(uinp)) < 0 ||  // device
        !++e || ioctl(uinp_fd, UI_DEV_CREATE) < 0) {
            report("uinput setup failed, step %d\n", e);
            return -1;
    }

    return 0;
}

void
input_event(unsigned int type, unsigned int code, int value)
{
    struct input_event event;

    memset(&event, 0, sizeof(event));
    gettimeofday(&event.time, NULL);

    event.type = type;
    event.code = code;
    event.value = value;
    if (write(uinp_fd, &event, sizeof(event)) < 0) {
        report("warning: input event failed, t/c/v 0x%x/0x%x/0x%x\n",
            type, code, value);
    }

}

void
send_a_motion(int x, int y)
{
    input_event(EV_REL, REL_X, x);
    input_event(EV_REL, REL_Y, y);
    input_event(EV_SYN, SYN_REPORT, 0);
}

void
send_a_scroll(int x, int y)
{
    if (x)  {
        dbg(1, "scroll %s", y > 0 ? "left" : "right");
        input_event(EV_REL, REL_HWHEEL, x > 0 ? -1 : 1);
    }
    if (y) {
        dbg(1, "scroll %s", y > 0 ? "up" : "down");
        input_event(EV_REL, REL_WHEEL, y > 0 ? -1 : 1);
    }
    input_event(EV_SYN, SYN_REPORT, 0);
}

void
send_a_key(int key_event_code, int down)
{
    input_event(EV_KEY, key_event_code, !!down);
    input_event(EV_SYN, SYN_REPORT, 0);
}


int socket_init(int tcp, char *host, int port)
{
    struct sockaddr_in sa[1];
    struct hostent *hent;
    int s;

    hent = gethostbyname(host);
    if (!hent) {
        report("gethostbyname failed for %s", host);
        return -1;
    }

    if (tcp) {
        if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0)
            die("tcp socket open");
    } else {
        if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
            die("udp socket open");
    }

    memset((char *) sa, 0, sizeof(*sa));
    sa->sin_family = AF_INET;
    sa->sin_port = htons(port);
    sa->sin_addr = *((struct in_addr *)hent->h_addr);
       
    if (connect(s, (struct sockaddr *)sa, sizeof(*sa)) < 0) {
        report("connect failed to %s:%d", host, port);
        close(s);
        return -1;
    }

    return s;
}

/* shared by tty_init()/tty_restore() */
struct termios prev_tios;
int tty_fd;

void
tty_restore(void)
{
    if (tty_fd >= 0)
        tcsetattr(tty_fd, TCSADRAIN, &prev_tios);
}

void
sighandler(int sig)
{
    tty_restore();
    deinit_uinput_device();
    die("got signal %d", sig);
}

int tty_init(char *term, int wait_term, int speed)
{
    int s;
    struct termios tios;
    int flags;
    long fflags;

    int logged = 0;

    while(1) {
        tty_fd = open(term, O_RDWR|O_NDELAY);
        if (tty_fd >= 0 || errno != ENOENT || !wait_term)
            break;

        if (!logged)
            report("waiting for tty creation");

        logged = 1;

        sleep(wait_term);
    }

    if (logged)
        report("found tty");

    if (tty_fd < 0)
        die("can't open tty '%s'", term);

    if (!isatty(tty_fd))
        die("%s is not a tty", term);

    fflags = fcntl(tty_fd, F_GETFL );
    fcntl (tty_fd, F_SETFL, fflags & ~O_NDELAY );

    s = tcgetattr(tty_fd, &prev_tios);
    if (s < 0)
        die("tcgetattr on %s", term);

    /* set up restore hooks quickly */
    atexit(tty_restore);

    tios = prev_tios;

    tios.c_oflag = 0;   /* no output flags at all */
    tios.c_lflag = 0;   /* no line flags at all */

    tios.c_cflag &= ~PARENB;    /* disable parity, both in and out */
    tios.c_cflag |= CSTOPB|CLOCAL|CS8|CREAD;   /* two stop bits on transmit */
                                            /* no modem control, 8bit chars, */
                                            /* receiver enabled, */

    tios.c_iflag = IGNBRK | IGNPAR;    /* ignore break, ignore parity errors */

    tios.c_cc[VMIN] = 2;
    tios.c_cc[VTIME] = 0;
    tios.c_cc[VSUSP]  = _POSIX_VDISABLE;
    tios.c_cc[VSTART] = _POSIX_VDISABLE;
    tios.c_cc[VSTOP]  = _POSIX_VDISABLE;

    s = cfsetspeed (&tios, speed);
    if (s < 0)
        die("cfsetspeed on %s", term);
    s = tcsetattr(tty_fd, TCSAFLUSH, &tios);
    if (s < 0)
        die("tcsetattr on %s", term);

    /* make sure RTS and DTR lines are high, since device may be
     * phantom-powered.  no termios/posix way to do this, that i
     * know of.
     */
    if (ioctl(tty_fd, TIOCMGET, &flags) >= 0) {
        flags &= ~TIOCM_RTS;
        flags &= ~TIOCM_DTR;
        ioctl(tty_fd, TIOCMSET, &flags);
    }

    return tty_fd;
}

int
timed_read(int from, unsigned char *buf, int n, int block)
{
    if (block) {
        return read(from, buf, n);
    } else {
        int ret;
        fd_set readfd;
        struct timeval to;

        FD_ZERO(&readfd);
        FD_SET(from, &readfd);
        to.tv_sec = 0;
        to.tv_usec = 20 * 1000;  // 20ms
        ret = select(from+1, &readfd, 0, &readfd, &to);
        if (ret == 0)
            return -2;  // timeout
        return read(from, buf, n);
    }
}

void send_hotkey(char *name)
{
    static int s = -1;
    static char keyname[25];

    if (s < 0)
        s = socket_init(0, spec_host, spec_port);

    if (s < 0) {
        report("failed to init hotkey socket");
        return;
    }

    strncpy(keyname, name, sizeof(keyname)-2);
    keyname[sizeof(keyname)-1] = '\0';
    strcat(keyname, "\n");
    if (write(s, keyname, strlen(keyname)) < 0) {
        close(s);
        s = -1;
    }
}

void set_scrolling(void)
{
    dbg(1, "scrolling");
    scrolling = 1;
}

void reset_scrolling(void)
{
    dbg(1, "NOT scrolling");
    scrolling = cumul_x_scroll = cumul_y_scroll = 0;
}

/*
 *  keyboard protocol:
 *
 *  keypress reports are 19 bits long.  they are preceded by a 0
 *  start bit, and are (usually) followed by a trailing stop bit. 
 *  (the stop bit will be missing if two keycodes arrive in
 *  direct succession, such as a "key-up" report immediately
 *  followed by "all-up" they are essentially fully identified
 *  after 7 bits -- the rest are redundancy, and press/release
 *  bits.
 *
 *     18                     0
 *      III|iiii|d110|IIIe|eeeu
 *
 *  - bits 12-18 (the top 7) are the keycode ("IIIiiii").
 *  - bit 11 ('d') is 1 for "press", 0 for "release".
 *  - bits 8-10 are a constant pattern, 110.
 *  - bits 5-7 are a repeat of the upper 3 bits of the keycode ("III").
 *  - bits 1-4 ('eeee') are the value "eeee = (15 - iiii)".
 *  - bit 0 ('u') is 0 for press, 1 for release.
 *
 *  however, since we don't get a bit at a time (we get a variable
 *  number at a time) it's easiest to just put all 19 bits of the
 *  keycode into the lookup table.  (besides, that's how the code
 *  was initially written, before the keycode format was fully decoded.)
 *
 */

key_desc_t *lookup_key(long ir_code)
{

#if DO_FULL_SANITY_CHECK
/* not needed, since all the bits are in the table */

    int I1, I2, i, e, d, u;

    if ((ir_code >> 12) > 0x7f) {
        dbg(3, "keycode out of range");
        return 0;
    }

    if ((ir_code & 0x700) != 0x600) {
        dbg(3, "keycode bad bits");
        return 0;
    }

    I1 = (ir_code >> 16) & 0x7;
    I2 = (ir_code >> 5) & 0x7;
    if (I1 != I2) {
        dbg(3, "keycode III mismatch");
        return 0;
    }

    i = (ir_code >> 12) & 0xf;
    e = (ir_code >> 1) & 0xf;
    if (e != 15 - i) {
        dbg(3, "keycode iiii/eeee mismatch");
        return 0;
    }

    d = (ir_code >> 11) & 1;
    u = ir_code & 1;
    if ((u ^ d) != 1) {
        dbg(3, "keycode u/d mismatch");
        return 0;
    }

    return &keys[ir_code >> 12];

#else
    key_desc_t *keyp = &keys[ir_code >> 12];

    if ((keyp->ir_code == ir_code) ||
        (keyp->ir_code == (ir_code ^ IR_UP_MASK ))) {
        return keyp;
    }

    return 0;
#endif

}

void emitkey(long ir_code)
{
#define N_KEYS_PRESSED 21  // all fingers + all toes + 1
    static key_desc_t *key_pressed[N_KEYS_PRESSED];
    key_desc_t *keyp;
    int i;

    dbg(1, " 0x%05lx", ir_code);

    keyp = lookup_key(ir_code); 
    if (!keyp) return;

    if (keyp->type == TYPE_ALL_UP) {
        /* cancel all outstanding keypresses, for safety */
        for (i = 0; i < N_KEYS_PRESSED; i++) {
            if (key_pressed[i]) {
                if (!noxmit)
                    send_a_key(key_pressed[i]->event_code, 0);
                key_pressed[i] = 0;
            }
        }

        reset_scrolling();

    } else if (keyp->type == TYPE_REPEAT) {
        /* ignore the repeat code */ ;

    } else if (keyp->ir_code == ir_code) {

        dbg(1, "%s pressed", keyp->name);

        if (do_grabscroll && keyp->type == TYPE_GRAB) {
            set_scrolling();
        } else if (!noxmit) {
            if (spec_host && keyp->type == TYPE_SPECIAL)
                send_hotkey(keyp->name);
            else
                send_a_key(keyp->event_code, 1);
        }

        /* record what's pressed */
        for (i = 0; i < N_KEYS_PRESSED; i++) {
            if (!key_pressed[i]) {
                key_pressed[i] = keyp;
            }
        }

    } else if (keyp->ir_code == (ir_code ^ IR_UP_MASK)) {

        dbg(1, "%s released", keyp->name);

        if (do_grabscroll && keyp->type == TYPE_GRAB) {
            reset_scrolling();
        } else if (!noxmit) {
            if (spec_host && keyp->type == TYPE_SPECIAL)
                ;
            else
                send_a_key(keyp->event_code, 0);
        }

        /* keep track of what's been released */
        for (i = 0; i < N_KEYS_PRESSED; i++) {
            if (key_pressed[i] == keyp) {
                key_pressed[i] = 0;
                break;
            }
        }

    } else {
        dbg(1, "%s: key lookup error: 0x%lx", me, ir_code);
    }

}

/*
 *  mouse protocol:
 *
 * mouse motion reports are 30 bits long.  they are preceded by a
 * 0 start bit, and are (sometimes) followed by a trailing stop
 * bit.  the stop bit may not always be present.  each report
 * contains both x and y motion information.  mouse buttons are
 * not in the mouse motion reports -- they're reported as
 * keypresses.
 *
 *  30                                   0
 *   11|1111|0011|0xxx|xxXX|X110|yyyy|yYYY
 *
 *  - bits 19-30 are the constant prefix 0x7e6.
 *  - bits 14-18 are the X motion value.
 *  - bits 11-13 are the X direction.  ("111" == left, "000" == right)
 *  - bits 8-10 are the constant "110".
 *  - bits 3-7 are the Y motion value.
 *  - bits 0-2 are the Y direction.  ("111" == up, "000" == down)
 *
 *  both the X and Y motion values ("xxxxx" and "yyyyy") work the same.
 *  the least significant bits are to the left, the most significant
 *  are to the right (i.e., backwards).  when traveling in a negative
 *  direction (up or to the left), the motion value should be inverted.
 *      0   4
 *      mmmmm
 *
 *  the "mmmmm" values reported are not very accurate.  the
 *  observed speed levels are:
 *      speed 4:  bit 4 set -- fastest
 *      speed 3:  bit 3, plus any of 0-3 
 *      speed 2:  bit 3
 *      speed 1:  any of bits 0-2 (they're erratic, and don't count nicely)
 *      no motion: all bits zero
 *
 */

int
motion_fixup(int raw, int *lastspeedp)
{
    int mdir, n, goal, last;

    mdir = ((raw & 7) == 7) ? -1 : 1;
    raw >>= 3;
    if (mdir < 0) raw = ~raw & 0x1f;

    n = 0;
    if (raw & 0x01) {
        n = 4;
    } else if (raw & 0x02) {
        n = 2;
        if (raw & 0x1c) 
            n = 3;
    } else if ((raw & 0x1c) || mdir < 0) {
        n = 1;
    }

    /* map the reported levels to better values */
    goal = mouse_speeds[n];

    /* transition between speeds gently -- only change speed by 1
     * for each mouse packet received.
     */
    last = *lastspeedp;
    dbg(2, "r 0x%x, n %d, g %d, last %d", raw, n, goal, last);
    if (goal == 0) last = 0;
    else if (last > goal * 2)   last -= 2;
    else if (last > goal)       last -= 1;
    else if (last < goal * 2)   last += 2;
    else if (last < goal)       last += 1;
    *lastspeedp = last;

    return last * mdir;
}

void emitmouse(long mousecode, int *last_y_motion)
{
    int x, y;
    int nx, ny;
    static int lastxspeed, lastyspeed;

    if ((mousecode & 0x700) != 0x600)
        return;

    x = (mousecode >> 11) & 0xff;
    y = mousecode & 0xff;
    dbgchar(2, '\n');
    nx = motion_fixup(x, &lastxspeed);
    ny = motion_fixup(y, &lastyspeed);
    dbg(1, " mouse x,y 0x%02x,0x%02x %2d,%2d", x,y, nx,ny);

    if (scrolling) {
        dbg(2, "would scroll");

        cumul_x_scroll += nx;
        if (abs(cumul_x_scroll) > SCROLL_QUANTUM) {
                send_a_scroll(cumul_x_scroll, 0);
                cumul_x_scroll = 0;
        }
        
        cumul_y_scroll += ny;
        if (abs(cumul_y_scroll) > SCROLL_QUANTUM) {
                send_a_scroll(0, cumul_y_scroll);
                cumul_y_scroll = 0;
        }

    } else if (!noxmit) {
            send_a_motion(nx, ny);
    }

    *last_y_motion = ny;
}


void
data_loop(int from, int tcp, char *lircdhost, int lircdport)
{
    unsigned char b[2];
    int n;
    int prevhilo = -1;
    int pulse;
    long time = 0;
    int bits = 0, hilo = 0, totbits = 0;
    long bitaccum = 0;
    int in_gap = 1;
    int wordlen = KEY_WORD_LEN;
    int block = 1;
    int last_y_motion;
    int phase_err_count = 0;
    static int to = -1;

    setbuf(stdout, NULL);  // for timely debug messages

    while (1) {

        /* leave extra space in buf for extra bytes we may read below */
        if ((n = timed_read(from, b, 2, block)) == -1)
            die("timed_read");

        /* in my experience, this results from a USB serial
         * device being unplugged */
        if (n == 0)
            return;

        if (n == 1) { /* short read -- force it even */
            if (timed_read(from, &b[1], 1, 1) == -1)
                die("timed_read");
            n++;
        }

        block = 1;

        if (n > 0) {
            pulse = (b[1] << 8) + b[0];
            hilo = pulse & 0x8000;
            time = pulse & 0x7fff;

            dbg(4, "%s: %s 0x%04x (%ld) (%ldus)\n", me,
                    hilo ? "pulse" : "space",
                    pulse, time, 1000000 * time / 16384);

            if (hilo == prevhilo) {
                /*
                 * if we somehow start our reads "halfway" through one
                 * of the 16 bit data words, we'll forever be
                 * out-of-sync.  we try to correct this by noticing
                 * two things:  a) if the data contains two zero bytes
                 * in succession, then they're definitely a pair
                 * (since this is the out-of-band data prefix), and b)
                 * the 16 bit values that we read should have
                 * alternating high bits:  0x8000, 0x0000, 0x8000,
                 * etc.
                 */
                if (phase_err_count++ > 10) {
                    report("too many phase corrections, re-opening tty");
                    return;
                }
                report("phase correction");
                b[0] = b[1];
                if (timed_read(from, &b[1], 1, 1) <= 0)
                    die("timed_read");
                hilo = (b[1] & 0x80);
            }
            prevhilo = hilo;
        }

        /* send lircd data to lircdhost */
        if (!noxmit && lircdhost)  {
            if (to < 0)
                to = socket_init(tcp, lircdhost, lircdport);
            if (to >= 0) {
                if (write(to, b, 2) < 0) {
                    if (errno != ECONNREFUSED)
                        die("write");
                    close(to);
                    to = -1;
                }
            }
        }

        if (airboard) {
            if (n > 0) {
#define BITTIME 13653   /*  1200 baud:  1/1200 * 16384 * 1000 */
                // report("h:%d t:%d ", hilo, time);
                bits = ((1000 * time) + BITTIME/2) / BITTIME;
                if (in_gap && hilo) { // skip marking during gap
                    // while (bits--)
                    dbgchar(2, 'S');
                    continue;
                } else if (in_gap && !hilo)  {
                    if (bits > 0) bits--;  // skip the start bit
                    dbgchar(2,'\t');
                    dbgchar(2,'s');
                    totbits = 0;
                }
                in_gap = 0;
            } else {
                // timeout -- fill in with 1's
                bits = wordlen - totbits;
                hilo = 1;
                in_gap = 1;
                dbgchar(2,'f');
            }

            if (hilo && bits > 12) {
                if (totbits > 2) {
                    bits = wordlen - totbits;
                    //report(".0x%05llx", bitaccum);
                } else if (totbits) {
                    bitaccum = 0;
                    totbits = 0;
                    continue;
                } else {
                    continue;
                }
            }

            while (bits--) {
                totbits++;
                dbgchar(2, hilo ? '1':'0');
                bitaccum = (bitaccum << 1) | (hilo ? 1 : 0);
                if (totbits == IR_MOUSE_PREFIX_LEN) {
                    if (bitaccum == IR_MOUSE_PREFIX)
                        wordlen = MOUSE_WORD_LEN;
                    else
                        wordlen = KEY_WORD_LEN;
                }
                // report(" 0x%llx", bitaccum);
                if (totbits == wordlen) {
                    if (wordlen == MOUSE_WORD_LEN)
                        emitmouse(bitaccum, &last_y_motion);
                    else
                        emitkey(bitaccum);
                    bitaccum = 0;
                    totbits = 0;
                    in_gap = 1;
                    if (hilo && bits) {
                        // report("t%d-", bits);
                        bits = 0;
                        in_gap = 1;
                    }
                }
            }

            if (!hilo) { /* we're looking for 1's next */
                /* because we get bit data in pairs of ones/zeros,
                 * and because the "resting" state of the transmission
                 * is ones (and the stop-bit of the "word" is a one),
                 * we won't get informed of trailing ones in our word
                 * until the _next_ word comes along.
                 */
                if (wordlen == MOUSE_WORD_LEN) {
                    /* we may be waiting for the end of a mouse
                     * packet with trailing 1's.  since we don't know
                     * what the trailing data might be, we force
                     * a timeout on the next read.
                     * we know from experiment that this can't happen
                     * unless we're travelling vertically upward, and
                     * we're within 8 bits of the end of the 30.  breaking
                     * from the read early can have bad effects otherwise,
                     * so we want to minimize doing it.
                     */
                    if (totbits >= 22 && last_y_motion  < 0)
                        block = 0;
                } else {
                    /* if we're waiting for a keycode, we simply do
                     * our match on what we have so far, filling in
                     * the missing ones ourself.
                     */
                    if (totbits >= 11) {
                        static int lowbits[] = { 0,
                            0x1, 0x3, 0x7, 0xf, 0x1f, 0x3f, 0x7f, 0xff
                        };
                        int needbits = wordlen - totbits;
                        int have = bitaccum << needbits | lowbits[needbits];
                        key_desc_t *keyp = lookup_key(have); 
                        if (keyp) {
                            if (keyp->ir_code == have ||
                                (keyp->ir_code ^ IR_UP_MASK) == have) {
                                dbgchar(2,'e');
                                emitkey(have);
                                bitaccum = 0;
                                totbits = 0;
                                in_gap = 1;
                            }
                        }
                    }
                }
            }
        }
    }
}

int
main(int argc, char *argv[])
{
    char *p;
    char *lircdhost = 0, *term = 0;
    int lircdport = LIRCD_UDP_PORT;
    int foreground = 0;
    int realtime = 0;
    int wait_term = 0;
    int tcp = 0;
    int speed = B38400;
    int tty = -1;
    int got_speeds = 0;
    int c;

    me = argv[0];
    p = strrchr(argv[0], '/');
    if (p) me = p + 1;

    while ((c = getopt(argc, argv, "t:Hw:flrdXh:p:Tas:m:g")) != EOF) {
        switch (c) {

        /* tty options */
        case 't':
            term = optarg;
            break;
        case 'H':
            speed = B115200;
            break;
        case 'w':
            wait_term = atoi(optarg);
            if (wait_term == 0)
                usage();
            break;

        /* daemon options */
        case 'f':
            foreground = 1;
            break;
        case 'l':
            logtosyslog = 1;
            break;
        case 'r':
            realtime = 1;
            break;
        case 'd':
            debug++;   // if > 1, syslog will not be used
            break;
        case 'X':
            noxmit = 1;
            break;


        /* lircd options */
        case 'h':
            lircdhost = optarg;
            break;
        case 'p':
            lircdport = atoi(optarg);
            break;
        case 'T':
            /* this is unlikely necessary, but the author had a
             * system which couldn't transmit UDP.  a tcp-to-udp
             * gateway passed the packets to lircd on the far end:
             *   #!/bin/sh
             *   while true ; do
             *       nc -l -p 8807
             *   done | nc -u localhost 8765
             */
            tcp = 1;
            break;

        /* airboard options */
        case 'a':
            airboard = 1;
            break;
        case 's':
            spec_host = strdup(optarg);
            spec_port = 0;
            p = strchr(spec_host, ':');
            if (p && p[1]) {
                spec_port = atoi(&p[1]);
                *p = '\0';
            }
            if (!spec_port) {
                fprintf(stderr,
                    "%s: both host and port must be specified for '-s'\n", me);
                usage();
            }
            break;

        case 'm':
            {   int a, b, c, d;
                if (sscanf(optarg, "%d,%d,%d,%d", &a, &b, &c, &d) != 4) {
                    fprintf(stderr,
                        "%s: bad format for mouse acceleration values.\n", me);
                    usage();
                }
                mouse_speeds[1] = a;
                mouse_speeds[2] = b;
                mouse_speeds[3] = c;
                mouse_speeds[4] = d;
                got_speeds = 1;
            }
            break;

        case 'g':
            do_grabscroll = 1;
            break;

        default:
            usage();
            break;
        }
    }

#if KEYCODE_RESEARCH
    key_desc_t *keyp;
    for (keyp = keys; keyp < &keys[sizeof(keys)/sizeof(keys[0])]; keyp++) {
        int ir = keyp->ir_code;

        if (!ir) continue;
        report("%02x:  %05x  %02x", ir >> 12, ir, (ir >> 1) & 0x7f);

        int b = ir >> 12;
        int I = b & 0xf0;
        int i = b & 0x0f;
        int n;
        // int d = 1;
        // int u = 0;

        n = ((I | i) << 12) | (0xe << 8) | (I << 1) | ((15 - i) << 1) | 0;
        report(" %05x", n);

        if (n != ir) report("fail");
    }
    exit(0);
#endif

    if ((spec_host || got_speeds) && !airboard) {
        fprintf(stderr,
            "%s: using '-s' or '-m' without '-a' makes no sense.\n", me);
        usage();
    }

    if (!term || optind != argc) {
        usage();
    }

    report("starting airboard-ir %s", VERSION);

    /* initialize uinput, if needed */
    if (airboard && !noxmit && setup_uinput() < 0)
        die("%s: unable to find uinput device\n", me);

    /* do the initial tty open here, so access/existence is checked
     * before daemonize or muck with the scheduler.
     */
    tty = tty_init(term, wait_term, speed);

    signal(SIGTERM, sighandler);
    signal(SIGHUP, sighandler);
    signal(SIGINT, sighandler);
    signal(SIGQUIT, sighandler);
    signal(SIGABRT, sighandler);
    signal(SIGUSR1, sighandler);
    signal(SIGUSR2, sighandler);

    if (realtime) {
        struct sched_param sparam;
        int min, max;

        /* first, lock down all our memory */
        long takespace[1024];
        memset(takespace, 0, sizeof(takespace)); /* force paging */
        if (mlockall(MCL_CURRENT|MCL_FUTURE) < 0)
            die("unable to mlockall");

        /* then, raise our scheduling priority */
        min = sched_get_priority_min(SCHED_FIFO);
        max = sched_get_priority_max(SCHED_FIFO);

        sparam.sched_priority = (min + max)/2; /* probably always 50 */
        if (sched_setscheduler(0, SCHED_FIFO, &sparam))
            die("unable to set SCHED_FIFO");

        report("memory locked, scheduler priority set");

    }

    /* ordered in this way so that we daemonize after getting the tty
     * and setting realtime, the first time through.
     */
    if (!foreground && !debug) {
        if (daemon(0, 0) < 0)
            die("failed to daemonize");
        daemonized = 1;
    }

    while (1) {

        if (tty < 0)
            tty = tty_init(term, wait_term, speed);

        data_loop(tty, tcp, lircdhost, lircdport);

        /* we'll only ever return from data_loop() if our read()
         * returns 0, which usually means our (USB-based) tty has
         * gone away.  loop if we were told to wait (-w) for it.
         */
        if (!wait_term)
            die("end-of-dataloop");

        tty_restore();
        close(tty);
        tty = -1;
    }

    return 0;
}


key_desc_t keys[NUM_KEYS] = {

    { 0x00e1e, "up",            KEY_UP,         0 },
    { 0x01e1c, "key_e",         KEY_E,          0 },
    { 0x02e1a, "f4",            KEY_F4, 0 },
    { 0x03e18, "key_n",         KEY_N,          0 },
    { 0x04e16, "video",         KEY_VIDEO,      TYPE_SPECIAL },
    { 0x05e14, "key_2",         KEY_2,          0 },
    { 0x00000, "",              0,              0 },
    { 0x07e10, "key_g",         KEY_G,          0 },
    { 0x00000, "",              0,              0 },
    { 0x09e0c, "l_bracket",     KEY_LEFTBRACE,  0 },
    { 0x0ae0a, "f12",           KEY_F12,        0 },
    { 0x0be08, "volup",         KEY_VOLUMEUP,   TYPE_SPECIAL },
    { 0x00000, "",              0,              0 },
    { 0x0de04, "key_0",         KEY_0,          0 },
    { 0x0ee02, "right_joy_but", BTN_RIGHT,      TYPE_MOUSE },
    { 0x0fe00, "enter",         KEY_ENTER,      0 },
    { 0x10e3e, "rightmeta",     KEY_RIGHTMETA,  0 },
    { 0x11e3c, "key_u",         KEY_U,          0 },
    { 0x12e3a, "f8",            KEY_F8, 0 },
    { 0x13e38, "slash",         KEY_SLASH,      0 },
    { 0x14e36, "pause",         KEY_PLAYPAUSE,  TYPE_SPECIAL },
    { 0x15e34, "key_6",         KEY_6,          0 },
    { 0x00000, "",              0,              0 },
    { 0x17e30, "key_l",         KEY_L,          0 },
    { 0x00000, "",              0,              0 },
    { 0x19e2c, "key_a",         KEY_A,          0 },
    { 0x00000, "",              0,              0 },
    { 0x1be28, "display",       KEY_DISPLAYTOGGLE,      TYPE_SPECIAL },
    { 0x1ce26, "left",          KEY_LEFT,       0 },
    { 0x1de24, "backspace",     KEY_BACKSPACE,  0 },
    { 0x1ee22, "left_joy_but",  BTN_LEFT,       TYPE_MOUSE },
    { 0x1fe20, "key_x",         KEY_X,          0 },
    { 0x20e5e, "end",           KEY_END,        0 },
    { 0x21e5c, "key_q",         KEY_Q,          0 },
    { 0x22e5a, "f2",            KEY_F2, 0 },
    { 0x23e58, "key_v",         KEY_V,          0 },
    { 0x24e56, "close",         KEY_CLOSE,      TYPE_SPECIAL },
    { 0x25e54, "backtick",      KEY_GRAVE,      0 },
    { 0x00000, "",              0,              0 },
    { 0x27e50, "key_d",         KEY_D,          0 },
    { 0x28e4e, "right",         KEY_RIGHT,      0 },
    { 0x29e4c, "key_o",         KEY_O,          0 },
    { 0x2ae4a, "f10",           KEY_F10,        0 },
    { 0x2be48, "r_shift",       KEY_RIGHTSHIFT, 0 },
    { 0x2ce46, "stop",          KEY_STOP,       TYPE_SPECIAL },
    { 0x2de44, "key_8",         KEY_8,          0 },
    { 0x00000, "",              0,              0 },
    { 0x2fe40, "apostrophe",    KEY_APOSTROPHE, 0 },
    { 0x30e7e, "pgup",          KEY_PAGEUP,     0 },
    { 0x31e7c, "key_t",         KEY_T,          0 },
    { 0x32e7a, "f6",            KEY_F6, 0 },
    { 0x33e78, "comma",         KEY_COMMA,      0 },
    { 0x34e76, "u_p",   KEY_MACRO,              TYPE_SPECIAL },
    { 0x35e74, "key_4",         KEY_4,          0 },
    { 0x00000, "",              0,              0 },
    { 0x37e70, "key_j",         KEY_J,          0 },
    { 0x00000, "",              0,              0 },
    { 0x39e6c, "backslash",     KEY_BACKSLASH,  0 },
    { 0x3ae6a, "scroll_lock",   KEY_SCROLLLOCK, 0 },
    { 0x3be68, "space",         KEY_SPACE,      0 },
    { 0x3ce66, "voldown",       KEY_VOLUMEDOWN, TYPE_SPECIAL },
    { 0x3de64, "equal",         KEY_EQUAL,      0 },
    { 0x3ee62, "hold_joy_but",  BTN_MIDDLE,     TYPE_MOUSE },
    { 0x00000, "",              0,              0 },
    { 0x40e9e, "fn",            KEY_FN,         TYPE_GRAB },
    { 0x41e9c, "key_w",         KEY_W,          0 },
    { 0x42e9a, "f3",            KEY_F3, 0 },
    { 0x43e98, "key_b",         KEY_B,          0 },
    { 0x44e96, "cd",            KEY_CD,         TYPE_SPECIAL },
    { 0x45e94, "key_1",         KEY_1,          0 },
    { 0x00000, "",              0,              0 },
    { 0x47e90, "key_f",         KEY_F,          0 },
    { 0x48e8e, "numlock",       KEY_NUMLOCK,    0 },
    { 0x49e8c, "key_p",         KEY_P,          0 },
    { 0x4ae8a, "f11",           KEY_F11,        0 },
    { 0x4be88, "ctrl",          KEY_LEFTCTRL,   0 },
    { 0x4ce86, "next",          KEY_NEXT,       TYPE_SPECIAL },
    { 0x4de84, "key_9",         KEY_9,          0 },
    { 0x00000, "",              0,              0 },
    { 0x00000, "",              0,              0 },
    { 0x50ebe, "pgdn",          KEY_PAGEDOWN,   0 },
    { 0x51ebc, "key_y",         KEY_Y,          0 },
    { 0x52eba, "f7",            KEY_F7, 0 },
    { 0x53eb8, "period",        KEY_DOT,        0 },
    { 0x54eb6, "prev",          KEY_PREVIOUS,   TYPE_SPECIAL },
    { 0x55eb4, "key_5",         KEY_5,          0 },
    { 0x00000, "",              0,              0 },
    { 0x57eb0, "key_k",         KEY_K,          0 },
    { 0x00000, "",              0,              0 },
#if PERSONAL_HACKS
    { 0x59eac, "capcontrol",    KEY_LEFTCTRL,   0 },
    { 0x5aeaa, "fake btn",      BTN_MIDDLE,     TYPE_MOUSE },
#else
    { 0x59eac, "capslock",      KEY_CAPSLOCK,   0 },
    { 0x5aeaa, "pause_break",   KEY_PAUSE,      0 },
#endif
    { 0x5bea8, "r_alt",         KEY_RIGHTALT,   0 },
    { 0x5cea6, "leftmeta",      KEY_LEFTMETA,   0 },
    { 0x5dea4, "all up!",       0,              TYPE_ALL_UP },
    { 0x5eea2, "esc",           KEY_ESC,        0 },
    { 0x5fea0, "key_z",         KEY_Z,          0 },
    { 0x60ede, "home",          KEY_HOME,       0 },
    { 0x61edc, "tab",           KEY_TAB,        0 },
    { 0x62eda, "key_f1",        KEY_F1, 0 },
    { 0x63ed8, "key_c",         KEY_C,          0 },
    { 0x64ed6, "r_ctrl",        KEY_RIGHTCTRL,  0 },
    { 0x65ed4, "repeatcode",    0,              TYPE_REPEAT },
    { 0x00000, "",              0,              0 },
    { 0x67ed0, "key_s",         KEY_S,          0 },
    { 0x68ece, "menu",          KEY_MENU,       0 },
    { 0x69ecc, "key_i",         KEY_I,          0 },
    { 0x6aeca, "f9",            KEY_F9, 0 },
    { 0x6bec8, "mute",          KEY_MUTE,       TYPE_SPECIAL },
    { 0x6cec6, "play",          KEY_PLAY,       TYPE_SPECIAL },
    { 0x6dec4, "key_7",         KEY_7,          0 },
    { 0x00000, "",              0,              0 },
    { 0x6fec0, "semicolon",     KEY_SEMICOLON,  0 },
    { 0x70efe, "down",          KEY_DOWN,       0 },
    { 0x71efc, "key_r",         KEY_R,          0 },
    { 0x72efa, "f5",            KEY_F5, 0 },
    { 0x73ef8, "key_m",         KEY_M,          0 },
    { 0x74ef6, "www",           KEY_WWW,        TYPE_SPECIAL },
    { 0x75ef4, "key_3",         KEY_3,          0 },
    { 0x00000, "",              0,              0 },
    { 0x77ef0, "key_h",         KEY_H,          0 },
    { 0x00000, "",              0,              0 },
    { 0x79eec, "r_bracket",     KEY_RIGHTBRACE, 0 },
    { 0x7aeea, "prtsc_sysrq",   KEY_PRINT,      0 },
    { 0x7bee8, "l_alt",         KEY_LEFTALT,    0 },
    { 0x7cee6, "del",           KEY_DELETE,     0 },
    { 0x7dee4, "minus",         KEY_MINUS,      0 },
    { 0x00000, "",              0,              0 },  /* 0x7e, mouse prefix */
    { 0x7fee0, "l_shift",       KEY_LEFTSHIFT,  0 },

};
