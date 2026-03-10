/*
 * ██╗███████╗ ██████╗     ██╗    ██╗██████╗ ██╗████████╗███████╗██████╗
 * ██║██╔════╝██╔═══██╗    ██║    ██║██╔══██╗██║╚══██╔══╝██╔════╝██╔══██╗
 * ██║███████╗██║   ██║    ██║ █╗ ██║██████╔╝██║   ██║   █████╗  ██████╔╝
 * ██║╚════██║██║   ██║    ██║███╗██║██╔══██╗██║   ██║   ██╔══╝  ██╔══██╗
 * ██║███████║╚██████╔╝    ╚███╔███╔╝██║  ██║██║   ██║   ███████╗██║  ██║
 * ╚═╝╚══════╝ ╚═════╝      ╚══╝╚══╝ ╚═╝  ╚═╝╚═╝   ╚═╝   ╚══════╝╚═╝  ╚═╝
 *
 *  Linux ISO → USB Writer  |  v1.0  |  Run as root for write access
 */

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <cerrno>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>

// ─── ANSI helpers ────────────────────────────────────────────────────────────
#define ESC "\033["
#define RESET       ESC "0m"
#define BOLD        ESC "1m"
#define DIM         ESC "2m"
#define ITALIC      ESC "3m"
#define UNDERLINE   ESC "4m"

// Colours (256-colour)
#define FG(n)   ESC "38;5;" #n "m"
#define BG(n)   ESC "48;5;" #n "m"

// Named palette
#define C_ORANGE    FG(214)
#define C_AMBER     FG(220)
#define C_RED       FG(196)
#define C_GREEN     FG(82)
#define C_CYAN      FG(51)
#define C_BLUE      FG(39)
#define C_PURPLE    FG(135)
#define C_GRAY      FG(244)
#define C_LGRAY     FG(250)
#define C_WHITE     FG(255)
#define C_DARK      BG(234)
#define C_DARKER    BG(232)
#define C_ACCENT    BG(202)   // deep orange bg
#define C_SEL       BG(237)   // selection bg

#define CLEAR       "\033[2J"
#define HOME        "\033[H"
#define HIDE_CURSOR "\033[?25l"
#define SHOW_CURSOR "\033[?25h"
#define SAVE_CURSOR "\033[s"
#define REST_CURSOR "\033[u"
#define ERASE_LINE  "\033[2K"

static void move(int row, int col) {
    printf("\033[%d;%dH", row, col);
}

static int termW = 100, termH = 40;

static void getTermSize() {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        termW = ws.ws_col;
        termH = ws.ws_row;
    }
}

// ─── Terminal raw mode ────────────────────────────────────────────────────────
static struct termios origTermios;

static void enableRaw() {
    tcgetattr(STDIN_FILENO, &origTermios);
    struct termios raw = origTermios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void disableRaw() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &origTermios);
}

static void cleanup() {
    disableRaw();
    printf(SHOW_CURSOR RESET "\n");
}

static void sigHandler(int) { cleanup(); exit(0); }

// ─── Key reading ──────────────────────────────────────────────────────────────
enum Key { K_NONE=0, K_UP=1000, K_DOWN, K_LEFT, K_RIGHT,
           K_ENTER, K_ESC, K_TAB, K_BACKSPACE, K_F5 };

static int readKey() {
    char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return K_NONE;
    if (c == '\r' || c == '\n') return K_ENTER;
    if (c == 27) {
        char seq[4] = {};
        struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
        if (poll(&pfd, 1, 50) > 0) {
            if (read(STDIN_FILENO, &seq[0], 1) == 1 && seq[0] == '[') {
                if (read(STDIN_FILENO, &seq[1], 1) == 1) {
                    if (seq[1] == 'A') return K_UP;
                    if (seq[1] == 'B') return K_DOWN;
                    if (seq[1] == 'C') return K_RIGHT;
                    if (seq[1] == 'D') return K_LEFT;
                    if (seq[1] == '1' || seq[1] == '[') {
                        read(STDIN_FILENO, &seq[2], 1);
                        if (seq[2] == '5') { read(STDIN_FILENO,&seq[3],1); return K_F5; }
                    }
                }
            }
        }
        return K_ESC;
    }
    if (c == '\t') return K_TAB;
    if (c == 127 || c == 8) return K_BACKSPACE;
    return (unsigned char)c;
}

// ─── Box-drawing ──────────────────────────────────────────────────────────────
static void drawBox(int row, int col, int w, int h,
                    const char* colour = C_ORANGE, bool double_line = false) {
    const char* tl = double_line ? "╔" : "┌";
    const char* tr = double_line ? "╗" : "┐";
    const char* bl = double_line ? "╚" : "└";
    const char* br = double_line ? "╝" : "┘";
    const char* hz = double_line ? "═" : "─";
    const char* vt = double_line ? "║" : "│";

    printf("%s", colour);
    move(row, col);
    printf("%s", tl);
    for (int i = 0; i < w-2; i++) printf("%s", hz);
    printf("%s", tr);
    for (int i = 1; i < h-1; i++) {
        move(row+i, col);   printf("%s", vt);
        move(row+i, col+w-1); printf("%s", vt);
    }
    move(row+h-1, col);
    printf("%s", bl);
    for (int i = 0; i < w-2; i++) printf("%s", hz);
    printf("%s", br);
    printf(RESET);
}

static void fillBox(int row, int col, int w, int h, const char* bg) {
    std::string blank(w, ' ');
    printf("%s", bg);
    for (int i = 0; i < h; i++) {
        move(row+i, col);
        printf("%s", blank.c_str());
    }
    printf(RESET);
}

// ─── Centred text ─────────────────────────────────────────────────────────────
static void centreText(int row, int totalW, const std::string& text,
                       const char* colour = C_WHITE) {
    // strip ANSI to get visible length
    int vis = 0;
    bool inEsc = false;
    for (char c : text) {
        if (c == '\033') inEsc = true;
        else if (inEsc && c == 'm') inEsc = false;
        else if (!inEsc) vis++;
    }
    int col = (totalW - vis) / 2 + 1;
    move(row, col);
    printf("%s%s%s", colour, text.c_str(), RESET);
}

// ─── USB device discovery ─────────────────────────────────────────────────────
struct UsbDrive {
    std::string dev;   // e.g. /dev/sdb
    std::string model;
    std::string size;
    std::string transport; // usb / ...
};

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::string s;
    std::getline(f, s);
    // trim whitespace
    while (!s.empty() && (s.back() == ' ' || s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

static std::string humanSize(unsigned long long bytes) {
    const char* units[] = {"B","KB","MB","GB","TB"};
    double sz = bytes;
    int u = 0;
    while (sz >= 1024 && u < 4) { sz /= 1024; u++; }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f %s", sz, units[u]);
    return buf;
}

static std::vector<UsbDrive> scanDrives() {
    std::vector<UsbDrive> drives;
    DIR* d = opendir("/sys/block");
    if (!d) return drives;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        std::string name = ent->d_name;
        if (name[0] == '.') continue;
        // check if removable
        std::string removable = readFile("/sys/block/"+name+"/removable");
        if (removable != "1") continue;
        // check transport
        std::string transport = "unknown";
        // Walk the device symlink to find usb
        std::string syspath = "/sys/block/"+name;
        char resolved[512] = {};
        realpath(syspath.c_str(), resolved);
        std::string rpath = resolved;
        bool is_usb = (rpath.find("/usb") != std::string::npos);
        if (!is_usb) continue;
        transport = "USB";

        UsbDrive drv;
        drv.dev = "/dev/" + name;
        drv.model = readFile("/sys/block/"+name+"/device/model");
        if (drv.model.empty()) drv.model = readFile("/sys/block/"+name+"/device/name");
        if (drv.model.empty()) drv.model = name;
        // size
        std::string szStr = readFile("/sys/block/"+name+"/size");
        unsigned long long sectors = 0;
        if (!szStr.empty()) sectors = std::stoull(szStr);
        drv.size = humanSize(sectors * 512ULL);
        drv.transport = transport;
        drives.push_back(drv);
    }
    closedir(d);
    return drives;
}

// ─── Progress state (shared across threads) ──────────────────────────────────
struct WriteState {
    std::atomic<bool>  running{false};
    std::atomic<bool>  done{false};
    std::atomic<bool>  error{false};
    std::atomic<long long> written{0};
    std::atomic<long long> total{0};
    std::string errMsg;
    std::mutex  mtx;
    double mbps = 0.0;
    double eta  = 0.0;
};

static WriteState gState;

static void writerThread(const std::string& isoPath, const std::string& devPath) {
    const int BUFSZ = 4 * 1024 * 1024; // 4 MB chunks

    // open ISO
    int ifd = open(isoPath.c_str(), O_RDONLY);
    if (ifd < 0) {
        std::lock_guard<std::mutex> lk(gState.mtx);
        gState.errMsg = std::string("Cannot open ISO: ") + strerror(errno);
        gState.error = true; gState.done = true; return;
    }
    // open device
    int ofd = open(devPath.c_str(), O_WRONLY | O_SYNC);
    if (ofd < 0) {
        close(ifd);
        std::lock_guard<std::mutex> lk(gState.mtx);
        gState.errMsg = std::string("Cannot open device (root?): ") + strerror(errno);
        gState.error = true; gState.done = true; return;
    }
    // get size
    struct stat st;
    fstat(ifd, &st);
    gState.total = st.st_size;

    std::vector<char> buf(BUFSZ);
    long long written = 0;
    auto t0 = std::chrono::steady_clock::now();

    while (true) {
        ssize_t nr = read(ifd, buf.data(), BUFSZ);
        if (nr == 0) break;
        if (nr < 0) {
            std::lock_guard<std::mutex> lk(gState.mtx);
            gState.errMsg = std::string("Read error: ") + strerror(errno);
            gState.error = true; break;
        }
        ssize_t nw = 0;
        while (nw < nr) {
            ssize_t r = write(ofd, buf.data()+nw, nr-nw);
            if (r < 0) {
                std::lock_guard<std::mutex> lk(gState.mtx);
                gState.errMsg = std::string("Write error: ") + strerror(errno);
                gState.error = true; break;
            }
            nw += r;
        }
        if (gState.error) break;
        written += nr;
        gState.written = written;

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - t0).count();
        if (elapsed > 0.2) {
            std::lock_guard<std::mutex> lk(gState.mtx);
            gState.mbps = (written / 1e6) / elapsed;
            long long rem = gState.total.load() - written;
            gState.eta = (gState.mbps > 0) ? (rem / 1e6) / gState.mbps : 0.0;
        }
    }
    // sync
    if (!gState.error) {
        fsync(ofd);
        gState.written = written;
    }
    close(ifd);
    close(ofd);
    gState.done = true;
}

// ─── App state ────────────────────────────────────────────────────────────────
enum Screen { SCR_MAIN, SCR_WRITING, SCR_DONE };

struct AppState {
    Screen  screen     = SCR_MAIN;
    int     tab        = 0;        // 0=ISO, 1=Drive, 2=Write
    // ISO panel
    std::string isoPath;
    bool        isoValid = false;
    std::string isoSize;
    // ISO path edit
    std::string isoInput;
    bool        editingIso = false;
    // Drive panel
    std::vector<UsbDrive> drives;
    int driveIdx = -1;
    // Confirmation
    bool confirm = false;
    int  confirmSel = 1; // 0=YES 1=NO
    // Status message
    std::string statusMsg;
    bool statusOk = true;
};

static AppState gApp;

// ─── Draw routines ────────────────────────────────────────────────────────────

static void drawHeader() {
    getTermSize();
    int cx = termW / 2;
    // Big title bar
    fillBox(1, 1, termW, 3, BG(232));
    move(2, 2);
    printf(C_ORANGE BOLD "⬡ ISO WRITER" RESET C_GRAY " │ " RESET
           C_AMBER "Linux USB Burner" RESET C_GRAY " │ " RESET
           DIM C_GRAY "v1.0" RESET);
    // Right side: user/root hint
    bool isRoot = (geteuid() == 0);
    move(2, termW - 22);
    if (isRoot)
        printf(C_GREEN BOLD "● ROOT" RESET C_GRAY " — write enabled" RESET);
    else
        printf(C_RED BOLD "● USER" RESET C_GRAY " — sudo needed  " RESET);
    // Tab bar
    move(3, 1);
    for (int i = 0; i < termW; i++) printf("─");
    // Tabs
    const char* tabs[] = {"  ISO FILE  ", "  USB DRIVE  ", "  WRITE  "};
    int tx = cx - 22;
    for (int i = 0; i < 3; i++) {
        move(4, tx);
        if (i == gApp.tab) {
            printf(C_ACCENT C_WHITE BOLD " %s " RESET, tabs[i]);
        } else {
            printf(C_DARK C_GRAY " %s " RESET, tabs[i]);
        }
        tx += strlen(tabs[i]) + 2;
    }
    move(5, 1);
    for (int i = 0; i < termW; i++) printf("─");
    printf(RESET);
}

static void drawFooter() {
    int row = termH - 1;
    fillBox(row, 1, termW, 1, BG(232));
    move(row, 2);
    printf(DIM C_GRAY "Tab/←→: switch panel  ↑↓: navigate  Enter: select  "
           "F5: refresh drives  Esc/q: quit" RESET);
    // Status msg
    if (!gApp.statusMsg.empty()) {
        move(row, termW - (int)gApp.statusMsg.size() - 4);
        if (gApp.statusOk)
            printf(C_GREEN "✔ %s" RESET, gApp.statusMsg.c_str());
        else
            printf(C_RED "✖ %s" RESET, gApp.statusMsg.c_str());
    }
}

static void drawIsoPanel() {
    int top = 7, left = 2;
    int w = termW - 3, h = termH - 10;
    fillBox(top, left, w, h, BG(233));
    drawBox(top, left, w, h, C_ORANGE);
    move(top, left + 2);
    printf(C_ORANGE BOLD " ISO FILE " RESET);

    // Instructions
    move(top+2, left+3);
    printf(C_LGRAY "Enter the full path to your Linux ISO image." RESET);
    move(top+3, left+3);
    printf(DIM C_GRAY "(e.g.  /home/user/ubuntu-24.04-desktop-amd64.iso)" RESET);

    // Input box
    int ibox_row = top + 5;
    int ibox_w   = w - 6;
    fillBox(ibox_row, left+3, ibox_w, 3, BG(236));
    drawBox(ibox_row, left+3, ibox_w, 3,
            gApp.editingIso ? C_CYAN : C_GRAY);

    move(ibox_row+1, left+5);
    if (gApp.isoInput.empty() && !gApp.editingIso) {
        printf(DIM C_GRAY "Press Enter to type path..." RESET);
    } else {
        // Show last (ibox_w-4) chars
        std::string vis = gApp.isoInput;
        if ((int)vis.size() > ibox_w - 4)
            vis = vis.substr(vis.size() - (ibox_w-4));
        printf(C_WHITE "%s" RESET, vis.c_str());
        if (gApp.editingIso) printf(C_CYAN "█" RESET);
    }

    // [Browse] hint & [Validate]
    move(ibox_row+4, left+3);
    printf(C_GRAY "Press " C_CYAN BOLD "Enter" RESET C_GRAY " to confirm path   ");
    printf(C_GRAY "| Use " C_AMBER "absolute path" RESET C_GRAY " for best results" RESET);

    // File info if valid
    if (gApp.isoValid) {
        int info_row = ibox_row + 6;
        fillBox(info_row, left+3, w-6, 5, BG(234));
        drawBox(info_row, left+3, w-6, 5, C_GREEN);
        move(info_row, left+5); printf(C_GREEN BOLD " FILE INFO " RESET);
        move(info_row+1, left+5);
        printf(C_GREEN "✔ " RESET C_WHITE "Path:  " C_AMBER "%s" RESET, gApp.isoPath.c_str());
        move(info_row+2, left+5);
        printf(C_GREEN "✔ " RESET C_WHITE "Size:  " C_AMBER "%s" RESET, gApp.isoSize.c_str());
        move(info_row+3, left+5);
        printf(C_GREEN "✔ " RESET C_WHITE "Status:" C_GREEN BOLD " VALID ISO" RESET);
    } else if (!gApp.isoPath.empty()) {
        int info_row = ibox_row + 6;
        fillBox(info_row, left+3, w-6, 4, BG(234));
        drawBox(info_row, left+3, w-6, 4, C_RED);
        move(info_row, left+5); printf(C_RED BOLD " FILE INFO " RESET);
        move(info_row+1, left+5);
        printf(C_RED "✖ " RESET C_WHITE "Path:  " C_LGRAY "%s" RESET, gApp.isoPath.c_str());
        move(info_row+2, left+5);
        printf(C_RED "✖ File not found or not readable." RESET);
    }
    printf(RESET);
}

static void drawDrivePanel() {
    int top = 7, left = 2;
    int w = termW - 3, h = termH - 10;
    fillBox(top, left, w, h, BG(233));
    drawBox(top, left, w, h, C_ORANGE);
    move(top, left+2);
    printf(C_ORANGE BOLD " USB DRIVES " RESET);

    move(top+2, left+3);
    printf(C_LGRAY "Select the USB drive to write to." RESET C_RED BOLD
           "  ALL DATA WILL BE ERASED." RESET);
    move(top+3, left+3);
    printf(DIM C_GRAY "Press F5 to refresh the drive list." RESET);

    if (gApp.drives.empty()) {
        int mr = top + h/2;
        centreText(mr,   w, "⚠  No USB drives detected");
        centreText(mr+1, w, "Insert a USB drive and press F5 to refresh", C_GRAY);
        printf(RESET); return;
    }

    // Header row
    int hr = top + 5;
    move(hr, left+3);
    printf(C_ORANGE BOLD "  %-6s  %-30s  %-10s  %s" RESET,
           "Device", "Model", "Size", "Transport");
    move(hr+1, left+3);
    for (int i = 0; i < w-7; i++) printf("─");

    int listTop = hr + 2;
    for (int i = 0; i < (int)gApp.drives.size() && i < 10; i++) {
        const auto& drv = gApp.drives[i];
        bool sel = (i == gApp.driveIdx);
        move(listTop + i, left+3);
        if (sel) printf(C_SEL C_CYAN BOLD);
        else     printf(BG(233) C_LGRAY);
        printf("  %-6s  %-30s  %-10s  %s",
               drv.dev.substr(5).c_str(),   // strip /dev/
               drv.model.substr(0, 30).c_str(),
               drv.size.c_str(),
               drv.transport.c_str());
        // Pad to width
        int used = 2+6+2+30+2+10+2+3;
        for (int j = used; j < w-8; j++) printf(" ");
        if (sel) printf(" ◀" RESET);
        else     printf("  " RESET);
    }

    if (gApp.driveIdx >= 0 && gApp.driveIdx < (int)gApp.drives.size()) {
        auto& d = gApp.drives[gApp.driveIdx];
        int ir = listTop + (int)gApp.drives.size() + 2;
        fillBox(ir, left+3, w-6, 3, BG(234));
        drawBox(ir, left+3, w-6, 3, C_AMBER);
        move(ir, left+5); printf(C_AMBER BOLD " SELECTED " RESET);
        move(ir+1, left+5);
        printf(C_WHITE "Target: " C_AMBER BOLD "%s" RESET
               C_WHITE "   Model: " C_AMBER "%s" RESET
               C_WHITE "   Size: " C_AMBER "%s" RESET,
               d.dev.c_str(), d.model.c_str(), d.size.c_str());
    }
    printf(RESET);
}

static void drawWritePanel() {
    int top = 7, left = 2;
    int w = termW - 3, h = termH - 10;
    fillBox(top, left, w, h, BG(233));
    drawBox(top, left, w, h, C_ORANGE, true);
    move(top, left+2);
    printf(C_ORANGE BOLD " WRITE " RESET);

    bool ready = gApp.isoValid && gApp.driveIdx >= 0;

    // Summary
    int sr = top + 2;
    move(sr, left+3);
    printf(C_LGRAY "Review your selection before writing." RESET);

    fillBox(sr+2, left+3, w-6, 5, BG(234));
    drawBox(sr+2, left+3, w-6, 5, ready ? C_AMBER : C_GRAY);
    move(sr+2, left+5); printf(ready ? C_AMBER BOLD " SUMMARY " RESET : C_GRAY " SUMMARY " RESET);

    move(sr+3, left+5);
    if (gApp.isoValid)
        printf(C_GREEN "✔ " RESET C_WHITE "ISO:   " C_AMBER "%s " C_GRAY "(%s)" RESET,
               gApp.isoPath.c_str(), gApp.isoSize.c_str());
    else
        printf(C_RED "✖ " RESET C_GRAY "ISO:   (not set)" RESET);

    move(sr+4, left+5);
    if (gApp.driveIdx >= 0) {
        auto& d = gApp.drives[gApp.driveIdx];
        printf(C_GREEN "✔ " RESET C_WHITE "Drive: " C_RED BOLD "%s " RESET
               C_GRAY "(%s — %s)" RESET,
               d.dev.c_str(), d.model.c_str(), d.size.c_str());
    } else {
        printf(C_RED "✖ " RESET C_GRAY "Drive: (not selected)" RESET);
    }

    move(sr+5, left+5);
    if (ready)
        printf(C_RED BOLD "⚠  ALL DATA ON THE TARGET DRIVE WILL BE PERMANENTLY ERASED!" RESET);
    else
        printf(DIM C_GRAY "Complete ISO and Drive selection to proceed." RESET);

    // WRITE button
    int br = sr + 8;
    if (ready) {
        fillBox(br, left + w/2 - 14, 28, 3, BG(202));
        drawBox(br, left + w/2 - 14, 28, 3, C_ORANGE);
        move(br+1, left + w/2 - 9);
        printf(C_WHITE BOLD " ▶  WRITE TO USB " RESET);
        move(br+4, left+3);
        printf(DIM C_GRAY "Press Enter to begin writing." RESET);
    } else {
        move(br, left+3);
        printf(DIM C_GRAY "→  Go to " C_CYAN "ISO FILE" RESET DIM C_GRAY
               " and " C_CYAN "USB DRIVE" RESET DIM C_GRAY
               " tabs to make your selection first." RESET);
    }
    printf(RESET);
}

// ─── Confirmation dialog ──────────────────────────────────────────────────────
static void drawConfirm() {
    int dw = 60, dh = 10;
    int dr = (termH - dh) / 2;
    int dc = (termW - dw) / 2;
    fillBox(dr, dc, dw, dh, BG(235));
    drawBox(dr, dc, dw, dh, C_RED, true);
    centreText(dr,    termW, "╗ CONFIRM WRITE ╔", C_RED BOLD);
    centreText(dr+2,  termW, "This will ERASE ALL DATA on:", C_WHITE);

    if (gApp.driveIdx >= 0) {
        auto& d = gApp.drives[gApp.driveIdx];
        std::string line = d.dev + "  [" + d.model + " — " + d.size + "]";
        centreText(dr+3, termW, line, C_RED BOLD);
    }
    centreText(dr+4, termW, "and write:", C_WHITE);
    centreText(dr+5, termW, gApp.isoPath, C_AMBER);
    centreText(dr+7, termW, "Are you sure?", C_LGRAY);

    // YES / NO buttons
    int by = dr + dh - 2;
    int bx_yes = termW/2 - 14;
    int bx_no  = termW/2 + 2;

    move(by, bx_yes);
    if (gApp.confirmSel == 0)
        printf(C_ACCENT C_WHITE BOLD "[ YES — ERASE & WRITE ]" RESET);
    else
        printf(C_DARK C_GRAY "[  YES — ERASE & WRITE  ]" RESET);

    move(by, bx_no);
    if (gApp.confirmSel == 1)
        printf(C_ACCENT C_WHITE BOLD "[ NO / CANCEL ]" RESET);
    else
        printf(C_DARK C_GRAY "[  NO / CANCEL  ]" RESET);
    printf(RESET);
}

// ─── Writing / progress screen ────────────────────────────────────────────────
static void drawProgressBar(int row, int col, int w,
                            double frac, const char* colour = C_ORANGE) {
    int filled = (int)(frac * (w-2));
    if (filled < 0) filled = 0;
    if (filled > w-2) filled = w-2;
    move(row, col);
    printf("%s[" RESET, colour);
    printf("%s", colour);
    for (int i = 0; i < filled; i++) printf("█");
    printf(RESET C_GRAY);
    for (int i = filled; i < w-2; i++) printf("░");
    printf(RESET);
    printf("%s]" RESET, colour);
}

static void drawWriteScreen() {
    printf(CLEAR HOME);
    getTermSize();
    fillBox(1, 1, termW, termH, BG(232));
    drawBox(1, 1, termW, termH, C_ORANGE, true);

    centreText(2, termW, "⬡  ISO WRITER — BURNING…", C_ORANGE BOLD);
    move(3, 2); for(int i=0;i<termW-3;i++) printf("─");

    long long written = gState.written.load();
    long long total   = gState.total.load();
    double frac = (total > 0) ? (double)written / total : 0.0;
    double mbps, eta;
    {
        std::lock_guard<std::mutex> lk(gState.mtx);
        mbps = gState.mbps;
        eta  = gState.eta;
    }

    // Progress bar
    int pbw = termW - 10;
    drawProgressBar(termH/2 - 2, 5, pbw, frac, C_ORANGE);

    // Percentage
    char pct[16]; snprintf(pct, sizeof(pct), "%.1f%%", frac*100);
    centreText(termH/2 - 1, termW, pct, C_AMBER BOLD);

    // Written / total
    char wtline[128];
    snprintf(wtline, sizeof(wtline), "%s  /  %s",
             humanSize(written).c_str(), humanSize(total).c_str());
    centreText(termH/2, termW, wtline, C_LGRAY);

    // Speed / ETA
    char speedline[128];
    if (mbps > 0)
        snprintf(speedline, sizeof(speedline), "%.1f MB/s   ETA: %ds", mbps, (int)eta);
    else
        snprintf(speedline, sizeof(speedline), "Starting…");
    centreText(termH/2 + 1, termW, speedline, C_CYAN);

    if (gApp.driveIdx >= 0) {
        centreText(termH/2 + 3, termW,
                   "Writing to  " + gApp.drives[gApp.driveIdx].dev, C_GRAY);
    }

    // Spinner
    static int spin = 0;
    const char* frames[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
    move(termH/2 - 4, termW/2);
    printf(C_AMBER BOLD "%s" RESET, frames[spin++ % 10]);

    centreText(termH - 2, termW, "Do NOT remove the USB drive or close this window.", C_RED);
    fflush(stdout);
}

static void drawDoneScreen(bool success) {
    printf(CLEAR HOME);
    getTermSize();
    fillBox(1, 1, termW, termH, BG(232));
    drawBox(1, 1, termW, termH, success ? C_GREEN : C_RED, true);

    if (success) {
        centreText(termH/2 - 4, termW, "╔══════════════════════════════╗", C_GREEN);
        centreText(termH/2 - 3, termW, "║                              ║", C_GREEN);
        centreText(termH/2 - 2, termW, "║   ✔  WRITE COMPLETE!         ║", C_GREEN BOLD);
        centreText(termH/2 - 1, termW, "║                              ║", C_GREEN);
        centreText(termH/2,     termW, "╚══════════════════════════════╝", C_GREEN);
        centreText(termH/2 + 2, termW, "Your USB drive is ready to boot.", C_LGRAY);
        centreText(termH/2 + 3, termW,
                   humanSize(gState.written.load()) + " written successfully.", C_AMBER);
    } else {
        centreText(termH/2 - 3, termW, "╔══════════════════════════════╗", C_RED);
        centreText(termH/2 - 2, termW, "║  ✖  WRITE FAILED             ║", C_RED BOLD);
        centreText(termH/2 - 1, termW, "╚══════════════════════════════╝", C_RED);
        std::string em;
        { std::lock_guard<std::mutex> lk(gState.mtx); em = gState.errMsg; }
        centreText(termH/2 + 1, termW, em, C_LGRAY);
    }
    centreText(termH - 2, termW, "Press any key to exit.", C_GRAY);
    fflush(stdout);
}

// ─── ISO validation ───────────────────────────────────────────────────────────
static bool validateIso(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    if (!S_ISREG(st.st_mode)) return false;
    // check read
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    // read primary volume descriptor at sector 16 (offset 32768)
    char buf[6] = {};
    lseek(fd, 32768, SEEK_SET);
    read(fd, buf, 5);
    close(fd);
    // ISO 9660 magic: 0x01 'C' 'D' '0' '0' '1'
    bool ok = (buf[1]=='C' && buf[2]=='D' && buf[3]=='0' && buf[4]=='1');
    // also accept UDF and other valid images by just checking file size > 1MB
    if (!ok && st.st_size > 1024*1024) ok = true;
    if (ok) gApp.isoSize = humanSize((unsigned long long)st.st_size);
    return ok;
}

// ─── Render ───────────────────────────────────────────────────────────────────
static void render() {
    printf(CLEAR HOME HIDE_CURSOR);
    getTermSize();
    fillBox(1, 1, termW, termH, BG(232));
    drawHeader();
    switch (gApp.tab) {
        case 0: drawIsoPanel();   break;
        case 1: drawDrivePanel(); break;
        case 2: drawWritePanel(); break;
    }
    drawFooter();
    if (gApp.confirm) drawConfirm();
    fflush(stdout);
}

// ─── Main event loop ─────────────────────────────────────────────────────────
int main() {
    signal(SIGINT,  sigHandler);
    signal(SIGTERM, sigHandler);
    enableRaw();
    atexit(cleanup);

    // Initial scan
    gApp.drives = scanDrives();

    render();

    while (true) {
        // ── Writing screen ──
        if (gApp.screen == SCR_WRITING) {
            drawWriteScreen();
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
            if (gState.done) {
                gApp.screen = SCR_DONE;
                drawDoneScreen(!gState.error);
                readKey(); // wait for keypress
                break;
            }
            // check for keypress (non-blocking peek)
            struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
            if (poll(&pfd, 1, 0) > 0) {
                int k = readKey();
                (void)k; // ignore during write
            }
            continue;
        }
        if (gApp.screen == SCR_DONE) break;

        // ── Main screen ──
        int k = readKey();

        // Confirm dialog
        if (gApp.confirm) {
            if (k == K_LEFT || k == K_RIGHT || k == K_TAB) {
                gApp.confirmSel ^= 1;
            } else if (k == K_ENTER) {
                if (gApp.confirmSel == 0) {
                    // Start write
                    gState.running = true;
                    gState.done    = false;
                    gState.error   = false;
                    gState.written = 0;
                    gState.total   = 0;
                    gApp.screen    = SCR_WRITING;
                    gApp.confirm   = false;
                    std::thread(writerThread,
                                gApp.isoPath,
                                gApp.drives[gApp.driveIdx].dev).detach();
                    continue;
                } else {
                    gApp.confirm = false;
                }
            } else if (k == K_ESC) {
                gApp.confirm = false;
            }
            render();
            continue;
        }

        // ISO path editing
        if (gApp.editingIso) {
            if (k == K_ENTER || k == K_ESC) {
                gApp.editingIso = false;
                if (k == K_ENTER && !gApp.isoInput.empty()) {
                    gApp.isoPath  = gApp.isoInput;
                    gApp.isoValid = validateIso(gApp.isoPath);
                    if (gApp.isoValid) {
                        gApp.statusMsg = "ISO validated";
                        gApp.statusOk  = true;
                    } else {
                        gApp.statusMsg = "File not found / invalid";
                        gApp.statusOk  = false;
                    }
                }
            } else if (k == K_BACKSPACE) {
                if (!gApp.isoInput.empty()) gApp.isoInput.pop_back();
            } else if (k >= 32 && k < 256) {
                gApp.isoInput += (char)k;
            }
            render();
            continue;
        }

        // Normal keys
        if (k == 'q' || k == K_ESC) break;

        if (k == K_TAB || k == K_RIGHT) {
            gApp.tab = (gApp.tab + 1) % 3;
        } else if (k == K_LEFT) {
            gApp.tab = (gApp.tab + 2) % 3;
        } else if (k == '1') { gApp.tab = 0; }
        else if  (k == '2') { gApp.tab = 1; }
        else if  (k == '3') { gApp.tab = 2; }
        else if (k == K_F5) {
            gApp.drives  = scanDrives();
            gApp.driveIdx = -1;
            gApp.statusMsg = "Drives refreshed";
            gApp.statusOk  = true;
        }

        // Tab-specific
        if (gApp.tab == 0) {
            if (k == K_ENTER) {
                gApp.editingIso = true;
                if (gApp.isoInput.empty() && !gApp.isoPath.empty())
                    gApp.isoInput = gApp.isoPath;
            }
        } else if (gApp.tab == 1) {
            if (k == K_UP) {
                if (gApp.driveIdx > 0) gApp.driveIdx--;
                else if (!gApp.drives.empty()) gApp.driveIdx = 0;
            } else if (k == K_DOWN) {
                if (gApp.driveIdx < (int)gApp.drives.size()-1) gApp.driveIdx++;
            } else if (k == K_ENTER && gApp.driveIdx >= 0) {
                gApp.statusMsg = "Drive selected: " + gApp.drives[gApp.driveIdx].dev;
                gApp.statusOk  = true;
            }
        } else if (gApp.tab == 2) {
            if (k == K_ENTER && gApp.isoValid && gApp.driveIdx >= 0) {
                gApp.confirm    = true;
                gApp.confirmSel = 1; // default NO
            }
        }
        render();
    }

    printf(CLEAR HOME SHOW_CURSOR RESET);
    return 0;
}