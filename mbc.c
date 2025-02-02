#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <linux/uinput.h>
#include <sys/inotify.h>
#include <poll.h>

#define MBC_BUILD_REVISION 33

#define DEVICE_NAME "Fake device"
#define DEVICE_PATH "/dev/uinput"
#define MISTER_COMMAND_DEVICE "/dev/MiSTer_cmd"
#define MBC_LINK_NAM "~~~"
#define CORE_EXT "rbf"
#define MBCSEQ "HDOFO"
#define ROMSUBLINK " !MBC"

#define HAS_PREFIX(P, N) (!strncmp(P, N, sizeof(P)-1)) // P must be a string literal

#define ARRSIZ(A) (sizeof(A)/sizeof(A[0]))
#define LOG(F,...) printf("%d - " F, __LINE__, __VA_ARGS__ )
#define PRINTERR(F,...) LOG("error - %s - " F, strerror(errno ? errno : EPERM), __VA_ARGS__ )
#define SBSEARCH(T, SA, C)	(bsearch(T, SA, sizeof(SA)/sizeof(SA[0]), sizeof(SA[0]), (C)))

#ifdef FAKE_OS_OPERATION
#define nanosleep(m,n) (LOG("nanosleep %ld %ld\n", (m)->tv_sec, (m)->tv_nsec), 0)
#define open(p,b)      (LOG("open '%s'\n", p), fake_fd(p))
#define ioctl(f,...)   (0)
#define write(f,d,s)   (LOG("write '%s' <-",f),hex_write(d,s),printf("\n"), 0)
#define close(f)       (LOG("close '%s'\n",fake_fd_name(f)), 0)
#define mkdir(p,m)     (LOG("mkdir '%s'\n",p), 0)
#define poll(p,n,m)    (LOG("%s\n","poll"), 0)
#define read(f,d,s)    (LOG("read '%s'\n",fake_fd_name(f)), 0)
#define inotify_init() (LOG("%s\n","inotifyinit"), 0)
#define inotify_add_watch(f,d,o)   (LOG("inotifyadd '%s'\n",fake_fd_name(f)), 0)
//#define opendir(p)     (LOG("opendir '%s'\n",p), fake_fd(p))
//#define readdir(f)     (LOG("readdir '%s'\n",fake_fd_name(f)), 0)
#define closedir(f)    (LOG("closedir '%s'\n",fake_fd_name(f)), 0)
#define inotify_rm_watch(f,w)   (LOG("inotifyrm '%s'\n",fake_fd_name(f)), 0)
#define fopen(p,o)     (LOG("fopen '%s'\n",p), fake_file(p))
#define fread(d,s,n,o) (LOG("fread '%s'\n",fake_file_name(o)), 0)
#define fwrite(d,s,n,o) (LOG("fwrite '%s'\n",fake_file_name(o)), 0)
#define fprintf(f,m,d) (LOG("fprintf '%s' <- '%s'\n",fake_file_name(f),m), 0)
#define fclose(f)      (LOG("fclose '%s'\n",fake_file_name(f)), 0)
#define mount(s,t,x,o, y)    (LOG("mount '%s' -> '%s'\n",s,t), 0)
#define umount(p)      (LOG("umount '%s'\n",p), 0)
#define umount2(p,...) (LOG("umount2 '%s'\n",p), 0)
#define remove(p)      (LOG("remove '%s'\n",p), 0)
#define rmdir(p)       (LOG("rmdir '%s'\n",p), 0)
//#define stat(p,s)      (LOG("stat '%s'\n",p), stat(p,s))
//#define getenv(k)      (LOG("getenv '%s'\n",k),getenv(k))
#undef S_ISREG
#define S_ISREG(s)      (1)
static int   fake_fd(const char* name)    { return (int)(char*)strdup(name); }
static char* fake_fd_name(int fd)         { return (char*)fd; }
static FILE* fake_file(const char* name)  { return (FILE*)(char*)strdup(name); }
static char* fake_file_name(FILE* f)      { return (char*)f; }
static void  hex_write(const char*b,int s){ for(int i=0;i<s;i+=1)printf(" %02x",b[i]); }
#endif // FAKE_OS_OPERATION

static void msleep(long ms){
  struct timespec w;
  w.tv_sec = ms / 1000;
  w.tv_nsec = (ms % 1000 ) * 1000000;
  while (nanosleep(&w, &w));
}

static int ev_open() {

  int fd = open(DEVICE_PATH, O_WRONLY | O_NONBLOCK);
  if (fd < 0) {
    PRINTERR("%s", DEVICE_PATH);
    return fd;
  }

  ioctl(fd, UI_SET_EVBIT, EV_KEY);
  for (int i=KEY_ESC; i<KEY_MAX; i++){
    ioctl(fd, UI_SET_KEYBIT, i);
  }

#if UINPUT_VERSION < 5
  struct uinput_user_dev uud;
  memset(&uud, 0, sizeof(uud));
  snprintf(uud.name, UINPUT_MAX_NAME_SIZE, DEVICE_NAME);
  write(fd, &uud, sizeof(uud));
#error MiSTer needs UINPUT 5 or above
#else
  struct uinput_setup usetup;
  memset(&usetup, 0, sizeof(usetup));
  usetup.id.bustype = BUS_USB;
  usetup.id.vendor = 0x1234; /* fake vendor */
  usetup.id.product = 0x5678; /* fake product */
  strcpy(usetup.name, DEVICE_NAME);
  ioctl(fd, UI_DEV_SETUP, &usetup);
  ioctl(fd, UI_DEV_CREATE);
#endif

  return fd;
}

static void ev_emit(int fd, int type, int code, int val) {
  struct input_event ie = {0,};
  ie.type = type;
  ie.code = code;
  ie.value = val;
  //gettimeofday(&ie.time, NULL);
  write(fd, &ie, sizeof(ie));
}

static int ev_close(int fd) {
  ioctl(fd, UI_DEV_DESTROY);
  return close(fd);
}

static void path_parentize(char* path, int keep_trailing_separator){
  for (int i = strlen(path); i > 0; i--)
    if (path[i] == '/') {
      if (!keep_trailing_separator) path[i] = '\0';
      else path[i+1] = '\0';
      break;
    }
}

static int is_dir(const char* path){
  struct stat st;
  int stat_error = stat(path, &st);
  if (!stat_error && S_ISDIR(st.st_mode))
    return 1;
  return 0;
}

static int mkdir_core(const char* path, mode_t mode){

  int created = !mkdir(path, mode);
  if (created) return 0;

  // no error if a directory already exist
  if (!created && errno == EEXIST && is_dir(path))
    return 0;

  return -1;
}

static int mkparent_core(char *path, mode_t mode) {
  int depth = 0;
  char *curr = path, *found = 0;

  while (0 != (found = strchr(curr, '/'))) {
    if (found != curr) { // skip root or double slashes in path
      depth += 1;

      *found = '\0';
      int err = mkdir_core(path, mode);
      *found = '/';

      if (err) return -depth;
    }
    curr = found + 1;
  }
  return 0;
}

static int mkparent(const char *path, mode_t mode) {
  char *pathdup = strdup(path);
  if (!pathdup) return -1;
  int status = mkparent_core(pathdup, mode);
  free(pathdup);
  return status;
}

static int mkdirpath(const char *path, mode_t mode) {
  if (is_dir(path)) return 0;
  int result = mkparent(path, mode);
  if (result) return result;
  return mkdir_core(path, mode);
}

typedef struct {
  int watcher;
  int file_n;
  struct pollfd pool[99];
} input_monitor;

int user_input_poll(input_monitor*monitor, int ms){
  int result = poll(monitor->pool, monitor->file_n, ms);
  if (0> result) return -4; // Poll error
  return result;
}

int user_input_clear(input_monitor*monitor){
  struct input_event ev;
  int count = 0;
  while (0< user_input_poll(monitor, 1))
    for (int f = 0; f < monitor->file_n; f += 1)
      if (monitor->pool[f].revents & POLLIN)
        if (0< read(monitor->pool[f].fd, &ev, sizeof(ev)))
          count += 1;
  return count;
}

int user_input_open(input_monitor*monitor){
  const char folder[] = "/dev/input";

  monitor->watcher = -1;
  monitor->file_n = 0;

  int inotify_fd = inotify_init();
  if (inotify_fd == -1) return -1; // Inotify error

  monitor->pool[monitor->file_n].fd = inotify_fd;
  monitor->pool[monitor->file_n].events = POLLIN;
  monitor->file_n += 1;

  monitor->watcher = inotify_add_watch( inotify_fd, folder, IN_CREATE | IN_DELETE );
  if (monitor->watcher == -1) return -2; // Watcher error

  struct dirent* dir = NULL;
  DIR* dirinfo = opendir(folder);
  if (dirinfo != NULL) {
    while (0 != (dir = readdir (dirinfo))){
      if (HAS_PREFIX("event", dir->d_name)){

        char out[sizeof(folder)+strlen(dir->d_name)+9];
        snprintf(out, sizeof(out)-1, "%s/%s", folder,dir->d_name);
        struct stat fileinfo;
        if (stat(out, &fileinfo) == 0 && !S_ISDIR(fileinfo.st_mode) && !S_ISREG(fileinfo.st_mode)) {

          if (monitor->file_n >= sizeof(monitor->pool)/sizeof(*(monitor->pool))) break;
          int fd = open(out,O_RDONLY|O_NONBLOCK);
          if (fd <= 0) return -3; // Open error

          monitor->pool[monitor->file_n].fd = fd;
          monitor->pool[monitor->file_n].events = POLLIN;
          monitor->file_n += 1;
        }
      }
    }
    closedir(dirinfo);
  }

  while(0< user_input_clear(monitor)); // drop old events

  return 0;
}

int is_user_input_event(int code){ return (code>0); }
int is_user_input_timeout(int code){ return (code==0); }

void user_input_close(input_monitor*monitor){

  if (monitor->watcher >= 0)
    inotify_rm_watch(monitor->pool[0].fd, monitor->watcher);

  for (int f = 0; f < monitor->file_n; f += 1)
    if (monitor->pool[f].fd > 0) close(monitor->pool[f].fd);
  monitor->file_n = 0;
}

static size_t updatehash( size_t hash, char c){
  return hash ^( c + (hash<<5) + (hash>>2));
}

size_t contenthash( const char* path){
  FILE *mnt  = fopen( path, "r");
  if( !mnt) return 0;
  int c;
  size_t hash = 0;
  while( EOF != ( c = fgetc( mnt)))
    hash = updatehash( hash, (char) c);
  fclose(mnt);
  if( 0 == hash) hash = 1; // 0 is considered invalid hash, so it can be used as error or initialization value
  return hash;
}

typedef struct {
  char *id;      // This must match the filename before the last _ . Otherwise it can be given explicitly at the command line. It must be UPPERCASE without any space.
  char *menuseq; // Sequence of input for the rom selection; searched in the internal DB
  char *core;    // Path prefix to the core; searched in the internal DB
  char *fsid;    // Name used in the filesystem to identify the folders of the system; if it starts with something different than '/', it will identify a subfolder of a default path
  char *romext;  // Valid extension for rom filename; searched in the internal DB
  char *sublink; // If not NULL, the auxiliary rom link will be made in the specified path, instead of default one
} system_t;

static system_t system_list[] = {
  // The first field can not contain '\0'.
  // The array must be lexicographically sorted wrt the first field (e.g.
  //   :sort vim command, but mind '!' and escaped chars at end of similar names).
 
  { "ALICEMC10",      "EEMDDDO" MBCSEQ,     "/media/fat/_Computer/AliceMC10_",         "AliceMC10",    "c10", },
  { "AMIGA.ADF",      "EEMO" MBCSEQ "E",    "/media/fat/_Computer/Minimig_",           "Amiga",        "adf", },
  { "AMIGA.HDF",      "EEMDDDODDDO" MBCSEQ ":0EDDDDDO","/media/fat/_Computer/Minimig_","Amiga",        "hdf", },
  { "AMSTRAD",        "EEMO" MBCSEQ,        "/media/fat/_Computer/Amstrad_",           "Amstrad",      "dsk", },
  { "AMSTRAD-PCW",    "EEMO" MBCSEQ,        "/media/fat/_Computer/Amstrad-PCW_",       "Amstrad PCW",  "dsk", },
  { "AMSTRAD-PCW.B",  "EEMDO" MBCSEQ,       "/media/fat/_Computer/Amstrad-PCW_",       "Amstrad PCW",  "dsk", },
  { "AMSTRAD.B",      "EEMDO" MBCSEQ,       "/media/fat/_Computer/Amstrad_",           "Amstrad",      "dsk", },
  { "AMSTRAD.TAP",    "EEMDDDO" MBCSEQ,     "/media/fat/_Computer/Amstrad_",           "Amstrad",      "cdt", },
  { "AO486",          "EEMO" MBCSEQ,        "/media/fat/_Computer/ao486_",             "AO486",        "img", },
  { "AO486.B",        "EEMDO" MBCSEQ,       "/media/fat/_Computer/ao486_",             "AO486",        "img", },
  { "AO486.C",        "EEMDDO" MBCSEQ,      "/media/fat/_Computer/ao486_",             "AO486",        "vhd", },
  { "AO486.D",        "EEMDDDO" MBCSEQ,     "/media/fat/_Computer/ao486_",             "AO486",        "vhd", },
  { "APOGEE",         "EEMO" MBCSEQ,        "/media/fat/_Computer/Apogee_",            "APOGEE",       "rka", },
  { "APPLE-I",        "EEMO" MBCSEQ,        "/media/fat/_Computer/Apple-I_",           "Apple-I",      "txt", },
  { "APPLE-II",       "EEMO" MBCSEQ,        "/media/fat/_Computer/Apple-II_",          "Apple-II",     "dsk", },
  { "AQUARIUS.BIN",   "EEMO" MBCSEQ,        "/media/fat/_Computer/Aquarius_",          "AQUARIUS",     "bin", },
  { "AQUARIUS.CAQ",   "EEMDO" MBCSEQ,       "/media/fat/_Computer/Aquarius_",          "AQUARIUS",     "caq", },
  { "ARCADE",         "O" MBCSEQ,           "/media/fat/menu",                         "/media/fat/_Arcade", "mra", "/media/fat/_Arcade/_ !MBC/~~~.mra"},
  { "ARCHIE.D1",      "EEMDDDO" MBCSEQ,     "/media/fat/_Computer/Archie_",            "ARCHIE",       "vhd", },
  { "ARCHIE.F0",      "EEMO" MBCSEQ,        "/media/fat/_Computer/Archie_",            "ARCHIE",       "img", },
  { "ARCHIE.F1",      "EEMDO" MBCSEQ,       "/media/fat/_Computer/Archie_",            "ARCHIE",       "img", },
  { "ASTROCADE",      "EEMO" MBCSEQ,        "/media/fat/_Console/Astrocade_",          "Astrocade",    "bin", },
  { "ATARI2600",      "EEMO" MBCSEQ,        "/media/fat/_Console/Atari2600_",          "ATARI2600",    "rom", },
  { "ATARI2600",      "EEMO" MBCSEQ,        "/media/fat/_Console/Atari2600_",          "Astrocade",    "rom", },
  { "ATARI5200",      "EEMO" MBCSEQ,        "/media/fat/_Console/Atari5200_",          "ATARI5200",    "rom", },
  { "ATARI7800",      "EEMO" MBCSEQ,        "/media/fat/_Console/Atari7800_",          "ATARI7800",    "a78", },
  { "ATARI800.CART",  "EEMDDO" MBCSEQ,      "/media/fat/_Computer/Atari800_",          "ATARI800",     "car", },
  { "ATARI800.D1",    "EEMO" MBCSEQ,        "/media/fat/_Computer/Atari800_",          "ATARI800",     "atr", },
  { "ATARI800.D2",    "EEMDO" MBCSEQ,       "/media/fat/_Computer/Atari800_",          "ATARI800",     "atr", },
  { "ATARILYNX",      "EEMO" MBCSEQ,        "/media/fat/_Console/AtariLynx_",          "AtariLynx",    "lnx", },
  { "BBCMICRO",       "EEMO" MBCSEQ,        "/media/fat/_Computer/BBCMicro_",          "BBCMicro",     "vhd", },
  { "BK0011M",        "EEMO" MBCSEQ,        "/media/fat/_Computer/BK0011M_",           "BK0011M",      "bin", },
  { "BK0011M.A",      "EEMDO" MBCSEQ,       "/media/fat/_Computer/BK0011M_",           "BK0011M",      "dsk", },
  { "BK0011M.B",      "EEMDDO" MBCSEQ,      "/media/fat/_Computer/BK0011M_",           "BK0011M",      "dsk", },
  { "BK0011M.HD",     "EEMDDDO" MBCSEQ,     "/media/fat/_Computer/BK0011M_",           "BK0011M",      "vhd", },
  { "C16.CART",       "EEMDO" MBCSEQ,       "/media/fat/_Computer/C16_",               "C16",          "bin", },
  { "C16.DISK",       "EEMDDO" MBCSEQ,      "/media/fat/_Computer/C16_",               "C16",          "d64", },
  { "C16.TAPE",       "EEMDDDO" MBCSEQ,     "/media/fat/_Computer/C16_",               "C16",          "tap", },
  { "C64.CART",       "EEMDDDDO" MBCSEQ,    "/media/fat/_Computer/C64_",               "C65",          "crt", },
  { "C64.DISK",       "EEMO" MBCSEQ,        "/media/fat/_Computer/C64_",               "C64",          "rom", },
  { "C64.PRG",        "EEMDDDO" MBCSEQ,     "/media/fat/_Computer/C64_",               "C64",          "prg", },
  { "C64.TAPE",       "EEMO" MBCSEQ,        "/media/fat/_Computer/C64_",               "C64",          "rom", },
  { "COCO_2",         "EEMDDDDDDO" MBCSEQ,  "/media/fat/_Computer/CoCo2_",             "CoCo2",        "rom", },
  { "COCO_2.CAS",     "EEMDDDDDDDO" MBCSEQ, "/media/fat/_Computer/CoCo2_",             "CoCo2",        "cas", },
  { "COCO_2.CCC",     "EEMDDDDDDO" MBCSEQ,  "/media/fat/_Computer/CoCo2_",             "CoCo2",        "ccc", },
  { "COLECO",         "EEMO" MBCSEQ,        "/media/fat/_Console/ColecoVision_",       "Coleco",       "col", },
  { "COLECO.SG",      "EEMDO" MBCSEQ,       "/media/fat/_Console/ColecoVision_",       "Coleco",       "sg",  },
  { "CUSTOM",         "EEMO" MBCSEQ,        "/media/fat/_Console/NES_",                "NES",          "nes", },
  { "EDSAC",          "EEMO" MBCSEQ,        "/media/fat/_Computer/EDSAC_",             "EDSAC",        "tap", },
  { "GALAKSIJA",      "EEMO" MBCSEQ,        "/media/fat/_Computer/Galaksija_",         "Galaksija",    "tap", },
  { "GAMEBOY",        "EEMO" MBCSEQ,        "/media/fat/_Console/Gameboy_",            "GameBoy",      "gb",  },
  { "GAMEBOY.COL",    "EEMO" MBCSEQ,        "/media/fat/_Console/Gameboy_",            "GameBoy",      "gbc", },
  { "GBA",            "EEMO" MBCSEQ,        "/media/fat/_Console/GBA_",                "GBA",          "gba", },
  { "GENESIS",        "EEMO" MBCSEQ,        "/media/fat/_Console/Genesis_",            "Genesis",      "gen",  },
  { "INTELLIVISION",  "EEMO" MBCSEQ,        "/media/fat/_Console/Intellivision_",      "Intellivision", "bin", },
  { "JUPITER",        "EEMO" MBCSEQ,        "/media/fat/_Computer/Jupiter_",           "Jupiter_",     "ace", },
  { "LASER310",       "EEMO" MBCSEQ,        "/media/fat/_Computer/Laser310_",          "Laser310_",    "vz",  },
  { "MACPLUS.2",      "EEMO" MBCSEQ,        "/media/fat/_Computer/MacPlus_",           "MACPLUS",      "dsk", },
  { "MACPLUS.VHD",    "EEMDO" MBCSEQ,       "/media/fat/_Computer/MacPlus_",           "MACPLUS",      "dsk", },
  { "MEGACD",         "EEMO" MBCSEQ,        "/media/fat/_Console/MegaCD_",             "MegaCD",       "chd", },
  { "MEGACD.CUE",     "EEMO" MBCSEQ,        "/media/fat/_Console/MegaCD_",             "MegaCD",       "cue", },
  { "MEGADRIVE",      "EEMO" MBCSEQ,        "/media/fat/_Console/Genesis_",            "Genesis",      "md",  },
  { "MEGADRIVE.BIN",  "EEMO" MBCSEQ,        "/media/fat/_Console/Genesis_",            "Genesis",      "bin",  },
  { "MSX",            "EEMO" MBCSEQ,        "/media/fat/_Computer/MSX_",               "MSX",          "vhd", },
  { "NEOGEO",         "EEMO" MBCSEQ,        "/media/fat/_Console/NeoGeo_",             "NeoGeo",       "neo", },
  { "NES",            "EEMO" MBCSEQ,        "/media/fat/_Console/NES_",                "NES",          "nes", },
  { "NES.FDS",        "EEMO" MBCSEQ,        "/media/fat/_Console/NES_",                "NES",          "fds", },
  { "ODYSSEY2",       "EEMO" MBCSEQ,        "/media/fat/_Console/Odyssey2_",           "ODYSSEY2",     "bin", },
  { "ORAO",           "EEMO" MBCSEQ,        "/media/fat/_Computer/ORAO_",              "ORAO",         "tap", },
  { "ORIC",           "EEMO" MBCSEQ,        "/media/fat/_Computer/Oric_",              "Oric_",        "dsk", },
  { "PDP1",           "EEMO" MBCSEQ,        "/media/fat/_Computer/PDP1_",              "PDP1",         "bin", },
  { "PET2001",        "EEMO" MBCSEQ,        "/media/fat/_Computer/PET2001_",           "PET2001",      "prg", },
  { "PET2001.TAP",    "EEMO" MBCSEQ,        "/media/fat/_Computer/PET2001_",           "PET2001",      "tap", },
  { "QL",             "EEMO" MBCSEQ,        "/media/fat/_Computer/QL_",                "QL_",          "mdv", },
  { "SAMCOUPE.1",     "EEMO" MBCSEQ,        "/media/fat/_Computer/SAMCoupe_",          "SAMCOUPE",     "img", },
  { "SAMCOUPE.2",     "EEMDO" MBCSEQ,       "/media/fat/_Computer/SAMCoupe_",          "SAMCOUPE",     "img", },
  { "SCRIPT",         "EDDDODOHDO",         "/media/fat/menu",                         "/media/fat/_Scripts", "sh", "/media/fat/Scripts/~~~.sh" },
  { "SMS",            "EEMO" MBCSEQ,        "/media/fat/_Console/SMS_",                "SMS",          "sms", },
  { "SMS.GG",         "EEMDO" MBCSEQ,       "/media/fat/_Console/SMS_",                "SMS",          "gg", },
  { "SNES",           "EEMO" MBCSEQ,        "/media/fat/_Console/SNES_",               "SNES",         "sfc", },
  { "SPECIALIST",     "EEMO" MBCSEQ,        "/media/fat/_Computer/Specialist_",        "Specialist_",  "rsk", },
  { "SPECIALIST.ODI", "EEMDO" MBCSEQ,       "/media/fat/_Computer/Specialist_",        "Specialist_",  "odi", },
  { "SPECTRUM",       "EEMDO" MBCSEQ,       "/media/fat/_Computer/ZX-Spectrum_",       "Spectrum",     "tap", },
  { "SPECTRUM.DSK",   "EEMO" MBCSEQ,        "/media/fat/_Computer/ZX-Spectrum_",       "Spectrum",     "dsk", },
  { "SPECTRUM.SNAP",  "EEMDDO" MBCSEQ,      "/media/fat/_Computer/ZX-Spectrum_",       "Spectrum",     "z80", },
  { "SUPERGRAFX",     "EEMDO" MBCSEQ,       "/media/fat/_Console/TurboGrafx16_",       "TGFX16",       "sgx", },
  { "TGFX16",         "EEMO" MBCSEQ,        "/media/fat/_Console/TurboGrafx16_",       "TGFX16",       "pce", },
  { "TGFX16-CD",      "EEMDDO" MBCSEQ,      "/media/fat/_Console/TurboGrafx16_",       "TGFX16-CD",    "chd", },
  { "TGFX16-CD.CUE",  "EEMDDO" MBCSEQ,      "/media/fat/_Console/TurboGrafx16_",       "TGFX16-CD",    "cue", },
  { "TI-99_4A",       "EEMDO" MBCSEQ,       "/media/fat/_Computer/Ti994a_",            "TI-99_4A",     "bin", },
  { "TI-99_4A.D",     "EEMDDO" MBCSEQ,      "/media/fat/_Computer/Ti994a_",            "TI-99_4A",     "bin", },
  { "TI-99_4A.G",     "EEMDDO" MBCSEQ,      "/media/fat/_Computer/Ti994a_",            "TI-99_4A",     "bin", },
  { "TRS-80",         "EEMO" MBCSEQ,        "/media/fat/_Computer/TRS-80_",            "TRS-80_",      "dsk", },
  { "TRS-80.1",       "EEMDO" MBCSEQ,       "/media/fat/_Computer/TRS-80_",            "TRS-80_",      "dsk", },
  { "TSCONF",         "EEMO" MBCSEQ,        "/media/fat/_Computer/TSConf_",            "TSConf_",      "vhd", },
  { "VC4000",         "EEMO" MBCSEQ,        "/media/fat/_Console/VC4000_",             "VC4000",       "bin", },
  { "VECTOR06",       "EEMO" MBCSEQ,        "/media/fat/_Computer/Vector-06C_""/core", "VECTOR06",     "rom", },
  { "VECTOR06.A",     "EEMDO" MBCSEQ,       "/media/fat/_Computer/Vector-06C_""/core", "VECTOR06",     "fdd", },
  { "VECTOR06.B",     "EEMDDO" MBCSEQ,      "/media/fat/_Computer/Vector-06C_""/core", "VECTOR06",     "fdd", },
  { "VECTREX",        "EEMO" MBCSEQ,        "/media/fat/_Console/Vectrex_",            "VECTREX",      "vec", },
  { "VECTREX.OVR",    "EEMDO" MBCSEQ,       "/media/fat/_Console/Vectrex_",            "VECTREX",      "ovr", },
  { "VIC20",          "EEMO" MBCSEQ,        "/media/fat/_Computer/VIC20_",             "VIC20",        "prg", },
  { "VIC20.CART",     "EEMDO" MBCSEQ,       "/media/fat/_Computer/VIC20_",             "VIC20",        "crt", },
  { "VIC20.CT",       "EEMDDO" MBCSEQ,      "/media/fat/_Computer/VIC20_",             "VIC20",        "ct",  },
  { "VIC20.DISK",     "EEMDDDO" MBCSEQ,     "/media/fat/_Computer/VIC20_",             "VIC20",        "d64", },
  { "WONDERSWAN",     "EEMO" MBCSEQ,        "/media/fat/_Console/WonderSwan_",         "WonderSwan",   "ws",  },
  { "WONDERSWAN.COL", "EEMO" MBCSEQ,        "/media/fat/_Console/WonderSwan_",         "WonderSwan",   "wsc", },
  { "ZX81",           "EEMO" MBCSEQ,        "/media/fat/_Computer/ZX81_",              "ZX81",         "0",   },
  { "ZX81.P",         "EEMO" MBCSEQ,        "/media/fat/_Computer/ZX81_",              "ZX81",         "p",   },
  { "ZXNEXT",         "EEMODOD!mOMUUO!m" MBCSEQ, "/media/fat/_Computer/ZXNext_",       "ZXNext",       "vhd", },

  // unsupported
  //{ "AMIGA",          "EEMO" MBCSEQ,        "/media/fat/_Computer/Minimig_",           "/media/fat/games/Amiga",     0, },
  //{ "ATARIST",        "EEMO" MBCSEQ,        "/media/fat/_Computer/AtariST_",           "/media/fat/games/AtariST",     0, },
  //{ "AY-3-8500",      0,                    "/media/fat/_Console/AY-3-8500_",          "/media/fat/games/AY-3-8500",    0, },
  //{ "Altair8800"      , 0 , 0, 0, 0, },
  //{ "MULTICOMP",      0,                    "/media/fat/_Computer/MultiComp_",         "/media/fat/games/MultiComp",   0, },
  //{ "MultiComp"       , 0 , 0, 0, 0, },
  //{ "SHARPMZ",        "EEMO" MBCSEQ,        "/media/fat/_Computer/SharpMZ_",           "/media/fat/games/SharpMZ_",     0, },
  //{ "X68000"          , 0 , 0, 0, 0, },
};

int core_wait = 3000; // ms
int inter_key_wait = 40; // ms
int sequence_wait = 1000; // ms

static void emulate_key_press(int fd, int key) {
  msleep(inter_key_wait);
  ev_emit(fd, EV_KEY, key, 1);
  ev_emit(fd, EV_SYN, SYN_REPORT, 0);
}

static void emulate_key_release(int fd, int key) {
  msleep(inter_key_wait);
  ev_emit(fd, EV_KEY, key, 0);
  ev_emit(fd, EV_SYN, SYN_REPORT, 0);
}

static void emulate_key(int fd, int key) {
  emulate_key_press(fd, key);
  emulate_key_release(fd, key);
}

static void key_emulator_wait_mount(){
  static size_t mnthash = 0;
  LOG("%s\n", "waiting for some change in the mount table");
  size_t newhash = mnthash;
  for(int retry = 0; retry < 20; retry += 1){
    newhash = contenthash("/proc/mounts");
    if (newhash != mnthash) break;
    msleep(500);
  }
  if (newhash != mnthash) LOG("%s (%ld)\n", "detected a change in the mounting points", newhash);
  else LOG("%s (%ld)\n", "no changes in the mounting points (timeout)", newhash);
  mnthash = newhash;
}

static void key_emulator_function(int fd, int code){
  switch (code){
    default: return;
    break; case KEY_M: key_emulator_wait_mount();
    break; case KEY_S: msleep(1000);
  }
}

#define TAG_KEY_NOTHING '\0'
#define TAG_KEY_PRESS   '{'
#define TAG_KEY_RELEASE '}'
#define TAG_KEY_FULL    ':'
#define TAG_KEY_FUNCT   '!'

static char* parse_hex_byte(char* seq, int* code, int* tag){
  int c, n;
  if (0> sscanf(seq, "%2x%n", &c, &n) || 2!= n) return 0;
  if (code) *code = c;
  if (tag) *tag = TAG_KEY_NOTHING;
  return seq+n;
}

static char* parse_tagged_byte(char* seq, int* code, int* tag){
  if (seq[1] == '\0') return 0;
  char* result = parse_hex_byte(seq+1, code, 0);
  if( !result) return 0;
  switch( *seq){
    default: return 0;
    // TODO : use different char and tag definition
    break; case TAG_KEY_FULL: if (tag) *tag = TAG_KEY_FULL;
    break; case TAG_KEY_PRESS: if (tag) *tag = TAG_KEY_PRESS;
    break; case TAG_KEY_RELEASE: if (tag) *tag = TAG_KEY_RELEASE;
  }
  return result;
}

static char* parse_alphanumeric_key(char* seq, int* code, int* tag){
  int i = 0; if (!code) code = &i;
  switch (*seq) {
    default: return 0;

    break;case '0': *code = KEY_0;
    break;case '1': *code = KEY_1;
    break;case '2': *code = KEY_2;
    break;case '3': *code = KEY_3;
    break;case '4': *code = KEY_4;
    break;case '5': *code = KEY_5;
    break;case '6': *code = KEY_6;
    break;case '7': *code = KEY_7;
    break;case '8': *code = KEY_8;
    break;case '9': *code = KEY_9;
    break;case 'a': *code = KEY_A;
    break;case 'b': *code = KEY_B;
    break;case 'c': *code = KEY_C;
    break;case 'd': *code = KEY_D;
    break;case 'e': *code = KEY_E;
    break;case 'f': *code = KEY_F;
    break;case 'g': *code = KEY_G;
    break;case 'h': *code = KEY_H;
    break;case 'i': *code = KEY_I;
    break;case 'j': *code = KEY_J;
    break;case 'k': *code = KEY_K;
    break;case 'l': *code = KEY_L;
    break;case 'm': *code = KEY_M;
    break;case 'n': *code = KEY_N;
    break;case 'o': *code = KEY_O;
    break;case 'p': *code = KEY_P;
    break;case 'q': *code = KEY_Q;
    break;case 'r': *code = KEY_R;
    break;case 's': *code = KEY_S;
    break;case 't': *code = KEY_T;
    break;case 'u': *code = KEY_U;
    break;case 'v': *code = KEY_V;
    break;case 'w': *code = KEY_W;
    break;case 'x': *code = KEY_X;
    break;case 'y': *code = KEY_Y;
    break;case 'z': *code = KEY_Z;
  }
  if (tag) *tag = TAG_KEY_FULL;
  return seq+1;
}

static char* parse_tagged_alphanumeric_key(char* seq, int* code, int* tag){
  char* result = parse_alphanumeric_key(seq+1, code, tag);
  if (!result) return 0;
  if (TAG_KEY_FUNCT != *seq) return 0; // TODO : use different char and tag definition
  if (tag) *tag = TAG_KEY_FUNCT;
  return result;
}

static char* parse_special_key(char* seq, int* code, int* tag){
  int i = 0; if (!code) code = &i;
  switch (*seq) {
    default: return 0;

    break;case 'U': *code = KEY_UP;    // 103 0x67 up
    break;case 'D': *code = KEY_DOWN;  // 108 0x6c down
    break;case 'L': *code = KEY_LEFT;  // 105 0x69 left
    break;case 'R': *code = KEY_RIGHT; // 106 0x6a right
    break;case 'O': *code = KEY_ENTER; //  28 0x1c enter (Open)
    break;case 'E': *code = KEY_ESC;   //   1 0x01 esc
    break;case 'H': *code = KEY_HOME;  // 102 0x66 home
    break;case 'F': *code = KEY_END;   // 107 0x6b end (Finish)
    break;case 'M': *code = KEY_F12;   //  88 0x58 f12 (Menu)
  }
  if (tag) *tag = TAG_KEY_FULL;
  return seq+1;
}

static char* parse_key_sequence(char* seq, int* code, int* tag){
  char *next;
  if (0!=( next = parse_alphanumeric_key(seq, code, tag) )) return next;
  if (0!=( next = parse_tagged_alphanumeric_key(seq, code, tag) )) return next;
  if (0!=( next = parse_special_key(seq, code, tag) )) return next;
  if (0!=( next = parse_tagged_byte(seq, code, tag) )) return next;
  return 0;
}

static int emulate_sequence(char* seq) {

  int fd = ev_open();
  if (fd < 0) {
    return -1;
  }

  // Wait that userspace detects the new device
  msleep(sequence_wait);

  while (seq && '\0' != *seq) {
    int code = 0, tag = 0;

    // Parse the sequence
    char* newseq = parse_key_sequence(seq, &code, &tag);
    if (0 == newseq || seq == newseq) goto err; // can not parse
    seq = newseq;

    // Emulate the keyboard event
    switch (tag) {
      default: goto err; // unsupported action

      break;case TAG_KEY_FULL: emulate_key(fd, code);
      break;case TAG_KEY_PRESS: emulate_key_press(fd, code);
      break;case TAG_KEY_RELEASE: emulate_key_release(fd, code);
      break;case TAG_KEY_FUNCT: key_emulator_function(fd, code);
    }
  }

  // Wait that userspace detects all the events
  msleep(sequence_wait);

  ev_close(fd);
  return 0;

err:
  ev_close(fd);
  return -1;
}

static char* after_string(char* str, char delim) {
  for (char* curr = str; '\0' != *curr; curr += 1){
    if (delim == *curr) {
      str = curr+1;
    }
  }
  return str;
}

static void  get_core_name(char* corepath, char* out, int size) {

  char* start = after_string(corepath, '/');
  if (NULL == start) {
    return;
  }

  char* end = after_string(start, '_');
  end -= 1;
  int len = end - start;
  if (len <= 0) {
    return;
  }

  size -= 1;
  if (size > len) size = len;
  strncpy(out, start, size);
  out[size] = '\0';

  for (int i = 0; i < len; i++){
    out[i] = toupper(out[i]);
  }
}

static int cmp_char_ptr_field(const void * a, const void * b) {
  return strcmp(*(const char**)a, *(const char**)b);
}

static system_t* get_system(char* corepath, char * name) {

  char system[64] = {0, };

  if (NULL == name) {
    get_core_name(corepath, system, ARRSIZ(system));
    if ('\0' == system[0]) {
      return NULL;
    }
    name = system;
  }

  system_t target = {name, 0};
  return (system_t*) SBSEARCH(&target, system_list, cmp_char_ptr_field);
}

static int load_core(system_t* sys, char* corepath) {

  if (NULL == sys) {
    PRINTERR("%s\n", "invalid system");
    return -1;
  }
  
  while (1) {
    int tmp = open(MISTER_COMMAND_DEVICE, O_WRONLY|O_NONBLOCK);
    if (0<= tmp) {
      close(tmp);
      break;
    }
    LOG("%s\n", "can not access the MiSTer command fifo; retrying");
    msleep(1000);
  }

  FILE* f = fopen(MISTER_COMMAND_DEVICE, "wb");
  if (0 == f) {
    PRINTERR("%s\n", MISTER_COMMAND_DEVICE);
    return -1;
  }
 
  // TODO : check that a file exists at corepath ? 
  
  int ret = fprintf(f, "load_core %s\n", corepath);
  if (0 > ret){
    return -1;
  }

  fclose(f);
  return 0;
}

// This MUST BE KEPT IN SYNC with the findPrefixDir function of the Main_MiSTer (file_io.cpp)
//
int findPrefixDirAux(const char* path, char *dir, size_t dir_len){
  char temp_dir[dir_len+1];
	if (0> snprintf(temp_dir, dir_len, "%s/%s", path, dir)) return 0;
  struct stat sb;
  if (stat(temp_dir, &sb)) return 0;
  if (!S_ISDIR(sb.st_mode)) return 0;
	if (0> snprintf(dir, dir_len, "%s", temp_dir)) return 0;
  return 1;
}
int findPrefixDir(char *dir, size_t dir_len){

  if (findPrefixDirAux("/media/usb0", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/usb0/games", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/usb1", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/usb1/games", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/usb2", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/usb2/games", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/usb3", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/usb3/games", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/usb4", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/usb4/games", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/usb5", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/usb5/games", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/usb6", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/usb6/games", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/fat/cifs", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/fat/cifs/games", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/fat", dir, dir_len)) return 1;
  if (findPrefixDirAux("/media/fat/games", dir, dir_len)) return 1;

	return 0;
}

static void get_base_path(system_t* sys, char* out, int size) {
  if ('/' == sys->fsid[0]) {
    snprintf(out, size-1, "%s", sys->fsid);
  } else {
    snprintf(out, size-1, "%s", sys->fsid);
    if (!findPrefixDir(out, size))
      snprintf(out, size-1, "/media/fat/games/%s", sys->fsid); // fallback
  }
}

static char* get_rom_extension( system_t* sys){
  if( !sys) return "";
  return sys->romext;
}

static void get_link_path(system_t* sys, const char* filename, char* out, int size) {
  if (NULL != sys->sublink) {
    snprintf(out, size-1, "%s", sys->sublink);
  } else {
    get_base_path(sys, out, size);
    int dl = strlen(out);
    snprintf(out+dl, size-dl-1, "/%s/%s.%s", ROMSUBLINK, filename?filename:MBC_LINK_NAM, get_rom_extension( sys));
  }
}

static int create_file(char* path){
  int result = mkparent(path, 0777);
  if (result) return result;
  FILE* f = fopen(path, "ab");
  if (!f) return errno;
  return fclose(f);
}

static int filesystem_bind(const char* source, const char* target) {
  int err = 0;
  for(int r = 0; r < 20; r += 1){
    if (r > 14) LOG("retrying the binding since the mount point is busy (%s -> %s)\n", source, target);
    if (r != 0) msleep(1000);

    err = mount(source, target, "", MS_BIND | MS_RDONLY | MS_REC, "");

    if (err) err = errno;
    if (EBUSY != err) break;
  }
  return err;
}

static int filesystem_unbind(const char* path) {
  int err = 0;
  for(int r = 0; r < 10; r += 1){
    if (r > 7) LOG("retrying the unbinding since the mount point is busy (%s)\n", path);
    if (r != 0) msleep(500);

    err = umount(path);

    if (err) err = errno;
    if (EBUSY != err) break;
  }
  if (EBUSY == err){
    LOG("%s\n", "trying to unmounting with the DETACH option since nothing else worked");
    err = umount2(path, MNT_DETACH);
  }
  return err;
}

static int is_empty_file(const char* path){
  struct stat st;
  int stat_error = stat(path, &st);
  if (stat_error) return 0;
  if (!S_ISREG(st.st_mode)) return 0;
  if (0 != st.st_size) return 0;
  return 1;
}

static int rom_link_default(system_t* sys, char* path) {

  //
  // We will make some folder and file that will appear in the MiSTer file
  // selection menu. In order to let this items to be automatically selected by
  // the emulated key sequence, a folder is created in the rom directory with a
  // name that places it at very beginning of the list. Moreover such folder
  // will always contain a single file to be selected.
  //
  // We need the auxiliary folder since some cores do not simply sort the item
  // by the file name, for example the NeoGeo shows the roms by an internal
  // name. So using a simple file inside the rom directory will not suffice to
  // make such item automatically selected because we can not predict where it
  // is placed in the list.
  //
  // Moreover, we can not use symbolyc links to link the auxiliary file with
  // the target rom since the rom directory shown in the MiSTer menu could be
  // on a filesystem without links support (e.g. vfat in /media/usb0). So we
  // use bind-mounts.
  //
  // The system is cleaned up in rom_unlink_default.
  //

  char filename[strlen(path)];
  filename[0] = '\0';
  strcpy(filename, after_string(path, '/'));
  char* ext = after_string(filename,'.');
  if (ext > filename+1) *(ext-1) = '\0';

  char linkpath[PATH_MAX] = {0};
  get_link_path(sys, filename, linkpath, sizeof(linkpath));

  if (create_file(linkpath)) {
    PRINTERR("Can not create rom link file or folder %s\n", linkpath);
    return -1;
  }

  if (filesystem_bind(path, linkpath)) {
    PRINTERR("Can not bind %s to %s\n", path, linkpath);
    return -1;
  }

  return 0;
}

static int rom_unlink_default(system_t* sys) {

  //
  // This must clear up the work done in rom_link_default. First we unbind the rom
  // file; if it succeeded, we remove the file that was an empty one made just
  // to have a mount point. The containing folder then is removed.  If
  // something goes wrong, the auxiliary folder and file may remain in the
  // filesystem. To mitigate this issue we retry critical operations multiple
  // times. Moreover we unmount and remove any file in the auxiliary folder so
  // a successive mbc invocation have chance to clean up entities created in
  // previous failiing invocations.
  //

  char aux_path[PATH_MAX] = {0};

  get_link_path(sys, NULL, aux_path, sizeof(aux_path));
  path_parentize(aux_path, 0);

  struct dirent* ep = NULL;
  DIR* dp = opendir(aux_path);
  if (dp != NULL) {
    while (0 != (ep = readdir (dp))){
      if (!strcmp(".",ep->d_name) || !strcmp("..",ep->d_name))
        continue;
      char p[PATH_MAX] = {0};
      snprintf(p, sizeof(p), "%s/%s", aux_path, ep->d_name);
      if (!filesystem_unbind(p) && is_empty_file(p))
        remove(p);
    }
  }

  rmdir(aux_path); // No issue if error

  return 0;
}

char* search_in_string(const char* pattern_start, const char* data, size_t *size){
  char *pattern, *candidate;

#define MATCH_RESET() do{ \
  pattern = (char*) pattern_start; \
  candidate = NULL; \
}while(0)

  MATCH_RESET();
  while( 1){

    if('\0' == *pattern) goto matched;
    if('\0' == *data) goto not_matched;
    if(pattern_start == pattern) candidate = (char*) data;

//    if('%' != *pattern){ // simple char vs wildcard delimiter
      if(*pattern == *data) pattern += 1; // simple char match
      else MATCH_RESET();  // simple char do not match
//    }else{
//
//      pattern += 1;
//      switch(*pattern){
//        break; default: // wrong wildcard
//          goto not_matched;
//
//        break; case '%': // match a '%' (escaped wildcard delimiter)
//          if('%' == *data) pattern += 1;
//          else MATCH_RESET();
//
//        break; case 'W': // match zero or more whitespace
//          while(' ' == *data || '\t' == *data || '\r' == *data || '\n' == *data)
//            data += 1;
//          data -= 1;
//          pattern += 1;
//      }
//    }
    data += 1;
  }
not_matched:
  candidate = NULL;

matched:
  if(size){
    if(!candidate) *size = 0;
    else *size = data - candidate + 1;
  }
  return candidate;

#undef MATCH_RESET
}


int get_absolute_dir_name(const char* source, char* out, size_t len){
  char dirpath[PATH_MAX];
  realpath(source, dirpath);
  if( -1 == snprintf(out, len, "%s", dirpath))
    return -1;
  return 0;
}

int get_relative_path_to_root(int skip, const char* path, char* out, size_t len){
  char dirpath[PATH_MAX];
  dirpath[0] = '\0';
  get_absolute_dir_name(path, dirpath, sizeof(dirpath));
  char* cur = out;
  snprintf(cur, len, "./");
  len -= 2;
  cur += 2;
  for(int i = 0, count = 0; dirpath[i] != '\0'; i += 1){
    if(dirpath[i] == '/'){
      count += 1;
      if(count > skip){
        snprintf(cur, len, "../");
        len -= 3;
        cur += 3;
      }
    }
  }
  return 0;
}

int cue_rebase(char* source, char* destination){

  //
  // To rebase a .cue we need to substitue all the line like the following:
  //   FILE "filename.bin" BINARY
  //
  // We suppose that between quote there is just a filename that normally is
  // located in the same directory of the .cue. The MiSTer handles this by
  // adding in front of the filename the path to the .cue containing folder:
  //   /path/to/original_cue_folder/filename.bin
  //
  // If we copy the file without modification in a new folder, the MiSTer
  // wil try to access
  //   /path/to/new_cue_folder/filename.bin
  //
  // To fake such system, we replace the original line with
  //   FILE "../../../path/to/original_cue_folder/filename.bin" BINARY
  //
  // so that the MiSTer will search for
  //   /path/to/new_cue_folder/../../../path/to/original_cue_folder/filename.bin
  //
  // There must be enought "../" to reach the root folder starting from the
  // new_cue_folder one. In this way
  //   /path/to/original_cue_folder
  //
  // is simply the absolute path to the auxiliary directory where the new .cue
  // file is placed
  //

  FILE* in = fopen(source, "r");
  if(!in){
    PRINTERR("Can not open %s\n", source);
    return -1;
  }

  char inc[4096]; inc[0] = '\0';
  size_t len = fread(inc, 1, sizeof( inc), in);
  fclose(in);

  FILE* out = fopen(destination, "w");
  if(!out){
    PRINTERR( "Can not open %s\n", destination);
    return -2;
  }

  char dirpath[PATH_MAX];
  strncpy(dirpath, destination, sizeof( dirpath));
  get_relative_path_to_root(1, dirpath, dirpath, sizeof( dirpath));
  int cat = strlen(dirpath)-1;
  get_absolute_dir_name(source, dirpath+cat, sizeof( dirpath)-cat-1);
  path_parentize(dirpath, 1);

  char* curr = inc;
  while(0 < len){
    size_t size = 0;
    char* next = search_in_string("FILE \"", curr, &size);
    if(NULL == next){
      fwrite(curr, 1, len, out);
      break;
    }else{
      fwrite(curr, 1, next - curr, out);
      fwrite("FILE \"", 1, sizeof("FILE \"")-1, out);
      fwrite(dirpath, 1, strlen(dirpath), out);
      len -= next - curr + size;
      curr = next + size - 1;
    }
  }

  fclose(out);
  return 0;
}

static int rom_link_cue(system_t* sys, char* path) {

  //
  // Look at rom_link_default for the common case overview.
  // The .cue is handled in a different way since the MiSTer may need to locate
  // more than one file. So we simply generate a new .cue in the auxiliary
  // directory that is a copy of the source one, except for the paths that
  // are rebased in the new destination folder (look at rebase_cue for more
  // detaiks).
  //
  // The system is cleaned up by the rom_unlink_cue function.
  // 

  char filename[strlen( path)];
  filename[0] = '\0';
  strcpy(filename, after_string(path, '/'));
  char* ext = after_string(filename,'.');
  if (ext > filename+1) *(ext-1) = '\0';

  char linkpath[PATH_MAX] = {0};
  get_link_path(sys, filename, linkpath, sizeof(linkpath));

  if (create_file(linkpath)) {
    PRINTERR("Can not create rom link file or folder %s\n", linkpath);
    return -1;
  }

  if (cue_rebase(path, linkpath)) {
    return -1;
  }

  return 0;
}

int stricmp(const char* a, const char* b) {
  for (; tolower(*a) == tolower(*b); a += 1, b += 1)
    if (*a == '\0')
      return 0;
  return tolower(*a) - tolower(*b);
}

static int rom_unlink_cue( system_t* sys) {

  //
  // This will clear the work done in the rom_link_cue function.
  // We simply delete all the .cue in the auxiliary directory, and the directory
  // itself. Look at rom_unlink_default for details on what can happen if
  // something goes wrong.
  //

  char aux_path[ PATH_MAX] = {0};

  get_link_path( sys, NULL, aux_path, sizeof(aux_path));
  path_parentize( aux_path, 0);

  struct dirent* ep = NULL;
  DIR* dp = opendir( aux_path);
  if (dp != NULL) {
    while (0 != ( ep = readdir( dp))){
      if (!strcmp( ".", ep->d_name) || !strcmp( "..", ep->d_name))
        continue;
      char p[ PATH_MAX] = {0};
      snprintf( p, sizeof(p), "%s/%s", aux_path, ep->d_name);
      if (!stricmp(get_rom_extension(sys), after_string(ep->d_name, '.')))
        remove( p);
    }
  }

  rmdir( aux_path); // No issue if error

  return 0;
}

static int rom_link( system_t* sys, char* path) {
  // The defauld link mechanism is in rom_link_default. Some files need a custom
  // one, so this is a dispatcher.

  if( !strcmp( get_rom_extension(sys), "cue")){
    return rom_link_cue( sys, path);
  }else{
    return rom_link_default( sys, path);
  }
}

static int rom_unlink( system_t* sys) {
  // The defauld link mechanism is in rom_unlink_default. Some files need a custom
  // one, so this is a dispatcher.

  if( !strcmp( get_rom_extension(sys), "cue")){
    return rom_unlink_cue( sys);
  }else{
    return rom_unlink_default( sys);
  }
}

static int emulate_system_sequence(system_t* sys) {
  if (NULL == sys) {
    return -1;
  }
  return emulate_sequence(sys->menuseq);
}

static int load_rom(system_t* sys, char* rom) {
  int err;

  err = rom_link(sys, rom);
  if (err) PRINTERR("%s\n", "Can not bind the rom");

  if (!err) {
    err = emulate_system_sequence(sys);
    if (err) PRINTERR("%s\n", "Error during key emulation");
  }

  err = rom_unlink(sys);
  if (err) PRINTERR("%s\n", "Can not unbind the rom");

  return err;
}

static int has_ext(char* name, char* ext){
  char* name_ext = after_string(name, '.');
  if (name_ext == name) return 0;
  while (1) {
    if (tolower(*ext) != tolower(*name_ext)) return 0;
    if (*name_ext != '\0') break;
    ext += 1;
    name_ext += 1;
  }
  return 1;
}

static int resolve_core_path(char* path, char* out, int len){
  strncpy(out, path, len);
  char* name = after_string(out, '/');
  if (*name == '\0' || name - out < 3) {
    return -1;
  }
  name[-1] = '\0';
  int matched = 0;
  struct dirent* ep = NULL;
  DIR* dp = opendir(out);
  if (dp != NULL) {
    int nlen = strlen(name);
    while (0 != (ep = readdir (dp))){
      if (!strncmp(name, ep->d_name, nlen) && has_ext(ep->d_name, CORE_EXT)){
        matched = 1;
        snprintf(out+strlen(out), len-strlen(out)-1, "/%s", ep->d_name);
        break;
      }
    }
  }
  if (!matched) {
    return -1;
  }
  return 0;
}

static int list_core(){
  for (int i=0; i<ARRSIZ(system_list); i++){
    int plen = 64 + strlen(system_list[i].core);
    char corepath[plen];
    if (0 != resolve_core_path(system_list[i].core, corepath, plen)){
      printf("#%s can not be found with prefix %s\n", system_list[i].id, system_list[i].core);
    } else {
      printf("%s %s\n", system_list[i].id, corepath);
    }
  }
  return 0;
}

static int load_rom_autocore(system_t* sys, char* rom) {

  if (NULL == sys) {
    return -1;
  }

  int plen = 64 + strlen(sys->core);
  char corepath[plen];

  if (resolve_core_path(sys->core, corepath, plen)){
    PRINTERR("Can not find the core at %s\n", sys->core);
    return -1;
  }

  int ret = load_core(sys, corepath);
  if (0 > ret) {
    PRINTERR("%s\n", "Can not load the core");
    return -1;
  }

  msleep(core_wait);

  return load_rom(sys, rom);
}

static int load_core_and_rom(system_t* sys, char* corepath, char* rom) {

  if (NULL == sys) {
    return -1;
  }

  int ret = load_core(sys, corepath);
  if (0 > ret) {
    PRINTERR("%s\n", "Can not load the core");
    return -1;
  }

  msleep(core_wait);

  return load_rom(sys, rom);
}

struct cmdentry {
  const char * name;
  void (*cmd)(int argc, char** argv);
};

int checkarg(int min, int val){
  if (val >= min+1) return 1;
  PRINTERR("At least %d arguments are needed\n", min);
  return 0;
}

int list_content_for(system_t* sys){
  DIR *dp;
  struct dirent *ep;
  int something_found = 0;

  char romdir[PATH_MAX] = {0};
  get_base_path(sys, romdir, sizeof(romdir));

  dp = opendir(romdir);
  if (dp != NULL) {
    while (0 != (ep = readdir (dp))){

      if (has_ext(ep->d_name, get_rom_extension( sys))){
        something_found = 1;
        printf("%s %s/%s\n", sys->id, romdir, ep->d_name);
      }
    }
    closedir(dp);
  }
  if (!something_found) {
    //printf("#%s no '.%s' files found in %s\n", sys->id, get_rom_extension( sys), romdir);
  }
  return something_found;
}

int list_content(){
  for (int i=0; i<ARRSIZ(system_list); i++){
    list_content_for(system_list+i);
  }
  return 0;
}

int monitor_user_input(int single, char* ms){

  int timeout;
  if (0> sscanf(ms, "%d", &timeout)){
    printf("error\n");
    return -1;
  }
  input_monitor monitor;
  int result = user_input_open(&monitor);
  if (result) goto end;

  for (int c = 1; 1;){

    result = user_input_poll(&monitor, timeout);
    if (!single) c += user_input_clear(&monitor);

    if (is_user_input_timeout(result)) printf("timeout\n");
    else if (is_user_input_event(result)) printf("event catched %d\n", c);

    if (single) break;
  }
  goto end;

end:
  user_input_close(&monitor);
  if (!is_user_input_timeout(result) && ! is_user_input_event(result))
    PRINTERR("input monitor error %d\n", result);

  return 0;
}

static int stream_mode();

// command list
static void cmd_exit(int argc, char** argv)         { exit(0); }
static void cmd_stream_mode(int argc, char** argv)  { stream_mode(); }
static void cmd_load_core(int argc, char** argv)    { if(checkarg(1,argc))load_core(get_system(argv[1],NULL),argv[1]); }
static void cmd_load_core_as(int argc, char** argv) { if(checkarg(2,argc))load_core(get_system(NULL,argv[1]),argv[2]); }
static void cmd_load_rom(int argc, char** argv)     { if(checkarg(2,argc))load_rom(get_system(NULL,argv[1]),argv[2]); }
static void cmd_list_core(int argc, char** argv)    { list_core(); }
static void cmd_rom_autocore(int argc, char** argv) { if(checkarg(2,argc))load_rom_autocore(get_system(NULL,argv[1]),argv[2]); }
static void cmd_load_all(int argc, char** argv)     { if(checkarg(2,argc))load_core_and_rom(get_system(argv[1],NULL),argv[1],argv[2]); }
static void cmd_load_all_as(int argc, char** argv)  { if(checkarg(3,argc))load_core_and_rom(get_system(NULL,argv[1]),argv[2],argv[3]); }
static void cmd_rom_link(int argc, char** argv)     { if(checkarg(2,argc))rom_link(get_system(NULL,argv[1]),argv[2]); }
static void cmd_raw_seq(int argc, char** argv)      { if(checkarg(1,argc))emulate_sequence(argv[1]); }
static void cmd_select_seq(int argc, char** argv)   { if(checkarg(1,argc))emulate_system_sequence(get_system(NULL,argv[1])); }
static void cmd_rom_unlink(int argc, char** argv)   { if(checkarg(1,argc))rom_unlink(get_system(NULL,argv[1])); }
static void cmd_list_content(int argc, char** argv) { list_content(); }
static void cmd_list_rom_for(int argc, char** argv) { if(checkarg(1,argc))list_content_for(get_system(NULL,argv[1])); }
static void cmd_wait_input(int argc, char** argv)   { if(checkarg(1,argc))monitor_user_input(1,argv[1]); }
static void cmd_catch_input(int argc, char** argv)  { if(checkarg(1,argc))monitor_user_input(0,argv[1]); }
//
struct cmdentry cmdlist[] = {
  //
  // The "name" field can not contain ' ' or '\0'.
  // The array must be lexicographically sorted wrt "name" field (e.g.
  //   :sort vim command, but mind '!' and escaped chars at end of similar names).
  //
  {"catch_input"  , cmd_catch_input  , } ,
  {"done"         , cmd_exit         , } ,
  {"list_content" , cmd_list_content , } ,
  {"list_core"    , cmd_list_core    , } ,
  {"list_rom_for" , cmd_list_rom_for , } ,
  {"load_all"     , cmd_load_all     , } ,
  {"load_all_as"  , cmd_load_all_as  , } ,
  {"load_core"    , cmd_load_core    , } ,
  {"load_core_as" , cmd_load_core_as , } ,
  {"load_rom"     , cmd_rom_autocore , } ,
  {"load_rom_only", cmd_load_rom     , } ,
  {"raw_seq"      , cmd_raw_seq      , } ,
  {"rom_link"     , cmd_rom_link     , } ,
  {"rom_unlink"   , cmd_rom_unlink   , } ,
  {"select_seq"   , cmd_select_seq   , } ,
  {"stream"       , cmd_stream_mode  , } ,
  {"wait_input"   , cmd_wait_input   , } ,
};

static int run_command(int narg, char** args) {

  // match the command
  struct cmdentry target = {args[0], 0};
  struct cmdentry * command = (struct cmdentry*)
    SBSEARCH(&target, cmdlist, cmp_char_ptr_field);

  // call the command
  if (NULL == command || NULL == command->cmd) {
    PRINTERR("%s\n", "unknown command");
    return -1;
  }
  command->cmd(narg, args);
  return 0;
}

static int stream_mode() {
  char* line = NULL;
  size_t size = 0;
  while (1) {

    // read line
    int len = getline(&line, &size, stdin);
    if (-1 == len) {
      break;
    }
    len = len-1;
    line[len] = '\0';

    // Split command in arguments
    int narg = 0;
    char *args[5] = {0,};
    int matchnew = 1;
    for (int i=0; i<size; i++){
      if ('\0' == line[i] || '\n' == line[i]) {
        line[i] = '\0';
        break;
      }
      if (' ' == line[i] || '\t' == line[i]) {
        line[i] = '\0';
        matchnew = 1;
      } else if (matchnew) {
        args[narg] = line+i;
        narg += 1;
        matchnew = 0;
      }
    }
    if (0 == narg) {
      continue;
    }

    // do it
    run_command(narg, args);
  }
  if (line) free(line);
  return 0;
}

static void read_options(int argc, char* argv[]) {

  char* val;

  system_t* custom_system = get_system(NULL, "CUSTOM");
  if (NULL == custom_system){
    printf("no CUSTOM system record: CUSTOM can not be used\n");
  } else {

    // Note: no need to free the duplicated string since they last until the end of the run

    val = getenv("MBC_CUSTOM_CORE");
    if (NULL != val && val[0] != '\0') custom_system->core = strdup(val);
    val = getenv("MBC_CUSTOM_FOLDER");
    if (NULL != val && val[0] != '\0') custom_system->fsid = strdup(val);
    val = getenv("MBC_CUSTOM_ROM_EXT");
    if (NULL != val && val[0] != '\0') custom_system->romext = strdup(val);

    val = getenv("MBC_CUSTOM_LINK");
    int custom_link = 0;
    if (NULL != val && val[0] != '\0') {
      custom_link = 1;
      custom_system->sublink = strdup(val);
    }

    val = getenv("MBC_CUSTOM_SEQUENCE");
    if (NULL != val && val[0] != '\0') {
      int siz = strlen(val);
      char* seq = malloc(siz + strlen(MBCSEQ) +1);
      if (seq) {
        strcpy(seq, val);
        if (!custom_link)
          strcpy(seq + siz, MBCSEQ);
      }
      custom_system->menuseq = seq;
    }
  }

  val = getenv("MBC_CORE_WAIT");
  if (NULL != val && val[0] != '\0') {
    int i;
    if (1 == sscanf(val, "%d", &i)) {
      core_wait = i;
    } else {
      printf("invalid core wait option from environment; fallling back to %d ms\n", core_wait);
    }
  }

  val = getenv("MBC_KEY_WAIT");
  if (NULL != val && val[0] != '\0') {
    int i;
    if (1 == sscanf(val, "%d", &i)) {
      inter_key_wait = i;
    } else {
      printf("invalid key wait option from environment; fallling back to %d ms\n", inter_key_wait);
    }
  }

  val = getenv("MBC_SEQUENCE_WAIT");
  if (NULL != val && val[0] != '\0') {
    int i;
    if (1 == sscanf(val, "%d", &i)) {
      sequence_wait = i;
    } else {
      printf("invalid sequence wait option from environment; fallling back to %d ms\n", sequence_wait);
    }
  }
}

static void print_help(char* name) {
  printf("MBC (Mister Batch Control) Revision %d\n", MBC_BUILD_REVISION);
#ifdef MBC_BUILD_DATE
  printf("Build timestamp: %s\n", MBC_BUILD_DATE);
#endif // MBC_BUILD_DATE
#ifdef MBC_BUILD_COMMIT
  printf("Build commit: %s\n", MBC_BUILD_COMMIT);
#endif // MBC_BUILD_COMMIT
  printf("Usage:\n");
  printf("  %s COMMAND [ARGS]\n", name);
  printf("\n");
  printf("E.g.:\n");
  printf("  %s load_rom NES /media/fat/NES/*.nes\n", name);
  printf("\n");
  printf("Supported COMMAND:");
  for (int i=0; i<ARRSIZ(cmdlist); i++){
    if (0 != i) printf(",");
    printf(" %s", cmdlist[i].name);
  }
  printf("\n");
  printf("\n");
  printf("Please refer to the Readme for further infromation: https://github.com/pocomane/MiSTer_Batch_Control\n");
}

int main(int argc, char* argv[]) {

  if (2 > argc) {
    print_help(argv[0]);
    return 0;
  }

  read_options(argc, argv);
    
  return run_command(argc-1, argv+1);
}

